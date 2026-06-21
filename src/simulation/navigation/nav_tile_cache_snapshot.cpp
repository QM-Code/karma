#include "karma/navigation.h"
#include "karma/navigation.h"

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <DetourAlloc.h>
#include <DetourTileCache.h>

#include "detail/detour_utils.h"
#include "detail/nav_tile_cache_impl.h"

namespace karma::navigation {

using detail::failed;
using detail::makeCompressor;
using detail::makeTileCacheAllocator;
using detail::makeTileCacheMeshProcess;
using detail::setResult;

namespace {

constexpr uint32_t kTileCacheSnapshotMagic = 0x4b4e5443u;  // KNTC
constexpr uint32_t kTileCacheSnapshotVersion = 1;

struct TileCacheSnapshotHeader {
  uint32_t magic = kTileCacheSnapshotMagic;
  uint32_t version = kTileCacheSnapshotVersion;
  uint32_t compression = 0;
  uint32_t navmesh_snapshot_size = 0;
  uint32_t vertex_count = 0;
  uint32_t index_count = 0;
  uint32_t triangle_area_count = 0;
  uint32_t off_mesh_count = 0;
  uint32_t convex_volume_count = 0;
  uint32_t area_config_count = 0;
  uint32_t tile_count = 0;
  dtTileCacheParams cache_params{};
};

struct NavMeshBuildConfigPod {
  uint32_t build_mode = 0;
  uint32_t partition_type = 0;
  float cell_size = 0.0f;
  float cell_height = 0.0f;
  float agent_height = 0.0f;
  float agent_radius = 0.0f;
  float agent_max_climb = 0.0f;
  float agent_max_slope_degrees = 0.0f;
  float edge_max_len = 0.0f;
  float edge_max_error = 0.0f;
  float region_min_size = 0.0f;
  float region_merge_size = 0.0f;
  int32_t verts_per_poly = 0;
  float detail_sample_dist = 0.0f;
  float detail_sample_max_error = 0.0f;
  uint16_t default_poly_flags = 0;
  uint16_t off_mesh_poly_flags = 0;
  int32_t tile_size = 0;
  int32_t max_tiles = 0;
  int32_t max_polys_per_tile = 0;
  uint8_t collect_build_debug_draw = 0;
};

struct NavTileCacheBuildConfigPod {
  int32_t expected_layers_per_tile = 0;
  int32_t max_obstacles = 0;
  int32_t max_layers_per_tile = 0;
  uint64_t allocator_size = 0;
  uint32_t compression = 0;
};

struct NavTileCacheBuildResultPod {
  uint32_t status = 0;
  uint32_t tile_count = 0;
  uint32_t layer_count = 0;
  uint32_t compressed_bytes = 0;
  uint32_t raw_bytes = 0;
  uint32_t navmesh_bytes = 0;
};

struct TileCacheSnapshotTileHeader {
  uint32_t data_size = 0;
};

template <class T>
void appendValue(std::vector<uint8_t>& out, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

bool readBytes(const std::vector<uint8_t>& data, size_t& offset, void* out, size_t size) {
  if (offset > data.size() || size > data.size() - offset) {
    return false;
  }
  std::memcpy(out, data.data() + offset, size);
  offset += size;
  return true;
}

template <class T>
bool readValue(const std::vector<uint8_t>& data, size_t& offset, T& out) {
  return readBytes(data, offset, &out, sizeof(T));
}

template <class T>
void appendVector(std::vector<uint8_t>& out, const std::vector<T>& values) {
  if (!values.empty()) {
    appendBytes(out, values.data(), values.size() * sizeof(T));
  }
}

template <class T>
bool readVector(const std::vector<uint8_t>& data,
                size_t& offset,
                size_t count,
                std::vector<T>& out) {
  out.resize(count);
  if (count == 0) {
    return true;
  }
  return readBytes(data, offset, out.data(), count * sizeof(T));
}

NavMeshBuildConfigPod toPod(const NavMeshBuildConfig& config) {
  return {
      .build_mode = static_cast<uint32_t>(config.build_mode),
      .partition_type = static_cast<uint32_t>(config.partition_type),
      .cell_size = config.cell_size,
      .cell_height = config.cell_height,
      .agent_height = config.agent_height,
      .agent_radius = config.agent_radius,
      .agent_max_climb = config.agent_max_climb,
      .agent_max_slope_degrees = config.agent_max_slope_degrees,
      .edge_max_len = config.edge_max_len,
      .edge_max_error = config.edge_max_error,
      .region_min_size = config.region_min_size,
      .region_merge_size = config.region_merge_size,
      .verts_per_poly = config.verts_per_poly,
      .detail_sample_dist = config.detail_sample_dist,
      .detail_sample_max_error = config.detail_sample_max_error,
      .default_poly_flags = config.default_poly_flags,
      .off_mesh_poly_flags = config.off_mesh_poly_flags,
      .tile_size = config.tile_size,
      .max_tiles = config.max_tiles,
      .max_polys_per_tile = config.max_polys_per_tile,
      .collect_build_debug_draw = config.collect_build_debug_draw ? uint8_t{1} : uint8_t{0},
  };
}

NavMeshBuildConfig fromPod(const NavMeshBuildConfigPod& pod) {
  NavMeshBuildConfig config;
  config.build_mode = static_cast<NavMeshBuildMode>(pod.build_mode);
  config.partition_type = static_cast<NavMeshPartitionType>(pod.partition_type);
  config.cell_size = pod.cell_size;
  config.cell_height = pod.cell_height;
  config.agent_height = pod.agent_height;
  config.agent_radius = pod.agent_radius;
  config.agent_max_climb = pod.agent_max_climb;
  config.agent_max_slope_degrees = pod.agent_max_slope_degrees;
  config.edge_max_len = pod.edge_max_len;
  config.edge_max_error = pod.edge_max_error;
  config.region_min_size = pod.region_min_size;
  config.region_merge_size = pod.region_merge_size;
  config.verts_per_poly = pod.verts_per_poly;
  config.detail_sample_dist = pod.detail_sample_dist;
  config.detail_sample_max_error = pod.detail_sample_max_error;
  config.default_poly_flags = pod.default_poly_flags;
  config.off_mesh_poly_flags = pod.off_mesh_poly_flags;
  config.tile_size = pod.tile_size;
  config.max_tiles = pod.max_tiles;
  config.max_polys_per_tile = pod.max_polys_per_tile;
  config.collect_build_debug_draw = pod.collect_build_debug_draw != 0;
  return config;
}

NavTileCacheBuildConfigPod toPod(const NavTileCacheBuildConfig& config) {
  return {
      .expected_layers_per_tile = config.expected_layers_per_tile,
      .max_obstacles = config.max_obstacles,
      .max_layers_per_tile = config.max_layers_per_tile,
      .allocator_size = static_cast<uint64_t>(config.allocator_size),
      .compression = static_cast<uint32_t>(config.compression),
  };
}

NavTileCacheBuildConfig fromPod(const NavTileCacheBuildConfigPod& pod) {
  NavTileCacheBuildConfig config;
  config.expected_layers_per_tile = pod.expected_layers_per_tile;
  config.max_obstacles = pod.max_obstacles;
  config.max_layers_per_tile = pod.max_layers_per_tile;
  config.allocator_size = static_cast<size_t>(pod.allocator_size);
  config.compression = static_cast<NavTileCacheCompression>(pod.compression);
  return config;
}

NavTileCacheBuildResultPod toPod(const NavTileCacheBuildResult& result) {
  return {
      .status = static_cast<uint32_t>(result.status),
      .tile_count = result.tile_count,
      .layer_count = result.layer_count,
      .compressed_bytes = result.compressed_bytes,
      .raw_bytes = result.raw_bytes,
      .navmesh_bytes = result.navmesh_bytes,
  };
}

NavTileCacheBuildResult fromPod(const NavTileCacheBuildResultPod& pod) {
  NavTileCacheBuildResult result;
  result.status = static_cast<NavStatus>(pod.status);
  result.message = "Navigation tile cache snapshot loaded.";
  result.tile_count = pod.tile_count;
  result.layer_count = pod.layer_count;
  result.compressed_bytes = pod.compressed_bytes;
  result.raw_bytes = pod.raw_bytes;
  result.navmesh_bytes = pod.navmesh_bytes;
  return result;
}

}  // namespace

NavTileCacheSnapshot NavTileCache::snapshot(const NavMesh& nav_mesh) const {
  NavTileCacheSnapshot snapshot;
  if (!isValid() || impl_->tile_cache == nullptr) {
    return snapshot;
  }
  const std::shared_ptr<const NavMeshSnapshot> nav_snapshot = nav_mesh.snapshot();
  if (nav_snapshot == nullptr || !nav_snapshot->valid()) {
    return snapshot;
  }

  std::vector<const dtCompressedTile*> tiles;
  for (int i = 0; i < impl_->tile_cache->getTileCount(); ++i) {
    const dtCompressedTile* tile = impl_->tile_cache->getTile(i);
    if (tile != nullptr && tile->header != nullptr && tile->data != nullptr && tile->dataSize > 0) {
      tiles.push_back(tile);
    }
  }

  TileCacheSnapshotHeader header;
  header.compression = static_cast<uint32_t>(impl_->cache_config.compression);
  header.navmesh_snapshot_size = static_cast<uint32_t>(nav_snapshot->data.size());
  header.vertex_count = static_cast<uint32_t>(impl_->geometry.vertices.size());
  header.index_count = static_cast<uint32_t>(impl_->geometry.indices.size());
  header.triangle_area_count = static_cast<uint32_t>(impl_->geometry.triangle_areas.size());
  header.off_mesh_count = static_cast<uint32_t>(impl_->geometry.off_mesh_connections.size());
  header.convex_volume_count = static_cast<uint32_t>(impl_->geometry.convex_volumes.size());
  header.area_config_count = static_cast<uint32_t>(impl_->nav_config.area_configs.size());
  header.tile_count = static_cast<uint32_t>(tiles.size());
  header.cache_params = *impl_->tile_cache->getParams();

  const NavMeshBuildConfigPod nav_config = toPod(impl_->nav_config);
  const NavTileCacheBuildConfigPod cache_config = toPod(impl_->cache_config);
  const NavTileCacheBuildResultPod build_result = toPod(impl_->last_result);
  appendValue(snapshot.data, header);
  appendValue(snapshot.data, nav_config);
  appendValue(snapshot.data, cache_config);
  appendValue(snapshot.data, build_result);
  appendVector(snapshot.data, impl_->geometry.vertices);
  appendVector(snapshot.data, impl_->geometry.indices);
  appendVector(snapshot.data, impl_->geometry.triangle_areas);
  appendVector(snapshot.data, impl_->geometry.off_mesh_connections);
  appendVector(snapshot.data, impl_->nav_config.area_configs);
  for (const NavConvexVolume& volume : impl_->geometry.convex_volumes) {
    const uint32_t vertex_count = static_cast<uint32_t>(volume.vertices.size());
    appendValue(snapshot.data, vertex_count);
    appendValue(snapshot.data, volume.min_y);
    appendValue(snapshot.data, volume.max_y);
    appendValue(snapshot.data, volume.area);
    appendVector(snapshot.data, volume.vertices);
  }
  appendVector(snapshot.data, nav_snapshot->data);
  for (const dtCompressedTile* tile : tiles) {
    TileCacheSnapshotTileHeader tile_header;
    tile_header.data_size = static_cast<uint32_t>(tile->dataSize);
    appendValue(snapshot.data, tile_header);
    appendBytes(snapshot.data, tile->data, static_cast<size_t>(tile->dataSize));
  }
  return snapshot;
}

bool NavTileCache::loadSnapshot(NavMesh& nav_mesh,
                                const NavTileCacheSnapshot& snapshot,
                                NavTileCacheBuildResult* result) {
  if (impl_ == nullptr) {
    impl_ = std::make_unique<Impl>();
  }
  impl_->reset();
  nav_mesh.reset();

  size_t offset = 0;
  TileCacheSnapshotHeader header;
  NavMeshBuildConfigPod nav_config_pod;
  NavTileCacheBuildConfigPod cache_config_pod;
  NavTileCacheBuildResultPod build_result_pod;
  if (!snapshot.valid() ||
      !readValue(snapshot.data, offset, header) ||
      header.magic != kTileCacheSnapshotMagic ||
      header.version != kTileCacheSnapshotVersion ||
      !readValue(snapshot.data, offset, nav_config_pod) ||
      !readValue(snapshot.data, offset, cache_config_pod) ||
      !readValue(snapshot.data, offset, build_result_pod)) {
    setResult(result, NavStatus::BuildFailed, "Invalid navigation tile-cache snapshot.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                  "Invalid navigation tile-cache snapshot."};
    return false;
  }

  NavMeshInputGeometry geometry;
  NavMeshBuildConfig nav_config = fromPod(nav_config_pod);
  NavTileCacheBuildConfig cache_config = fromPod(cache_config_pod);
  if (!readVector(snapshot.data, offset, header.vertex_count, geometry.vertices) ||
      !readVector(snapshot.data, offset, header.index_count, geometry.indices) ||
      !readVector(snapshot.data, offset, header.triangle_area_count, geometry.triangle_areas) ||
      !readVector(snapshot.data, offset, header.off_mesh_count, geometry.off_mesh_connections) ||
      !readVector(snapshot.data, offset, header.area_config_count, nav_config.area_configs)) {
    setResult(result, NavStatus::BuildFailed, "Navigation tile-cache snapshot geometry is invalid.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                  "Navigation tile-cache snapshot geometry is invalid."};
    return false;
  }

  geometry.convex_volumes.clear();
  geometry.convex_volumes.reserve(header.convex_volume_count);
  for (uint32_t i = 0; i < header.convex_volume_count; ++i) {
    uint32_t vertex_count = 0;
    NavConvexVolume volume;
    if (!readValue(snapshot.data, offset, vertex_count) ||
        !readValue(snapshot.data, offset, volume.min_y) ||
        !readValue(snapshot.data, offset, volume.max_y) ||
        !readValue(snapshot.data, offset, volume.area) ||
        !readVector(snapshot.data, offset, vertex_count, volume.vertices)) {
      setResult(result, NavStatus::BuildFailed, "Navigation tile-cache snapshot volumes are invalid.");
      impl_->last_result = result != nullptr
          ? *result
          : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                    "Navigation tile-cache snapshot volumes are invalid."};
      return false;
    }
    geometry.convex_volumes.push_back(std::move(volume));
  }

  NavMeshSnapshot nav_snapshot;
  if (!readVector(snapshot.data, offset, header.navmesh_snapshot_size, nav_snapshot.data) ||
      !nav_mesh.loadSnapshot(nav_snapshot)) {
    setResult(result, NavStatus::BuildFailed, "Navigation tile-cache navmesh snapshot is invalid.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                  "Navigation tile-cache navmesh snapshot is invalid."};
    return false;
  }

  impl_->allocator = makeTileCacheAllocator(cache_config.allocator_size);
  impl_->compressor = makeCompressor(cache_config.compression);
  impl_->mesh_process = makeTileCacheMeshProcess(geometry, nav_config);
  impl_->tile_cache = dtAllocTileCache();
  if (impl_->tile_cache == nullptr ||
      failed(impl_->tile_cache->init(&header.cache_params,
                                     impl_->allocator.get(),
                                     impl_->compressor.get(),
                                     impl_->mesh_process.get()))) {
    setResult(result, NavStatus::BuildFailed, "Failed to initialize Detour tile cache from snapshot.");
    impl_->last_result = result != nullptr
        ? *result
        : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                  "Failed to initialize Detour tile cache from snapshot."};
    return false;
  }

  for (uint32_t i = 0; i < header.tile_count; ++i) {
    TileCacheSnapshotTileHeader tile_header;
    if (!readValue(snapshot.data, offset, tile_header) ||
        tile_header.data_size == 0 ||
        offset > snapshot.data.size() ||
        tile_header.data_size > snapshot.data.size() - offset) {
      setResult(result, NavStatus::BuildFailed, "Navigation tile-cache snapshot tile data is invalid.");
      impl_->last_result = result != nullptr
          ? *result
          : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                    "Navigation tile-cache snapshot tile data is invalid."};
      return false;
    }
    unsigned char* tile_data = static_cast<unsigned char*>(dtAlloc(tile_header.data_size, DT_ALLOC_PERM));
    if (tile_data == nullptr) {
      setResult(result, NavStatus::BuildFailed, "Failed to allocate tile-cache snapshot data.");
      impl_->last_result = result != nullptr
          ? *result
          : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                    "Failed to allocate tile-cache snapshot data."};
      return false;
    }
    std::memcpy(tile_data, snapshot.data.data() + offset, tile_header.data_size);
    offset += tile_header.data_size;
    if (failed(impl_->tile_cache->addTile(tile_data,
                                          static_cast<int>(tile_header.data_size),
                                          DT_COMPRESSEDTILE_FREE_DATA,
                                          nullptr))) {
      dtFree(tile_data);
      setResult(result, NavStatus::BuildFailed, "Failed to add tile-cache snapshot tile.");
      impl_->last_result = result != nullptr
          ? *result
          : NavTileCacheBuildResult{NavStatus::BuildFailed,
                                    "Failed to add tile-cache snapshot tile."};
      return false;
    }
  }

  impl_->geometry = std::move(geometry);
  impl_->nav_config = std::move(nav_config);
  impl_->cache_config = cache_config;
  impl_->last_result = fromPod(build_result_pod);
  impl_->pending_changes = false;
  if (result != nullptr) {
    *result = impl_->last_result;
  }
  return true;
}

}  // namespace karma::navigation
