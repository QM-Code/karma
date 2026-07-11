#include "scene_bake_artifacts.h"

#include <atomic>
#include <chrono>
#include <system_error>

namespace karma::scenes::detail {
namespace {

std::string transactionToken() {
  static std::atomic<uint64_t> sequence{0u};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::to_string(stamp) + "." +
         std::to_string(sequence.fetch_add(1u));
}

}  // namespace

BakeArtifactTransaction::BakeArtifactTransaction()
    : token_(transactionToken()) {}

BakeArtifactTransaction::~BakeArtifactTransaction() {
  discard();
}

std::filesystem::path BakeArtifactTransaction::stage(
    const std::filesystem::path& final_path) {
  for (const Entry& entry : entries_) {
    if (entry.final_path == final_path) return entry.staged_path;
  }
  const std::filesystem::path staged_path =
      final_path.parent_path() /
      (final_path.filename().string() + ".stage." + token_);
  const std::filesystem::path backup_path =
      final_path.parent_path() /
      (final_path.filename().string() + ".backup." + token_);
  entries_.push_back(Entry{
      .staged_path = staged_path,
      .final_path = final_path,
      .backup_path = backup_path,
  });
  return staged_path;
}

bool BakeArtifactTransaction::publish(std::string* diagnostic) {
  if (complete_) return true;
  std::error_code error;
  for (const Entry& entry : entries_) {
    if (!std::filesystem::is_regular_file(entry.staged_path, error)) {
      if (diagnostic) {
        *diagnostic = "staged bake artifact is missing: " +
                      entry.staged_path.generic_string();
      }
      discard();
      return false;
    }
    error.clear();
  }

  for (Entry& entry : entries_) {
    if (std::filesystem::exists(entry.final_path, error)) {
      error.clear();
      std::filesystem::rename(entry.final_path, entry.backup_path, error);
      if (error) {
        if (diagnostic) {
          *diagnostic = "failed to stage previous bake artifact: " +
                        error.message();
        }
        discard();
        return false;
      }
      entry.backed_up = true;
    }
    error.clear();
    std::filesystem::rename(entry.staged_path, entry.final_path, error);
    if (error) {
      if (diagnostic) {
        *diagnostic = "failed to publish bake artifact: " + error.message();
      }
      discard();
      return false;
    }
    entry.published = true;
  }

  for (Entry& entry : entries_) {
    if (entry.backed_up) {
      std::filesystem::remove(entry.backup_path, error);
      error.clear();
    }
  }
  complete_ = true;
  entries_.clear();
  return true;
}

void BakeArtifactTransaction::discard() {
  if (complete_) return;
  std::error_code error;
  for (auto iterator = entries_.rbegin(); iterator != entries_.rend();
       ++iterator) {
    Entry& entry = *iterator;
    std::filesystem::remove(entry.staged_path, error);
    error.clear();
    if (entry.published) {
      std::filesystem::remove(entry.final_path, error);
      error.clear();
    }
    if (entry.backed_up &&
        std::filesystem::exists(entry.backup_path, error)) {
      error.clear();
      std::filesystem::rename(entry.backup_path, entry.final_path, error);
      error.clear();
    }
  }
  entries_.clear();
  complete_ = true;
}

bool isPortableBakeArtifactPath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
  const std::filesystem::path normalized = path.lexically_normal();
  if (normalized.empty() || normalized == ".") return false;
  for (const auto& part : normalized) {
    if (part == "..") return false;
  }
  return true;
}

}  // namespace karma::scenes::detail
