#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace karma::ui::native {

/// Configuration for the development-source file watcher.
struct FileWatcherConfig {
  /// Roots are normalized to absolute paths without changing their order.
  std::vector<std::filesystem::path> roots;

  /// A content-fingerprint scan supplements the native backend at this
  /// interval. A non-positive interval scans on every drain.
  std::chrono::milliseconds fingerprint_poll_interval{250};

  /// Disables the native backend. This is useful on unsupported platforms and
  /// for deterministic tests of the portable fallback.
  bool force_polling = false;
};

struct FileWatcherPollResult {
  /// Normalized absolute paths which may have been added, changed, renamed, or
  /// removed. Paths are sorted and deduplicated within each result.
  std::vector<std::filesystem::path> changed_paths;

  /// The caller should rescan its complete dependency graph. This is set for
  /// native queue overflow, directory topology changes that cannot be
  /// represented losslessly, inaccessible roots, and filesystem scan errors.
  bool rescan_required = false;
};

/// Recursive, non-blocking watcher for native UI development source roots.
///
/// FileWatcher owns all native handles and closes/cancels them in its
/// destructor. It does not run callbacks or debounce events; drain() is meant
/// to be called by the UI system on its owning thread.
class FileWatcher {
 public:
  explicit FileWatcher(FileWatcherConfig config);
  ~FileWatcher();

  FileWatcher(const FileWatcher&) = delete;
  FileWatcher& operator=(const FileWatcher&) = delete;
  FileWatcher(FileWatcher&&) noexcept;
  FileWatcher& operator=(FileWatcher&&) noexcept;

  /// Drains native events without blocking and performs a fallback scan when
  /// its configured interval has elapsed.
  [[nodiscard]] FileWatcherPollResult drain();

  /// The normalized roots in the same priority order supplied by the caller.
  [[nodiscard]] const std::vector<std::filesystem::path>& roots() const noexcept;

  /// True when an OS notification backend is currently active. The portable
  /// fingerprint scan remains enabled as a safety net when this is true.
  [[nodiscard]] bool nativeBackendActive() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
