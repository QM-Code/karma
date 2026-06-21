#include "karma/navigation.h"
#include "karma/navigation.h"

#include <algorithm>
#include <cfloat>
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

}  // namespace

NavPath NavQuery::findPath(const math::Vec3& start,
                           const math::Vec3& end,
                           const math::Vec3& search_extents,
                           int max_points,
                           const NavQueryFilter& filter,
                           int straight_path_options) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  dtPolyRef end_ref = 0;
  float nearest_start[3]{};
  float nearest_end[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &detour_filter, &start_ref, nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }
  if (!succeeded(query_->findNearestPoly(ptr(end), ptr(search_extents), &detour_filter, &end_ref, nearest_end)) ||
      end_ref == 0) {
    result.status = NavStatus::InvalidEnd;
    return result;
  }

  std::vector<dtPolyRef> polys(static_cast<size_t>(kMaxPathPolys));
  int poly_count = 0;
  if (!succeeded(query_->findPath(start_ref,
                                  end_ref,
                                  nearest_start,
                                  nearest_end,
                                  &detour_filter,
                                  polys.data(),
                                  &poly_count,
                                  static_cast<int>(polys.size()))) ||
      poly_count <= 0) {
    result.status = NavStatus::NoPath;
    return result;
  }

  const int clamped_max_points = std::max(max_points, 2);
  std::vector<float> straight(static_cast<size_t>(clamped_max_points) * 3u);
  std::vector<unsigned char> flags(static_cast<size_t>(clamped_max_points));
  std::vector<dtPolyRef> straight_polys(static_cast<size_t>(clamped_max_points));
  int point_count = 0;
  if (!succeeded(query_->findStraightPath(nearest_start,
                                          nearest_end,
                                          polys.data(),
                                          poly_count,
                                          straight.data(),
                                          flags.data(),
                                          straight_polys.data(),
                                          &point_count,
                                          clamped_max_points,
                                          straight_path_options)) ||
      point_count <= 0) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.partial = polys[static_cast<size_t>(poly_count - 1)] != end_ref;
  result.status = result.partial ? NavStatus::PartialPath : NavStatus::Success;
  result.points.reserve(static_cast<size_t>(point_count));
  result.point_flags.reserve(static_cast<size_t>(point_count));
  for (int i = 0; i < point_count; ++i) {
    result.points.push_back(toVec3(&straight[static_cast<size_t>(i) * 3u]));
    result.point_flags.push_back(mapStraightPathFlags(flags[static_cast<size_t>(i)]));
  }
  return result;
}

NavPath NavQuery::findSmoothPath(const math::Vec3& start,
                                 const math::Vec3& end,
                                 const math::Vec3& search_extents,
                                 const NavSmoothPathConfig& smooth,
                                 int max_path_polys,
                                 const NavQueryFilter& filter) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  dtPolyRef end_ref = 0;
  float nearest_start[3]{};
  float nearest_end[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &detour_filter, &start_ref, nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }
  if (!succeeded(query_->findNearestPoly(ptr(end), ptr(search_extents), &detour_filter, &end_ref, nearest_end)) ||
      end_ref == 0) {
    result.status = NavStatus::InvalidEnd;
    return result;
  }

  const int poly_capacity = std::clamp(max_path_polys, 2, kMaxPathPolys);
  std::vector<dtPolyRef> polys(static_cast<size_t>(poly_capacity));
  int poly_count = 0;
  if (!succeeded(query_->findPath(start_ref,
                                  end_ref,
                                  nearest_start,
                                  nearest_end,
                                  &detour_filter,
                                  polys.data(),
                                  &poly_count,
                                  poly_capacity)) ||
      poly_count <= 0) {
    result.status = NavStatus::NoPath;
    return result;
  }

  float current[3]{nearest_start[0], nearest_start[1], nearest_start[2]};
  float target[3]{nearest_end[0], nearest_end[1], nearest_end[2]};
  dtPolyRef current_ref = start_ref;
  result.points.push_back(toVec3(current));
  result.point_flags.push_back(NavPathPointFlagStart);

  const float step_size = std::max(smooth.step_size, 0.001f);
  const float slop = std::max(smooth.slop, 0.001f);
  const int max_smooth_points = std::max(smooth.max_smooth_points, 2);
  while (result.points.size() < static_cast<size_t>(max_smooth_points)) {
    float delta[3]{
        target[0] - current[0],
        target[1] - current[1],
        target[2] - current[2],
    };
    const float distance_2d = std::sqrt(delta[0] * delta[0] + delta[2] * delta[2]);
    if (distance_2d <= slop) {
      result.points.push_back(toVec3(target));
      result.point_flags.push_back(NavPathPointFlagEnd);
      result.status = polys[static_cast<size_t>(poly_count - 1)] == end_ref
          ? NavStatus::Success
          : NavStatus::PartialPath;
      result.partial = result.status == NavStatus::PartialPath;
      return result;
    }

    const float scale = std::min(step_size / distance_2d, 1.0f);
    float move_target[3]{
        current[0] + delta[0] * scale,
        current[1] + delta[1] * scale,
        current[2] + delta[2] * scale,
    };
    float moved[3]{};
    std::vector<dtPolyRef> visited(16);
    int visited_count = 0;
    if (!succeeded(query_->moveAlongSurface(current_ref,
                                            current,
                                            move_target,
                                            &detour_filter,
                                            moved,
                                            visited.data(),
                                            &visited_count,
                                            static_cast<int>(visited.size())))) {
      result.status = NavStatus::QueryFailed;
      return result;
    }
    if (visited_count > 0) {
      current_ref = visited[static_cast<size_t>(visited_count - 1)];
    }
    float height = moved[1];
    query_->getPolyHeight(current_ref, moved, &height);
    moved[1] = height;
    current[0] = moved[0];
    current[1] = moved[1];
    current[2] = moved[2];
    result.points.push_back(toVec3(current));
    result.point_flags.push_back(NavPathPointFlagNone);
  }

  result.status = NavStatus::PartialPath;
  result.partial = true;
  return result;
}

NavPath NavQuery::raycast(const math::Vec3& start,
                          const math::Vec3& end,
                          const math::Vec3& search_extents,
                          int max_points,
                          const NavQueryFilter& filter) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  float nearest_start[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &detour_filter, &start_ref, nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }

  const int clamped_max_points = std::clamp(max_points, 2, kMaxPathPolys);
  std::vector<dtPolyRef> polys(static_cast<size_t>(clamped_max_points));
  int poly_count = 0;
  float t = 0.0f;
  float hit_normal[3]{};
  const dtStatus status = query_->raycast(start_ref,
                                          nearest_start,
                                          ptr(end),
                                          &detour_filter,
                                          &t,
                                          hit_normal,
                                          polys.data(),
                                          &poly_count,
                                          clamped_max_points);
  if (!succeeded(status)) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.status = NavStatus::Success;
  result.points.push_back(toVec3(nearest_start));
  result.point_flags.push_back(NavPathPointFlagStart);
  if (t > 1.0f || t == FLT_MAX) {
    result.points.push_back(end);
  } else {
    const math::Vec3 hit{
        nearest_start[0] + (end.x - nearest_start[0]) * t,
        nearest_start[1] + (end.y - nearest_start[1]) * t,
        nearest_start[2] + (end.z - nearest_start[2]) * t};
    result.points.push_back(hit);
  }
  result.point_flags.push_back(NavPathPointFlagEnd);
  return result;
}

NavRaycastResult NavQuery::raycastDetailed(const math::Vec3& start,
                                           const math::Vec3& end,
                                           const math::Vec3& search_extents,
                                           int max_polys,
                                           const NavQueryFilter& filter) const {
  NavRaycastResult result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
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

  const int capacity = std::clamp(max_polys, 1, kMaxPathPolys);
  std::vector<dtPolyRef> polys(static_cast<size_t>(capacity));
  int poly_count = 0;
  float t = 0.0f;
  float hit_normal[3]{};
  const dtStatus status = query_->raycast(start_ref,
                                          nearest_start,
                                          ptr(end),
                                          &detour_filter,
                                          &t,
                                          hit_normal,
                                          polys.data(),
                                          &poly_count,
                                          capacity);
  if (!succeeded(status)) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.status = NavStatus::Success;
  result.hit_fraction = t;
  result.hit_normal = toVec3(hit_normal);
  result.hit_position = (t > 1.0f || t == FLT_MAX)
      ? end
      : math::Vec3{nearest_start[0] + (end.x - nearest_start[0]) * t,
                   nearest_start[1] + (end.y - nearest_start[1]) * t,
                   nearest_start[2] + (end.z - nearest_start[2]) * t};
  result.visited_polys.reserve(static_cast<size_t>(poly_count));
  for (int i = 0; i < poly_count; ++i) {
    result.visited_polys.push_back(static_cast<uint64_t>(polys[static_cast<size_t>(i)]));
  }
  result.path_cost = 0.0f;
  for (size_t i = 1; i < result.visited_polys.size(); ++i) {
    math::Vec3 a{};
    math::Vec3 b{};
    if (nav_mesh_ != nullptr &&
        nav_mesh_->polyCenter(result.visited_polys[i - 1], a) &&
        nav_mesh_->polyCenter(result.visited_polys[i], b)) {
      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float dz = b.z - a.z;
      result.path_cost += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return result;
}


}  // namespace karma::navigation
