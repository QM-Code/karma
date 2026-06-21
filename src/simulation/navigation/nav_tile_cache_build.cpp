#include "karma/navigation.h"
#include "karma/navigation.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourTileCache.h>
#include <Recast.h>

#include "detail/detour_utils.h"
#include "detail/nav_mesh_access.h"
#include "detail/nav_mesh_debug.h"
#include "detail/nav_tile_cache_impl.h"

namespace karma::navigation {

using detail::buildDebugEdges;
using detail::calcLayerBufferSize;
using detail::computeBounds;
using detail::configureRecast;
using detail::failed;
using detail::freeTileData;
using detail::ilog2;
using detail::makeCompressor;
using detail::makeTileCacheAllocator;
using detail::makeTileCacheMeshProcess;
using detail::nextPow2;
using detail::ptr;
using detail::rasterizeTileLayers;
using detail::setResult;
using detail::TileCacheData;
using detail::validCacheConfig;
using detail::validNavConfig;

bool NavTileCache::build(NavMesh& nav_mesh,
                         const NavMeshInputGeometry& geometry,
                         const NavMeshBuildConfig& nav_config,
                         const NavTileCacheBuildConfig& cache_config,
                         NavTileCacheBuildResult* result) {
  if (impl_ == nullptr) {
    impl_ = std::make_unique<Impl>();
  }
  impl_->reset();
  nav_mesh.reset();

  if (geometry.empty() || geometry.indices.size() % 3u != 0u) {
    setResult(result, NavStatus::EmptyInput, "Navigation geometry has no triangles.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::EmptyInput, "Navigation geometry has no triangles."};
    return false;
  }
  if (!geometry.triangle_areas.empty() &&
      geometry.triangle_areas.size() != static_cast<size_t>(geometry.triangleCount())) {
    setResult(result, NavStatus::BuildFailed, "Navigation triangle area count does not match triangle count.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                  "Navigation triangle area count does not match triangle count."};
    return false;
  }
  if (!validNavConfig(nav_config) || !validCacheConfig(cache_config)) {
    setResult(result, NavStatus::InvalidConfig, "Navigation tile-cache config is invalid.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::InvalidConfig, "Navigation tile-cache config is invalid."};
    return false;
  }

  math::Vec3 bounds_min;
  math::Vec3 bounds_max;
  computeBounds(geometry, bounds_min, bounds_max);

  int grid_width = 0;
  int grid_height = 0;
  rcCalcGridSize(ptr(bounds_min), ptr(bounds_max), nav_config.cell_size, &grid_width, &grid_height);
  const int tile_width = (grid_width + nav_config.tile_size - 1) / nav_config.tile_size;
  const int tile_height = (grid_height + nav_config.tile_size - 1) / nav_config.tile_size;
  if (tile_width <= 0 || tile_height <= 0) {
    setResult(result, NavStatus::BuildFailed, "Navigation tile-cache grid is empty.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed, "Navigation tile-cache grid is empty."};
    return false;
  }

  const int layer_slots = tile_width * tile_height * cache_config.expected_layers_per_tile;
  const int tile_bits = std::min(ilog2(nextPow2(layer_slots)), 14);
  const int poly_bits = 22 - tile_bits;
  const int max_tiles = nav_config.max_tiles > 0 ? nav_config.max_tiles : (1 << tile_bits);
  const int max_polys_per_tile = nav_config.max_polys_per_tile > 0
      ? nav_config.max_polys_per_tile
      : (1 << poly_bits);

  impl_->allocator = makeTileCacheAllocator(cache_config.allocator_size);
  impl_->compressor = makeCompressor(cache_config.compression);
  impl_->mesh_process = makeTileCacheMeshProcess(geometry, nav_config);

  dtTileCacheParams cache_params{};
  cache_params.orig[0] = bounds_min.x;
  cache_params.orig[1] = bounds_min.y;
  cache_params.orig[2] = bounds_min.z;
  cache_params.cs = nav_config.cell_size;
  cache_params.ch = nav_config.cell_height;
  cache_params.width = nav_config.tile_size;
  cache_params.height = nav_config.tile_size;
  cache_params.walkableHeight = nav_config.agent_height;
  cache_params.walkableRadius = nav_config.agent_radius;
  cache_params.walkableClimb = nav_config.agent_max_climb;
  cache_params.maxSimplificationError = nav_config.edge_max_error;
  cache_params.maxTiles = std::max(1, layer_slots);
  cache_params.maxObstacles = cache_config.max_obstacles;

  impl_->tile_cache = dtAllocTileCache();
  if (impl_->tile_cache == nullptr ||
      failed(impl_->tile_cache->init(&cache_params,
                                     impl_->allocator.get(),
                                     impl_->compressor.get(),
                                     impl_->mesh_process.get()))) {
    setResult(result, NavStatus::BuildFailed, "Failed to initialize Detour tile cache.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed, "Failed to initialize Detour tile cache."};
    return false;
  }

  dtNavMeshParams nav_params{};
  nav_params.orig[0] = bounds_min.x;
  nav_params.orig[1] = bounds_min.y;
  nav_params.orig[2] = bounds_min.z;
  nav_params.tileWidth = static_cast<float>(nav_config.tile_size) * nav_config.cell_size;
  nav_params.tileHeight = static_cast<float>(nav_config.tile_size) * nav_config.cell_size;
  nav_params.maxTiles = max_tiles;
  nav_params.maxPolys = max_polys_per_tile;

  dtNavMesh* detour_nav = dtAllocNavMesh();
  if (detour_nav == nullptr || failed(detour_nav->init(&nav_params))) {
    dtFreeNavMesh(detour_nav);
    setResult(result, NavStatus::BuildFailed, "Failed to initialize Detour navmesh for tile cache.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                  "Failed to initialize Detour navmesh for tile cache."};
    return false;
  }

  detail::NavMeshAccess::adoptDetour(nav_mesh,
                                     detour_nav,
                                     nav_config,
                                     bounds_min,
                                     bounds_max);

  rcConfig rc_config{};
  configureRecast(nav_config, bounds_min, bounds_max, rc_config);
  rc_config.tileSize = nav_config.tile_size;
  rc_config.borderSize = rc_config.walkableRadius + 3;
  rc_config.width = rc_config.tileSize + rc_config.borderSize * 2;
  rc_config.height = rc_config.tileSize + rc_config.borderSize * 2;

  uint32_t layer_count = 0;
  uint32_t compressed_bytes = 0;
  uint32_t raw_bytes = 0;
  for (int y = 0; y < tile_height; ++y) {
    for (int x = 0; x < tile_width; ++x) {
      std::vector<TileCacheData> tiles = rasterizeTileLayers(geometry,
                                                             nav_config,
                                                             bounds_min,
                                                             bounds_max,
                                                             x,
                                                             y,
                                                             rc_config,
                                                             *impl_->compressor,
                                                             cache_config.max_layers_per_tile,
                                                             result);
      for (TileCacheData& tile : tiles) {
        const int tile_size = tile.data_size;
        if (failed(impl_->tile_cache->addTile(tile.data,
                                              tile.data_size,
                                              DT_COMPRESSEDTILE_FREE_DATA,
                                              nullptr))) {
          freeTileData(tile);
          continue;
        }
        tile.data = nullptr;
        tile.data_size = 0;
        ++layer_count;
        compressed_bytes += static_cast<uint32_t>(tile_size);
        raw_bytes += static_cast<uint32_t>(calcLayerBufferSize(cache_params.width, cache_params.height));
      }
    }
  }

  if (layer_count == 0) {
    setResult(result, NavStatus::BuildFailed, "Navigation tile cache produced no layers.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed, "Navigation tile cache produced no layers."};
    return false;
  }

  for (int y = 0; y < tile_height; ++y) {
    for (int x = 0; x < tile_width; ++x) {
      impl_->tile_cache->buildNavMeshTilesAt(x, y, detail::NavMeshAccess::detour(nav_mesh));
    }
  }

  const dtNavMesh* const_nav_mesh = detail::NavMeshAccess::detour(nav_mesh);
  uint32_t navmesh_bytes = 0;
  for (int i = 0; i < const_nav_mesh->getMaxTiles(); ++i) {
    const dtMeshTile* tile = const_nav_mesh->getTile(i);
    if (tile != nullptr && tile->header != nullptr) {
      navmesh_bytes += static_cast<uint32_t>(tile->dataSize);
    }
  }

  detail::NavMeshAccess::setDebugEdges(
      nav_mesh,
      buildDebugEdges(*detail::NavMeshAccess::detour(nav_mesh)));
  detail::NavMeshAccess::refreshSnapshot(nav_mesh);

  NavMeshBuildResult mesh_result{};
  mesh_result.status = NavStatus::Success;
  mesh_result.message = "Navigation tile cache built.";
  mesh_result.vertex_count = static_cast<uint32_t>(geometry.vertices.size());
  mesh_result.triangle_count = geometry.triangleCount();
  mesh_result.polygon_count = 0;
  for (int i = 0; i < const_nav_mesh->getMaxTiles(); ++i) {
    const dtMeshTile* tile = const_nav_mesh->getTile(i);
    if (tile != nullptr && tile->header != nullptr) {
      mesh_result.polygon_count += static_cast<uint32_t>(tile->header->polyCount);
    }
  }
  detail::NavMeshAccess::setLastBuildResult(nav_mesh, mesh_result);

  NavTileCacheBuildResult success{};
  success.status = NavStatus::Success;
  success.message = "Navigation tile cache built.";
  success.tile_count = static_cast<uint32_t>(tile_width * tile_height);
  success.layer_count = layer_count;
  success.compressed_bytes = compressed_bytes;
  success.raw_bytes = raw_bytes;
  success.navmesh_bytes = navmesh_bytes;
  impl_->last_result = success;
  impl_->geometry = geometry;
  impl_->nav_config = nav_config;
  impl_->nav_config.build_mode = NavMeshBuildMode::Tiled;
  impl_->cache_config = cache_config;
  if (result != nullptr) {
    *result = success;
  }
  return true;
}

}  // namespace karma::navigation
