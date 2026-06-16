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

class NavCrowd;
class NavTileCache;

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
/// Straight-path vertex emission options used by Detour path queries.
enum NavStraightPathOption : int {
  NavStraightPathOptionNone = 0,
  NavStraightPathOptionAreaCrossings = 1,
  NavStraightPathOptionAllCrossings = 2,
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
/// Recast region partitioning strategy used during navmesh baking.
enum class NavMeshPartitionType {
  Watershed,
  Monotone,
  Layers,
};

/// \ingroup karma_navigation
/// Recast/Detour navmesh build topology.
enum class NavMeshBuildMode {
  Solo,
  Tiled,
};

/// \ingroup karma_navigation
/// Navigation debug draw data layers inspired by RecastDemo draw modes.
enum class NavMeshDebugDrawMode : uint8_t {
  NavMeshEdges,
  NavMesh,
  NavMeshBVTree,
  NavMeshPortals,
  Voxels,
  WalkableVoxels,
  Compact,
  CompactDistance,
  CompactRegions,
  RegionConnections,
  RawContours,
  BothContours,
  Contours,
  PolyMesh,
  PolyMeshDetail,
};

static constexpr size_t kNavMeshDebugDrawModeCount = 15;

/// \ingroup karma_navigation
/// Renderer-neutral debug line emitted by Recast/Detour debug capture.
struct NavDebugLine {
  math::Vec3 start{};
  math::Vec3 end{};
  math::Color color{0.1f, 0.85f, 0.35f, 1.0f};
  float thickness = 1.0f;
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
  NavMeshBuildMode build_mode = NavMeshBuildMode::Solo;
  NavMeshPartitionType partition_type = NavMeshPartitionType::Watershed;
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
  int tile_size = 32;
  int max_tiles = 0;
  int max_polys_per_tile = 0;
  bool collect_build_debug_draw = false;
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
/// Convex area marker applied to compact heightfields before contour extraction.
struct NavConvexVolume {
  std::vector<math::Vec3> vertices;
  float min_y = 0.0f;
  float max_y = 0.0f;
  unsigned char area = kNavAreaDefault;
};

/// \ingroup karma_navigation
/// Triangle geometry and off-mesh links used to bake a navmesh.
struct NavMeshInputGeometry {
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
  std::vector<unsigned char> triangle_areas;
  std::vector<NavOffMeshConnection> off_mesh_connections;
  std::vector<NavConvexVolume> convex_volumes;

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

/// \ingroup karma_navigation
/// Result from Detour polygon neighbourhood queries.
struct NavPolyQueryResult {
  NavStatus status = NavStatus::QueryFailed;
  std::vector<uint64_t> polys;
  std::vector<uint64_t> parents;
  std::vector<float> costs;

  bool success() const { return status == NavStatus::Success; }
};

/// \ingroup karma_navigation
/// Wall segment returned by `NavQuery::getPolyWallSegments`.
struct NavWallSegment {
  math::Vec3 start{};
  math::Vec3 end{};
  uint64_t neighbor_ref = 0;
};

/// \ingroup karma_navigation
/// Result from Detour wall segment queries.
struct NavWallSegments {
  NavStatus status = NavStatus::QueryFailed;
  std::vector<NavWallSegment> segments;

  bool success() const { return status == NavStatus::Success; }
};

/// \ingroup karma_navigation
/// Decoded Detour polygon reference fields.
struct NavPolyRefParts {
  uint32_t salt = 0;
  uint32_t tile = 0;
  uint32_t poly = 0;
};

/// \ingroup karma_navigation
/// Detour tile reference and coordinate metadata.
struct NavTileInfo {
  uint64_t ref = 0;
  int index = -1;
  int x = 0;
  int y = 0;
  int layer = 0;
  int poly_count = 0;
  int vert_count = 0;
};

/// \ingroup karma_navigation
/// Mutable Detour tile state payload from `storeTileState`.
struct NavTileStateSnapshot {
  uint64_t tile_ref = 0;
  std::vector<uint8_t> data;

  bool valid() const { return tile_ref != 0 && !data.empty(); }
};

/// \ingroup karma_navigation
/// Endpoints for a Detour off-mesh connection polygon.
struct NavOffMeshConnectionEndpoints {
  math::Vec3 start{};
  math::Vec3 end{};
};

/// \ingroup karma_navigation
/// Detailed Detour raycast result.
struct NavRaycastResult {
  NavStatus status = NavStatus::QueryFailed;
  float hit_fraction = 0.0f;
  float path_cost = 0.0f;
  math::Vec3 hit_position{};
  math::Vec3 hit_normal{};
  std::vector<uint64_t> visited_polys;

  bool hit() const { return hit_fraction >= 0.0f && hit_fraction <= 1.0f; }
  bool success() const { return status == NavStatus::Success; }
};

/// \ingroup karma_navigation
/// Closest point query result for a polygon.
struct NavClosestPointResult {
  NavStatus status = NavStatus::QueryFailed;
  math::Vec3 point{};
  bool position_over_poly = false;

  bool success() const { return status == NavStatus::Success; }
};

/// \ingroup karma_navigation
/// Portal edge endpoints between adjacent polygons.
struct NavPortalPoints {
  NavStatus status = NavStatus::QueryFailed;
  math::Vec3 left{};
  math::Vec3 right{};
  unsigned char from_type = 0;
  unsigned char to_type = 0;

  bool success() const { return status == NavStatus::Success; }
};

/// \ingroup karma_navigation
/// Options for smooth path sampling over a Detour corridor.
struct NavSmoothPathConfig {
  float step_size = 0.5f;
  float slop = 0.01f;
  int max_smooth_points = 2048;
};

/// Human-readable name for a navigation status.
const char* navStatusName(NavStatus status);
/// Human-readable name for a navigation debug draw mode.
const char* navMeshDebugDrawModeName(NavMeshDebugDrawMode mode);

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
  /// Bakes a tiled navmesh from triangle geometry.
  bool buildTiled(const NavMeshInputGeometry& geometry,
                  const NavMeshBuildConfig& config,
                  NavMeshBuildResult* result = nullptr);
  /// Rebuilds one tile containing `world_position`; valid only for tiled navmeshes.
  bool rebuildTile(const NavMeshInputGeometry& geometry,
                   const math::Vec3& world_position,
                   NavMeshBuildResult* result = nullptr);
  /// Removes one tile containing `world_position`; valid only for tiled navmeshes.
  bool removeTile(const math::Vec3& world_position);
  /// Removes all tiles from a tiled navmesh without changing its parameters.
  bool removeAllTiles();
  /// Rehydrates a navmesh from snapshot data.
  bool loadSnapshot(const NavMeshSnapshot& snapshot,
                    NavMeshBuildResult* result = nullptr);
  /// Sets Detour flags for one polygon reference.
  bool setPolyFlags(uint64_t poly_ref, uint16_t flags);
  /// Reads Detour flags for one polygon reference.
  bool getPolyFlags(uint64_t poly_ref, uint16_t& out_flags) const;
  /// Sets Detour area id for one polygon reference.
  bool setPolyArea(uint64_t poly_ref, unsigned char area);
  /// Reads Detour area id for one polygon reference.
  bool getPolyArea(uint64_t poly_ref, unsigned char& out_area) const;
  /// Decodes a Detour polygon reference into salt/tile/poly indices.
  bool decodePolyRef(uint64_t poly_ref, NavPolyRefParts& out_parts) const;
  /// Returns all active Detour tiles.
  std::vector<NavTileInfo> tiles() const;
  /// Returns the Detour tile reference at tile coordinates.
  uint64_t tileRefAt(int x, int y, int layer = 0) const;
  /// Stores mutable tile state such as polygon flags and areas.
  bool storeTileState(uint64_t tile_ref, NavTileStateSnapshot& out_state) const;
  /// Restores mutable tile state and refreshes snapshots/debug data.
  bool restoreTileState(const NavTileStateSnapshot& state);
  /// Returns off-mesh connection endpoints for a previous and off-mesh polygon pair.
  bool offMeshConnectionEndpoints(uint64_t previous_poly_ref,
                                  uint64_t off_mesh_poly_ref,
                                  NavOffMeshConnectionEndpoints& out_endpoints) const;
  /// Computes the center of one Detour polygon reference.
  bool polyCenter(uint64_t poly_ref, math::Vec3& out_center) const;
  /// Marks polygons unreachable from `start` with `disabled_flags`.
  uint32_t pruneUnreachable(const math::Vec3& start,
                            uint16_t disabled_flags,
                            const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                            const NavQueryFilter& filter = NavQueryFilter{});
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
  /// Returns true when a debug draw layer has line data.
  bool hasDebugDrawMode(NavMeshDebugDrawMode mode) const;
  /// Returns captured line data for a debug draw layer.
  const std::vector<NavDebugLine>& debugDrawLines(NavMeshDebugDrawMode mode) const;

  /// Draws navmesh debug edges through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 const math::Color& color = {0.1f, 0.85f, 0.35f, 1.0f},
                 bool depth_test = true) const;
  /// Draws a selected Recast/Detour debug layer through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 NavMeshDebugDrawMode mode,
                 bool depth_test = true,
                 const math::Color& fallback_color = {0.1f, 0.85f, 0.35f, 1.0f}) const;
  /// Draws highlighted Detour polygon references through the graphics device.
  void debugDrawPolygons(renderer::GraphicsDevice& graphics,
                         const std::vector<uint64_t>& poly_refs,
                         const math::Color& color = {0.0f, 0.0f, 0.0f, 0.35f},
                         bool depth_test = false) const;

 private:
  friend class NavQuery;
  friend class NavCrowd;
  friend class NavTileCache;
  void refreshSnapshot();
  void refreshDetourDebugDraw();
  dtNavMesh* nav_mesh_ = nullptr;
  NavMeshBuildConfig config_{};
  NavMeshBuildResult last_result_{};
  math::Vec3 bounds_min_{};
  math::Vec3 bounds_max_{};
  std::vector<math::Vec3> debug_edges_;
  std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount> debug_draw_lines_;
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
                   const NavQueryFilter& filter = NavQueryFilter{},
                   int straight_path_options = NavStraightPathOptionNone) const;
  /// Finds a smoothed path by iteratively moving along the Detour corridor.
  NavPath findSmoothPath(const math::Vec3& start,
                         const math::Vec3& end,
                         const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                         const NavSmoothPathConfig& smooth = NavSmoothPathConfig{},
                         int max_path_polys = 1024,
                         const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Performs a Detour raycast across the navmesh.
  NavPath raycast(const math::Vec3& start,
                  const math::Vec3& end,
                  const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                  int max_points = 256,
                  const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Performs a Detour raycast and exposes hit normal, visited polys, cost, and fraction.
  NavRaycastResult raycastDetailed(const math::Vec3& start,
                                   const math::Vec3& end,
                                   const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                                   int max_polys = 256,
                                   const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds polygon refs overlapping an AABB.
  NavPolyQueryResult queryPolygons(const math::Vec3& center,
                                   const math::Vec3& half_extents,
                                   int max_polys = 256,
                                   const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds the closest point on a polygon.
  NavClosestPointResult closestPointOnPoly(uint64_t poly_ref, const math::Vec3& point) const;
  /// Finds the closest point on a polygon boundary.
  NavClosestPointResult closestPointOnPolyBoundary(uint64_t poly_ref,
                                                   const math::Vec3& point) const;
  /// Finds the nearest navigable point around `point`.
  bool findNearestPoint(const math::Vec3& point,
                        math::Vec3& out_point,
                        const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                        const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds the nearest polygon reference and optionally the nearest point around `point`.
  bool findNearestPoly(const math::Vec3& point,
                       uint64_t& out_poly_ref,
                       math::Vec3* out_point = nullptr,
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
  /// Finds a random point around `center` within `radius`.
  bool findRandomPointAroundCircle(const math::Vec3& center,
                                   float radius,
                                   math::Vec3& out_point,
                                   const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                                   const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds polygon refs around a circle on the navmesh.
  NavPolyQueryResult findPolysAroundCircle(const math::Vec3& center,
                                           float radius,
                                           const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                                           int max_polys = 256,
                                           const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds polygon refs around a convex shape on the navmesh.
  NavPolyQueryResult findPolysAroundShape(const math::Vec3& start,
                                          const std::vector<math::Vec3>& shape,
                                          const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                                          int max_polys = 256,
                                          const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Finds local polygon neighbourhood around a point.
  NavPolyQueryResult findLocalNeighbourhood(const math::Vec3& center,
                                            float radius,
                                            const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                                            int max_polys = 256,
                                            const NavQueryFilter& filter = NavQueryFilter{}) const;
  /// Returns wall/portal segments for the nearest polygon.
  NavWallSegments getPolyWallSegments(const math::Vec3& point,
                                      const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                                      int max_segments = 256,
                                      const NavQueryFilter& filter = NavQueryFilter{}) const;

  /// Begins an incremental sliced path request.
  NavStatus beginSlicedPath(const math::Vec3& start,
                            const math::Vec3& end,
                            const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                            const NavQueryFilter& filter = NavQueryFilter{});
  /// Advances a sliced path request.
  NavStatus updateSlicedPath(int max_iterations, bool& out_done);
  /// Finalizes a sliced path request and returns the path.
  NavPath finalizeSlicedPath(int max_points = 256);
  /// Finalizes a sliced path request constrained by an existing corridor.
  NavPath finalizeSlicedPathPartial(const std::vector<uint64_t>& existing_path,
                                    int max_points = 256);
  /// Extracts a path from the last Dijkstra-style neighbourhood search.
  NavPolyQueryResult pathFromDijkstraSearch(uint64_t end_poly_ref, int max_polys = 256) const;
  /// Returns true when the polygon is in the query object's Detour closed list.
  bool isInClosedList(uint64_t poly_ref) const;
  /// Returns portal endpoints between adjacent polygons.
  NavPortalPoints portalPoints(uint64_t from_poly_ref, uint64_t to_poly_ref) const;
  /// Returns the midpoint of the portal edge between adjacent polygons.
  bool edgeMidPoint(uint64_t from_poly_ref, uint64_t to_poly_ref, math::Vec3& out_midpoint) const;
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
