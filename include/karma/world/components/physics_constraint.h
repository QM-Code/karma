#pragma once

#include <array>
#include <cstdint>

#include "karma/core/math/types.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

enum class PhysicsConstraintKind : uint8_t {
  Fixed,
  Point,
  Distance,
  Hinge,
  Slider,
  Cone,
  SwingTwist,
  SixDof,
};

enum class PhysicsConstraintFrameSpace : uint8_t {
  World,
  LocalToBodyCenterOfMass,
};

enum class PhysicsConstraintSpringMode : uint8_t {
  FrequencyAndDamping,
  StiffnessAndDamping,
};

struct PhysicsConstraintSpring {
  PhysicsConstraintSpringMode mode = PhysicsConstraintSpringMode::FrequencyAndDamping;
  float frequency_or_stiffness = 0.0f;
  float damping = 0.0f;
};

/// \ingroup karma_components
/// Two-body Jolt-style constraint authored on a constraint entity.
struct PhysicsConstraintComponent : ecs::ComponentTag {
  ecs::Entity body_a{};
  ecs::Entity body_b{};
  PhysicsConstraintKind kind = PhysicsConstraintKind::Fixed;
  PhysicsConstraintFrameSpace space = PhysicsConstraintFrameSpace::World;
  bool enabled = true;
  uint32_t priority = 0;
  uint32_t velocity_solver_steps = 0;
  uint32_t position_solver_steps = 0;
  float draw_size = 1.0f;
  uint64_t user_data = 0;

  bool auto_detect_point = false;
  math::Vec3 point1{};
  math::Vec3 point2{};
  math::Vec3 axis1{0.0f, 1.0f, 0.0f};
  math::Vec3 axis2{0.0f, 1.0f, 0.0f};
  math::Vec3 normal1{1.0f, 0.0f, 0.0f};
  math::Vec3 normal2{1.0f, 0.0f, 0.0f};
  math::Vec3 plane_axis1{0.0f, 1.0f, 0.0f};
  math::Vec3 plane_axis2{0.0f, 1.0f, 0.0f};

  float min_distance = -1.0f;
  float max_distance = -1.0f;
  float limits_min = -3.14159265358979323846f;
  float limits_max = 3.14159265358979323846f;
  float half_cone_angle = 0.0f;
  float normal_half_cone_angle = 0.0f;
  float plane_half_cone_angle = 0.0f;
  float twist_min_angle = 0.0f;
  float twist_max_angle = 0.0f;
  float max_friction_force = 0.0f;
  float max_friction_torque = 0.0f;
  PhysicsConstraintSpring limit_spring{};

  std::array<float, 6> six_dof_min_limits{{-3.402823466e+38F, -3.402823466e+38F,
                                           -3.402823466e+38F, -3.402823466e+38F,
                                           -3.402823466e+38F, -3.402823466e+38F}};
  std::array<float, 6> six_dof_max_limits{{3.402823466e+38F, 3.402823466e+38F,
                                           3.402823466e+38F, 3.402823466e+38F,
                                           3.402823466e+38F, 3.402823466e+38F}};
  std::array<float, 6> six_dof_max_friction{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
};

}  // namespace karma::components
