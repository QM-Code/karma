#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "karma/core/math/types.h"

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

/// \ingroup karma_navigation
/// Navigation area id used for non-walkable geometry.
static constexpr unsigned char kNavAreaNull = 0;
/// Default walkable navigation area id.
static constexpr unsigned char kNavAreaDefault = 1;
static constexpr unsigned char kNavAreaMax = 63;
static constexpr size_t kNavAreaCount = 64;

/// Polygon flag for walkable navigation polygons.
static constexpr uint16_t kNavPolyFlagWalk = 1u << 0u;
/// Polygon flag for Detour off-mesh connection polygons.
static constexpr uint16_t kNavPolyFlagOffMesh = 1u << 1u;
/// Mask including every navigation polygon flag.
static constexpr uint16_t kNavPolyFlagAll = 0xffffu;

/// \ingroup karma_navigation
/// Path point flags emitted by Detour path queries.
enum NavPathPointFlag : uint8_t {
  NavPathPointFlagNone = 0,
  NavPathPointFlagStart = 1u << 0u,
  NavPathPointFlagEnd = 1u << 1u,
  NavPathPointFlagOffMeshConnection = 1u << 2u,
};

/// \ingroup karma_navigation
/// Build/query status shared by navmesh operations.
enum class NavStatus {
  Success,
  InProgress,
  PartialPath,
  EmptyInput,
  InvalidConfig,
  BuildFailed,
  NoNavMesh,
  InvalidStart,
  InvalidEnd,
  NoPath,
  QueryFailed,
};

/// \ingroup karma_navigation
/// Area flag/cost configuration used when building a navmesh.
struct NavAreaConfig {
  unsigned char area = kNavAreaDefault;
  uint16_t flags = kNavPolyFlagWalk;
  float cost = 1.0f;
};

/// \ingroup karma_navigation
/// Query filter controlling included polygon flags and area costs.
struct NavQueryFilter {
  NavQueryFilter();

  uint16_t include_flags = kNavPolyFlagAll;
  uint16_t exclude_flags = 0;
  std::array<float, kNavAreaCount> area_costs{};

  /// Sets traversal cost for an area id.
  void setAreaCost(unsigned char area, float cost);
  /// Returns traversal cost for an area id.
  float areaCost(unsigned char area) const;
};

/// \ingroup karma_navigation
/// Static navmesh build settings.
struct NavMeshBuildConfig {
  float cell_size = 0.3f;
  float cell_height = 0.2f;
  float agent_height = 2.0f;
  float agent_radius = 0.6f;
  float agent_max_climb = 0.9f;
  float agent_max_slope_degrees = 45.0f;
  float edge_max_len = 12.0f;
  float edge_max_error = 1.3f;
  float region_min_size = 8.0f;
  float region_merge_size = 20.0f;
  int verts_per_poly = 6;
  float detail_sample_dist = 6.0f;
  float detail_sample_max_error = 1.0f;
  uint16_t default_poly_flags = kNavPolyFlagWalk;
  uint16_t off_mesh_poly_flags = kNavPolyFlagWalk | kNavPolyFlagOffMesh;
  std::vector<NavAreaConfig> area_configs;
};

/// Builds a query filter from build config area settings.
NavQueryFilter makeQueryFilter(const NavMeshBuildConfig& config,
                               uint16_t include_flags = kNavPolyFlagAll,
                               uint16_t exclude_flags = 0);

/// \ingroup karma_navigation
/// Authored Detour off-mesh connection.
struct NavOffMeshConnection {
  math::Vec3 start{};
  math::Vec3 end{};
  float radius = 0.4f;
  unsigned char area = kNavAreaDefault;
  uint16_t flags = kNavPolyFlagWalk | kNavPolyFlagOffMesh;
  bool bidirectional = true;
  uint32_t user_id = 0;
};

/// \ingroup karma_navigation
/// Triangle geometry and off-mesh links used to bake a navmesh.
struct NavMeshInputGeometry {
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
  std::vector<unsigned char> triangle_areas;
  std::vector<NavOffMeshConnection> off_mesh_connections;

  /// Returns true when there is not enough triangle data to build.
  bool empty() const { return vertices.empty() || indices.size() < 3; }
  /// Number of triangles represented by `indices`.
  uint32_t triangleCount() const { return static_cast<uint32_t>(indices.size() / 3u); }
};

/// \ingroup karma_navigation
/// Result metadata from a navmesh build.
struct NavMeshBuildResult {
  NavStatus status = NavStatus::BuildFailed;
  std::string message;
  uint32_t vertex_count = 0;
  uint32_t triangle_count = 0;
  uint32_t polygon_count = 0;
};

/// \ingroup karma_navigation
/// Serialized navmesh payload suitable for worker-thread queries.
struct NavMeshSnapshot {
  std::vector<uint8_t> data;

  /// Returns true when snapshot data is available.
  bool valid() const { return !data.empty(); }
};

/// \ingroup karma_navigation
/// Path or raycast result.
struct NavPath {
  NavStatus status = NavStatus::QueryFailed;
  std::vector<math::Vec3> points;
  std::vector<uint8_t> point_flags;
  bool partial = false;

  /// Returns true for complete or partial path success.
  bool success() const { return status == NavStatus::Success || status == NavStatus::PartialPath; }
};

/// Human-readable name for a navigation status.
const char* navStatusName(NavStatus status);

/// \ingroup karma_navigation
/// Owns a Recast/Detour navmesh and debug draw data.
class NavMesh {
 public:
  NavMesh();
  ~NavMesh();

  NavMesh(const NavMesh&) = delete;
  NavMesh& operator=(const NavMesh&) = delete;
  NavMesh(NavMesh&&) noexcept;
  NavMesh& operator=(NavMesh&&) noexcept;

  /// Bakes a navmesh from triangle geometry.
  bool build(const NavMeshInputGeometry& geometry,
             const NavMeshBuildConfig& config = {},
             NavMeshBuildResult* result = nullptr);
  /// Releases Detour data and clears build metadata.
  void reset();

  /// Returns true after a successful build.
  bool isValid() const;
  /// Build configuration used by the last build.
  const NavMeshBuildConfig& config() const { return config_; }
  /// Metadata from the last build attempt.
  const NavMeshBuildResult& lastBuildResult() const { return last_result_; }
  /// Serialized snapshot for read-only query workers.
  std::shared_ptr<const NavMeshSnapshot> snapshot() const { return snapshot_; }
  /// World-space minimum navmesh bounds.
  const math::Vec3& boundsMin() const { return bounds_min_; }
  /// World-space maximum navmesh bounds.
  const math::Vec3& boundsMax() const { return bounds_max_; }

  /// Draws navmesh debug edges through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 const math::Color& color = {0.1f, 0.85f, 0.35f, 1.0f},
                 bool depth_test = true) const;

 private:
  friend class NavQuery;
  dtNavMesh* nav_mesh_ = nullptr;
  NavMeshBuildConfig config_{};
  NavMeshBuildResult last_result_{};
  math::Vec3 bounds_min_{};
  math::Vec3 bounds_max_{};
  std::vector<math::Vec3> debug_edges_;
  std::shared_ptr<const NavMeshSnapshot> snapshot_;
};

/// \ingroup karma_navigation
/// Detour query wrapper for pathfinding and spatial navmesh queries.
class NavQuery {
 public:
  explicit NavQuery(const NavMesh& nav_mesh, int max_nodes = 2048);
  explicit NavQuery(const NavMeshSnapshot& snapshot, int max_nodes = 2048);
  ~NavQuery();

  NavQuery(const NavQuery&) = delete;
  NavQuery& operator=(const NavQuery&) = delete;
  NavQuery(NavQuery&&) noexcept;
  NavQuery& operator=(NavQuery&&) noexcept;

  /// Returns true when the query object has valid Detour state.
  bool isValid() const;
  /// Finds a path between two world-space points.
  NavPath findPath(const math::Vec3& start,
                   const math::Vec3& end,
                   const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                   int max_points = 256,
                   const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Performs a Detour raycast across the navmesh.
  NavPath raycast(const math::Vec3& start,
                  const math::Vec3& end,
                  const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                  int max_points = 256,
                  const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds the nearest navigable point around `point`.
  bool findNearestPoint(const math::Vec3& point,
                        math::Vec3& out_point,
                        const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                        const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Moves from start toward end along the navmesh surface.
  bool moveAlongSurface(const math::Vec3& start,
                        const math::Vec3& end,
                        math::Vec3& out_point,
                        const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                        const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds navmesh height at or near a world point.
  bool findHeight(const math::Vec3& point,
                  float& out_height,
                  const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                  const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds distance and optional hit info to the nearest wall.
  bool findDistanceToWall(const math::Vec3& point,
                          float max_radius,
                          float& out_distance,
                          math::Vec3* out_hit_position = nullptr,
                          math::Vec3* out_hit_normal = nullptr,
                          const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                          const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds a random point on the navmesh.
  bool findRandomPoint(math::Vec3& out_point, const NavQueryFilter& filter = NavQueryFilter{}) const;

  /// Begins an incremental sliced path request.
  NavStatus beginSlicedPath(const math::Vec3& start,
                            const math::Vec3& end,
                            const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                            const NavQueryFilter& filter = NavQueryFilter{});
  /// Advances a sliced path request.
  NavStatus updateSlicedPath(int max_iterations, bool& out_done);
  /// Finalizes a sliced path request and returns the path.
  NavPath finalizeSlicedPath(int max_points = 256);
  /// Cancels any active sliced path request.
  void cancelSlicedPath();
  /// Returns true while a sliced path request is active.
  bool hasActiveSlicedPath() const { return sliced_active_; }

  /// Draws path lines through the graphics device.
  static void debugDrawPath(renderer::GraphicsDevice& graphics,
                            const NavPath& path,
                            const math::Color& color = {1.0f, 0.9f, 0.1f, 1.0f},
                            bool depth_test = true);

 private:
  const NavMesh* nav_mesh_ = nullptr;
  dtNavMesh* owned_nav_mesh_ = nullptr;
  dtNavMeshQuery* query_ = nullptr;
  std::unique_ptr<dtQueryFilter> sliced_filter_;
  uint64_t sliced_start_ref_ = 0;
  uint64_t sliced_end_ref_ = 0;
  math::Vec3 sliced_start_{};
  math::Vec3 sliced_end_{};
  bool sliced_active_ = false;
};

}  // namespace karma::navigation
