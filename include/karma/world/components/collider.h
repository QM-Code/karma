#pragma once

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

struct ColliderComponent : ecs::ComponentTag {
  bool is_trigger = false;
  bool debug_draw = false;
};

struct BoxColliderComponent : ColliderComponent {
  math::Vec3 center{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

struct SphereColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
};

struct CapsuleColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
};

struct MeshColliderComponent : ColliderComponent {};

}  // namespace karma::components
