#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "karma/navigation.h"
#include "karma/navigation.h"
#include "karma/navigation.h"

namespace karma::navigation::detail {

struct NavCacheConfig {
  bool enabled = true;
  std::filesystem::path root;
};

const NavCacheConfig& navCacheConfig();

NavMeshBuildConfig cacheEffectiveConfig(const NavMeshBuildConfig& config);

std::string navMeshCacheFingerprint(const NavMeshInputGeometry& geometry,
                                    uint32_t source_mask,
                                    const NavMeshBuildConfig& config);
std::string navTileCacheFingerprint(const NavMeshInputGeometry& geometry,
                                    uint32_t source_mask,
                                    const NavMeshBuildConfig& nav_config,
                                    const NavTileCacheBuildConfig& cache_config);

std::filesystem::path navMeshCachePath(std::string_view fingerprint);
std::filesystem::path navTileCachePath(std::string_view fingerprint);

NavMeshSnapshotMetadata makeSnapshotMetadata(const NavMesh& nav_mesh);
NavMeshSnapshotMetadata makeSnapshotMetadata(const NavMeshInputGeometry& geometry,
                                             const NavMeshBuildConfig& config,
                                             const NavMeshBuildResult& result);

bool readNavMeshCache(const std::filesystem::path& path,
                      NavMeshSnapshot& snapshot,
                      NavMeshSnapshotMetadata& metadata,
                      std::string* diagnostic = nullptr);
bool writeNavMeshCache(const std::filesystem::path& path,
                       const NavMeshSnapshot& snapshot,
                       const NavMeshSnapshotMetadata& metadata,
                       std::string* diagnostic = nullptr);

bool readNavTileCache(const std::filesystem::path& path,
                      NavTileCacheSnapshot& snapshot,
                      std::string* diagnostic = nullptr);
bool writeNavTileCache(const std::filesystem::path& path,
                       const NavTileCacheSnapshot& snapshot,
                       std::string* diagnostic = nullptr);

}  // namespace karma::navigation::detail
