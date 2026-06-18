#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

enum class PhysicsVehicleControllerKind : uint8_t {
  Wheeled,
  Motorcycle,
  Tracked,
};

enum class PhysicsVehicleCollisionTesterKind : uint8_t {
  Ray,
  SphereCast,
  CylinderCast,
};

enum class PhysicsVehicleTransmissionKind : uint8_t {
  Automatic,
  Manual,
};

enum class PhysicsVehicleSpringKind : uint8_t {
  FrequencyAndDamping,
  StiffnessAndDamping,
};

struct PhysicsVehicleCurvePoint {
  float x = 0.0f;
  float y = 0.0f;
};

struct PhysicsVehicleSpring {
  PhysicsVehicleSpringKind mode = PhysicsVehicleSpringKind::FrequencyAndDamping;
  float frequency_or_stiffness = 0.0f;
  float damping = 0.0f;
};

struct PhysicsVehicleInputState {
  float forward = 0.0f;
  float right = 0.0f;
  float brake = 0.0f;
  float hand_brake = 0.0f;
  float left_ratio = 1.0f;
  float right_ratio = 1.0f;
  int current_gear = 0;
  float clutch_friction = 1.0f;
};

struct PhysicsVehicleEngine {
  float max_torque = 500.0f;
  float min_rpm = 1000.0f;
  float max_rpm = 6000.0f;
  float inertia = 0.5f;
  float angular_damping = 0.2f;
  std::vector<PhysicsVehicleCurvePoint> normalized_torque;
};

struct PhysicsVehicleTransmission {
  PhysicsVehicleTransmissionKind mode = PhysicsVehicleTransmissionKind::Automatic;
  std::vector<float> gear_ratios{2.66f, 1.78f, 1.3f, 1.0f, 0.74f};
  std::vector<float> reverse_gear_ratios{-2.9f};
  float switch_time = 0.5f;
  float clutch_release_time = 0.3f;
  float switch_latency = 0.5f;
  float shift_up_rpm = 4000.0f;
  float shift_down_rpm = 2000.0f;
  float clutch_strength = 10.0f;
};

struct PhysicsVehicleDifferential {
  int left_wheel = -1;
  int right_wheel = -1;
  float differential_ratio = 3.42f;
  float left_right_split = 0.5f;
  float limited_slip_ratio = 1.4f;
  float engine_torque_ratio = 1.0f;
};

struct PhysicsVehicleAntiRollBar {
  int left_wheel = 0;
  int right_wheel = 1;
  float stiffness = 1000.0f;
};

struct PhysicsVehicleTrack {
  uint32_t driven_wheel = 0;
  std::vector<uint32_t> wheels;
  float inertia = 10.0f;
  float angular_damping = 0.5f;
  float max_brake_torque = 15000.0f;
  float differential_ratio = 6.0f;
};

struct PhysicsVehicleWheel {
  math::Vec3 position{};
  math::Vec3 suspension_force_point{};
  math::Vec3 suspension_direction{0.0f, -1.0f, 0.0f};
  math::Vec3 steering_axis{0.0f, 1.0f, 0.0f};
  math::Vec3 wheel_up{0.0f, 1.0f, 0.0f};
  math::Vec3 wheel_forward{0.0f, 0.0f, 1.0f};
  float suspension_min_length = 0.3f;
  float suspension_max_length = 0.5f;
  float suspension_preload_length = 0.0f;
  PhysicsVehicleSpring suspension_spring{PhysicsVehicleSpringKind::FrequencyAndDamping, 1.5f, 0.5f};
  float radius = 0.3f;
  float width = 0.1f;
  bool enable_suspension_force_point = false;

  float inertia = 0.9f;
  float angular_damping = 0.2f;
  float max_steer_angle = 1.22173048f;
  std::vector<PhysicsVehicleCurvePoint> longitudinal_friction;
  std::vector<PhysicsVehicleCurvePoint> lateral_friction;
  float max_brake_torque = 1500.0f;
  float max_hand_brake_torque = 4000.0f;

  float tracked_longitudinal_friction = 4.0f;
  float tracked_lateral_friction = 2.0f;
};

struct PhysicsMotorcycleSettings {
  float max_lean_angle = 0.785398163f;
  float lean_spring_constant = 5000.0f;
  float lean_spring_damping = 1000.0f;
  float lean_spring_integration_coefficient = 0.0f;
  float lean_spring_integration_decay = 4.0f;
  float lean_smoothing_factor = 0.8f;
  bool enable_lean_controller = true;
  bool enable_lean_steering_limit = true;
};

/// \ingroup karma_components
/// Jolt-style vehicle constraint authored on an ECS rigid body.
struct PhysicsVehicleComponent : ecs::ComponentTag {
  bool enabled = true;
  PhysicsVehicleControllerKind controller = PhysicsVehicleControllerKind::Wheeled;
  PhysicsVehicleCollisionTesterKind collision_tester = PhysicsVehicleCollisionTesterKind::Ray;
  math::Vec3 up{0.0f, 1.0f, 0.0f};
  math::Vec3 forward{0.0f, 0.0f, 1.0f};
  float max_pitch_roll_angle = 3.14159265358979323846f;
  float collision_test_sphere_radius = 0.3f;
  float collision_test_cylinder_convex_radius_fraction = 0.1f;
  float collision_test_max_slope_angle = 1.3962634f;
  uint32_t collision_test_layer = 1u;
  uint32_t num_steps_between_collision_test_active = 1;
  uint32_t num_steps_between_collision_test_inactive = 1;
  bool override_gravity = false;
  math::Vec3 gravity{0.0f, -9.8f, 0.0f};
  uint32_t priority = 0;
  uint32_t velocity_solver_steps = 0;
  uint32_t position_solver_steps = 0;
  float draw_size = 1.0f;
  uint64_t user_data = 0;

  std::vector<PhysicsVehicleWheel> wheels;
  std::vector<PhysicsVehicleAntiRollBar> anti_roll_bars;
  PhysicsVehicleEngine engine{};
  PhysicsVehicleTransmission transmission{};
  std::vector<PhysicsVehicleDifferential> differentials;
  float differential_limited_slip_ratio = 1.4f;
  PhysicsMotorcycleSettings motorcycle{};
  std::array<PhysicsVehicleTrack, 2> tracks{};
  PhysicsVehicleInputState input{};
};

}  // namespace karma::components
