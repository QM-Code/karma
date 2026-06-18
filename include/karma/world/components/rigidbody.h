#pragma once

#include <cstdint>
#include <stdexcept>

#include "karma/world/components/collider.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/world.h"

namespace karma::components {

/// \ingroup karma_components
/// ECS-facing rigid body motion type.
enum class RigidbodyMotionType : uint8_t {
  Dynamic,
  Kinematic,
  Static,
};

/// \ingroup karma_components
/// Continuous collision detection mode.
enum class RigidbodyMotionQuality : uint8_t {
  Discrete,
  LinearCast,
};

/// \ingroup karma_components
/// Bit mask values for unlocked rigid body degrees of freedom.
enum RigidbodyDof : uint8_t {
  RigidbodyDofNone = 0,
  RigidbodyDofTranslationX = 1u << 0u,
  RigidbodyDofTranslationY = 1u << 1u,
  RigidbodyDofTranslationZ = 1u << 2u,
  RigidbodyDofRotationX = 1u << 3u,
  RigidbodyDofRotationY = 1u << 4u,
  RigidbodyDofRotationZ = 1u << 5u,
  RigidbodyDofAll = RigidbodyDofTranslationX | RigidbodyDofTranslationY | RigidbodyDofTranslationZ |
                    RigidbodyDofRotationX | RigidbodyDofRotationY | RigidbodyDofRotationZ,
  RigidbodyDofPlane2D = RigidbodyDofTranslationX | RigidbodyDofTranslationY | RigidbodyDofRotationZ,
};

/// \ingroup karma_components
/// Dynamic rigid-body authoring data consumed by `PhysicsSystem`.
class RigidbodyComponent : public ecs::ComponentTag {
 public:
  RigidbodyMotionType motion_type = RigidbodyMotionType::Dynamic;
  RigidbodyMotionQuality motion_quality = RigidbodyMotionQuality::Discrete;
  uint8_t allowed_dofs = RigidbodyDofAll;
  float mass = 1.0f;
  math::Vec3 velocity{};
  math::Vec3 angular_velocity{};
  bool is_kinematic = false;
  bool use_gravity = true;
  bool is_trigger = false;
  float gravity_factor = 1.0f;
  float linear_damping = 0.05f;
  float angular_damping = 0.05f;
  float max_linear_velocity = 500.0f;
  float max_angular_velocity = 47.1238898f;
  float inertia_multiplier = 1.0f;
  uint32_t velocity_solver_steps = 0;
  uint32_t position_solver_steps = 0;
  bool allow_sleeping = true;
  bool allow_dynamic_or_kinematic = false;
  bool collide_kinematic_vs_non_dynamic = false;
  bool use_manifold_reduction = true;
  bool apply_gyroscopic_force = false;
  bool enhanced_internal_edge_removal = false;

  static void Validate(ecs::World& world, ecs::Entity entity) {
    if (!world.has<TransformComponent>(entity)) {
      throw std::runtime_error("RigidbodyComponent requires TransformComponent on the same entity.");
    }
    if (!world.has<ColliderComponent>(entity)) {
      throw std::runtime_error("RigidbodyComponent requires ColliderComponent on the same entity.");
    }
  }
};

/// \ingroup karma_components
/// Per-step force and impulse commands consumed by `PhysicsSystem`.
struct PhysicsBodyForcesComponent : ecs::ComponentTag {
  math::Vec3 force{};
  math::Vec3 force_position{};
  bool force_at_position = false;
  math::Vec3 torque{};
  math::Vec3 impulse{};
  math::Vec3 impulse_position{};
  bool impulse_at_position = false;
  math::Vec3 angular_impulse{};
  bool clear_after_step = true;

  void clearTransient() {
    force = {};
    force_position = {};
    force_at_position = false;
    torque = {};
    impulse = {};
    impulse_position = {};
    impulse_at_position = false;
    angular_impulse = {};
  }
};

}  // namespace karma::components
