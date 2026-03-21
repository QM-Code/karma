#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "karma/components/transform.h"
#include "karma/ecs/component.h"
#include "karma/ecs/entity.h"

namespace karma::components {

enum class PrefabMemberKind : uint8_t {
  Mesh = 0,
  Particle = 1,
  Light = 2,
  Beam = 3,
  VolumeSphere = 4,
};

struct PrefabInstanceComponent : ecs::ComponentTag {
  std::string prefab_name;
  std::vector<ecs::Entity> members;
  bool enabled = true;
};

struct PrefabMemberComponent : ecs::ComponentTag {
  ecs::Entity root{};
  std::string name;
  TransformComponent local_transform{};
  PrefabMemberKind kind = PrefabMemberKind::Mesh;
  bool mesh_visible = true;
  bool beam_visible = true;
  bool volume_sphere_visible = true;
  float light_intensity = 0.0f;
  float light_range = 0.0f;
};

}  // namespace karma::components
