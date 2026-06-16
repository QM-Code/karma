#pragma once

#include <filesystem>

#include "karma/simulation/navigation/nav_tile_cache.h"

namespace karma::content {

/// \ingroup karma_content
/// Loads an opaque navigation tile-cache snapshot from disk.
navigation::NavTileCacheSnapshot loadNavTileCacheSnapshot(const std::filesystem::path& path);

/// \ingroup karma_content
/// Saves an opaque navigation tile-cache snapshot to disk.
bool saveNavTileCacheSnapshot(const std::filesystem::path& path,
                              const navigation::NavTileCacheSnapshot& snapshot);

}  // namespace karma::content
