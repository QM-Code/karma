#include "karma/simulation/navigation/nav_mesh.h"

#include <cmath>
#include <utility>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>

#include "detail/nav_mesh_debug.h"

namespace karma::navigation {

using detail::clearDebugLines;

NavQueryFilter::NavQueryFilter() {
  area_costs.fill(1.0f);
}

void NavQueryFilter::setAreaCost(unsigned char area, float cost) {
  if (area >= area_costs.size() || cost <= 0.0f || !std::isfinite(cost)) {
    return;
  }
  area_costs[area] = cost;
}

float NavQueryFilter::areaCost(unsigned char area) const {
  if (area >= area_costs.size()) {
    return 1.0f;
  }
  return area_costs[area];
}

NavQueryFilter makeQueryFilter(const NavMeshBuildConfig& config,
                               uint16_t include_flags,
                               uint16_t exclude_flags) {
  NavQueryFilter filter;
  filter.include_flags = include_flags;
  filter.exclude_flags = exclude_flags;
  for (const NavAreaConfig& area_config : config.area_configs) {
    filter.setAreaCost(area_config.area, area_config.cost);
  }
  return filter;
}

const char* navStatusName(NavStatus status) {
  switch (status) {
    case NavStatus::Success: return "Success";
    case NavStatus::InProgress: return "InProgress";
    case NavStatus::PartialPath: return "PartialPath";
    case NavStatus::EmptyInput: return "EmptyInput";
    case NavStatus::InvalidConfig: return "InvalidConfig";
    case NavStatus::BuildFailed: return "BuildFailed";
    case NavStatus::NoNavMesh: return "NoNavMesh";
    case NavStatus::InvalidStart: return "InvalidStart";
    case NavStatus::InvalidEnd: return "InvalidEnd";
    case NavStatus::NoPath: return "NoPath";
    case NavStatus::QueryFailed: return "QueryFailed";
    default: return "Unknown";
  }
}

const char* navMeshDebugDrawModeName(NavMeshDebugDrawMode mode) {
  switch (mode) {
    case NavMeshDebugDrawMode::NavMeshEdges: return "NavMeshEdges";
    case NavMeshDebugDrawMode::NavMesh: return "NavMesh";
    case NavMeshDebugDrawMode::NavMeshBVTree: return "NavMeshBVTree";
    case NavMeshDebugDrawMode::NavMeshPortals: return "NavMeshPortals";
    case NavMeshDebugDrawMode::Voxels: return "Voxels";
    case NavMeshDebugDrawMode::WalkableVoxels: return "WalkableVoxels";
    case NavMeshDebugDrawMode::Compact: return "Compact";
    case NavMeshDebugDrawMode::CompactDistance: return "CompactDistance";
    case NavMeshDebugDrawMode::CompactRegions: return "CompactRegions";
    case NavMeshDebugDrawMode::RegionConnections: return "RegionConnections";
    case NavMeshDebugDrawMode::RawContours: return "RawContours";
    case NavMeshDebugDrawMode::BothContours: return "BothContours";
    case NavMeshDebugDrawMode::Contours: return "Contours";
    case NavMeshDebugDrawMode::PolyMesh: return "PolyMesh";
    case NavMeshDebugDrawMode::PolyMeshDetail: return "PolyMeshDetail";
    default: return "Unknown";
  }
}

NavMesh::NavMesh() = default;

NavMesh::~NavMesh() {
  reset();
}

NavMesh::NavMesh(NavMesh&& other) noexcept {
  *this = std::move(other);
}

NavMesh& NavMesh::operator=(NavMesh&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  nav_mesh_ = other.nav_mesh_;
  config_ = other.config_;
  last_result_ = std::move(other.last_result_);
  bounds_min_ = other.bounds_min_;
  bounds_max_ = other.bounds_max_;
  debug_edges_ = std::move(other.debug_edges_);
  debug_draw_lines_ = std::move(other.debug_draw_lines_);
  snapshot_ = std::move(other.snapshot_);
  other.nav_mesh_ = nullptr;
  return *this;
}

void NavMesh::reset() {
  if (nav_mesh_ != nullptr) {
    dtFreeNavMesh(nav_mesh_);
    nav_mesh_ = nullptr;
  }
  last_result_ = {};
  debug_edges_.clear();
  clearDebugLines(debug_draw_lines_);
  snapshot_.reset();
}

bool NavMesh::isValid() const {
  return nav_mesh_ != nullptr;
}

}  // namespace karma::navigation
