#pragma once

#include <stdexcept>

#include "karma/components/collider.h"
#include "karma/components/transform.h"
#include "karma/ecs/component.h"
#include "karma/ecs/world.h"

namespace karma::components {

struct PlayerControllerComponent : ecs::ComponentTag {
  bool enabled = true;

  void setDesiredVelocity(const math::Vec3& velocity) { desired_velocity_ = velocity; }
  void addImpulse(const math::Vec3& velocity) { add_velocity_ = velocity; }
  const math::Vec3& desiredVelocity() const { return desired_velocity_; }
  const math::Vec3& addVelocity() const { return add_velocity_; }
  void clearImpulse() { add_velocity_ = {}; }

  static void Validate(ecs::World& world, ecs::Entity entity) {
    if (!world.has<ColliderComponent>(entity)) {
      throw std::runtime_error(
        "PlayerControllerComponent requires ColliderComponent on the same entity.");
    }
  }

 private:
  // Game-driven intent; systems/physics can consume these however they want.
  math::Vec3 desired_velocity_{};
  math::Vec3 add_velocity_{};
};

}  // namespace karma::components
