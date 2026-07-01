#include "detail/nav_cache.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace karma::navigation::detail {
namespace {

constexpr uint32_t kNavMeshCacheMagic = 0x4b4e5643u;   // KNVC
constexpr uint32_t kNavTileCacheMagic = 0x4b4e5446u;   // KNTF
constexpr uint32_t kNavCacheVersion = 1;
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

struct NavMeshCacheHeader {
  uint32_t magic = kNavMeshCacheMagic;
  uint32_t version = kNavCacheVersion;
  uint32_t snapshot_size = 0;
  uint32_t area_config_count = 0;
  uint32_t result_message_size = 0;
};

struct NavTileCacheFileHeader {
  uint32_t magic = kNavTileCacheMagic;
  uint32_t version = kNavCacheVersion;
  uint32_t snapshot_size = 0;
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

struct NavMeshBuildResultPod {
  uint32_t status = 0;
  uint32_t vertex_count = 0;
  uint32_t triangle_count = 0;
  uint32_t polygon_count = 0;
};

struct NavMeshBoundsPod {
  float min[3]{};
  float max[3]{};
};

bool envFlagOff(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const std::string_view text(value);
  return text == "0" || text == "false" || text == "FALSE" ||
         text == "off" || text == "OFF";
}

bool envFlagOn(const char* value) {
  return value != nullptr && value[0] != '\0' && !envFlagOff(value);
}

std::filesystem::path homePath() {
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return home;
  }
  return {};
}

std::filesystem::path defaultUserCacheRoot() {
#if defined(_WIN32)
  if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && local[0] != '\0') {
    return std::filesystem::path(local) / "Karma" / "navigation";
  }
  return std::filesystem::path("Karma") / "navigation";
#elif defined(__APPLE__)
  const std::filesystem::path home = homePath();
  return home.empty() ? std::filesystem::path("karma/navigation")
                      : home / "Library" / "Caches" / "karma" / "navigation";
#else
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "karma" / "navigation";
  }
  const std::filesystem::path home = homePath();
  return home.empty() ? std::filesystem::path(".cache/karma/navigation")
                      : home / ".cache" / "karma" / "navigation";
#endif
}

std::filesystem::path configuredRoot() {
  if (const char* dir = std::getenv("KARMA_NAV_CACHE_DIR"); dir != nullptr && dir[0] != '\0') {
    return dir;
  }
  if (const char* asset_dir = std::getenv("KARMA_ASSET_CACHE_DIR");
      asset_dir != nullptr && asset_dir[0] != '\0') {
    return std::filesystem::path(asset_dir) / "navigation";
  }
  return defaultUserCacheRoot();
}

std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!stream && size > 0) {
    return std::nullopt;
  }
  return bytes;
}

bool fsyncFile(const std::filesystem::path& path) {
#if defined(_WIN32)
  (void)path;
  return true;
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  const bool ok = ::fsync(fd) == 0;
  ::close(fd);
  return ok;
#endif
}

bool writeAtomic(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes,
                 std::string* diagnostic) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    if (diagnostic != nullptr) {
      *diagnostic = "failed to create cache directory: " + ec.message();
    }
    return false;
  }

  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path temp =
      path.parent_path() / (path.filename().string() + ".tmp." + std::to_string(stamp));
  {
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream) {
      if (diagnostic != nullptr) {
        *diagnostic = "failed to open temp cache file: " + temp.string();
      }
      return false;
    }
    if (!bytes.empty()) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    stream.flush();
    if (!stream) {
      if (diagnostic != nullptr) {
        *diagnostic = "failed to write temp cache file: " + temp.string();
      }
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  (void)fsyncFile(temp);

  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
  }
  if (ec) {
    if (diagnostic != nullptr) {
      *diagnostic = "failed to commit cache file: " + ec.message();
    }
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
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

template <class T>
void appendVector(std::vector<uint8_t>& out, const std::vector<T>& values) {
  if (!values.empty()) {
    appendBytes(out, values.data(), values.size() * sizeof(T));
  }
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

NavMeshBuildResultPod toPod(const NavMeshBuildResult& result) {
  return {
      .status = static_cast<uint32_t>(result.status),
      .vertex_count = result.vertex_count,
      .triangle_count = result.triangle_count,
      .polygon_count = result.polygon_count,
  };
}

NavMeshBuildResult fromPod(const NavMeshBuildResultPod& pod, std::string message) {
  return {
      .status = static_cast<NavStatus>(pod.status),
      .message = std::move(message),
      .vertex_count = pod.vertex_count,
      .triangle_count = pod.triangle_count,
      .polygon_count = pod.polygon_count,
  };
}

NavMeshBoundsPod toPod(const math::Vec3& min, const math::Vec3& max) {
  return {
      .min = {min.x, min.y, min.z},
      .max = {max.x, max.y, max.z},
  };
}

void computeBounds(const NavMeshInputGeometry& geometry,
                   math::Vec3& out_min,
                   math::Vec3& out_max) {
  if (geometry.vertices.empty()) {
    out_min = {};
    out_max = {};
    return;
  }
  out_min = geometry.vertices.front();
  out_max = geometry.vertices.front();
  for (const math::Vec3& vertex : geometry.vertices) {
    out_min.x = std::min(out_min.x, vertex.x);
    out_min.y = std::min(out_min.y, vertex.y);
    out_min.z = std::min(out_min.z, vertex.z);
    out_max.x = std::max(out_max.x, vertex.x);
    out_max.y = std::max(out_max.y, vertex.y);
    out_max.z = std::max(out_max.z, vertex.z);
  }
}

uint64_t hashAppend(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

template <class T>
uint64_t hashValue(uint64_t hash, const T& value) {
  return hashAppend(hash, &value, sizeof(T));
}

uint64_t hashString(uint64_t hash, std::string_view value) {
  const uint64_t size = static_cast<uint64_t>(value.size());
  hash = hashValue(hash, size);
  return hashAppend(hash, value.data(), value.size());
}

template <class T>
uint64_t hashVector(uint64_t hash, const std::vector<T>& values) {
  const uint64_t size = static_cast<uint64_t>(values.size());
  hash = hashValue(hash, size);
  if (!values.empty()) {
    hash = hashAppend(hash, values.data(), values.size() * sizeof(T));
  }
  return hash;
}

uint64_t hashGeometry(uint64_t hash, const NavMeshInputGeometry& geometry) {
  hash = hashVector(hash, geometry.vertices);
  hash = hashVector(hash, geometry.indices);
  hash = hashVector(hash, geometry.triangle_areas);
  hash = hashValue(hash, static_cast<uint64_t>(geometry.off_mesh_connections.size()));
  for (const NavOffMeshConnection& connection : geometry.off_mesh_connections) {
    hash = hashValue(hash, connection.start);
    hash = hashValue(hash, connection.end);
    hash = hashValue(hash, connection.radius);
    hash = hashValue(hash, connection.area);
    hash = hashValue(hash, connection.flags);
    const uint8_t bidirectional = connection.bidirectional ? 1u : 0u;
    hash = hashValue(hash, bidirectional);
    hash = hashValue(hash, connection.user_id);
  }
  hash = hashValue(hash, static_cast<uint64_t>(geometry.convex_volumes.size()));
  for (const NavConvexVolume& volume : geometry.convex_volumes) {
    hash = hashVector(hash, volume.vertices);
    hash = hashValue(hash, volume.min_y);
    hash = hashValue(hash, volume.max_y);
    hash = hashValue(hash, volume.area);
  }
  return hash;
}

uint64_t hashNavConfig(uint64_t hash, const NavMeshBuildConfig& config) {
  const NavMeshBuildConfig effective = cacheEffectiveConfig(config);
  hash = hashValue(hash, static_cast<uint32_t>(effective.build_mode));
  hash = hashValue(hash, static_cast<uint32_t>(effective.partition_type));
  hash = hashValue(hash, effective.cell_size);
  hash = hashValue(hash, effective.cell_height);
  hash = hashValue(hash, effective.agent_height);
  hash = hashValue(hash, effective.agent_radius);
  hash = hashValue(hash, effective.agent_max_climb);
  hash = hashValue(hash, effective.agent_max_slope_degrees);
  hash = hashValue(hash, effective.edge_max_len);
  hash = hashValue(hash, effective.edge_max_error);
  hash = hashValue(hash, effective.region_min_size);
  hash = hashValue(hash, effective.region_merge_size);
  hash = hashValue(hash, effective.verts_per_poly);
  hash = hashValue(hash, effective.detail_sample_dist);
  hash = hashValue(hash, effective.detail_sample_max_error);
  hash = hashValue(hash, effective.default_poly_flags);
  hash = hashValue(hash, effective.off_mesh_poly_flags);
  hash = hashValue(hash, effective.tile_size);
  hash = hashValue(hash, effective.max_tiles);
  hash = hashValue(hash, effective.max_polys_per_tile);
  hash = hashValue(hash, static_cast<uint8_t>(effective.collect_build_debug_draw ? 1u : 0u));
  hash = hashValue(hash, static_cast<uint64_t>(effective.area_configs.size()));
  for (const NavAreaConfig& area : effective.area_configs) {
    hash = hashValue(hash, area.area);
    hash = hashValue(hash, area.flags);
    hash = hashValue(hash, area.cost);
  }
  return hash;
}

uint64_t hashTileConfig(uint64_t hash, const NavTileCacheBuildConfig& config) {
  hash = hashValue(hash, config.expected_layers_per_tile);
  hash = hashValue(hash, config.max_obstacles);
  hash = hashValue(hash, config.max_layers_per_tile);
  hash = hashValue(hash, static_cast<uint64_t>(config.allocator_size));
  hash = hashValue(hash, static_cast<uint32_t>(config.compression));
  return hash;
}

std::string hex64(uint64_t value) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16u, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kDigits[value & 0x0full];
    value >>= 4u;
  }
  return out;
}

}  // namespace

const NavCacheConfig& navCacheConfig() {
  static const NavCacheConfig config = [] {
    NavCacheConfig out;
    out.enabled = !envFlagOff(std::getenv("KARMA_NAV_CACHE"));
    out.root = configuredRoot();
    if (out.enabled && envFlagOn(std::getenv("KARMA_NAV_CACHE_FLUSH"))) {
      static bool flushed_once = false;
      if (!flushed_once) {
        flushed_once = true;
        std::error_code ec;
        std::filesystem::remove_all(out.root, ec);
      }
    }
    return out;
  }();
  return config;
}

NavMeshBuildConfig cacheEffectiveConfig(const NavMeshBuildConfig& config) {
  NavMeshBuildConfig effective = config;
  effective.collect_build_debug_draw = false;
  return effective;
}

std::string navMeshCacheFingerprint(const NavMeshInputGeometry& geometry,
                                    uint32_t source_mask,
                                    const NavMeshBuildConfig& config) {
  uint64_t hash = kFnvOffset;
  hash = hashString(hash, "karma-navmesh-cache-v1");
  hash = hashValue(hash, source_mask);
  hash = hashGeometry(hash, geometry);
  hash = hashNavConfig(hash, config);
  return hex64(hash);
}

std::string navTileCacheFingerprint(const NavMeshInputGeometry& geometry,
                                    uint32_t source_mask,
                                    const NavMeshBuildConfig& nav_config,
                                    const NavTileCacheBuildConfig& cache_config) {
  uint64_t hash = kFnvOffset;
  hash = hashString(hash, "karma-navtile-cache-v1");
  hash = hashValue(hash, source_mask);
  hash = hashGeometry(hash, geometry);
  hash = hashNavConfig(hash, nav_config);
  hash = hashTileConfig(hash, cache_config);
  return hex64(hash);
}

std::filesystem::path navMeshCachePath(std::string_view fingerprint) {
  return navCacheConfig().root / "navmesh" / (std::string(fingerprint) + ".knav");
}

std::filesystem::path navTileCachePath(std::string_view fingerprint) {
  return navCacheConfig().root / "tilecache" / (std::string(fingerprint) + ".kntc");
}

NavMeshSnapshotMetadata makeSnapshotMetadata(const NavMesh& nav_mesh) {
  return {
      .build_config = nav_mesh.config(),
      .build_result = nav_mesh.lastBuildResult(),
      .bounds_min = nav_mesh.boundsMin(),
      .bounds_max = nav_mesh.boundsMax(),
  };
}

NavMeshSnapshotMetadata makeSnapshotMetadata(const NavMeshInputGeometry& geometry,
                                             const NavMeshBuildConfig& config,
                                             const NavMeshBuildResult& result) {
  NavMeshSnapshotMetadata metadata;
  metadata.build_config = config;
  metadata.build_result = result;
  computeBounds(geometry, metadata.bounds_min, metadata.bounds_max);
  return metadata;
}

bool readNavMeshCache(const std::filesystem::path& path,
                      NavMeshSnapshot& snapshot,
                      NavMeshSnapshotMetadata& metadata,
                      std::string* diagnostic) {
  const std::optional<std::vector<uint8_t>> bytes = readBinaryFile(path);
  if (!bytes.has_value()) {
    return false;
  }

  size_t offset = 0;
  NavMeshCacheHeader header;
  NavMeshBuildConfigPod config_pod;
  NavMeshBuildResultPod result_pod;
  NavMeshBoundsPod bounds_pod;
  if (!readValue(*bytes, offset, header) ||
      header.magic != kNavMeshCacheMagic ||
      header.version != kNavCacheVersion ||
      !readValue(*bytes, offset, config_pod) ||
      !readValue(*bytes, offset, result_pod) ||
      !readValue(*bytes, offset, bounds_pod)) {
    if (diagnostic != nullptr) {
      *diagnostic = "invalid navmesh cache header";
    }
    return false;
  }

  metadata.build_config = fromPod(config_pod);
  if (!readVector(*bytes, offset, header.area_config_count, metadata.build_config.area_configs)) {
    if (diagnostic != nullptr) {
      *diagnostic = "invalid navmesh cache area config data";
    }
    return false;
  }

  if (offset > bytes->size() || header.result_message_size > bytes->size() - offset) {
    if (diagnostic != nullptr) {
      *diagnostic = "invalid navmesh cache result message";
    }
    return false;
  }
  std::string message(reinterpret_cast<const char*>(bytes->data() + offset),
                      header.result_message_size);
  offset += header.result_message_size;
  metadata.build_result = fromPod(result_pod, std::move(message));
  metadata.bounds_min = {bounds_pod.min[0], bounds_pod.min[1], bounds_pod.min[2]};
  metadata.bounds_max = {bounds_pod.max[0], bounds_pod.max[1], bounds_pod.max[2]};

  if (!readVector(*bytes, offset, header.snapshot_size, snapshot.data) ||
      offset != bytes->size()) {
    if (diagnostic != nullptr) {
      *diagnostic = "invalid navmesh cache snapshot payload";
    }
    snapshot.data.clear();
    return false;
  }
  return snapshot.valid();
}

bool writeNavMeshCache(const std::filesystem::path& path,
                       const NavMeshSnapshot& snapshot,
                       const NavMeshSnapshotMetadata& metadata,
                       std::string* diagnostic) {
  if (!snapshot.valid()) {
    return false;
  }

  NavMeshCacheHeader header;
  header.snapshot_size = static_cast<uint32_t>(snapshot.data.size());
  header.area_config_count = static_cast<uint32_t>(metadata.build_config.area_configs.size());
  header.result_message_size = static_cast<uint32_t>(metadata.build_result.message.size());

  std::vector<uint8_t> bytes;
  appendValue(bytes, header);
  appendValue(bytes, toPod(metadata.build_config));
  appendValue(bytes, toPod(metadata.build_result));
  appendValue(bytes, toPod(metadata.bounds_min, metadata.bounds_max));
  appendVector(bytes, metadata.build_config.area_configs);
  appendBytes(bytes, metadata.build_result.message.data(), metadata.build_result.message.size());
  appendVector(bytes, snapshot.data);
  return writeAtomic(path, bytes, diagnostic);
}

bool readNavTileCache(const std::filesystem::path& path,
                      NavTileCacheSnapshot& snapshot,
                      std::string* diagnostic) {
  const std::optional<std::vector<uint8_t>> bytes = readBinaryFile(path);
  if (!bytes.has_value()) {
    return false;
  }
  size_t offset = 0;
  NavTileCacheFileHeader header;
  if (!readValue(*bytes, offset, header) ||
      header.magic != kNavTileCacheMagic ||
      header.version != kNavCacheVersion ||
      !readVector(*bytes, offset, header.snapshot_size, snapshot.data) ||
      offset != bytes->size()) {
    if (diagnostic != nullptr) {
      *diagnostic = "invalid tile-cache cache payload";
    }
    snapshot.data.clear();
    return false;
  }
  return snapshot.valid();
}

bool writeNavTileCache(const std::filesystem::path& path,
                       const NavTileCacheSnapshot& snapshot,
                       std::string* diagnostic) {
  if (!snapshot.valid()) {
    return false;
  }
  NavTileCacheFileHeader header;
  header.snapshot_size = static_cast<uint32_t>(snapshot.data.size());
  std::vector<uint8_t> bytes;
  appendValue(bytes, header);
  appendVector(bytes, snapshot.data);
  return writeAtomic(path, bytes, diagnostic);
}

}  // namespace karma::navigation::detail
