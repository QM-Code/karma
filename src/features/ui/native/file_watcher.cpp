#include "features/ui/native/file_watcher.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace karma::ui::native {
namespace {

using Clock = std::chrono::steady_clock;

std::filesystem::path normalizeAbsolutePath(
    const std::filesystem::path& requested) {
  std::error_code error;
  std::filesystem::path absolute = requested;
  if (absolute.is_relative()) {
    const std::filesystem::path current =
        std::filesystem::current_path(error);
    if (!error) {
      absolute = current / absolute;
    }
  }
  return absolute.lexically_normal();
}

std::filesystem::path normalizeRootPath(
    const std::filesystem::path& requested) {
  const std::filesystem::path absolute = normalizeAbsolutePath(requested);
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(absolute, error);
  return error ? absolute : canonical.lexically_normal();
}

std::string pathKey(const std::filesystem::path& path) {
  const std::u8string encoded = path.generic_u8string();
  std::string key;
  key.reserve(encoded.size());
  for (const char8_t unit : encoded) {
    key.push_back(static_cast<char>(unit));
  }
#if defined(_WIN32)
  std::transform(key.begin(), key.end(), key.begin(), [](char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a')
                                       : value;
  });
#endif
  return key;
}

bool sameOrDescendant(std::string_view candidate, std::string_view parent) {
  if (candidate == parent) {
    return true;
  }
  return candidate.size() > parent.size() &&
         candidate.compare(0, parent.size(), parent) == 0 &&
         (parent.ends_with('/') || candidate[parent.size()] == '/');
}

void appendChanged(FileWatcherPollResult& result,
                   const std::filesystem::path& path) {
  if (!path.empty()) {
    result.changed_paths.push_back(path.lexically_normal());
  }
}

}  // namespace

class FileWatcher::Impl {
 public:
  explicit Impl(FileWatcherConfig config)
      : poll_interval_(config.fingerprint_poll_interval),
        force_polling_(config.force_polling) {
    roots_.reserve(config.roots.size());
    for (const std::filesystem::path& root : config.roots) {
      roots_.push_back(normalizeRootPath(root));
    }

    if (!force_polling_) {
      startNativeBackend();
    }

    bool scan_failed = false;
    snapshot_ = scanRoots(scan_failed);
    pending_rescan_ = pending_rescan_ || scan_failed;
    scheduleNextScan(Clock::now());
  }

  ~Impl() { stopNativeBackend(); }

  FileWatcherPollResult drain() {
    FileWatcherPollResult result;
    result.rescan_required = std::exchange(pending_rescan_, false);

    drainNativeBackend(result);

    const Clock::time_point now = Clock::now();
    const bool scan_due = poll_interval_.count() <= 0 || now >= next_scan_;
    if (scan_due) {
      bool scan_failed = false;
      // Native notifications make metadata reuse safe on the frequent scan.
      // A periodic full-content pass still catches dropped notifications and
      // same-size edits with deliberately preserved timestamps. Polling-only
      // platforms retain full hashing on every configured scan.
      constexpr std::size_t kFullContentScanPeriod = 20u;
      const bool force_content_hash = !nativeBackendActive() ||
          ++metadata_scan_count_ >= kFullContentScanPeriod;
      if (force_content_hash) metadata_scan_count_ = 0u;
      Snapshot next = scanRoots(scan_failed, force_content_hash);
      appendSnapshotChanges(snapshot_, next, result);
      snapshot_ = std::move(next);
      result.rescan_required = result.rescan_required || scan_failed;
      scheduleNextScan(now);
    } else {
      absorbNativeChanges(result);
    }

    std::sort(result.changed_paths.begin(), result.changed_paths.end(),
              [](const std::filesystem::path& left,
                 const std::filesystem::path& right) {
                return pathKey(left) < pathKey(right);
              });
    result.changed_paths.erase(
        std::unique(result.changed_paths.begin(), result.changed_paths.end(),
                    [](const std::filesystem::path& left,
                       const std::filesystem::path& right) {
                      return pathKey(left) == pathKey(right);
                    }),
        result.changed_paths.end());
    return result;
  }

  const std::vector<std::filesystem::path>& roots() const noexcept {
    return roots_;
  }

  bool nativeBackendActive() const noexcept {
#if defined(__linux__)
    return inotify_fd_ >= 0 && !linux_watch_paths_.empty();
#elif defined(_WIN32)
    return !windows_watches_.empty();
#else
    return false;
#endif
  }

 private:
  struct Fingerprint {
    std::uint64_t content_hash = 0;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};

    friend bool operator==(const Fingerprint&, const Fingerprint&) = default;
  };

  struct SnapshotEntry {
    std::filesystem::path path;
    Fingerprint fingerprint;
  };

  using Snapshot = std::unordered_map<std::string, SnapshotEntry>;

  static std::optional<Fingerprint> fingerprintFile(
      const std::filesystem::path& path,
      const Fingerprint* previous = nullptr,
      bool force_content_hash = true) {
    std::error_code error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    if (!force_content_hash && previous != nullptr &&
        previous->size == file_size && previous->modified == modified) {
      return *previous;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      return std::nullopt;
    }

    // Watcher fingerprints are process-local change detectors rather than
    // serialized asset hashes. Mix full machine words so a periodic safety
    // audit of large loose textures does not monopolize a Debug UI frame.
    constexpr std::uint64_t kSeed = 0x9e3779b97f4a7c15ULL;
    constexpr std::uint64_t kMultiplier = 0xd6e8feb86659fd93ULL;
    std::uint64_t hash = kSeed;
    std::uintmax_t size = 0;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const std::streamsize count = input.gcount();
      std::size_t index = 0u;
      const std::size_t byte_count = static_cast<std::size_t>(count);
      while (index + sizeof(std::uint64_t) <= byte_count) {
        std::uint64_t word = 0u;
        std::memcpy(&word, buffer.data() + index, sizeof(word));
        hash ^= word + kSeed;
        hash *= kMultiplier;
        hash = std::rotl(hash, 27);
        index += sizeof(word);
      }
      if (index < byte_count) {
        std::uint64_t tail = 0u;
        std::memcpy(&tail, buffer.data() + index, byte_count - index);
        hash ^= tail + kSeed + static_cast<std::uint64_t>(byte_count - index);
        hash *= kMultiplier;
        hash = std::rotl(hash, 27);
      }
      size += static_cast<std::uintmax_t>(count);
    }
    if (!input.eof()) {
      return std::nullopt;
    }
    hash ^= static_cast<std::uint64_t>(size) + kSeed;
    hash *= kMultiplier;
    return Fingerprint{.content_hash = hash,
                       .size = size,
                       .modified = modified};
  }

  Snapshot scanRoots(bool& failed, bool force_content_hash = true) const {
    Snapshot snapshot;
    for (const std::filesystem::path& root : roots_) {
      std::error_code error;
      const std::filesystem::file_status root_status =
          std::filesystem::symlink_status(root, error);
      if (error || !std::filesystem::is_directory(root_status)) {
        failed = true;
        continue;
      }

      std::filesystem::recursive_directory_iterator iterator(
          root, std::filesystem::directory_options::skip_permission_denied,
          error);
      if (error) {
        failed = true;
        continue;
      }
      const std::filesystem::recursive_directory_iterator end;
      while (iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        const std::filesystem::file_status status = entry.symlink_status(error);
        if (error) {
          failed = true;
          error.clear();
        } else if (std::filesystem::is_symlink(status)) {
          if (iterator.recursion_pending()) {
            iterator.disable_recursion_pending();
          }
        } else if (std::filesystem::is_regular_file(status)) {
          const std::filesystem::path path =
              normalizeAbsolutePath(entry.path());
          const std::string key = pathKey(path);
          const auto previous = snapshot_.find(key);
          const Fingerprint* previous_fingerprint =
              previous == snapshot_.end() ? nullptr : &previous->second.fingerprint;
          const std::optional<Fingerprint> fingerprint = fingerprintFile(
              path, previous_fingerprint, force_content_hash);
          if (fingerprint.has_value()) {
            snapshot.emplace(key,
                             SnapshotEntry{.path = path,
                                           .fingerprint = *fingerprint});
          } else {
            failed = true;
          }
        }

        iterator.increment(error);
        if (error) {
          failed = true;
          error.clear();
        }
      }
    }
    return snapshot;
  }

  static void appendSnapshotChanges(const Snapshot& previous,
                                    const Snapshot& next,
                                    FileWatcherPollResult& result) {
    for (const auto& [key, entry] : next) {
      const auto found = previous.find(key);
      if (found == previous.end() ||
          found->second.fingerprint != entry.fingerprint) {
        appendChanged(result, entry.path);
      }
    }
    for (const auto& [key, entry] : previous) {
      if (!next.contains(key)) {
        appendChanged(result, entry.path);
      }
    }
  }

  void absorbNativeChanges(FileWatcherPollResult& result) {
    for (const std::filesystem::path& path : result.changed_paths) {
      const std::string key = pathKey(path);
      std::error_code error;
      const std::filesystem::file_status status =
          std::filesystem::symlink_status(path, error);
      if (!error && std::filesystem::is_regular_file(status)) {
        const std::optional<Fingerprint> fingerprint = fingerprintFile(path);
        if (fingerprint.has_value()) {
          snapshot_[key] =
              SnapshotEntry{.path = path, .fingerprint = *fingerprint};
        } else {
          result.rescan_required = true;
        }
        continue;
      }

      if (!error && std::filesystem::is_directory(status)) {
        continue;
      }

      for (auto iterator = snapshot_.begin(); iterator != snapshot_.end();) {
        if (sameOrDescendant(iterator->first, key)) {
          iterator = snapshot_.erase(iterator);
        } else {
          ++iterator;
        }
      }
    }
  }

  void scheduleNextScan(Clock::time_point now) {
    if (poll_interval_.count() <= 0) {
      next_scan_ = now;
    } else {
      next_scan_ = now + poll_interval_;
    }
  }

#if defined(__linux__)
  static constexpr std::uint32_t kInotifyMask =
      IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF |
      IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO | IN_UNMOUNT;

  void addLinuxDirectory(const std::filesystem::path& directory) {
    const int descriptor = inotify_add_watch(
        inotify_fd_, directory.c_str(), kInotifyMask | IN_DONT_FOLLOW);
    if (descriptor < 0) {
      pending_rescan_ = true;
      return;
    }
    linux_watch_paths_[descriptor] = directory;
  }

  void addLinuxTree(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::is_directory(
            std::filesystem::symlink_status(root, error)) ||
        error) {
      pending_rescan_ = true;
      return;
    }
    addLinuxDirectory(root);

    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied,
        error);
    if (error) {
      pending_rescan_ = true;
      return;
    }
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
      const std::filesystem::file_status status =
          iterator->symlink_status(error);
      if (error) {
        pending_rescan_ = true;
        error.clear();
      } else if (std::filesystem::is_symlink(status)) {
        if (iterator.recursion_pending()) {
          iterator.disable_recursion_pending();
        }
      } else if (std::filesystem::is_directory(status)) {
        addLinuxDirectory(normalizeAbsolutePath(iterator->path()));
      }
      iterator.increment(error);
      if (error) {
        pending_rescan_ = true;
        error.clear();
      }
    }
  }

  void startNativeBackend() {
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
      pending_rescan_ = true;
      return;
    }
    for (const std::filesystem::path& root : roots_) {
      addLinuxTree(root);
    }
  }

  void stopNativeBackend() {
    linux_watch_paths_.clear();
    if (inotify_fd_ >= 0) {
      close(inotify_fd_);
      inotify_fd_ = -1;
    }
  }

  void rebuildLinuxBackend() {
    stopNativeBackend();
    if (!force_polling_) {
      startNativeBackend();
    }
  }

  void drainNativeBackend(FileWatcherPollResult& result) {
    if (inotify_fd_ < 0) {
      return;
    }

    bool rebuild = false;
    alignas(inotify_event) std::array<char, 64 * 1024> buffer{};
    for (;;) {
      const ssize_t byte_count = read(inotify_fd_, buffer.data(), buffer.size());
      if (byte_count < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          result.rescan_required = true;
          rebuild = true;
        }
        break;
      }
      if (byte_count == 0) {
        break;
      }

      std::size_t offset = 0;
      while (offset + sizeof(inotify_event) <=
             static_cast<std::size_t>(byte_count)) {
        const auto* event = reinterpret_cast<const inotify_event*>(
            buffer.data() + offset);
        const std::size_t event_size = sizeof(inotify_event) + event->len;
        if (event_size > static_cast<std::size_t>(byte_count) - offset) {
          result.rescan_required = true;
          rebuild = true;
          break;
        }
        offset += event_size;

        if ((event->mask & IN_Q_OVERFLOW) != 0U) {
          result.rescan_required = true;
          rebuild = true;
          continue;
        }

        const auto watched = linux_watch_paths_.find(event->wd);
        if (watched == linux_watch_paths_.end()) {
          if ((event->mask & IN_IGNORED) == 0U) {
            result.rescan_required = true;
          }
          continue;
        }

        std::filesystem::path path = watched->second;
        if (event->len > 0 && event->name[0] != '\0') {
          path /= event->name;
        }
        path = normalizeAbsolutePath(path);

        if ((event->mask & IN_IGNORED) == 0U) {
          appendChanged(result, path);
        }

        const bool directory = (event->mask & IN_ISDIR) != 0U;
        const std::uint32_t topology_mask =
            IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF |
            IN_MOVED_FROM | IN_MOVED_TO | IN_UNMOUNT;
        if (directory && (event->mask & topology_mask) != 0U) {
          // inotify does not recursively follow a directory that is moved into
          // an existing watch. Rebuilding after this drain closes that gap, and
          // the rescan bit tells the owner not to rely on the directory event
          // as a lossless list of its children.
          result.rescan_required = true;
          rebuild = true;
        }
        if ((event->mask & IN_IGNORED) != 0U) {
          linux_watch_paths_.erase(watched);
        }
      }
    }

    if (rebuild) {
      rebuildLinuxBackend();
    }
  }

#elif defined(_WIN32)
  struct WindowsWatch {
    std::filesystem::path root;
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped{};
    std::array<std::byte, 64 * 1024> buffer{};
    bool armed = false;
  };

  static bool armWindowsWatch(WindowsWatch& watch) {
    ResetEvent(watch.event);
    watch.overlapped = {};
    watch.overlapped.hEvent = watch.event;
    constexpr DWORD filter =
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_ATTRIBUTES;
    watch.armed =
        ReadDirectoryChangesW(watch.directory, watch.buffer.data(),
                              static_cast<DWORD>(watch.buffer.size()), TRUE,
                              filter, nullptr, &watch.overlapped, nullptr) != 0;
    return watch.armed;
  }

  void startNativeBackend() {
    for (const std::filesystem::path& root : roots_) {
      auto watch = std::make_unique<WindowsWatch>();
      watch->root = root;
      watch->directory = CreateFileW(
          root.c_str(), FILE_LIST_DIRECTORY,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
          nullptr);
      if (watch->directory == INVALID_HANDLE_VALUE) {
        pending_rescan_ = true;
        continue;
      }
      watch->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (watch->event == nullptr || !armWindowsWatch(*watch)) {
        pending_rescan_ = true;
        if (watch->event != nullptr) {
          CloseHandle(watch->event);
        }
        CloseHandle(watch->directory);
        continue;
      }
      windows_watches_.push_back(std::move(watch));
    }
  }

  void stopNativeBackend() {
    for (const std::unique_ptr<WindowsWatch>& watch : windows_watches_) {
      if (watch->directory != INVALID_HANDLE_VALUE) {
        CancelIoEx(watch->directory, &watch->overlapped);
      }
      if (watch->directory != INVALID_HANDLE_VALUE) {
        CloseHandle(watch->directory);
      }
      if (watch->event != nullptr) {
        CloseHandle(watch->event);
      }
    }
    windows_watches_.clear();
  }

  void drainNativeBackend(FileWatcherPollResult& result) {
    for (const std::unique_ptr<WindowsWatch>& watch : windows_watches_) {
      if (!watch->armed || WaitForSingleObject(watch->event, 0) != WAIT_OBJECT_0) {
        continue;
      }

      DWORD bytes = 0;
      if (GetOverlappedResult(watch->directory, &watch->overlapped, &bytes,
                              FALSE) == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
          continue;
        }
        result.rescan_required = true;
        watch->armed = false;
      } else if (bytes == 0) {
        result.rescan_required = true;
      } else {
        std::size_t offset = 0;
        while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= bytes) {
          const auto* notification =
              reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                  watch->buffer.data() + offset);
          const std::wstring relative(
              notification->FileName,
              notification->FileNameLength / sizeof(wchar_t));
          appendChanged(result,
                        normalizeAbsolutePath(watch->root / relative));
          if (notification->Action == FILE_ACTION_RENAMED_OLD_NAME ||
              notification->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            result.rescan_required = true;
          }
          if (notification->NextEntryOffset == 0) {
            break;
          }
          if (notification->NextEntryOffset > bytes - offset) {
            result.rescan_required = true;
            break;
          }
          offset += notification->NextEntryOffset;
        }
      }

      if (!armWindowsWatch(*watch)) {
        result.rescan_required = true;
      }
    }
  }

#else
  void startNativeBackend() {}
  void stopNativeBackend() {}
  void drainNativeBackend(FileWatcherPollResult&) {}
#endif

  std::vector<std::filesystem::path> roots_;
  std::chrono::milliseconds poll_interval_;
  bool force_polling_ = false;
  bool pending_rescan_ = false;
  std::size_t metadata_scan_count_ = 0u;
  Clock::time_point next_scan_{};
  Snapshot snapshot_;

#if defined(__linux__)
  int inotify_fd_ = -1;
  std::unordered_map<int, std::filesystem::path> linux_watch_paths_;
#elif defined(_WIN32)
  std::vector<std::unique_ptr<WindowsWatch>> windows_watches_;
#endif
};

FileWatcher::FileWatcher(FileWatcherConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

FileWatcher::~FileWatcher() = default;

FileWatcher::FileWatcher(FileWatcher&&) noexcept = default;

FileWatcher& FileWatcher::operator=(FileWatcher&&) noexcept = default;

FileWatcherPollResult FileWatcher::drain() {
  return impl_ ? impl_->drain() : FileWatcherPollResult{};
}

const std::vector<std::filesystem::path>& FileWatcher::roots() const noexcept {
  static const std::vector<std::filesystem::path> kEmptyRoots;
  return impl_ ? impl_->roots() : kEmptyRoots;
}

bool FileWatcher::nativeBackendActive() const noexcept {
  return impl_ && impl_->nativeBackendActive();
}

}  // namespace karma::ui::native
