#include "karma/simulation/navigation/nav_query.h"
#include "karma/simulation/navigation/nav_mesh.h"

#include <algorithm>
#include <utility>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "karma/rendering/renderer/device.h"
#include "detail/detour_utils.h"

namespace karma::navigation {
namespace {

using detail::succeeded;
using detail::makeDetourFilter;
using detail::ptr;
using detail::toVec3;

}  // namespace

NavQuery::NavQuery(const NavMesh& nav_mesh, int max_nodes)
    : nav_mesh_(&nav_mesh) {
  if (!nav_mesh.isValid()) {
    return;
  }
  query_ = dtAllocNavMeshQuery();
  if (query_ == nullptr ||
      !succeeded(query_->init(nav_mesh.nav_mesh_, std::max(max_nodes, 1)))) {
    dtFreeNavMeshQuery(query_);
    query_ = nullptr;
  }
}

NavQuery::NavQuery(const NavMeshSnapshot& snapshot, int max_nodes) {
  NavMesh loaded;
  if (!loaded.loadSnapshot(snapshot)) {
    return;
  }
  owned_nav_mesh_ = loaded.nav_mesh_;
  loaded.nav_mesh_ = nullptr;

  query_ = dtAllocNavMeshQuery();
  if (query_ == nullptr ||
      !succeeded(query_->init(owned_nav_mesh_, std::max(max_nodes, 1)))) {
    dtFreeNavMeshQuery(query_);
    query_ = nullptr;
    dtFreeNavMesh(owned_nav_mesh_);
    owned_nav_mesh_ = nullptr;
  }
}

NavQuery::~NavQuery() {
  dtFreeNavMeshQuery(query_);
  dtFreeNavMesh(owned_nav_mesh_);
}

NavQuery::NavQuery(NavQuery&& other) noexcept {
  *this = std::move(other);
}

NavQuery& NavQuery::operator=(NavQuery&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  dtFreeNavMeshQuery(query_);
  dtFreeNavMesh(owned_nav_mesh_);
  nav_mesh_ = other.nav_mesh_;
  owned_nav_mesh_ = other.owned_nav_mesh_;
  query_ = other.query_;
  sliced_filter_ = std::move(other.sliced_filter_);
  sliced_start_ref_ = other.sliced_start_ref_;
  sliced_end_ref_ = other.sliced_end_ref_;
  sliced_start_ = other.sliced_start_;
  sliced_end_ = other.sliced_end_;
  sliced_active_ = other.sliced_active_;
  other.nav_mesh_ = nullptr;
  other.owned_nav_mesh_ = nullptr;
  other.query_ = nullptr;
  other.sliced_start_ref_ = 0;
  other.sliced_end_ref_ = 0;
  other.sliced_active_ = false;
  return *this;
}

bool NavQuery::isValid() const {
  const bool has_nav_mesh = (nav_mesh_ != nullptr && nav_mesh_->isValid()) ||
                            owned_nav_mesh_ != nullptr;
  return has_nav_mesh && query_ != nullptr;
}

bool NavQuery::findNearestPoint(const math::Vec3& point,
                                math::Vec3& out_point,
                                const math::Vec3& search_extents,
                                const NavQueryFilter& filter) const {
  uint64_t ref = 0;
  return findNearestPoly(point, ref, &out_point, search_extents, filter);
}

bool NavQuery::findNearestPoly(const math::Vec3& point,
                               uint64_t& out_poly_ref,
                               math::Vec3* out_point,
                               const math::Vec3& search_extents,
                               const NavQueryFilter& filter) const {
  out_poly_ref = 0;
  if (!isValid()) {
    return false;
  }
  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  const dtStatus status = query_->findNearestPoly(ptr(point),
                                                  ptr(search_extents),
                                                  &detour_filter,
                                                  &ref,
                                                  nearest);
  if (!succeeded(status) || ref == 0) {
    return false;
  }
  if (out_point != nullptr) {
    query_->closestPointOnPoly(ref, ptr(point), nearest, nullptr);
    float height = nearest[1];
    if (succeeded(query_->getPolyHeight(ref, nearest, &height))) {
      nearest[1] = height;
    }
    *out_point = toVec3(nearest);
  }
  out_poly_ref = static_cast<uint64_t>(ref);
  return true;
}

void NavQuery::debugDrawPath(renderer::GraphicsDevice& graphics,
                             const NavPath& path,
                             const math::Color& color,
                             bool depth_test) {
  if (path.points.size() < 2) {
    return;
  }
  for (size_t i = 1; i < path.points.size(); ++i) {
    graphics.drawLine(path.points[i - 1], path.points[i], color, depth_test, 2.0f);
  }
}

}  // namespace karma::navigation
