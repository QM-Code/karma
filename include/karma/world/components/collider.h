#pragma once

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Shared collider flags for physics and ECS collision queries.
struct ColliderComponent : ecs::ComponentTag {
  bool is_trigger = false;
  bool debug_draw = false;
};

/// \ingroup karma_components
/// Axis-aligned or transform-oriented box collider.
struct BoxColliderComponent : ColliderComponent {
  math::Vec3 center{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

/// \ingroup karma_components
/// Sphere collider.
struct SphereColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
};

/// \ingroup karma_components
/// Capsule collider.
struct CapsuleColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Mesh collider using render or imported mesh geometry.
struct MeshColliderComponent : ColliderComponent {};

}  // namespace karma::components
