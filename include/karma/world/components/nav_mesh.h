#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"
#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_mesh.h"
#include "karma/rendering/renderer/mesh_data.h"

namespace karma::components {

struct NavMeshSurfaceComponent : ecs::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  unsigned char area = navigation::kNavAreaDefault;
  bool walkable = true;
  std::shared_ptr<const renderer::MeshData> mesh_data;
  std::string mesh_key;
};

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

struct NavMeshComponent : ecs::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  bool debug_draw = true;
  uint64_t build_version = 0;
  uint32_t source_mask = 0xffffffffu;
  navigation::NavMeshBuildConfig build_config{};
  navigation::NavMeshBuildResult last_build_result{};
  navigation::NavMesh nav_mesh{};
};

}  // namespace karma::components
