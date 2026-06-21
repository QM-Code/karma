#include "karma/navigation.h"
#include "karma/navigation.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <DetourCommon.h>
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

NavStatus NavQuery::beginSlicedPath(const math::Vec3& start,
                                    const math::Vec3& end,
                                    const math::Vec3& search_extents,
                                    const NavQueryFilter& filter) {
  cancelSlicedPath();
  if (!isValid()) {
    return NavStatus::NoNavMesh;
  }

  sliced_filter_ = std::make_unique<dtQueryFilter>(makeDetourFilter(filter));
  dtPolyRef start_ref = 0;
  dtPolyRef end_ref = 0;
  float nearest_start[3]{};
  float nearest_end[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start),
                                         ptr(search_extents),
                                         sliced_filter_.get(),
                                         &start_ref,
                                         nearest_start)) ||
      start_ref == 0) {
    sliced_filter_.reset();
    return NavStatus::InvalidStart;
  }
  if (!succeeded(query_->findNearestPoly(ptr(end),
                                         ptr(search_extents),
                                         sliced_filter_.get(),
                                         &end_ref,
                                         nearest_end)) ||
      end_ref == 0) {
    sliced_filter_.reset();
    return NavStatus::InvalidEnd;
  }

  const dtStatus status = query_->initSlicedFindPath(start_ref,
                                                     end_ref,
                                                     nearest_start,
                                                     nearest_end,
                                                     sliced_filter_.get());
  if (failed(status)) {
    sliced_filter_.reset();
    return NavStatus::QueryFailed;
  }

  sliced_start_ref_ = static_cast<uint64_t>(start_ref);
  sliced_end_ref_ = static_cast<uint64_t>(end_ref);
  sliced_start_ = toVec3(nearest_start);
  sliced_end_ = toVec3(nearest_end);
  sliced_active_ = true;
  return dtStatusInProgress(status) ? NavStatus::InProgress : NavStatus::Success;
}

NavStatus NavQuery::updateSlicedPath(int max_iterations, bool& out_done) {
  out_done = true;
  if (!isValid() || !sliced_active_) {
    return NavStatus::QueryFailed;
  }

  int done_iters = 0;
  const dtStatus status = query_->updateSlicedFindPath(std::max(max_iterations, 1), &done_iters);
  (void)done_iters;
  if (dtStatusInProgress(status)) {
    out_done = false;
    return NavStatus::InProgress;
  }
  if (failed(status)) {
    cancelSlicedPath();
    return NavStatus::QueryFailed;
  }
  return dtStatusDetail(status, DT_PARTIAL_RESULT) ? NavStatus::PartialPath : NavStatus::Success;
}

NavPath NavQuery::finalizeSlicedPath(int max_points) {
  NavPath result{};
  if (!isValid() || !sliced_active_) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  std::vector<dtPolyRef> polys(static_cast<size_t>(kMaxPathPolys));
  int poly_count = 0;
  const dtStatus status = query_->finalizeSlicedFindPath(polys.data(),
                                                        &poly_count,
                                                        static_cast<int>(polys.size()));
  const dtPolyRef end_ref = static_cast<dtPolyRef>(sliced_end_ref_);
  const math::Vec3 sliced_start = sliced_start_;
  const math::Vec3 sliced_end = sliced_end_;
  cancelSlicedPath();
  if (!succeeded(status) || poly_count <= 0) {
    result.status = failed(status) ? NavStatus::QueryFailed : NavStatus::NoPath;
    return result;
  }

  const int clamped_max_points = std::max(max_points, 2);
  std::vector<float> straight(static_cast<size_t>(clamped_max_points) * 3u);
  std::vector<unsigned char> flags(static_cast<size_t>(clamped_max_points));
  std::vector<dtPolyRef> straight_polys(static_cast<size_t>(clamped_max_points));
  int point_count = 0;
  if (!succeeded(query_->findStraightPath(ptr(sliced_start),
                                          ptr(sliced_end),
                                          polys.data(),
                                          poly_count,
                                          straight.data(),
                                          flags.data(),
                                          straight_polys.data(),
                                          &point_count,
                                          clamped_max_points)) ||
      point_count <= 0) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.partial = dtStatusDetail(status, DT_PARTIAL_RESULT) ||
                   polys[static_cast<size_t>(poly_count - 1)] != end_ref;
  result.status = result.partial ? NavStatus::PartialPath : NavStatus::Success;
  result.points.reserve(static_cast<size_t>(point_count));
  result.point_flags.reserve(static_cast<size_t>(point_count));
  for (int i = 0; i < point_count; ++i) {
    result.points.push_back(toVec3(&straight[static_cast<size_t>(i) * 3u]));
    result.point_flags.push_back(mapStraightPathFlags(flags[static_cast<size_t>(i)]));
  }
  return result;
}

NavPath NavQuery::finalizeSlicedPathPartial(const std::vector<uint64_t>& existing_path,
                                            int max_points) {
  NavPath result{};
  if (!isValid() || !sliced_active_ || existing_path.empty()) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  std::vector<dtPolyRef> existing;
  existing.reserve(existing_path.size());
  for (uint64_t ref : existing_path) {
    if (ref != 0) {
      existing.push_back(static_cast<dtPolyRef>(ref));
    }
  }
  if (existing.empty()) {
    result.status = NavStatus::InvalidConfig;
    return result;
  }

  std::vector<dtPolyRef> polys(static_cast<size_t>(kMaxPathPolys));
  int poly_count = 0;
  const dtStatus status = query_->finalizeSlicedFindPathPartial(
      existing.data(),
      static_cast<int>(existing.size()),
      polys.data(),
      &poly_count,
      static_cast<int>(polys.size()));
  const dtPolyRef end_ref = static_cast<dtPolyRef>(sliced_end_ref_);
  const math::Vec3 sliced_start = sliced_start_;
  const math::Vec3 sliced_end = sliced_end_;
  cancelSlicedPath();
  if (!succeeded(status) || poly_count <= 0) {
    result.status = failed(status) ? NavStatus::QueryFailed : NavStatus::NoPath;
    return result;
  }

  const int clamped_max_points = std::max(max_points, 2);
  std::vector<float> straight(static_cast<size_t>(clamped_max_points) * 3u);
  std::vector<unsigned char> flags(static_cast<size_t>(clamped_max_points));
  std::vector<dtPolyRef> straight_polys(static_cast<size_t>(clamped_max_points));
  int point_count = 0;
  if (!succeeded(query_->findStraightPath(ptr(sliced_start),
                                          ptr(sliced_end),
                                          polys.data(),
                                          poly_count,
                                          straight.data(),
                                          flags.data(),
                                          straight_polys.data(),
                                          &point_count,
                                          clamped_max_points)) ||
      point_count <= 0) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.partial = dtStatusDetail(status, DT_PARTIAL_RESULT) ||
                   polys[static_cast<size_t>(poly_count - 1)] != end_ref;
  result.status = result.partial ? NavStatus::PartialPath : NavStatus::Success;
  for (int i = 0; i < point_count; ++i) {
    result.points.push_back(toVec3(&straight[static_cast<size_t>(i) * 3u]));
    result.point_flags.push_back(mapStraightPathFlags(flags[static_cast<size_t>(i)]));
  }
  return result;
}

NavPolyQueryResult NavQuery::pathFromDijkstraSearch(uint64_t end_poly_ref, int max_polys) const {
  NavPolyQueryResult result{};
  if (!isValid() || end_poly_ref == 0) {
    result.status = isValid() ? NavStatus::InvalidEnd : NavStatus::NoNavMesh;
    return result;
  }
  const int capacity = std::max(max_polys, 1);
  std::vector<dtPolyRef> path(static_cast<size_t>(capacity));
  int count = 0;
  const dtStatus status = query_->getPathFromDijkstraSearch(static_cast<dtPolyRef>(end_poly_ref),
                                                            path.data(),
                                                            &count,
                                                            capacity);
  if (!succeeded(status)) {
    result.status = NavStatus::QueryFailed;
    return result;
  }
  result.status = dtStatusDetail(status, DT_PARTIAL_RESULT) ? NavStatus::PartialPath : NavStatus::Success;
  for (int i = 0; i < count; ++i) {
    result.polys.push_back(static_cast<uint64_t>(path[static_cast<size_t>(i)]));
  }
  return result;
}

bool NavQuery::isInClosedList(uint64_t poly_ref) const {
  return isValid() && poly_ref != 0 && query_->isInClosedList(static_cast<dtPolyRef>(poly_ref));
}

NavPortalPoints NavQuery::portalPoints(uint64_t from_poly_ref, uint64_t to_poly_ref) const {
  NavPortalPoints result{};
  if (!isValid() || from_poly_ref == 0 || to_poly_ref == 0) {
    result.status = isValid() ? NavStatus::InvalidConfig : NavStatus::NoNavMesh;
    return result;
  }
  if (nav_mesh_ != nullptr) {
    math::Vec3 center{};
    if (nav_mesh_->polyCenter(from_poly_ref, center)) {
      const NavWallSegments segments = getPolyWallSegments(center);
      if (segments.success()) {
        for (const NavWallSegment& segment : segments.segments) {
          if (segment.neighbor_ref == to_poly_ref) {
            result.status = NavStatus::Success;
            result.left = segment.start;
            result.right = segment.end;
            result.from_type = 0;
            result.to_type = 0;
            return result;
          }
        }
      }
    }
  }
  result.status = NavStatus::QueryFailed;
  return result;
}

bool NavQuery::edgeMidPoint(uint64_t from_poly_ref,
                            uint64_t to_poly_ref,
                            math::Vec3& out_midpoint) const {
  if (!isValid() || from_poly_ref == 0 || to_poly_ref == 0) {
    return false;
  }
  const NavPortalPoints portal = portalPoints(from_poly_ref, to_poly_ref);
  if (!portal.success()) {
    return false;
  }
  out_midpoint = {(portal.left.x + portal.right.x) * 0.5f,
                  (portal.left.y + portal.right.y) * 0.5f,
                  (portal.left.z + portal.right.z) * 0.5f};
  return true;
}

void NavQuery::cancelSlicedPath() {
  sliced_filter_.reset();
  sliced_start_ref_ = 0;
  sliced_end_ref_ = 0;
  sliced_start_ = {};
  sliced_end_ = {};
  sliced_active_ = false;
}


}  // namespace karma::navigation
