#pragma once

#include <stdexcept>

#include "karma/world/components/collider.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/world.h"

namespace karma::components {

/// \ingroup karma_components
/// Game-input bridge for the physics character controller.
///
/// The component requires `TransformComponent` and a `ColliderComponent` with a
/// box shape on the same entity.
struct CharacterControllerComponent : ecs::ComponentTag {
  bool enabled = true;
  /// System-written current controller velocity.
  math::Vec3 velocity{};
  /// System-written current controller angular velocity.
  math::Vec3 angular_velocity{};
  /// System-written controller forward vector.
  math::Vec3 forward{0.0f, 0.0f, -1.0f};
  /// System-written grounded state.
  bool grounded = false;

  /// Sets continuous desired movement velocity.
  void setDesiredVelocity(const math::Vec3& velocity) { desired_velocity_ = velocity; }
  /// Sets continuous desired angular velocity.
  void setDesiredAngularVelocity(const math::Vec3& velocity) {
    desired_angular_velocity_ = velocity;
  }
  /// Adds a one-shot velocity impulse.
  void addImpulse(const math::Vec3& velocity) { add_velocity_ = velocity; }
  /// Replaces the pending additive velocity.
  void setAddVelocity(const math::Vec3& velocity) { add_velocity_ = velocity; }
  /// Returns the desired movement velocity.
  const math::Vec3& desiredVelocity() const { return desired_velocity_; }
  /// Returns the desired angular velocity.
  const math::Vec3& desiredAngularVelocity() const { return desired_angular_velocity_; }
  /// Returns the pending additive velocity.
  const math::Vec3& addVelocity() const { return add_velocity_; }
  /// Clears the pending additive velocity.
  void clearImpulse() { add_velocity_ = {}; }

  static void Validate(ecs::World& world, ecs::Entity entity) {
    if (!world.has<TransformComponent>(entity)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires TransformComponent on the same entity.");
    }
    if (!world.has<ColliderComponent>(entity)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires ColliderComponent on the same entity.");
    }
    const ColliderComponent& collider = world.get<ColliderComponent>(entity);
    if (!colliderTypeMatchesShape(collider)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires ColliderComponent type to match its shape.");
    }
    if (!isCharacterControllerShape(collider.type)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires a box ColliderComponent.");
    }
  }

 private:
  math::Vec3 desired_velocity_{};
  math::Vec3 desired_angular_velocity_{};
  math::Vec3 add_velocity_{};
};

}  // namespace karma::components
