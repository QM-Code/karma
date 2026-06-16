#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_types.h"

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace karma::renderer {
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
