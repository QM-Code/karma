#pragma once

#include "karma/math.h"
#include "karma/world.h"
#include "karma/rendering.h"

namespace karma::assets { class AssetRegistry; }



#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>


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
/// Per-edge traversal data supplied to dynamic path-cost providers.
struct NavTraversalContext {
  uint64_t previous_poly_ref = 0;
  uint64_t current_poly_ref = 0;
  uint64_t next_poly_ref = 0;
  math::Vec3 from{};
  math::Vec3 to{};
  float base_distance = 0.0f;
  unsigned char area = kNavAreaDefault;
  float area_cost = 1.0f;
  float base_cost = 0.0f;
  const NavQueryFilter* filter = nullptr;
};

/// \ingroup karma_navigation
/// Dynamic traversal-cost provider called by Detour during path search.
class NavTraversalCostProvider {
 public:
  virtual ~NavTraversalCostProvider() = default;

  /// Returns the final cost for traversing `context.from` to `context.to`.
  virtual float traversalCost(const NavTraversalContext& context) const = 0;
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

/// Human-readable name for a navigation status.
const char* navStatusName(NavStatus status);
/// Human-readable name for a navigation debug draw mode.
const char* navMeshDebugDrawModeName(NavMeshDebugDrawMode mode);

}  // namespace karma::navigation


#include <cstdint>
#include <vector>


namespace karma::navigation {

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

}  // namespace karma::navigation


#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


class dtNavMesh;

namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::navigation {

class NavCrowd;
class NavQuery;
class NavTileCache;

namespace detail {
struct NavMeshAccess;
}

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
/// Build metadata stored beside serialized navmesh payloads.
struct NavMeshSnapshotMetadata {
  NavMeshBuildConfig build_config{};
  NavMeshBuildResult build_result{};
  math::Vec3 bounds_min{};
  math::Vec3 bounds_max{};
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
  /// Rehydrates a navmesh from snapshot data and restores build metadata.
  bool loadSnapshot(const NavMeshSnapshot& snapshot,
                    const NavMeshSnapshotMetadata& metadata,
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
  void debugDraw(rendering::GraphicsDevice& graphics,
                 const math::Color& color = {0.1f, 0.85f, 0.35f, 1.0f},
                 bool depth_test = true) const;
  /// Draws a selected Recast/Detour debug layer through the graphics device.
  void debugDraw(rendering::GraphicsDevice& graphics,
                 NavMeshDebugDrawMode mode,
                 bool depth_test = true,
                 const math::Color& fallback_color = {0.1f, 0.85f, 0.35f, 1.0f}) const;
  /// Draws highlighted Detour polygon references through the graphics device.
  void debugDrawPolygons(rendering::GraphicsDevice& graphics,
                         const std::vector<uint64_t>& poly_refs,
                         const math::Color& color = {0.0f, 0.0f, 0.0f, 0.35f},
                         bool depth_test = false) const;

 private:
  friend struct detail::NavMeshAccess;
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

}  // namespace karma::navigation


#include <cstdint>
#include <memory>
#include <vector>


class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::navigation {

class NavMesh;
struct NavMeshSnapshot;

/// \ingroup karma_navigation
/// Path or raycast result.
struct NavPath {
  NavStatus status = NavStatus::QueryFailed;
  std::vector<math::Vec3> points;
  std::vector<uint8_t> point_flags;
  std::vector<float> point_speed_multipliers;
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
                   int straight_path_options = NavStraightPathOptionNone,
                   const NavTraversalCostProvider* traversal_cost_provider = nullptr) const;
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
  static void debugDrawPath(rendering::GraphicsDevice& graphics,
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


#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::navigation {

class NavMesh;

/// \ingroup karma_navigation
/// Crowd steering feature bits mirroring DetourCrowd update flags.
enum NavCrowdUpdateFlag : uint8_t {
  NavCrowdUpdateFlagNone = 0,
  NavCrowdUpdateFlagAnticipateTurns = 1u << 0u,
  NavCrowdUpdateFlagObstacleAvoidance = 1u << 1u,
  NavCrowdUpdateFlagSeparation = 1u << 2u,
  NavCrowdUpdateFlagOptimizeVisibility = 1u << 3u,
  NavCrowdUpdateFlagOptimizeTopology = 1u << 4u,
};

/// \ingroup karma_navigation
/// Agent navmesh traversal state reported by DetourCrowd.
enum class NavCrowdAgentState {
  Invalid,
  Walking,
  OffMesh,
};

/// \ingroup karma_navigation
/// Agent move-request state reported by DetourCrowd.
enum class NavCrowdTargetState {
  None,
  Failed,
  Valid,
  Requesting,
  WaitingForQueue,
  WaitingForPath,
  Velocity,
};

/// \ingroup karma_navigation
/// Obstacle-avoidance sampler configuration for one DetourCrowd quality slot.
struct NavCrowdObstacleAvoidanceParams {
  float velocity_bias = 0.5f;
  float weight_desired_velocity = 2.0f;
  float weight_current_velocity = 0.75f;
  float weight_side = 0.75f;
  float weight_time_of_impact = 2.5f;
  float horizontal_time = 2.5f;
  uint8_t grid_size = 33;
  uint8_t adaptive_divisions = 7;
  uint8_t adaptive_rings = 2;
  uint8_t adaptive_depth = 2;
};

/// \ingroup karma_navigation
/// Shared crowd configuration.
struct NavCrowdConfig {
  int max_agents = 64;
  float max_agent_radius = 0.6f;
  math::Vec3 query_extents{2.0f, 4.0f, 2.0f};
  std::vector<NavQueryFilter> query_filters;
  std::vector<NavCrowdObstacleAvoidanceParams> avoidance_params;
};

/// \ingroup karma_navigation
/// Per-agent crowd steering configuration.
struct NavCrowdAgentParams {
  float radius = 0.6f;
  float height = 2.0f;
  float max_acceleration = 8.0f;
  float max_speed = 3.5f;
  float collision_query_range = 0.0f;
  float path_optimization_range = 0.0f;
  float separation_weight = 2.0f;
  uint8_t update_flags = NavCrowdUpdateFlagAnticipateTurns |
                         NavCrowdUpdateFlagObstacleAvoidance |
                         NavCrowdUpdateFlagOptimizeVisibility |
                         NavCrowdUpdateFlagOptimizeTopology;
  uint8_t obstacle_avoidance_type = 2;
  uint8_t query_filter_type = 0;
};

/// \ingroup karma_navigation
/// Crowd initialization result.
struct NavCrowdBuildResult {
  NavStatus status = NavStatus::BuildFailed;
  std::string message;
  uint32_t agent_capacity = 0;
};

/// \ingroup karma_navigation
/// Runtime crowd agent state.
struct NavCrowdAgentInfo {
  int agent_id = -1;
  bool active = false;
  bool partial = false;
  NavCrowdAgentState state = NavCrowdAgentState::Invalid;
  NavCrowdTargetState target_state = NavCrowdTargetState::None;
  math::Vec3 position{};
  math::Vec3 velocity{};
  math::Vec3 desired_velocity{};
  math::Vec3 adjusted_desired_velocity{};
  math::Vec3 target_position{};
  float desired_speed = 0.0f;
  float radius = 0.0f;
  float height = 0.0f;
  int neighbor_count = 0;
  int corner_count = 0;
};

/// \ingroup karma_navigation
/// Crowd corner point with Detour straight-path flags and polygon reference.
struct NavCrowdDebugCorner {
  math::Vec3 position{};
  uint8_t flags = 0;
  uint64_t poly_ref = 0;
};

/// \ingroup karma_navigation
/// Crowd local-boundary collision segment.
struct NavCrowdDebugSegment {
  math::Vec3 start{};
  math::Vec3 end{};
};

/// \ingroup karma_navigation
/// Crowd neighbour link.
struct NavCrowdDebugNeighbour {
  int agent_id = -1;
  float distance = 0.0f;
};

/// \ingroup karma_navigation
/// Per-agent DetourCrowd debug data.
struct NavCrowdDebugAgent {
  int agent_id = -1;
  std::vector<uint64_t> corridor_polys;
  std::vector<NavCrowdDebugCorner> corners;
  std::vector<NavCrowdDebugSegment> boundary_segments;
  std::vector<NavCrowdDebugNeighbour> neighbours;
};

/// \ingroup karma_navigation
/// Debug capture request for crowd diagnostics.
struct NavCrowdDebugRequest {
  bool enabled = false;
  bool all_agents = true;
  int selected_agent_id = -1;
  bool include_corridor = true;
  bool include_corners = true;
  bool include_collision_segments = true;
  bool include_neighbours = true;
};

/// \ingroup karma_navigation
/// Snapshot of requested DetourCrowd debug data.
struct NavCrowdDebugSnapshot {
  std::vector<NavCrowdDebugAgent> agents;

  bool empty() const { return agents.empty(); }
};

/// \ingroup karma_navigation
/// DetourCrowd wrapper for local steering and dynamic avoidance over a `NavMesh`.
class NavCrowd {
 public:
  NavCrowd();
  ~NavCrowd();

  NavCrowd(const NavCrowd&) = delete;
  NavCrowd& operator=(const NavCrowd&) = delete;
  NavCrowd(NavCrowd&&) noexcept;
  NavCrowd& operator=(NavCrowd&&) noexcept;

  /// Initializes crowd steering against a valid navmesh.
  bool init(NavMesh& nav_mesh,
            const NavCrowdConfig& config = {},
            NavCrowdBuildResult* result = nullptr);
  /// Releases DetourCrowd state.
  void reset();
  /// Returns true after successful initialization.
  bool isValid() const;
  /// Maximum configured agent count.
  int agentCapacity() const;
  /// Current active agent count.
  int activeAgentCount() const;

  /// Adds a crowd agent at a navmesh position, returning its Detour slot id or -1.
  int addAgent(const math::Vec3& position, const NavCrowdAgentParams& params = {});
  /// Updates a crowd agent's steering parameters.
  bool updateAgentParams(int agent_id, const NavCrowdAgentParams& params);
  /// Removes a crowd agent slot.
  void removeAgent(int agent_id);
  /// Requests a navmesh target for one crowd agent.
  bool requestMoveTarget(int agent_id,
                         const math::Vec3& target,
                         const math::Vec3& search_extents = {0.0f, 0.0f, 0.0f});
  /// Requests direct velocity steering for one crowd agent.
  bool requestMoveVelocity(int agent_id, const math::Vec3& velocity);
  /// Clears a crowd agent's target/velocity request.
  bool resetMoveTarget(int agent_id);
  /// Advances crowd steering.
  void update(float dt);
  /// Captures debug data from active agents.
  NavCrowdDebugSnapshot debugSnapshot(const NavCrowdDebugRequest& request = {}) const;
  /// Returns current state for one agent.
  bool agentInfo(int agent_id, NavCrowdAgentInfo& out_info) const;
  /// Returns all active agents.
  std::vector<NavCrowdAgentInfo> agents() const;
  /// Updates one query filter slot.
  bool setQueryFilter(uint8_t filter_index, const NavQueryFilter& filter);
  /// Updates one obstacle avoidance quality slot.
  bool setObstacleAvoidanceParams(uint8_t slot, const NavCrowdObstacleAvoidanceParams& params);
  /// Metadata from the last initialization attempt.
  const NavCrowdBuildResult& lastBuildResult() const;
  /// Draws active crowd agents, velocities, and move targets through the graphics device.
  void debugDraw(rendering::GraphicsDevice& graphics,
                 const math::Color& agent_color = {0.2f, 0.65f, 1.0f, 1.0f},
                 const math::Color& velocity_color = {0.95f, 0.95f, 0.15f, 1.0f},
                 bool depth_test = false) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::navigation


#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace karma::rendering {
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
  void debugDraw(rendering::GraphicsDevice& graphics,
                 const math::Color& color = {0.96f, 0.45f, 0.12f, 1.0f},
                 bool depth_test = false,
                 bool draw_tile_bounds = true) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::navigation


#include <cstdint>


namespace karma::world {
class World;
}

namespace karma::assets {
class AssetRegistry;
}

namespace karma::navigation {

/// \ingroup karma_navigation
/// Appends transformed mesh triangles to navmesh input geometry.
void appendGeometry(NavMeshInputGeometry& out,
                    const world::MeshData& mesh,
                    const math::Vec3& position = {},
                    const math::Quat& rotation = {},
                    const math::Vec3& scale = {1.0f, 1.0f, 1.0f},
                    unsigned char area = kNavAreaDefault);

/// Collects navmesh geometry from ECS navmesh surface/off-mesh-link components.
NavMeshInputGeometry collectNavMeshGeometry(const world::World& world,
                                            uint32_t source_mask = 0xffffffffu);
/// Collects navmesh geometry from ECS components, resolving mesh asset keys
/// through the runtime asset registry when embedded mesh data is not present.
NavMeshInputGeometry collectNavMeshGeometry(const world::World& world,
                                            const assets::AssetRegistry* assets,
                                            uint32_t source_mask = 0xffffffffu);

}  // namespace karma::navigation


#include <cstdint>
#include <memory>
#include <string_view>


namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::assets {
class AssetRegistry;
}

namespace karma::navigation {

/// \ingroup karma_navigation
/// Runtime counters and timings for `NavigationSystem`.
struct NavigationSystemStats {
  double last_update_ms = 0.0;
  double last_rebuild_ms = 0.0;
  double last_submit_ms = 0.0;
  double last_move_ms = 0.0;
  double last_apply_ms = 0.0;
  double last_worker_queue_wait_ms = 0.0;
  double last_worker_solve_ms = 0.0;
  double last_cache_read_ms = 0.0;
  double last_cache_write_ms = 0.0;
  uint64_t submitted_requests = 0;
  uint64_t completed_requests = 0;
  uint64_t failed_requests = 0;
  uint64_t stale_results = 0;
  uint64_t pending_requests = 0;
  uint64_t cache_hits = 0;
  uint64_t cache_misses = 0;
  uint64_t cache_writes = 0;
  uint64_t last_request_id = 0;
  uint32_t last_path_point_count = 0;
  NavStatus last_path_status = NavStatus::QueryFailed;
  bool last_worker_cache_rebuilt = false;
  bool last_cache_hit = false;
  bool last_cache_miss = false;
  bool last_cache_write = false;
};

/// \ingroup karma_navigation
/// Engine-owned async navmesh rebuild/path request system.
///
/// The system consumes `NavMeshComponent` and `NavMeshAgentComponent`, queues
/// worker-thread path solves, and writes path/status data back to agents.
class NavigationSystem : public world::ISystem {
 public:
  explicit NavigationSystem(const assets::AssetRegistry* assets = nullptr);
  ~NavigationSystem() override;

  NavigationSystem(const NavigationSystem&) = delete;
  NavigationSystem& operator=(const NavigationSystem&) = delete;
  NavigationSystem(NavigationSystem&&) = delete;
  NavigationSystem& operator=(NavigationSystem&&) = delete;

  std::string_view name() const override { return "NavigationSystem"; }
  void update(world::World& world, float dt) override;

  /// Draws current navmesh/path debug information.
  void debugDraw(world::World& world,
                 rendering::GraphicsDevice& graphics,
                 bool depth_test = false) const;
  /// Latest navigation diagnostics.
  const NavigationSystemStats& stats() const { return stats_; }

  /// Requests a path for an agent entity.
  static bool requestMoveTo(world::World& world,
                            world::Entity agent_entity,
                            const math::Vec3& destination,
                            std::shared_ptr<const NavTraversalCostProvider> traversal_cost_provider = nullptr);
  /// Installs an already solved path for an agent entity without recomputing it.
  static bool requestFollowPath(world::World& world,
                                world::Entity agent_entity,
                                const NavPath& path);
  /// Clears an agent path/request state.
  static void clearPath(world::World& world, world::Entity agent_entity);
  /// Requests a crowd-controlled move target for an agent entity.
  static bool requestCrowdMoveTo(world::World& world,
                                 world::Entity agent_entity,
                                 const math::Vec3& destination);
  /// Requests direct crowd velocity steering for an agent entity.
  static bool requestCrowdVelocity(world::World& world,
                                   world::Entity agent_entity,
                                   const math::Vec3& velocity);
  /// Clears a crowd-controlled agent target/request state.
  static void clearCrowdTarget(world::World& world, world::Entity agent_entity);
  /// Requests a one-shot cache-bypassing rebuild that captures Recast build-debug lines.
  static bool requestBuildDebugDraw(world::World& world, world::Entity nav_entity);

 private:
  struct WorkerState;

  void submitPathRequests(world::World& world);
  void applyCompletedPaths(world::World& world);

  std::unique_ptr<WorkerState> worker_;
  const assets::AssetRegistry* assets_ = nullptr;
  uint64_t next_request_id_ = 1;
  NavigationSystemStats stats_{};
};

}  // namespace karma::navigation


#include <cstdint>


namespace karma::navigation {

/// \ingroup karma_navigation
/// Per-frame timings logged by navigation diagnostics.
struct NavigationDiagnosticsFrame {
  float dt = 0.0f;
  double on_update_ms = 0.0;
  double click_ms = 0.0;
  double camera_ms = 0.0;
  double debug_draw_ms = 0.0;
};

/// \ingroup karma_navigation
/// Environment-gated navigation diagnostics logger.
class NavigationDiagnostics {
 public:
  void initializeFromEnvironment();
  bool enabled() const { return enabled_; }
  void logIfChanged(const NavigationSystemStats& stats,
                    const NavigationDiagnosticsFrame& frame);

 private:
  bool enabled_ = false;
  uint64_t logged_submitted_requests_ = 0;
  uint64_t logged_completed_requests_ = 0;
  uint64_t logged_failed_requests_ = 0;
  uint64_t logged_stale_results_ = 0;
};

}  // namespace karma::navigation
