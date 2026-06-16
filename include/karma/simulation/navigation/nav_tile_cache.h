#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_geometry_types.h"
#include "karma/simulation/navigation/nav_types.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

class NavMesh;

/// \ingroup karma_navigation
/// Tile-cache layer compression mode for serialized and built cache data.
enum class NavTileCacheCompression : uint8_t {
  None,
  FastLz,
};

/// \ingroup karma_navigation
/// Build-time settings for Detour's dynamic obstacle tile cache.
struct NavTileCacheBuildConfig {
  int expected_layers_per_tile = 4;
  int max_obstacles = 128;
  int max_layers_per_tile = 32;
  size_t allocator_size = 32000;
  NavTileCacheCompression compression = NavTileCacheCompression::FastLz;
};

/// \ingroup karma_navigation
/// Result metadata from a tile-cache bake.
struct NavTileCacheBuildResult {
  NavStatus status = NavStatus::BuildFailed;
  std::string message;
  uint32_t tile_count = 0;
  uint32_t layer_count = 0;
  uint32_t compressed_bytes = 0;
  uint32_t raw_bytes = 0;
  uint32_t navmesh_bytes = 0;
};

/// \ingroup karma_navigation
/// Serialized tile-cache asset payload.
struct NavTileCacheSnapshot {
  std::vector<uint8_t> data;

  bool valid() const { return !data.empty(); }
};

/// \ingroup karma_navigation
/// Dynamic obstacle shape supported by DetourTileCache.
enum class NavTileCacheObstacleShape {
  Cylinder,
  Box,
  OrientedBox,
};

/// \ingroup karma_navigation
/// Runtime obstacle processing state mirrored from DetourTileCache.
enum class NavTileCacheObstacleState {
  Empty,
  Processing,
  Processed,
  Removing,
};

/// \ingroup karma_navigation
/// Obstacle handle and shape data exposed for diagnostics/tools.
struct NavTileCacheObstacleInfo {
  uint64_t ref = 0;
  NavTileCacheObstacleShape shape = NavTileCacheObstacleShape::Cylinder;
  NavTileCacheObstacleState state = NavTileCacheObstacleState::Empty;
  math::Vec3 position{};
  math::Vec3 half_extents{};
  float radius = 0.0f;
  float height = 0.0f;
  float yaw_radians = 0.0f;
};

/// \ingroup karma_navigation
/// Compressed tile metadata exposed for diagnostics/tools.
struct NavTileCacheTileInfo {
  uint64_t ref = 0;
  int x = 0;
  int y = 0;
  int layer = 0;
  math::Vec3 bounds_min{};
  math::Vec3 bounds_max{};
  uint32_t data_size = 0;
};

/// \ingroup karma_navigation
/// Owns DetourTileCache data and updates a tiled `NavMesh` for dynamic obstacles.
class NavTileCache {
 public:
  NavTileCache();
  ~NavTileCache();

  NavTileCache(const NavTileCache&) = delete;
  NavTileCache& operator=(const NavTileCache&) = delete;
  NavTileCache(NavTileCache&&) noexcept;
  NavTileCache& operator=(NavTileCache&&) noexcept;

  /// Builds compressed tile-cache layers and initializes `nav_mesh` with tiled Detour data.
  bool build(NavMesh& nav_mesh,
             const NavMeshInputGeometry& geometry,
             const NavMeshBuildConfig& nav_config,
             const NavTileCacheBuildConfig& cache_config = {},
             NavTileCacheBuildResult* result = nullptr);
  /// Serializes tile-cache layers, build inputs, and the current navmesh snapshot.
  NavTileCacheSnapshot snapshot(const NavMesh& nav_mesh) const;
  /// Rehydrates tile-cache state and navmesh data from a serialized snapshot.
  bool loadSnapshot(NavMesh& nav_mesh,
                    const NavTileCacheSnapshot& snapshot,
                    NavTileCacheBuildResult* result = nullptr);
  /// Releases tile-cache state.
  void reset();
  /// Returns true after a successful tile-cache build.
  bool isValid() const;

  /// Adds a cylindrical temporary obstacle.
  bool addCylinderObstacle(const math::Vec3& position,
                           float radius,
                           float height,
                           uint64_t* out_ref = nullptr);
  /// Adds an axis-aligned box temporary obstacle.
  bool addBoxObstacle(const math::Vec3& bounds_min,
                      const math::Vec3& bounds_max,
                      uint64_t* out_ref = nullptr);
  /// Adds a Y-axis oriented box temporary obstacle.
  bool addOrientedBoxObstacle(const math::Vec3& center,
                              const math::Vec3& half_extents,
                              float yaw_radians,
                              uint64_t* out_ref = nullptr);
  /// Queues removal of a temporary obstacle.
  bool removeObstacle(uint64_t ref);
  /// Queues removal of all active temporary obstacles.
  void clearObstacles();
  /// Applies pending obstacle requests and refreshes `nav_mesh` when tiles change.
  bool update(float dt, NavMesh& nav_mesh, bool* up_to_date = nullptr);

  /// Number of obstacle slots configured for the cache.
  uint32_t obstacleCapacity() const;
  /// Number of non-empty obstacles.
  uint32_t obstacleCount() const;
  /// Current obstacle diagnostics.
  std::vector<NavTileCacheObstacleInfo> obstacles() const;
  /// Number of compressed tile slots configured for the cache.
  uint32_t tileCapacity() const;
  /// Number of compressed tiles currently stored.
  uint32_t tileCount() const;
  /// Current compressed tile diagnostics.
  std::vector<NavTileCacheTileInfo> tiles() const;
  /// Metadata from the last build attempt.
  const NavTileCacheBuildResult& lastBuildResult() const;
  /// Draws compressed tile bounds and temporary obstacle diagnostics through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 const math::Color& color = {0.96f, 0.45f, 0.12f, 1.0f},
                 bool depth_test = false,
                 bool draw_tile_bounds = true) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::navigation
