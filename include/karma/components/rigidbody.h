#pragma once

#include "karma/components/transform.h"
#include "karma/ecs/component.h"

namespace karma::components {

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
