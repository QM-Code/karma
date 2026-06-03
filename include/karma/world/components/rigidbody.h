#pragma once

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Dynamic rigid-body authoring data consumed by `PhysicsSystem`.
class RigidbodyComponent : public ecs::ComponentTag {
 public:
  float mass = 1.0f;
  math::Vec3 velocity{};
  math::Vec3 angular_velocity{};
  bool is_kinematic = false;
  bool use_gravity = true;
  bool is_trigger = false;
};

}  // namespace karma::components
