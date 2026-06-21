#include "karma/assets.h"

#include <fstream>

namespace karma::assets {

navigation::NavTileCacheSnapshot loadNavTileCacheSnapshot(const std::filesystem::path& path) {
  navigation::NavTileCacheSnapshot snapshot;
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

bool saveNavTileCacheSnapshot(const std::filesystem::path& path,
                              const navigation::NavTileCacheSnapshot& snapshot) {
  if (!snapshot.valid()) {
    return false;
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  stream.write(reinterpret_cast<const char*>(snapshot.data.data()),
               static_cast<std::streamsize>(snapshot.data.size()));
  return static_cast<bool>(stream);
}

}  // namespace karma::assets
