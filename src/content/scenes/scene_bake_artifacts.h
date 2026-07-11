#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace karma::scenes::detail {

/// Stages bake artifacts beside their final paths and publishes the complete
/// set only after every requested bake domain succeeds.
class BakeArtifactTransaction {
 public:
  BakeArtifactTransaction();
  ~BakeArtifactTransaction();

  BakeArtifactTransaction(const BakeArtifactTransaction&) = delete;
  BakeArtifactTransaction& operator=(const BakeArtifactTransaction&) = delete;

  std::filesystem::path stage(const std::filesystem::path& final_path);
  bool publish(std::string* diagnostic = nullptr);
  void discard();

 private:
  struct Entry {
    std::filesystem::path staged_path;
    std::filesystem::path final_path;
    std::filesystem::path backup_path;
    bool backed_up = false;
    bool published = false;
  };

  std::string token_;
  std::vector<Entry> entries_;
  bool complete_ = false;
};

bool isPortableBakeArtifactPath(const std::filesystem::path& path);

}  // namespace karma::scenes::detail
