#pragma once

#include <utility>
#include <vector>

#include "karma/simulation/navigation/nav_mesh.h"

namespace karma::navigation::detail {

struct NavMeshAccess {
  static dtNavMesh* detour(NavMesh& nav_mesh) {
    return nav_mesh.nav_mesh_;
  }

  static const dtNavMesh* detour(const NavMesh& nav_mesh) {
    return nav_mesh.nav_mesh_;
  }

  static dtNavMesh* releaseDetour(NavMesh& nav_mesh) {
    dtNavMesh* out = nav_mesh.nav_mesh_;
    nav_mesh.nav_mesh_ = nullptr;
    return out;
  }

  static void adoptDetour(NavMesh& nav_mesh,
                          dtNavMesh* detour_nav_mesh,
                          const NavMeshBuildConfig& config,
                          const math::Vec3& bounds_min,
                          const math::Vec3& bounds_max) {
    nav_mesh.nav_mesh_ = detour_nav_mesh;
    nav_mesh.config_ = config;
    nav_mesh.config_.build_mode = NavMeshBuildMode::Tiled;
    nav_mesh.bounds_min_ = bounds_min;
    nav_mesh.bounds_max_ = bounds_max;
  }

  static void setDebugEdges(NavMesh& nav_mesh, std::vector<math::Vec3> edges) {
    nav_mesh.debug_edges_ = std::move(edges);
  }

  static void setLastBuildResult(NavMesh& nav_mesh, const NavMeshBuildResult& result) {
    nav_mesh.last_result_ = result;
  }

  static void refreshSnapshot(NavMesh& nav_mesh) {
    nav_mesh.refreshSnapshot();
  }
};

}  // namespace karma::navigation::detail
