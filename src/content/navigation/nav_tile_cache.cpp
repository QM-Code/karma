#include "karma/assets.h"

#include <fstream>

namespace karma::assets {

namespace {

template <typename Snapshot>
Snapshot loadNavigationSnapshot(const std::filesystem::path& path) {
  Snapshot snapshot;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return snapshot;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size <= 0) {
    return snapshot;
  }
  stream.seekg(0, std::ios::beg);
  snapshot.data.resize(static_cast<size_t>(size));
  if (!stream.read(reinterpret_cast<char*>(snapshot.data.data()), size)) {
    snapshot.data.clear();
  }
  return snapshot;
}

template <typename Snapshot>
bool saveNavigationSnapshot(const std::filesystem::path& path,
                            const Snapshot& snapshot) {
  if (!snapshot.valid()) {
    return false;
  }
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  stream.write(reinterpret_cast<const char*>(snapshot.data.data()),
               static_cast<std::streamsize>(snapshot.data.size()));
  return static_cast<bool>(stream);
}

}  // namespace

navigation::NavMeshSnapshot loadNavMeshSnapshot(const std::filesystem::path& path) {
  return loadNavigationSnapshot<navigation::NavMeshSnapshot>(path);
}

bool saveNavMeshSnapshot(const std::filesystem::path& path,
                         const navigation::NavMeshSnapshot& snapshot) {
  return saveNavigationSnapshot(path, snapshot);
}

navigation::NavTileCacheSnapshot loadNavTileCacheSnapshot(const std::filesystem::path& path) {
  return loadNavigationSnapshot<navigation::NavTileCacheSnapshot>(path);
}

bool saveNavTileCacheSnapshot(const std::filesystem::path& path,
                              const navigation::NavTileCacheSnapshot& snapshot) {
  return saveNavigationSnapshot(path, snapshot);
}

}  // namespace karma::assets
