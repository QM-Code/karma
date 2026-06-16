#pragma once

#include <cstdint>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_types.h"

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
