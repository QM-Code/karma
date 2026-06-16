#include "karma/simulation/navigation/nav_query.h"
#include "karma/simulation/navigation/nav_mesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "detail/detour_utils.h"

namespace karma::navigation {
namespace {

using detail::failed;
using detail::kMaxPathPolys;
using detail::makeDetourFilter;
using detail::mapStraightPathFlags;
using detail::ptr;
using detail::succeeded;
using detail::toVec3;
using detail::randomUnit;

}  // namespace

NavPolyQueryResult NavQuery::queryPolygons(const math::Vec3& center,
                                           const math::Vec3& half_extents,
                                           int max_polys,
                                           const NavQueryFilter& filter) const {
  NavPolyQueryResult result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }
  if (half_extents.x < 0.0f || half_extents.y < 0.0f || half_extents.z < 0.0f) {
    result.status = NavStatus::InvalidConfig;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  const int capacity = std::max(max_polys, 1);
  std::vector<dtPolyRef> polys(static_cast<size_t>(capacity));
  int count = 0;
  if (!succeeded(query_->queryPolygons(ptr(center),
                                       ptr(half_extents),
                                       &detour_filter,
                                       polys.data(),
                                       &count,
                                       capacity))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  result.polys.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    result.polys.push_back(static_cast<uint64_t>(polys[static_cast<size_t>(i)]));
  }
  return result;
}

NavClosestPointResult NavQuery::closestPointOnPoly(uint64_t poly_ref,
                                                   const math::Vec3& point) const {
  NavClosestPointResult result{};
  if (!isValid() || poly_ref == 0) {
    result.status = isValid() ? NavStatus::InvalidStart : NavStatus::NoNavMesh;
    return result;
  }
  float closest[3]{};
  bool over_poly = false;
  if (!succeeded(query_->closestPointOnPoly(static_cast<dtPolyRef>(poly_ref),
                                           ptr(point),
                                           closest,
                                           &over_poly))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  result.point = toVec3(closest);
  result.position_over_poly = over_poly;
  return result;
}

NavClosestPointResult NavQuery::closestPointOnPolyBoundary(uint64_t poly_ref,
                                                           const math::Vec3& point) const {
  NavClosestPointResult result{};
  if (!isValid() || poly_ref == 0) {
    result.status = isValid() ? NavStatus::InvalidStart : NavStatus::NoNavMesh;
    return result;
  }
  float closest[3]{};
  if (!succeeded(query_->closestPointOnPolyBoundary(static_cast<dtPolyRef>(poly_ref),
                                                   ptr(point),
                                                   closest))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  result.point = toVec3(closest);
  result.position_over_poly = false;
  return result;
}

bool NavQuery::moveAlongSurface(const math::Vec3& start,
                                const math::Vec3& end,
                                math::Vec3& out_point,
                                const math::Vec3& search_extents,
                                const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  float nearest_start[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &start_ref,
                                         nearest_start)) ||
      start_ref == 0) {
    return false;
  }

  float result[3]{};
  std::vector<dtPolyRef> visited(static_cast<size_t>(kMaxPathPolys));
  int visited_count = 0;
  const dtStatus status = query_->moveAlongSurface(start_ref,
                                                   nearest_start,
                                                   ptr(end),
                                                   &detour_filter,
                                                   result,
                                                   visited.data(),
                                                   &visited_count,
                                                   static_cast<int>(visited.size()));
  if (!succeeded(status)) {
    return false;
  }

  out_point = toVec3(result);
  return true;
}

bool NavQuery::findHeight(const math::Vec3& point,
                          float& out_height,
                          const math::Vec3& search_extents,
                          const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(point),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &ref,
                                         nearest)) ||
      ref == 0) {
    return false;
  }

  float height = 0.0f;
  if (!succeeded(query_->getPolyHeight(ref, nearest, &height))) {
    return false;
  }
  out_height = height;
  return true;
}

bool NavQuery::findDistanceToWall(const math::Vec3& point,
                                  float max_radius,
                                  float& out_distance,
                                  math::Vec3* out_hit_position,
                                  math::Vec3* out_hit_normal,
                                  const math::Vec3& search_extents,
                                  const NavQueryFilter& filter) const {
  if (!isValid() || max_radius <= 0.0f) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(point),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &ref,
                                         nearest)) ||
      ref == 0) {
    return false;
  }

  float distance = 0.0f;
  float hit_position[3]{};
  float hit_normal[3]{};
  if (!succeeded(query_->findDistanceToWall(ref,
                                            nearest,
                                            max_radius,
                                            &detour_filter,
                                            &distance,
                                            hit_position,
                                            hit_normal))) {
    return false;
  }

  out_distance = distance;
  if (out_hit_position != nullptr) {
    *out_hit_position = toVec3(hit_position);
  }
  if (out_hit_normal != nullptr) {
    *out_hit_normal = toVec3(hit_normal);
  }
  return true;
}

bool NavQuery::findRandomPoint(math::Vec3& out_point, const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float point[3]{};
  if (!succeeded(query_->findRandomPoint(&detour_filter, randomUnit, &ref, point)) || ref == 0) {
    return false;
  }
  out_point = toVec3(point);
  return true;
}

bool NavQuery::findRandomPointAroundCircle(const math::Vec3& center,
                                           float radius,
                                           math::Vec3& out_point,
                                           const math::Vec3& search_extents,
                                           const NavQueryFilter& filter) const {
  if (!isValid() || radius <= 0.0f) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef center_ref = 0;
  float nearest_center[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(center),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &center_ref,
                                         nearest_center)) ||
      center_ref == 0) {
    return false;
  }

  dtPolyRef random_ref = 0;
  float point[3]{};
  if (!succeeded(query_->findRandomPointAroundCircle(center_ref,
                                                     nearest_center,
                                                     radius,
                                                     &detour_filter,
                                                     randomUnit,
                                                     &random_ref,
                                                     point)) ||
      random_ref == 0) {
    return false;
  }
  out_point = toVec3(point);
  return true;
}

NavPolyQueryResult NavQuery::findPolysAroundCircle(const math::Vec3& center,
                                                   float radius,
                                                   const math::Vec3& search_extents,
                                                   int max_polys,
                                                   const NavQueryFilter& filter) const {
  NavPolyQueryResult result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }
  if (radius <= 0.0f) {
    result.status = NavStatus::InvalidConfig;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef center_ref = 0;
  float nearest_center[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(center),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &center_ref,
                                         nearest_center)) ||
      center_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }

  const int capacity = std::max(max_polys, 1);
  std::vector<dtPolyRef> polys(static_cast<size_t>(capacity));
  std::vector<dtPolyRef> parents(static_cast<size_t>(capacity));
  std::vector<float> costs(static_cast<size_t>(capacity));
  int count = 0;
  if (!succeeded(query_->findPolysAroundCircle(center_ref,
                                               nearest_center,
                                               radius,
                                               &detour_filter,
                                               polys.data(),
                                               parents.data(),
                                               costs.data(),
                                               &count,
                                               capacity))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  result.polys.reserve(static_cast<size_t>(count));
  result.parents.reserve(static_cast<size_t>(count));
  result.costs.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    result.polys.push_back(static_cast<uint64_t>(polys[static_cast<size_t>(i)]));
    result.parents.push_back(static_cast<uint64_t>(parents[static_cast<size_t>(i)]));
    result.costs.push_back(costs[static_cast<size_t>(i)]);
  }
  return result;
}

NavPolyQueryResult NavQuery::findPolysAroundShape(const math::Vec3& start,
                                                  const std::vector<math::Vec3>& shape,
                                                  const math::Vec3& search_extents,
                                                  int max_polys,
                                                  const NavQueryFilter& filter) const {
  NavPolyQueryResult result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }
  if (shape.size() < 3) {
    result.status = NavStatus::InvalidConfig;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  float nearest_start[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &start_ref,
                                         nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }

  std::vector<float> shape_points;
  shape_points.reserve(shape.size() * 3u);
  for (const math::Vec3& point : shape) {
    shape_points.push_back(point.x);
    shape_points.push_back(point.y);
    shape_points.push_back(point.z);
  }

  const int capacity = std::max(max_polys, 1);
  std::vector<dtPolyRef> polys(static_cast<size_t>(capacity));
  std::vector<dtPolyRef> parents(static_cast<size_t>(capacity));
  std::vector<float> costs(static_cast<size_t>(capacity));
  int count = 0;
  if (!succeeded(query_->findPolysAroundShape(start_ref,
                                              shape_points.data(),
                                              static_cast<int>(shape.size()),
                                              &detour_filter,
                                              polys.data(),
                                              parents.data(),
                                              costs.data(),
                                              &count,
                                              capacity))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  for (int i = 0; i < count; ++i) {
    result.polys.push_back(static_cast<uint64_t>(polys[static_cast<size_t>(i)]));
    result.parents.push_back(static_cast<uint64_t>(parents[static_cast<size_t>(i)]));
    result.costs.push_back(costs[static_cast<size_t>(i)]);
  }
  return result;
}

NavPolyQueryResult NavQuery::findLocalNeighbourhood(const math::Vec3& center,
                                                    float radius,
                                                    const math::Vec3& search_extents,
                                                    int max_polys,
                                                    const NavQueryFilter& filter) const {
  NavPolyQueryResult result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }
  if (radius <= 0.0f) {
    result.status = NavStatus::InvalidConfig;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef center_ref = 0;
  float nearest_center[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(center),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &center_ref,
                                         nearest_center)) ||
      center_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }

  const int capacity = std::max(max_polys, 1);
  std::vector<dtPolyRef> polys(static_cast<size_t>(capacity));
  std::vector<dtPolyRef> parents(static_cast<size_t>(capacity));
  int count = 0;
  if (!succeeded(query_->findLocalNeighbourhood(center_ref,
                                                nearest_center,
                                                radius,
                                                &detour_filter,
                                                polys.data(),
                                                parents.data(),
                                                &count,
                                                capacity))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  for (int i = 0; i < count; ++i) {
    result.polys.push_back(static_cast<uint64_t>(polys[static_cast<size_t>(i)]));
    result.parents.push_back(static_cast<uint64_t>(parents[static_cast<size_t>(i)]));
  }
  return result;
}

NavWallSegments NavQuery::getPolyWallSegments(const math::Vec3& point,
                                              const math::Vec3& search_extents,
                                              int max_segments,
                                              const NavQueryFilter& filter) const {
  NavWallSegments result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(point),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &ref,
                                         nearest)) ||
      ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }

  const int capacity = std::max(max_segments, 1);
  std::vector<float> segment_vertices(static_cast<size_t>(capacity) * 6u);
  std::vector<dtPolyRef> segment_refs(static_cast<size_t>(capacity));
  int count = 0;
  if (!succeeded(query_->getPolyWallSegments(ref,
                                             &detour_filter,
                                             segment_vertices.data(),
                                             segment_refs.data(),
                                             &count,
                                             capacity))) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = NavStatus::Success;
  result.segments.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    const size_t base = static_cast<size_t>(i) * 6u;
    result.segments.push_back({
        .start = {segment_vertices[base], segment_vertices[base + 1], segment_vertices[base + 2]},
        .end = {segment_vertices[base + 3], segment_vertices[base + 4], segment_vertices[base + 5]},
        .neighbor_ref = static_cast<uint64_t>(segment_refs[static_cast<size_t>(i)]),
    });
  }
  return result;
}


}  // namespace karma::navigation
