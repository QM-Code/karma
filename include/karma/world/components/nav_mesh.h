#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"
#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_mesh.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::components {

/// \ingroup karma_components
/// Marks mesh geometry as a source surface for navmesh builds.
struct NavMeshSurfaceComponent : ecs::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  unsigned char area = navigation::kNavAreaDefault;
  bool walkable = true;
  std::shared_ptr<const geometry::MeshData> mesh_data;
  std::string mesh_key;
};

/// \ingroup karma_components
/// Authored off-mesh navigation connection.
struct NavOffMeshLinkComponent : ecs::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  ecs::Entity end_entity{};
  math::Vec3 start_offset{};
  math::Vec3 end_offset{};
  float radius = 0.4f;
  unsigned char area = navigation::kNavAreaDefault;
  uint16_t flags = navigation::kNavPolyFlagWalk | navigation::kNavPolyFlagOffMesh;
  bool bidirectional = true;
  uint32_t user_id = 0;
};

/// \ingroup karma_components
/// Marks a vertical convex volume with a navigation area during navmesh builds.
struct NavConvexVolumeComponent : ecs::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  std::vector<math::Vec3> vertices;
  float min_y = 0.0f;
  float max_y = 2.0f;
  unsigned char area = navigation::kNavAreaDefault;
};

/// \ingroup karma_components
/// Owns a baked navigation mesh and build settings for an entity.
struct NavMeshComponent : ecs::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  bool debug_draw = true;
  navigation::NavMeshDebugDrawMode debug_draw_mode = navigation::NavMeshDebugDrawMode::NavMeshEdges;
  uint64_t build_version = 0;
  uint32_t source_mask = 0xffffffffu;
  navigation::NavMeshBuildConfig build_config{};
  navigation::NavMeshBuildResult last_build_result{};
  navigation::NavMesh nav_mesh{};
};

}  // namespace karma::components
