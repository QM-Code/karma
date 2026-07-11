#include "features/ui/native/file_watcher.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

using karma::ui::native::FileWatcher;
using karma::ui::native::FileWatcherConfig;
using karma::ui::native::FileWatcherPollResult;

std::filesystem::path normalized(const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(path, error);
  if (!error) {
    return canonical.lexically_normal();
  }
  return std::filesystem::absolute(path).lexically_normal();
}

class TemporaryDirectory {
 public:
  explicit TemporaryDirectory(std::string_view label) {
    static std::atomic_uint64_t sequence{0};
    path = std::filesystem::temp_directory_path() /
           (std::string(label) + "_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "_" + std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directories(path);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  std::filesystem::path path;
};

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.close();
  assert(output);
}

bool containsPath(const FileWatcherPollResult& result,
                  const std::filesystem::path& requested) {
  const std::filesystem::path expected = normalized(requested);
  return std::find(result.changed_paths.begin(), result.changed_paths.end(),
                   expected) != result.changed_paths.end();
}

std::size_t countPath(const FileWatcherPollResult& result,
                      const std::filesystem::path& requested) {
  const std::filesystem::path expected = normalized(requested);
  return static_cast<std::size_t>(
      std::count(result.changed_paths.begin(), result.changed_paths.end(),
                 expected));
}

FileWatcherPollResult waitForPath(FileWatcher& watcher,
                                  const std::filesystem::path& path) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  FileWatcherPollResult combined;
  while (std::chrono::steady_clock::now() < deadline) {
    FileWatcherPollResult next = watcher.drain();
    combined.rescan_required =
        combined.rescan_required || next.rescan_required;
    combined.changed_paths.insert(combined.changed_paths.end(),
                                  next.changed_paths.begin(),
                                  next.changed_paths.end());
    if (containsPath(combined, path)) {
      return combined;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(false && "timed out waiting for a file watcher event");
  return combined;
}

void testOrderedNormalizedRootsAndQuietBaseline() {
  TemporaryDirectory temporary("karma_ui_watcher_roots");
  const std::filesystem::path first = temporary.path / "first";
  const std::filesystem::path second = temporary.path / "second";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);
  writeText(first / "already-there.kui.json5", "{version: 2}");

  FileWatcher watcher(FileWatcherConfig{
      .roots = {first / "missing" / "..", second},
      .fingerprint_poll_interval = std::chrono::milliseconds(0),
      .force_polling = true,
  });
  assert(!watcher.nativeBackendActive());
  assert(watcher.roots().size() == 2);
  assert(watcher.roots()[0] == normalized(first));
  assert(watcher.roots()[1] == normalized(second));

  const FileWatcherPollResult baseline = watcher.drain();
  assert(baseline.changed_paths.empty());
  assert(!baseline.rescan_required);
}

void testPortableFingerprintFallback() {
  TemporaryDirectory temporary("karma_ui_watcher_polling");
  TemporaryDirectory outside("karma_ui_watcher_outside");
  const std::filesystem::path nested = temporary.path / "nested";
  const std::filesystem::path source = nested / "menu.kui.json5";
  const std::filesystem::path renamed = nested / "renamed.kui.json5";

  // The overlapping second root verifies path-level deduplication without
  // discarding the configured root order.
  std::filesystem::create_directories(nested);
  FileWatcher watcher(FileWatcherConfig{
      .roots = {temporary.path, nested},
      .fingerprint_poll_interval = std::chrono::milliseconds(0),
      .force_polling = true,
  });

  writeText(source, "alpha");
  FileWatcherPollResult added = watcher.drain();
  assert(containsPath(added, source));
  assert(countPath(added, source) == 1);
  assert(!added.rescan_required);
  assert(watcher.drain().changed_paths.empty());

  // Content hashing catches same-size edits even when an editor restores the
  // original write time.
  const auto original_write_time = std::filesystem::last_write_time(source);
  writeText(source, "bravo");
  std::filesystem::last_write_time(source, original_write_time);
  const FileWatcherPollResult modified = watcher.drain();
  assert(containsPath(modified, source));

  std::filesystem::rename(source, renamed);
  const FileWatcherPollResult moved = watcher.drain();
  assert(containsPath(moved, source));
  assert(containsPath(moved, renamed));

  std::filesystem::remove(renamed);
  const FileWatcherPollResult removed = watcher.drain();
  assert(containsPath(removed, renamed));

  // Recursive fallback scanning never follows a symlink out of a configured
  // source root.
  const std::filesystem::path outside_file = outside.path / "outside.json5";
  writeText(outside_file, "outside");
  std::error_code symlink_error;
  std::filesystem::create_symlink(outside_file, nested / "outside-link.json5",
                                  symlink_error);
  if (!symlink_error) {
    const FileWatcherPollResult symlink = watcher.drain();
    assert(!containsPath(symlink, outside_file));
  }

  std::filesystem::remove_all(temporary.path);
  const FileWatcherPollResult missing_root = watcher.drain();
  assert(missing_root.rescan_required);
}

#if defined(__linux__)
void testLinuxInotifyRecursiveChanges() {
  TemporaryDirectory temporary("karma_ui_watcher_inotify");
  const std::filesystem::path existing_directory = temporary.path / "nested";
  const std::filesystem::path existing_file =
      existing_directory / "theme.kstyle.json5";
  std::filesystem::create_directories(existing_directory);
  writeText(existing_file, "{version: 2}");

  FileWatcher watcher(FileWatcherConfig{
      .roots = {temporary.path},
      .fingerprint_poll_interval = std::chrono::hours(1),
  });
  assert(watcher.nativeBackendActive());
  assert(watcher.drain().changed_paths.empty());

  writeText(existing_file, "{version: 2, changed: true}");
  const FileWatcherPollResult modified = waitForPath(watcher, existing_file);
  assert(!modified.rescan_required);

  // A new directory causes an explicit rescan request because inotify cannot
  // guarantee events for files created before its recursive watch is added.
  const std::filesystem::path added_directory = temporary.path / "added";
  std::filesystem::create_directory(added_directory);
  const FileWatcherPollResult directory_event =
      waitForPath(watcher, added_directory);
  assert(directory_event.rescan_required);

  // drain() has rebuilt the recursive watch before it returns.
  const std::filesystem::path added_file =
      added_directory / "panel.kui.json5";
  writeText(added_file, "{version: 2}");
  const FileWatcherPollResult added = waitForPath(watcher, added_file);
  assert(containsPath(added, added_file));

  const std::filesystem::path renamed_file =
      added_directory / "panel-renamed.kui.json5";
  std::filesystem::rename(added_file, renamed_file);
  const FileWatcherPollResult old_name = waitForPath(watcher, added_file);
  assert(containsPath(old_name, added_file));
  FileWatcherPollResult new_name = old_name;
  if (!containsPath(new_name, renamed_file)) {
    new_name = waitForPath(watcher, renamed_file);
  }
  assert(containsPath(new_name, renamed_file));

  std::filesystem::remove(renamed_file);
  assert(containsPath(waitForPath(watcher, renamed_file), renamed_file));
}
#endif

void testMoveTransfersOwnershipAndMovedFromIsSafe() {
  TemporaryDirectory temporary("karma_ui_watcher_move");
  FileWatcher original(FileWatcherConfig{
      .roots = {temporary.path},
      .fingerprint_poll_interval = std::chrono::milliseconds(0),
      .force_polling = true,
  });
  FileWatcher moved = std::move(original);
  assert(original.roots().empty());
  assert(original.drain().changed_paths.empty());
  assert(moved.roots().size() == 1);

  const std::filesystem::path source = temporary.path / "moved.kui.json5";
  writeText(source, "{version: 2}");
  assert(containsPath(moved.drain(), source));
}

}  // namespace

int main() {
  testOrderedNormalizedRootsAndQuietBaseline();
  testPortableFingerprintFallback();
#if defined(__linux__)
  testLinuxInotifyRecursiveChanges();
#endif
  testMoveTransfersOwnershipAndMovedFromIsSafe();
  std::cout << "ui_file_watcher_tests: ok\n";
  return 0;
}
