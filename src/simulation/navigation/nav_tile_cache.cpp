#include "karma/simulation/navigation/nav_tile_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourStatus.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <Recast.h>

#include "karma/rendering/renderer/device.h"

#include "fastlz.h"

namespace karma::navigation {
namespace {

constexpr float kPi = 3.14159265358979323846f;
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

const float* ptr(const math::Vec3& v) {
  return &v.x;
}

math::Vec3 toVec3(const float* v) {
  return {v[0], v[1], v[2]};
}

math::Vec3 add(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 rotateYaw(const math::Vec3& point, float yaw) {
  const float c = std::cos(yaw);
  const float s = std::sin(yaw);
  return {point.x * c - point.z * s, point.y, point.x * s + point.z * c};
}

void drawCircle(renderer::GraphicsDevice& graphics,
                const math::Vec3& center,
                float radius,
                const math::Color& color,
                bool depth_test) {
  constexpr int kSegments = 32;
  math::Vec3 prev{center.x + radius, center.y, center.z};
  for (int i = 1; i <= kSegments; ++i) {
    const float angle = (static_cast<float>(i) / static_cast<float>(kSegments)) * kPi * 2.0f;
    const math::Vec3 next{
        center.x + std::cos(angle) * radius,
        center.y,
        center.z + std::sin(angle) * radius,
    };
    graphics.drawLine(prev, next, color, depth_test, 1.0f);
    prev = next;
  }
}

void drawBox(renderer::GraphicsDevice& graphics,
             const math::Vec3& center,
             const math::Vec3& half_extents,
             float yaw,
             const math::Color& color,
             bool depth_test) {
  const math::Vec3 local[8] = {
      {-half_extents.x, -half_extents.y, -half_extents.z},
      { half_extents.x, -half_extents.y, -half_extents.z},
      { half_extents.x,  half_extents.y, -half_extents.z},
      {-half_extents.x,  half_extents.y, -half_extents.z},
      {-half_extents.x, -half_extents.y,  half_extents.z},
      { half_extents.x, -half_extents.y,  half_extents.z},
      { half_extents.x,  half_extents.y,  half_extents.z},
      {-half_extents.x,  half_extents.y,  half_extents.z},
  };
  math::Vec3 corners[8];
  for (int i = 0; i < 8; ++i) {
    corners[i] = add(center, rotateYaw(local[i], yaw));
  }

  const int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };
  for (const auto& edge : edges) {
    graphics.drawLine(corners[edge[0]], corners[edge[1]], color, depth_test, 1.0f);
  }
}

bool succeeded(dtStatus status) {
  return dtStatusSucceed(status) != 0;
}

bool failed(dtStatus status) {
  return dtStatusFailed(status) != 0;
}

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

unsigned char sanitizeArea(unsigned char area) {
  if (area > kNavAreaMax) {
    return kNavAreaDefault;
  }
  return area;
}

uint16_t flagsForArea(const NavMeshBuildConfig& config, unsigned char area) {
  if (area == kNavAreaNull) {
    return 0;
  }
  for (const NavAreaConfig& area_config : config.area_configs) {
    if (area_config.area == area) {
      return area_config.flags;
    }
  }
  return config.default_poly_flags;
}

void setResult(NavTileCacheBuildResult* result,
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

std::vector<math::Vec3> buildDebugEdges(const dtNavMesh& nav_mesh) {
  std::vector<math::Vec3> edges;
  for (int tile_index = 0; tile_index < nav_mesh.getMaxTiles(); ++tile_index) {
    const dtMeshTile* tile = nav_mesh.getTile(tile_index);
    if (tile == nullptr || tile->header == nullptr || tile->verts == nullptr) {
      continue;
    }
    for (int poly_index = 0; poly_index < tile->header->polyCount; ++poly_index) {
      const dtPoly& poly = tile->polys[poly_index];
      if (poly.vertCount < 2) {
        continue;
      }
      for (int edge_index = 0; edge_index < poly.vertCount; ++edge_index) {
        const int next = (edge_index + 1) % poly.vertCount;
        edges.push_back(toVec3(&tile->verts[poly.verts[edge_index] * 3]));
        edges.push_back(toVec3(&tile->verts[poly.verts[next] * 3]));
      }
    }
  }
  return edges;
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

struct TileCacheData {
  unsigned char* data = nullptr;
  int data_size = 0;
};

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

}  // namespace

struct NavTileCache::Impl {
  ~Impl() {
    reset();
  }

  void reset() {
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

  dtTileCache* tile_cache = nullptr;
  std::unique_ptr<LinearAllocator> allocator;
  std::unique_ptr<dtTileCacheCompressor> compressor;
  std::unique_ptr<TileCacheMeshProcess> mesh_process;
  NavMeshInputGeometry geometry{};
  NavMeshBuildConfig nav_config{};
  NavTileCacheBuildConfig cache_config{};
  NavTileCacheBuildResult last_result{};
  bool pending_changes = false;
};

NavTileCache::NavTileCache()
    : impl_(std::make_unique<Impl>()) {}

NavTileCache::~NavTileCache() = default;

NavTileCache::NavTileCache(NavTileCache&&) noexcept = default;

NavTileCache& NavTileCache::operator=(NavTileCache&&) noexcept = default;

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

  impl_->allocator = std::make_unique<LinearAllocator>(cache_config.allocator_size);
  impl_->compressor = makeCompressor(cache_config.compression);
  impl_->mesh_process = std::make_unique<TileCacheMeshProcess>();
  impl_->mesh_process->configure(geometry, nav_config);

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

  nav_mesh.nav_mesh_ = detour_nav;
  nav_mesh.config_ = nav_config;
  nav_mesh.config_.build_mode = NavMeshBuildMode::Tiled;
  nav_mesh.bounds_min_ = bounds_min;
  nav_mesh.bounds_max_ = bounds_max;

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
      impl_->tile_cache->buildNavMeshTilesAt(x, y, nav_mesh.nav_mesh_);
    }
  }

  const dtNavMesh* const_nav_mesh = nav_mesh.nav_mesh_;
  uint32_t navmesh_bytes = 0;
  for (int i = 0; i < const_nav_mesh->getMaxTiles(); ++i) {
    const dtMeshTile* tile = const_nav_mesh->getTile(i);
    if (tile != nullptr && tile->header != nullptr) {
      navmesh_bytes += static_cast<uint32_t>(tile->dataSize);
    }
  }

  nav_mesh.debug_edges_ = buildDebugEdges(*nav_mesh.nav_mesh_);
  nav_mesh.refreshSnapshot();

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
  nav_mesh.last_result_ = mesh_result;

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

  impl_->allocator = std::make_unique<LinearAllocator>(cache_config.allocator_size);
  impl_->compressor = makeCompressor(cache_config.compression);
  impl_->mesh_process = std::make_unique<TileCacheMeshProcess>();
  impl_->mesh_process->configure(geometry, nav_config);
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

void NavTileCache::reset() {
  if (impl_ != nullptr) {
    impl_->reset();
  }
}

bool NavTileCache::isValid() const {
  return impl_ != nullptr && impl_->tile_cache != nullptr;
}

bool NavTileCache::addCylinderObstacle(const math::Vec3& position,
                                       float radius,
                                       float height,
                                       uint64_t* out_ref) {
  if (!isValid() || radius <= 0.0f || height <= 0.0f) {
    return false;
  }
  dtObstacleRef ref = 0;
  if (failed(impl_->tile_cache->addObstacle(ptr(position), radius, height, &ref))) {
    return false;
  }
  impl_->pending_changes = true;
  if (out_ref != nullptr) {
    *out_ref = static_cast<uint64_t>(ref);
  }
  return true;
}

bool NavTileCache::addBoxObstacle(const math::Vec3& bounds_min,
                                  const math::Vec3& bounds_max,
                                  uint64_t* out_ref) {
  if (!isValid() ||
      bounds_max.x <= bounds_min.x ||
      bounds_max.y <= bounds_min.y ||
      bounds_max.z <= bounds_min.z) {
    return false;
  }
  dtObstacleRef ref = 0;
  if (failed(impl_->tile_cache->addBoxObstacle(ptr(bounds_min), ptr(bounds_max), &ref))) {
    return false;
  }
  impl_->pending_changes = true;
  if (out_ref != nullptr) {
    *out_ref = static_cast<uint64_t>(ref);
  }
  return true;
}

bool NavTileCache::addOrientedBoxObstacle(const math::Vec3& center,
                                          const math::Vec3& half_extents,
                                          float yaw_radians,
                                          uint64_t* out_ref) {
  if (!isValid() ||
      half_extents.x <= 0.0f ||
      half_extents.y <= 0.0f ||
      half_extents.z <= 0.0f) {
    return false;
  }
  dtObstacleRef ref = 0;
  if (failed(impl_->tile_cache->addBoxObstacle(ptr(center), ptr(half_extents), yaw_radians, &ref))) {
    return false;
  }
  impl_->pending_changes = true;
  if (out_ref != nullptr) {
    *out_ref = static_cast<uint64_t>(ref);
  }
  return true;
}

bool NavTileCache::removeObstacle(uint64_t ref) {
  if (!isValid() || ref == 0) {
    return false;
  }
  if (failed(impl_->tile_cache->removeObstacle(static_cast<dtObstacleRef>(ref)))) {
    return false;
  }
  impl_->pending_changes = true;
  return true;
}

void NavTileCache::clearObstacles() {
  if (!isValid()) {
    return;
  }
  for (int i = 0; i < impl_->tile_cache->getObstacleCount(); ++i) {
    const dtTileCacheObstacle* obstacle = impl_->tile_cache->getObstacle(i);
    if (obstacle == nullptr || obstacle->state == DT_OBSTACLE_EMPTY) {
      continue;
    }
    const dtObstacleRef ref = impl_->tile_cache->getObstacleRef(obstacle);
    if (ref != 0 && succeeded(impl_->tile_cache->removeObstacle(ref))) {
      impl_->pending_changes = true;
    }
  }
}

bool NavTileCache::update(float dt, NavMesh& nav_mesh, bool* up_to_date) {
  if (!isValid() || nav_mesh.nav_mesh_ == nullptr) {
    if (up_to_date != nullptr) {
      *up_to_date = true;
    }
    return false;
  }

  bool detour_up_to_date = true;
  const bool had_pending_changes = impl_->pending_changes;
  const dtStatus status = impl_->tile_cache->update(dt, nav_mesh.nav_mesh_, &detour_up_to_date);
  if (up_to_date != nullptr) {
    *up_to_date = detour_up_to_date;
  }
  if (failed(status)) {
    return false;
  }
  if (had_pending_changes || !detour_up_to_date) {
    nav_mesh.debug_edges_ = buildDebugEdges(*nav_mesh.nav_mesh_);
    nav_mesh.refreshSnapshot();
  }
  impl_->pending_changes = !detour_up_to_date;
  return true;
}

uint32_t NavTileCache::obstacleCapacity() const {
  return isValid() ? static_cast<uint32_t>(impl_->tile_cache->getObstacleCount()) : 0;
}

uint32_t NavTileCache::obstacleCount() const {
  if (!isValid()) {
    return 0;
  }
  uint32_t count = 0;
  for (int i = 0; i < impl_->tile_cache->getObstacleCount(); ++i) {
    const dtTileCacheObstacle* obstacle = impl_->tile_cache->getObstacle(i);
    if (obstacle != nullptr && obstacle->state != DT_OBSTACLE_EMPTY) {
      ++count;
    }
  }
  return count;
}

std::vector<NavTileCacheObstacleInfo> NavTileCache::obstacles() const {
  std::vector<NavTileCacheObstacleInfo> out;
  if (!isValid()) {
    return out;
  }

  for (int i = 0; i < impl_->tile_cache->getObstacleCount(); ++i) {
    const dtTileCacheObstacle* obstacle = impl_->tile_cache->getObstacle(i);
    if (obstacle == nullptr || obstacle->state == DT_OBSTACLE_EMPTY) {
      continue;
    }
    NavTileCacheObstacleInfo info;
    info.ref = static_cast<uint64_t>(impl_->tile_cache->getObstacleRef(obstacle));
    info.shape = mapObstacleShape(obstacle->type);
    info.state = mapObstacleState(obstacle->state);
    switch (obstacle->type) {
      case DT_OBSTACLE_BOX:
        info.position = {
            (obstacle->box.bmin[0] + obstacle->box.bmax[0]) * 0.5f,
            (obstacle->box.bmin[1] + obstacle->box.bmax[1]) * 0.5f,
            (obstacle->box.bmin[2] + obstacle->box.bmax[2]) * 0.5f,
        };
        info.half_extents = {
            (obstacle->box.bmax[0] - obstacle->box.bmin[0]) * 0.5f,
            (obstacle->box.bmax[1] - obstacle->box.bmin[1]) * 0.5f,
            (obstacle->box.bmax[2] - obstacle->box.bmin[2]) * 0.5f,
        };
        break;
      case DT_OBSTACLE_ORIENTED_BOX:
        info.position = toVec3(obstacle->orientedBox.center);
        info.half_extents = toVec3(obstacle->orientedBox.halfExtents);
        info.yaw_radians = std::atan2(-2.0f * obstacle->orientedBox.rotAux[0],
                                      2.0f * obstacle->orientedBox.rotAux[1]);
        break;
      case DT_OBSTACLE_CYLINDER:
      default:
        info.position = toVec3(obstacle->cylinder.pos);
        info.radius = obstacle->cylinder.radius;
        info.height = obstacle->cylinder.height;
        break;
    }
    out.push_back(info);
  }
  return out;
}

uint32_t NavTileCache::tileCapacity() const {
  return isValid() ? static_cast<uint32_t>(impl_->tile_cache->getTileCount()) : 0;
}

uint32_t NavTileCache::tileCount() const {
  if (!isValid()) {
    return 0;
  }
  uint32_t count = 0;
  for (int i = 0; i < impl_->tile_cache->getTileCount(); ++i) {
    const dtCompressedTile* tile = impl_->tile_cache->getTile(i);
    if (tile != nullptr && tile->header != nullptr) {
      ++count;
    }
  }
  return count;
}

std::vector<NavTileCacheTileInfo> NavTileCache::tiles() const {
  std::vector<NavTileCacheTileInfo> out;
  if (!isValid()) {
    return out;
  }
  for (int i = 0; i < impl_->tile_cache->getTileCount(); ++i) {
    const dtCompressedTile* tile = impl_->tile_cache->getTile(i);
    if (tile == nullptr || tile->header == nullptr) {
      continue;
    }
    out.push_back({
        .ref = static_cast<uint64_t>(impl_->tile_cache->getTileRef(tile)),
        .x = tile->header->tx,
        .y = tile->header->ty,
        .layer = tile->header->tlayer,
        .bounds_min = toVec3(tile->header->bmin),
        .bounds_max = toVec3(tile->header->bmax),
        .data_size = static_cast<uint32_t>(tile->dataSize),
    });
  }
  return out;
}

const NavTileCacheBuildResult& NavTileCache::lastBuildResult() const {
  static const NavTileCacheBuildResult empty{};
  return impl_ != nullptr ? impl_->last_result : empty;
}

void NavTileCache::debugDraw(renderer::GraphicsDevice& graphics,
                             const math::Color& color,
                             bool depth_test,
                             bool draw_tile_bounds) const {
  if (draw_tile_bounds) {
    const math::Color bounds_color{0.96f, 0.78f, 0.18f, 0.55f};
    for (const NavTileCacheTileInfo& tile : tiles()) {
      const math::Vec3 center{
          (tile.bounds_min.x + tile.bounds_max.x) * 0.5f,
          (tile.bounds_min.y + tile.bounds_max.y) * 0.5f,
          (tile.bounds_min.z + tile.bounds_max.z) * 0.5f,
      };
      const math::Vec3 half_extents{
          (tile.bounds_max.x - tile.bounds_min.x) * 0.5f,
          (tile.bounds_max.y - tile.bounds_min.y) * 0.5f,
          (tile.bounds_max.z - tile.bounds_min.z) * 0.5f,
      };
      drawBox(graphics, center, half_extents, 0.0f, bounds_color, depth_test);
    }
  }

  for (const NavTileCacheObstacleInfo& obstacle : obstacles()) {
    switch (obstacle.shape) {
      case NavTileCacheObstacleShape::Cylinder: {
        const math::Vec3 bottom = obstacle.position;
        const math::Vec3 top{obstacle.position.x,
                             obstacle.position.y + obstacle.height,
                             obstacle.position.z};
        drawCircle(graphics, bottom, obstacle.radius, color, depth_test);
        drawCircle(graphics, top, obstacle.radius, color, depth_test);
        graphics.drawLine({bottom.x + obstacle.radius, bottom.y, bottom.z},
                          {top.x + obstacle.radius, top.y, top.z},
                          color,
                          depth_test,
                          1.0f);
        graphics.drawLine({bottom.x - obstacle.radius, bottom.y, bottom.z},
                          {top.x - obstacle.radius, top.y, top.z},
                          color,
                          depth_test,
                          1.0f);
        graphics.drawLine({bottom.x, bottom.y, bottom.z + obstacle.radius},
                          {top.x, top.y, top.z + obstacle.radius},
                          color,
                          depth_test,
                          1.0f);
        graphics.drawLine({bottom.x, bottom.y, bottom.z - obstacle.radius},
                          {top.x, top.y, top.z - obstacle.radius},
                          color,
                          depth_test,
                          1.0f);
        break;
      }
      case NavTileCacheObstacleShape::Box:
        drawBox(graphics, obstacle.position, obstacle.half_extents, 0.0f, color, depth_test);
        break;
      case NavTileCacheObstacleShape::OrientedBox:
        drawBox(graphics,
                obstacle.position,
                obstacle.half_extents,
                obstacle.yaw_radians,
                color,
                depth_test);
        break;
    }
  }
}

}  // namespace karma::navigation
