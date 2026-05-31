#pragma once

#include <stdexcept>

#include "karma/world/components/collider.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/world.h"

namespace karma::components {

struct PlayerControllerComponent : ecs::ComponentTag {
  bool enabled = true;

  void setDesiredVelocity(const math::Vec3& velocity) { desired_velocity_ = velocity; }
  void addImpulse(const math::Vec3& velocity) { add_velocity_ = velocity; }
  void setAddVelocity(const math::Vec3& velocity) { add_velocity_ = velocity; }
  const math::Vec3& desiredVelocity() const { return desired_velocity_; }
  const math::Vec3& addVelocity() const { return add_velocity_; }
  void clearImpulse() { add_velocity_ = {}; }

  static void Validate(ecs::World& world, ecs::Entity entity) {
    if (!world.has<BoxColliderComponent>(entity) &&
        !world.has<SphereColliderComponent>(entity) &&
        !world.has<CapsuleColliderComponent>(entity)) {
      throw std::runtime_error(
        "PlayerControllerComponent requires BoxColliderComponent, SphereColliderComponent, "
        "or CapsuleColliderComponent on the same entity.");
    }
  }

 private:
  // Game-driven intent; systems/physics can consume these however they want.
  math::Vec3 desired_velocity_{};
  math::Vec3 add_velocity_{};
};

}  // namespace karma::components
