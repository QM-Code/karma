#include "detail/nav_tile_cache_impl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourStatus.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <Recast.h>

#include "detail/detour_utils.h"

#include "fastlz.h"

namespace karma::navigation::detail {

bool validNavConfig(const NavMeshBuildConfig& config) {
  if (config.cell_size <= 0.0f ||
      config.cell_height <= 0.0f ||
      config.agent_height <= 0.0f ||
      config.agent_radius < 0.0f ||
      config.agent_max_climb < 0.0f ||
      config.agent_max_slope_degrees < 0.0f ||
      config.agent_max_slope_degrees > 90.0f ||
      config.verts_per_poly < 3 ||
      config.verts_per_poly > DT_VERTS_PER_POLYGON ||
      config.tile_size <= 0) {
    return false;
  }
  for (const NavAreaConfig& area_config : config.area_configs) {
    if (area_config.area == kNavAreaNull ||
        area_config.area > kNavAreaMax ||
        area_config.cost <= 0.0f ||
        !std::isfinite(area_config.cost)) {
      return false;
    }
  }
  return true;
}

bool validCacheConfig(const NavTileCacheBuildConfig& config) {
  return config.expected_layers_per_tile > 0 &&
         config.max_obstacles > 0 &&
         config.max_layers_per_tile > 0 &&
         config.allocator_size > 0;
}

void computeBounds(const NavMeshInputGeometry& geometry, math::Vec3& min, math::Vec3& max) {
  min = geometry.vertices.front();
  max = geometry.vertices.front();
  for (const math::Vec3& v : geometry.vertices) {
    min.x = std::min(min.x, v.x);
    min.y = std::min(min.y, v.y);
    min.z = std::min(min.z, v.z);
    max.x = std::max(max.x, v.x);
    max.y = std::max(max.y, v.y);
    max.z = std::max(max.z, v.z);
  }
}

std::vector<float> flattenVertices(const std::vector<math::Vec3>& vertices) {
  std::vector<float> out;
  out.reserve(vertices.size() * 3u);
  for (const math::Vec3& vertex : vertices) {
    out.push_back(vertex.x);
    out.push_back(vertex.y);
    out.push_back(vertex.z);
  }
  return out;
}

bool flattenIndices(const std::vector<uint32_t>& indices, std::vector<int>& out) {
  out.clear();
  out.reserve(indices.size());
  for (const uint32_t index : indices) {
    if (index > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    out.push_back(static_cast<int>(index));
  }
  return true;
}

void configureRecast(const NavMeshBuildConfig& config,
                     const math::Vec3& bounds_min,
                     const math::Vec3& bounds_max,
                     rcConfig& cfg) {
  cfg = {};
  cfg.cs = config.cell_size;
  cfg.ch = config.cell_height;
  cfg.walkableSlopeAngle = config.agent_max_slope_degrees;
  cfg.walkableHeight = static_cast<int>(std::ceil(config.agent_height / cfg.ch));
  cfg.walkableClimb = static_cast<int>(std::floor(config.agent_max_climb / cfg.ch));
  cfg.walkableRadius = static_cast<int>(std::ceil(config.agent_radius / cfg.cs));
  cfg.maxEdgeLen = static_cast<int>(config.edge_max_len / cfg.cs);
  cfg.maxSimplificationError = config.edge_max_error;
  cfg.minRegionArea = static_cast<int>(rcSqr(config.region_min_size));
  cfg.mergeRegionArea = static_cast<int>(rcSqr(config.region_merge_size));
  cfg.maxVertsPerPoly = config.verts_per_poly;
  cfg.detailSampleDist = config.detail_sample_dist < 0.9f
      ? 0.0f
      : cfg.cs * config.detail_sample_dist;
  cfg.detailSampleMaxError = cfg.ch * config.detail_sample_max_error;
  std::memcpy(cfg.bmin, ptr(bounds_min), sizeof(cfg.bmin));
  std::memcpy(cfg.bmax, ptr(bounds_max), sizeof(cfg.bmax));
  rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
}

void markConvexVolumes(rcContext& context,
                       const NavMeshInputGeometry& geometry,
                       rcCompactHeightfield& compact) {
  for (const NavConvexVolume& volume : geometry.convex_volumes) {
    if (volume.vertices.size() < 3 || volume.area == kNavAreaNull) {
      continue;
    }
    std::vector<float> verts;
    verts.reserve(volume.vertices.size() * 3u);
    for (const math::Vec3& vertex : volume.vertices) {
      verts.push_back(vertex.x);
      verts.push_back(vertex.y);
      verts.push_back(vertex.z);
    }
    rcMarkConvexPolyArea(&context,
                         verts.data(),
                         static_cast<int>(volume.vertices.size()),
                         volume.min_y,
                         volume.max_y,
                         sanitizeArea(volume.area),
                         compact);
  }
}

void fillOffMeshConnectionArrays(const NavMeshInputGeometry& geometry,
                                 const NavMeshBuildConfig& config,
                                 std::vector<float>& vertices,
                                 std::vector<float>& radii,
                                 std::vector<unsigned short>& flags,
                                 std::vector<unsigned char>& areas,
                                 std::vector<unsigned char>& dirs,
                                 std::vector<unsigned int>& user_ids) {
  vertices.clear();
  radii.clear();
  flags.clear();
  areas.clear();
  dirs.clear();
  user_ids.clear();
  vertices.reserve(geometry.off_mesh_connections.size() * 6u);
  radii.reserve(geometry.off_mesh_connections.size());
  flags.reserve(geometry.off_mesh_connections.size());
  areas.reserve(geometry.off_mesh_connections.size());
  dirs.reserve(geometry.off_mesh_connections.size());
  user_ids.reserve(geometry.off_mesh_connections.size());
  for (const NavOffMeshConnection& connection : geometry.off_mesh_connections) {
    if (connection.radius <= 0.0f || connection.area == kNavAreaNull) {
      continue;
    }
    const unsigned char area = sanitizeArea(connection.area);
    vertices.insert(vertices.end(),
                    {connection.start.x,
                     connection.start.y,
                     connection.start.z,
                     connection.end.x,
                     connection.end.y,
                     connection.end.z});
    radii.push_back(connection.radius);
    flags.push_back(connection.flags != 0 ? connection.flags : config.off_mesh_poly_flags);
    areas.push_back(area);
    dirs.push_back(connection.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
    user_ids.push_back(connection.user_id);
  }
}

int nextPow2(int value) {
  int out = 1;
  while (out < value) {
    out <<= 1;
  }
  return out;
}

int ilog2(int value) {
  int log = 0;
  while ((value >>= 1) != 0) {
    ++log;
  }
  return log;
}

int calcLayerBufferSize(int grid_width, int grid_height) {
  const int header_size = dtAlign4(sizeof(dtTileCacheLayerHeader));
  const int grid_size = grid_width * grid_height;
  return header_size + grid_size * 4;
}

NavTileCacheObstacleState mapObstacleState(unsigned char state) {
  switch (state) {
    case DT_OBSTACLE_EMPTY: return NavTileCacheObstacleState::Empty;
    case DT_OBSTACLE_PROCESSING: return NavTileCacheObstacleState::Processing;
    case DT_OBSTACLE_PROCESSED: return NavTileCacheObstacleState::Processed;
    case DT_OBSTACLE_REMOVING: return NavTileCacheObstacleState::Removing;
    default: return NavTileCacheObstacleState::Empty;
  }
}

NavTileCacheObstacleShape mapObstacleShape(unsigned char type) {
  switch (type) {
    case DT_OBSTACLE_BOX: return NavTileCacheObstacleShape::Box;
    case DT_OBSTACLE_ORIENTED_BOX: return NavTileCacheObstacleShape::OrientedBox;
    case DT_OBSTACLE_CYLINDER:
    default:
      return NavTileCacheObstacleShape::Cylinder;
  }
}

class CopyCompressor final : public dtTileCacheCompressor {
 public:
  int maxCompressedSize(const int buffer_size) override {
    return buffer_size;
  }

  dtStatus compress(const unsigned char* buffer,
                    const int buffer_size,
                    unsigned char* compressed,
                    const int max_compressed_size,
                    int* compressed_size) override {
    if (buffer_size > max_compressed_size) {
      return DT_FAILURE | DT_BUFFER_TOO_SMALL;
    }
    std::memcpy(compressed, buffer, static_cast<size_t>(buffer_size));
    *compressed_size = buffer_size;
    return DT_SUCCESS;
  }

  dtStatus decompress(const unsigned char* compressed,
                      const int compressed_size,
                      unsigned char* buffer,
                      const int max_buffer_size,
                      int* buffer_size) override {
    if (compressed_size > max_buffer_size) {
      return DT_FAILURE | DT_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, compressed, static_cast<size_t>(compressed_size));
    *buffer_size = compressed_size;
    return DT_SUCCESS;
  }
};

class FastLzCompressor final : public dtTileCacheCompressor {
 public:
  int maxCompressedSize(const int buffer_size) override {
    return std::max(66, buffer_size + buffer_size / 20 + 16);
  }

  dtStatus compress(const unsigned char* buffer,
                    const int buffer_size,
                    unsigned char* compressed,
                    const int max_compressed_size,
                    int* compressed_size) override {
    const int required = maxCompressedSize(buffer_size);
    if (required > max_compressed_size) {
      return DT_FAILURE | DT_BUFFER_TOO_SMALL;
    }
    const int size = fastlz_compress(buffer, buffer_size, compressed);
    if (size <= 0 || size > max_compressed_size) {
      return DT_FAILURE;
    }
    *compressed_size = size;
    return DT_SUCCESS;
  }

  dtStatus decompress(const unsigned char* compressed,
                      const int compressed_size,
                      unsigned char* buffer,
                      const int max_buffer_size,
                      int* buffer_size) override {
    const int size = fastlz_decompress(compressed, compressed_size, buffer, max_buffer_size);
    if (size <= 0) {
      return DT_FAILURE;
    }
    *buffer_size = size;
    return DT_SUCCESS;
  }
};

std::unique_ptr<dtTileCacheCompressor> makeCompressor(NavTileCacheCompression compression) {
  switch (compression) {
    case NavTileCacheCompression::None:
      return std::make_unique<CopyCompressor>();
    case NavTileCacheCompression::FastLz:
    default:
      return std::make_unique<FastLzCompressor>();
  }
}

class LinearAllocator final : public dtTileCacheAlloc {
 public:
  explicit LinearAllocator(size_t capacity) {
    resize(capacity);
  }

  ~LinearAllocator() override {
    dtFree(buffer_);
  }

  void resize(size_t capacity) {
    dtFree(buffer_);
    buffer_ = static_cast<unsigned char*>(dtAlloc(capacity, DT_ALLOC_PERM));
    capacity_ = capacity;
    top_ = 0;
    high_ = 0;
  }

  void reset() override {
    high_ = std::max(high_, top_);
    top_ = 0;
  }

  void* alloc(const size_t size) override {
    if (buffer_ == nullptr || top_ + size > capacity_) {
      return nullptr;
    }
    unsigned char* mem = &buffer_[top_];
    top_ += size;
    return mem;
  }

  void free(void*) override {}

  size_t highWatermark() const { return high_; }

 private:
  unsigned char* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t top_ = 0;
  size_t high_ = 0;
};

class TileCacheMeshProcess final : public dtTileCacheMeshProcess {
 public:
  void configure(const NavMeshInputGeometry& geometry,
                 const NavMeshBuildConfig& config) {
    config_ = config;
    fillOffMeshConnectionArrays(geometry,
                                config,
                                off_mesh_vertices_,
                                off_mesh_radii_,
                                off_mesh_flags_,
                                off_mesh_areas_,
                                off_mesh_dirs_,
                                off_mesh_user_ids_);
  }

  void process(dtNavMeshCreateParams* params,
               unsigned char* poly_areas,
               unsigned short* poly_flags) override {
    for (int i = 0; i < params->polyCount; ++i) {
      if (poly_areas[i] == DT_TILECACHE_WALKABLE_AREA) {
        poly_areas[i] = kNavAreaDefault;
      } else {
        poly_areas[i] = sanitizeArea(poly_areas[i]);
      }
      poly_flags[i] = flagsForArea(config_, poly_areas[i]);
    }

    params->offMeshConVerts = off_mesh_vertices_.empty() ? nullptr : off_mesh_vertices_.data();
    params->offMeshConRad = off_mesh_radii_.empty() ? nullptr : off_mesh_radii_.data();
    params->offMeshConDir = off_mesh_dirs_.empty() ? nullptr : off_mesh_dirs_.data();
    params->offMeshConAreas = off_mesh_areas_.empty() ? nullptr : off_mesh_areas_.data();
    params->offMeshConFlags = off_mesh_flags_.empty() ? nullptr : off_mesh_flags_.data();
    params->offMeshConUserID = off_mesh_user_ids_.empty() ? nullptr : off_mesh_user_ids_.data();
    params->offMeshConCount = static_cast<int>(off_mesh_radii_.size());
  }

 private:
  NavMeshBuildConfig config_{};
  std::vector<float> off_mesh_vertices_;
  std::vector<float> off_mesh_radii_;
  std::vector<unsigned short> off_mesh_flags_;
  std::vector<unsigned char> off_mesh_areas_;
  std::vector<unsigned char> off_mesh_dirs_;
  std::vector<unsigned int> off_mesh_user_ids_;
};

std::unique_ptr<dtTileCacheAlloc> makeTileCacheAllocator(size_t capacity) {
  return std::make_unique<LinearAllocator>(capacity);
}

std::unique_ptr<dtTileCacheMeshProcess> makeTileCacheMeshProcess(
    const NavMeshInputGeometry& geometry,
    const NavMeshBuildConfig& config) {
  auto process = std::make_unique<TileCacheMeshProcess>();
  process->configure(geometry, config);
  return process;
}

void freeTileData(TileCacheData& tile) {
  dtFree(tile.data);
  tile.data = nullptr;
  tile.data_size = 0;
}

std::vector<TileCacheData> rasterizeTileLayers(const NavMeshInputGeometry& geometry,
                                               const NavMeshBuildConfig& config,
                                               const math::Vec3& bounds_min,
                                               const math::Vec3& bounds_max,
                                               int tx,
                                               int ty,
                                               const rcConfig& base_cfg,
                                               dtTileCacheCompressor& compressor,
                                               int max_layers,
                                               NavTileCacheBuildResult* result) {
  std::vector<TileCacheData> out;

  std::vector<float> vertices = flattenVertices(geometry.vertices);
  std::vector<int> indices;
  if (!flattenIndices(geometry.indices, indices)) {
    setResult(result, NavStatus::BuildFailed, "Navigation indices exceed Recast limits.");
    return out;
  }

  rcContext context;
  rcConfig cfg = base_cfg;
  const float tile_world_size = static_cast<float>(config.tile_size) * config.cell_size;
  cfg.bmin[0] = bounds_min.x + static_cast<float>(tx) * tile_world_size;
  cfg.bmin[1] = bounds_min.y;
  cfg.bmin[2] = bounds_min.z + static_cast<float>(ty) * tile_world_size;
  cfg.bmax[0] = bounds_min.x + static_cast<float>(tx + 1) * tile_world_size;
  cfg.bmax[1] = bounds_max.y;
  cfg.bmax[2] = bounds_min.z + static_cast<float>(ty + 1) * tile_world_size;
  cfg.bmin[0] -= static_cast<float>(cfg.borderSize) * cfg.cs;
  cfg.bmin[2] -= static_cast<float>(cfg.borderSize) * cfg.cs;
  cfg.bmax[0] += static_cast<float>(cfg.borderSize) * cfg.cs;
  cfg.bmax[2] += static_cast<float>(cfg.borderSize) * cfg.cs;

  rcHeightfield* solid = rcAllocHeightfield();
  rcCompactHeightfield* compact = nullptr;
  rcHeightfieldLayerSet* layer_set = nullptr;

  auto cleanup_recast = [&]() {
    rcFreeHeightField(solid);
    rcFreeCompactHeightfield(compact);
    rcFreeHeightfieldLayerSet(layer_set);
  };

  const int vertex_count = static_cast<int>(geometry.vertices.size());
  const int triangle_count = static_cast<int>(geometry.triangleCount());
  if (solid == nullptr ||
      !rcCreateHeightfield(&context, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to create tile-cache heightfield.");
    return out;
  }

  std::vector<unsigned char> slope_areas(static_cast<size_t>(triangle_count), kNavAreaNull);
  rcMarkWalkableTriangles(&context,
                          cfg.walkableSlopeAngle,
                          vertices.data(),
                          vertex_count,
                          indices.data(),
                          triangle_count,
                          slope_areas.data());
  std::vector<unsigned char> triangle_areas(static_cast<size_t>(triangle_count), kNavAreaNull);
  const bool has_explicit_areas = !geometry.triangle_areas.empty();
  for (int i = 0; i < triangle_count; ++i) {
    if (slope_areas[static_cast<size_t>(i)] == RC_NULL_AREA) {
      continue;
    }
    const unsigned char requested_area =
        has_explicit_areas ? geometry.triangle_areas[static_cast<size_t>(i)] : kNavAreaDefault;
    triangle_areas[static_cast<size_t>(i)] = sanitizeArea(requested_area);
  }

  if (!rcRasterizeTriangles(&context,
                            vertices.data(),
                            vertex_count,
                            indices.data(),
                            triangle_areas.data(),
                            triangle_count,
                            *solid,
                            cfg.walkableClimb)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to rasterize tile-cache triangles.");
    return out;
  }

  rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid);
  rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
  rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);

  compact = rcAllocCompactHeightfield();
  if (compact == nullptr ||
      !rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid, *compact)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build tile-cache compact heightfield.");
    return out;
  }

  if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to erode tile-cache walkable area.");
    return out;
  }

  markConvexVolumes(context, geometry, *compact);

  layer_set = rcAllocHeightfieldLayerSet();
  if (layer_set == nullptr ||
      !rcBuildHeightfieldLayers(&context, *compact, cfg.borderSize, cfg.walkableHeight, *layer_set)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build tile-cache heightfield layers.");
    return out;
  }

  const int layer_count = std::min(layer_set->nlayers, max_layers);
  out.reserve(static_cast<size_t>(layer_count));
  for (int i = 0; i < layer_count; ++i) {
    const rcHeightfieldLayer& layer = layer_set->layers[i];
    dtTileCacheLayerHeader header{};
    header.magic = DT_TILECACHE_MAGIC;
    header.version = DT_TILECACHE_VERSION;
    header.tx = tx;
    header.ty = ty;
    header.tlayer = i;
    dtVcopy(header.bmin, layer.bmin);
    dtVcopy(header.bmax, layer.bmax);
    header.width = static_cast<unsigned char>(layer.width);
    header.height = static_cast<unsigned char>(layer.height);
    header.minx = static_cast<unsigned char>(layer.minx);
    header.maxx = static_cast<unsigned char>(layer.maxx);
    header.miny = static_cast<unsigned char>(layer.miny);
    header.maxy = static_cast<unsigned char>(layer.maxy);
    header.hmin = static_cast<unsigned short>(layer.hmin);
    header.hmax = static_cast<unsigned short>(layer.hmax);

    TileCacheData tile;
    if (failed(dtBuildTileCacheLayer(&compressor,
                                     &header,
                                     layer.heights,
                                     layer.areas,
                                     layer.cons,
                                     &tile.data,
                                     &tile.data_size))) {
      freeTileData(tile);
      setResult(result, NavStatus::BuildFailed, "Failed to compress tile-cache layer.");
      continue;
    }
    out.push_back(tile);
  }

  cleanup_recast();
  return out;
}

}  // namespace karma::navigation::detail

namespace karma::navigation {

NavTileCache::Impl::~Impl() {
  reset();
}

void NavTileCache::Impl::reset() {
  if (tile_cache != nullptr) {
    dtFreeTileCache(tile_cache);
    tile_cache = nullptr;
  }
  allocator.reset();
  compressor.reset();
  mesh_process.reset();
  last_result = {};
  pending_changes = false;
}

}  // namespace karma::navigation
