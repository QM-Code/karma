#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <Recast.h>

#include "karma/navigation.h"

class dtNavMesh;

namespace karma::navigation::detail {

struct TileCacheData {
  unsigned char* data = nullptr;
  int data_size = 0;
};

inline void setResult(NavTileCacheBuildResult* result,
                      NavStatus status,
                      std::string message,
                      uint32_t tile_count = 0,
                      uint32_t layer_count = 0,
                      uint32_t compressed_bytes = 0,
                      uint32_t raw_bytes = 0,
                      uint32_t navmesh_bytes = 0) {
  if (result == nullptr) {
    return;
  }
  result->status = status;
  result->message = std::move(message);
  result->tile_count = tile_count;
  result->layer_count = layer_count;
  result->compressed_bytes = compressed_bytes;
  result->raw_bytes = raw_bytes;
  result->navmesh_bytes = navmesh_bytes;
}

bool validNavConfig(const NavMeshBuildConfig& config);
bool validCacheConfig(const NavTileCacheBuildConfig& config);
void computeBounds(const NavMeshInputGeometry& geometry, math::Vec3& min, math::Vec3& max);
void configureRecast(const NavMeshBuildConfig& config,
                     const math::Vec3& bounds_min,
                     const math::Vec3& bounds_max,
                     rcConfig& cfg);
int nextPow2(int value);
int ilog2(int value);
int calcLayerBufferSize(int grid_width, int grid_height);

NavTileCacheObstacleState mapObstacleState(unsigned char state);
NavTileCacheObstacleShape mapObstacleShape(unsigned char type);

std::unique_ptr<dtTileCacheAlloc> makeTileCacheAllocator(size_t capacity);
std::unique_ptr<dtTileCacheCompressor> makeCompressor(NavTileCacheCompression compression);
std::unique_ptr<dtTileCacheMeshProcess> makeTileCacheMeshProcess(
    const NavMeshInputGeometry& geometry,
    const NavMeshBuildConfig& config);

void freeTileData(TileCacheData& tile);
std::vector<TileCacheData> rasterizeTileLayers(const NavMeshInputGeometry& geometry,
                                               const NavMeshBuildConfig& config,
                                               const math::Vec3& bounds_min,
                                               const math::Vec3& bounds_max,
                                               int tx,
                                               int ty,
                                               const rcConfig& base_cfg,
                                               dtTileCacheCompressor& compressor,
                                               int max_layers,
                                               NavTileCacheBuildResult* result);

}  // namespace karma::navigation::detail

namespace karma::navigation {

struct NavTileCache::Impl {
  ~Impl();

  void reset();

  dtTileCache* tile_cache = nullptr;
  std::unique_ptr<dtTileCacheAlloc> allocator;
  std::unique_ptr<dtTileCacheCompressor> compressor;
  std::unique_ptr<dtTileCacheMeshProcess> mesh_process;
  NavMeshInputGeometry geometry{};
  NavMeshBuildConfig nav_config{};
  NavTileCacheBuildConfig cache_config{};
  NavTileCacheBuildResult last_result{};
  bool pending_changes = false;
};

}  // namespace karma::navigation
