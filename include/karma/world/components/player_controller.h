#pragma once

#include <stdexcept>

#include "karma/world/components/collider.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/world.h"

namespace karma::components {

/// \ingroup karma_components
/// Game-input bridge for the physics player controller.
///
/// The component requires a box, sphere, or capsule collider on the same entity.
/// Game code writes desired velocity/impulses; `PhysicsSystem` consumes them
/// and writes transform, contacts, and ground state.
struct PlayerControllerComponent : ecs::ComponentTag {
  bool enabled = true;

  /// Sets continuous desired movement velocity.
  void setDesiredVelocity(const math::Vec3& velocity) { desired_velocity_ = velocity; }
  /// Adds a one-shot velocity impulse.
  void addImpulse(const math::Vec3& velocity) { add_velocity_ = velocity; }
  /// Replaces the pending additive velocity.
  void setAddVelocity(const math::Vec3& velocity) { add_velocity_ = velocity; }
  /// Returns the desired movement velocity.
  const math::Vec3& desiredVelocity() const { return desired_velocity_; }
  /// Returns the pending additive velocity.
  const math::Vec3& addVelocity() const { return add_velocity_; }
  /// Clears the pending additive velocity.
  void clearImpulse() { add_velocity_ = {}; }

  /// Validates required collider components when added to a world.
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
