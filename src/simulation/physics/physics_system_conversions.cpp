#include "physics_system_internal.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

#include "karma/core/math/glm.h"
#include "karma/core/math/vec3.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/visibility.h"

namespace karma::physics::system_internal {

bool nearlyEqualVec3(const math::Vec3& a, const math::Vec3& b, float eps) {
  return std::abs(a.x - b.x) <= eps &&
         std::abs(a.y - b.y) <= eps &&
         std::abs(a.z - b.z) <= eps;
}

math::Vec3 negateVec3(const math::Vec3& v) {
  return {-v.x, -v.y, -v.z};
}

int colliderShapeKind(const ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::ColliderComponent>(entity)) {
    return -1;
  }
  switch (components::colliderShapeType(world.get<components::ColliderComponent>(entity).shape)) {
    case components::ColliderShapeType::Box:
      return 0;
    case components::ColliderShapeType::Sphere:
      return 1;
    case components::ColliderShapeType::Capsule:
      return 2;
    default:
      return -1;
  }
}

components::ColliderShapeType colliderShape(const ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::ColliderComponent>(entity)) {
    return components::ColliderShapeType::Box;
  }
  return components::colliderShapeType(world.get<components::ColliderComponent>(entity).shape);
}

bool colliderIsTrigger(const ecs::World& world, ecs::Entity entity) {
  return world.has<components::ColliderComponent>(entity) &&
         world.get<components::ColliderComponent>(entity).is_trigger;
}

bool hasPhysicsCollider(const ecs::World& world, ecs::Entity entity) {
  return world.has<components::ColliderComponent>(entity);
}

ecs::Entity entityFromKey(uint64_t key) {
  return {static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu)};
}

bool collisionEnabled(ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::VisibilityComponent>(entity)) {
    return true;
  }
  return world.get<components::VisibilityComponent>(entity).collision_layer_mask != 0;
}

uint32_t collisionLayers(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::PhysicsCollisionFilterComponent>(entity)) {
    return world.get<components::PhysicsCollisionFilterComponent>(entity).layers;
  }
  if (world.has<components::VisibilityComponent>(entity)) {
    return world.get<components::VisibilityComponent>(entity).collision_layer_mask;
  }
  return 1u;
}

bool matchesCollisionLayerMask(const ecs::World& world, ecs::Entity entity, uint32_t mask) {
  return (collisionLayers(world, entity) & mask) != 0u;
}

math::Vec3 scaledLocal(const math::Vec3& value, const math::Vec3& scale) {
  return math::multiply(value, scale);
}

math::Vec3 absScaledLocal(const math::Vec3& value, const math::Vec3& scale) {
  return {std::abs(value.x * scale.x),
          std::abs(value.y * scale.y),
          std::abs(value.z * scale.z)};
}

float maxAbsScale(const math::Vec3& scale) {
  return std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
}

float maxAbsScaleXZ(const math::Vec3& scale) {
  return std::max(std::abs(scale.x), std::abs(scale.z));
}

PhysicsMaterial materialForEntity(const ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::PhysicsMaterialComponent>(entity)) {
    return {};
  }
  const auto& material = world.get<components::PhysicsMaterialComponent>(entity);
  return {
      .friction = material.friction,
      .restitution = material.restitution,
  };
}

PhysicsCollisionFilter collisionFilterForEntity(const ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::PhysicsCollisionFilterComponent>(entity)) {
    return {};
  }
  const auto& filter = world.get<components::PhysicsCollisionFilterComponent>(entity);
  return {
      .layers = filter.layers,
      .collides_with = filter.collides_with,
  };
}

PhysicsMotionType toPhysicsMotion(const components::RigidbodyComponent& body) {
  if (body.is_kinematic) {
    return PhysicsMotionType::Kinematic;
  }
  switch (body.motion_type) {
    case components::RigidbodyMotionType::Static:
      return PhysicsMotionType::Static;
    case components::RigidbodyMotionType::Kinematic:
      return PhysicsMotionType::Kinematic;
    case components::RigidbodyMotionType::Dynamic:
      return PhysicsMotionType::Dynamic;
  }
  return PhysicsMotionType::Dynamic;
}

PhysicsMotionQuality toPhysicsQuality(components::RigidbodyMotionQuality quality) {
  switch (quality) {
    case components::RigidbodyMotionQuality::Discrete:
      return PhysicsMotionQuality::Discrete;
    case components::RigidbodyMotionQuality::LinearCast:
      return PhysicsMotionQuality::LinearCast;
  }
  return PhysicsMotionQuality::Discrete;
}

template <typename T>
void hashCombine(std::size_t& seed, const T& value) {
  seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

void hashVec3(std::size_t& seed, const glm::vec3& value) {
  hashCombine(seed, value.x);
  hashCombine(seed, value.y);
  hashCombine(seed, value.z);
}

void hashQuat(std::size_t& seed, const glm::quat& value) {
  hashCombine(seed, value.w);
  hashCombine(seed, value.x);
  hashCombine(seed, value.y);
  hashCombine(seed, value.z);
}

void hashShape(std::size_t& seed, const PhysicsShapeDesc& shape) {
  hashCombine(seed, static_cast<uint8_t>(shape.type));
  hashVec3(seed, shape.center);
  hashQuat(seed, shape.rotation);
  hashVec3(seed, shape.half_extents);
  hashCombine(seed, shape.radius);
  hashCombine(seed, shape.height);
  hashCombine(seed, shape.top_radius);
  hashCombine(seed, shape.bottom_radius);
  hashCombine(seed, shape.convex_radius);
  for (const glm::vec3& point : shape.triangle) {
    hashVec3(seed, point);
  }
  for (const glm::vec3& point : shape.points) {
    hashVec3(seed, point);
  }
  hashCombine(seed, shape.mesh_asset_key);
  for (const glm::vec3& vertex : shape.mesh_vertices) {
    hashVec3(seed, vertex);
  }
  for (uint32_t index : shape.mesh_indices) {
    hashCombine(seed, index);
  }
  for (float sample : shape.height_samples) {
    hashCombine(seed, sample);
  }
  hashCombine(seed, shape.height_sample_count);
  hashVec3(seed, shape.height_offset);
  hashVec3(seed, shape.height_scale);
  hashCombine(seed, shape.height_block_size);
  hashCombine(seed, shape.height_bits_per_sample);
  for (const PhysicsShapeDesc& child : shape.children) {
    hashShape(seed, child);
  }
}

std::size_t bodySignature(const PhysicsBodyDesc& desc) {
  std::size_t seed = 0;
  hashShape(seed, desc.shape);
  hashCombine(seed, static_cast<uint8_t>(desc.motion));
  hashCombine(seed, static_cast<uint8_t>(desc.motion_quality));
  hashCombine(seed, desc.allowed_dofs);
  hashCombine(seed, desc.mass);
  hashCombine(seed, desc.inertia_multiplier);
  hashCombine(seed, desc.gravity_factor);
  hashCombine(seed, desc.linear_damping);
  hashCombine(seed, desc.angular_damping);
  hashCombine(seed, desc.max_linear_velocity);
  hashCombine(seed, desc.max_angular_velocity);
  hashCombine(seed, desc.velocity_solver_steps);
  hashCombine(seed, desc.position_solver_steps);
  hashCombine(seed, desc.material.friction);
  hashCombine(seed, desc.material.restitution);
  hashCombine(seed, desc.collision_filter.layers);
  hashCombine(seed, desc.collision_filter.collides_with);
  hashCombine(seed, desc.sensor);
  hashCombine(seed, desc.allow_sleeping);
  hashCombine(seed, desc.allow_dynamic_or_kinematic);
  hashCombine(seed, desc.collide_kinematic_vs_non_dynamic);
  hashCombine(seed, desc.use_manifold_reduction);
  hashCombine(seed, desc.apply_gyroscopic_force);
  hashCombine(seed, desc.enhanced_internal_edge_removal);
  return seed;
}

PhysicsConstraintType toPhysicsConstraintType(components::PhysicsConstraintKind kind) {
  switch (kind) {
    case components::PhysicsConstraintKind::Fixed:
      return PhysicsConstraintType::Fixed;
    case components::PhysicsConstraintKind::Point:
      return PhysicsConstraintType::Point;
    case components::PhysicsConstraintKind::Distance:
      return PhysicsConstraintType::Distance;
    case components::PhysicsConstraintKind::Hinge:
      return PhysicsConstraintType::Hinge;
    case components::PhysicsConstraintKind::Slider:
      return PhysicsConstraintType::Slider;
    case components::PhysicsConstraintKind::Cone:
      return PhysicsConstraintType::Cone;
    case components::PhysicsConstraintKind::SwingTwist:
      return PhysicsConstraintType::SwingTwist;
    case components::PhysicsConstraintKind::SixDof:
      return PhysicsConstraintType::SixDof;
  }
  return PhysicsConstraintType::Fixed;
}

PhysicsConstraintSpace toPhysicsConstraintSpace(components::PhysicsConstraintFrameSpace space) {
  switch (space) {
    case components::PhysicsConstraintFrameSpace::World:
      return PhysicsConstraintSpace::World;
    case components::PhysicsConstraintFrameSpace::LocalToBodyCenterOfMass:
      return PhysicsConstraintSpace::LocalToBodyCenterOfMass;
  }
  return PhysicsConstraintSpace::World;
}

PhysicsSpringSettings toPhysicsSpring(const components::PhysicsConstraintSpring& spring) {
  PhysicsSpringSettings result;
  result.mode = spring.mode == components::PhysicsConstraintSpringMode::StiffnessAndDamping
                    ? PhysicsSpringMode::StiffnessAndDamping
                    : PhysicsSpringMode::FrequencyAndDamping;
  result.frequency_or_stiffness = spring.frequency_or_stiffness;
  result.damping = spring.damping;
  return result;
}

PhysicsConstraintDesc buildConstraintDesc(const components::PhysicsConstraintComponent& component) {
  PhysicsConstraintDesc desc;
  desc.type = toPhysicsConstraintType(component.kind);
  desc.space = toPhysicsConstraintSpace(component.space);
  desc.enabled = component.enabled;
  desc.priority = component.priority;
  desc.velocity_solver_steps = component.velocity_solver_steps;
  desc.position_solver_steps = component.position_solver_steps;
  desc.draw_size = component.draw_size;
  desc.user_data = component.user_data;
  desc.auto_detect_point = component.auto_detect_point;
  desc.point1 = math::toGlm(component.point1);
  desc.point2 = math::toGlm(component.point2);
  desc.axis1 = math::toGlm(component.axis1);
  desc.axis2 = math::toGlm(component.axis2);
  desc.normal1 = math::toGlm(component.normal1);
  desc.normal2 = math::toGlm(component.normal2);
  desc.plane_axis1 = math::toGlm(component.plane_axis1);
  desc.plane_axis2 = math::toGlm(component.plane_axis2);
  desc.min_distance = component.min_distance;
  desc.max_distance = component.max_distance;
  desc.limits_min = component.limits_min;
  desc.limits_max = component.limits_max;
  desc.half_cone_angle = component.half_cone_angle;
  desc.normal_half_cone_angle = component.normal_half_cone_angle;
  desc.plane_half_cone_angle = component.plane_half_cone_angle;
  desc.twist_min_angle = component.twist_min_angle;
  desc.twist_max_angle = component.twist_max_angle;
  desc.max_friction_force = component.max_friction_force;
  desc.max_friction_torque = component.max_friction_torque;
  desc.limit_spring = toPhysicsSpring(component.limit_spring);
  desc.six_dof_min_limits = component.six_dof_min_limits;
  desc.six_dof_max_limits = component.six_dof_max_limits;
  desc.six_dof_max_friction = component.six_dof_max_friction;
  return desc;
}

std::size_t constraintSignature(const PhysicsConstraintDesc& desc,
                                std::uintptr_t body_a,
                                std::uintptr_t body_b) {
  std::size_t seed = 0;
  hashCombine(seed, body_a);
  hashCombine(seed, body_b);
  hashCombine(seed, static_cast<uint8_t>(desc.type));
  hashCombine(seed, static_cast<uint8_t>(desc.space));
  hashCombine(seed, desc.enabled);
  hashCombine(seed, desc.priority);
  hashCombine(seed, desc.velocity_solver_steps);
  hashCombine(seed, desc.position_solver_steps);
  hashCombine(seed, desc.draw_size);
  hashCombine(seed, desc.user_data);
  hashCombine(seed, desc.auto_detect_point);
  hashVec3(seed, desc.point1);
  hashVec3(seed, desc.point2);
  hashVec3(seed, desc.axis1);
  hashVec3(seed, desc.axis2);
  hashVec3(seed, desc.normal1);
  hashVec3(seed, desc.normal2);
  hashVec3(seed, desc.plane_axis1);
  hashVec3(seed, desc.plane_axis2);
  hashCombine(seed, desc.min_distance);
  hashCombine(seed, desc.max_distance);
  hashCombine(seed, desc.limits_min);
  hashCombine(seed, desc.limits_max);
  hashCombine(seed, desc.half_cone_angle);
  hashCombine(seed, desc.normal_half_cone_angle);
  hashCombine(seed, desc.plane_half_cone_angle);
  hashCombine(seed, desc.twist_min_angle);
  hashCombine(seed, desc.twist_max_angle);
  hashCombine(seed, desc.max_friction_force);
  hashCombine(seed, desc.max_friction_torque);
  hashCombine(seed, static_cast<uint8_t>(desc.limit_spring.mode));
  hashCombine(seed, desc.limit_spring.frequency_or_stiffness);
  hashCombine(seed, desc.limit_spring.damping);
  for (float value : desc.six_dof_min_limits) hashCombine(seed, value);
  for (float value : desc.six_dof_max_limits) hashCombine(seed, value);
  for (float value : desc.six_dof_max_friction) hashCombine(seed, value);
  return seed;
}

PhysicsVehicleControllerType toPhysicsVehicleController(components::PhysicsVehicleControllerKind kind) {
  switch (kind) {
    case components::PhysicsVehicleControllerKind::Wheeled:
      return PhysicsVehicleControllerType::Wheeled;
    case components::PhysicsVehicleControllerKind::Motorcycle:
      return PhysicsVehicleControllerType::Motorcycle;
    case components::PhysicsVehicleControllerKind::Tracked:
      return PhysicsVehicleControllerType::Tracked;
  }
  return PhysicsVehicleControllerType::Wheeled;
}

PhysicsVehicleCollisionTesterType toPhysicsVehicleCollisionTester(
    components::PhysicsVehicleCollisionTesterKind kind) {
  switch (kind) {
    case components::PhysicsVehicleCollisionTesterKind::Ray:
      return PhysicsVehicleCollisionTesterType::Ray;
    case components::PhysicsVehicleCollisionTesterKind::SphereCast:
      return PhysicsVehicleCollisionTesterType::SphereCast;
    case components::PhysicsVehicleCollisionTesterKind::CylinderCast:
      return PhysicsVehicleCollisionTesterType::CylinderCast;
  }
  return PhysicsVehicleCollisionTesterType::Ray;
}

PhysicsVehicleTransmissionMode toPhysicsVehicleTransmission(
    components::PhysicsVehicleTransmissionKind kind) {
  switch (kind) {
    case components::PhysicsVehicleTransmissionKind::Automatic:
      return PhysicsVehicleTransmissionMode::Automatic;
    case components::PhysicsVehicleTransmissionKind::Manual:
      return PhysicsVehicleTransmissionMode::Manual;
  }
  return PhysicsVehicleTransmissionMode::Automatic;
}

PhysicsSpringSettings toPhysicsSpring(const components::PhysicsVehicleSpring& spring) {
  return {
      .mode = spring.mode == components::PhysicsVehicleSpringKind::StiffnessAndDamping
                  ? PhysicsSpringMode::StiffnessAndDamping
                  : PhysicsSpringMode::FrequencyAndDamping,
      .frequency_or_stiffness = spring.frequency_or_stiffness,
      .damping = spring.damping,
  };
}

PhysicsVehicleDesc buildVehicleDesc(const components::PhysicsVehicleComponent& component) {
  PhysicsVehicleDesc desc;
  desc.controller = toPhysicsVehicleController(component.controller);
  desc.collision_tester = toPhysicsVehicleCollisionTester(component.collision_tester);
  desc.up = math::toGlm(component.up);
  desc.forward = math::toGlm(component.forward);
  desc.max_pitch_roll_angle = component.max_pitch_roll_angle;
  desc.collision_test_sphere_radius = component.collision_test_sphere_radius;
  desc.collision_test_cylinder_convex_radius_fraction =
      component.collision_test_cylinder_convex_radius_fraction;
  desc.collision_test_max_slope_angle = component.collision_test_max_slope_angle;
  desc.collision_test_layer = component.collision_test_layer;
  desc.num_steps_between_collision_test_active = component.num_steps_between_collision_test_active;
  desc.num_steps_between_collision_test_inactive = component.num_steps_between_collision_test_inactive;
  desc.override_gravity = component.override_gravity;
  desc.gravity = math::toGlm(component.gravity);
  desc.priority = component.priority;
  desc.velocity_solver_steps = component.velocity_solver_steps;
  desc.position_solver_steps = component.position_solver_steps;
  desc.draw_size = component.draw_size;
  desc.user_data = component.user_data;

  desc.wheels.reserve(component.wheels.size());
  for (const auto& wheel : component.wheels) {
    PhysicsVehicleWheelDesc wheel_desc;
    wheel_desc.position = math::toGlm(wheel.position);
    wheel_desc.suspension_force_point = math::toGlm(wheel.suspension_force_point);
    wheel_desc.suspension_direction = math::toGlm(wheel.suspension_direction);
    wheel_desc.steering_axis = math::toGlm(wheel.steering_axis);
    wheel_desc.wheel_up = math::toGlm(wheel.wheel_up);
    wheel_desc.wheel_forward = math::toGlm(wheel.wheel_forward);
    wheel_desc.suspension_min_length = wheel.suspension_min_length;
    wheel_desc.suspension_max_length = wheel.suspension_max_length;
    wheel_desc.suspension_preload_length = wheel.suspension_preload_length;
    wheel_desc.suspension_spring = toPhysicsSpring(wheel.suspension_spring);
    wheel_desc.radius = wheel.radius;
    wheel_desc.width = wheel.width;
    wheel_desc.enable_suspension_force_point = wheel.enable_suspension_force_point;
    wheel_desc.inertia = wheel.inertia;
    wheel_desc.angular_damping = wheel.angular_damping;
    wheel_desc.max_steer_angle = wheel.max_steer_angle;
    wheel_desc.longitudinal_friction.reserve(wheel.longitudinal_friction.size());
    for (const auto& point : wheel.longitudinal_friction) {
      wheel_desc.longitudinal_friction.push_back({point.x, point.y});
    }
    wheel_desc.lateral_friction.reserve(wheel.lateral_friction.size());
    for (const auto& point : wheel.lateral_friction) {
      wheel_desc.lateral_friction.push_back({point.x, point.y});
    }
    wheel_desc.max_brake_torque = wheel.max_brake_torque;
    wheel_desc.max_hand_brake_torque = wheel.max_hand_brake_torque;
    wheel_desc.tracked_longitudinal_friction = wheel.tracked_longitudinal_friction;
    wheel_desc.tracked_lateral_friction = wheel.tracked_lateral_friction;
    desc.wheels.push_back(std::move(wheel_desc));
  }

  desc.anti_roll_bars.reserve(component.anti_roll_bars.size());
  for (const auto& anti_roll_bar : component.anti_roll_bars) {
    desc.anti_roll_bars.push_back({
        .left_wheel = anti_roll_bar.left_wheel,
        .right_wheel = anti_roll_bar.right_wheel,
        .stiffness = anti_roll_bar.stiffness,
    });
  }

  desc.engine.max_torque = component.engine.max_torque;
  desc.engine.min_rpm = component.engine.min_rpm;
  desc.engine.max_rpm = component.engine.max_rpm;
  desc.engine.inertia = component.engine.inertia;
  desc.engine.angular_damping = component.engine.angular_damping;
  desc.engine.normalized_torque.reserve(component.engine.normalized_torque.size());
  for (const auto& point : component.engine.normalized_torque) {
    desc.engine.normalized_torque.push_back({point.x, point.y});
  }

  desc.transmission.mode = toPhysicsVehicleTransmission(component.transmission.mode);
  desc.transmission.gear_ratios = component.transmission.gear_ratios;
  desc.transmission.reverse_gear_ratios = component.transmission.reverse_gear_ratios;
  desc.transmission.switch_time = component.transmission.switch_time;
  desc.transmission.clutch_release_time = component.transmission.clutch_release_time;
  desc.transmission.switch_latency = component.transmission.switch_latency;
  desc.transmission.shift_up_rpm = component.transmission.shift_up_rpm;
  desc.transmission.shift_down_rpm = component.transmission.shift_down_rpm;
  desc.transmission.clutch_strength = component.transmission.clutch_strength;

  desc.differentials.reserve(component.differentials.size());
  for (const auto& differential : component.differentials) {
    desc.differentials.push_back({
        .left_wheel = differential.left_wheel,
        .right_wheel = differential.right_wheel,
        .differential_ratio = differential.differential_ratio,
        .left_right_split = differential.left_right_split,
        .limited_slip_ratio = differential.limited_slip_ratio,
        .engine_torque_ratio = differential.engine_torque_ratio,
    });
  }

  desc.differential_limited_slip_ratio = component.differential_limited_slip_ratio;
  desc.motorcycle.max_lean_angle = component.motorcycle.max_lean_angle;
  desc.motorcycle.lean_spring_constant = component.motorcycle.lean_spring_constant;
  desc.motorcycle.lean_spring_damping = component.motorcycle.lean_spring_damping;
  desc.motorcycle.lean_spring_integration_coefficient =
      component.motorcycle.lean_spring_integration_coefficient;
  desc.motorcycle.lean_spring_integration_decay =
      component.motorcycle.lean_spring_integration_decay;
  desc.motorcycle.lean_smoothing_factor = component.motorcycle.lean_smoothing_factor;
  desc.motorcycle.enable_lean_controller = component.motorcycle.enable_lean_controller;
  desc.motorcycle.enable_lean_steering_limit = component.motorcycle.enable_lean_steering_limit;

  for (size_t i = 0; i < desc.tracks.size(); ++i) {
    desc.tracks[i].driven_wheel = component.tracks[i].driven_wheel;
    desc.tracks[i].wheels = component.tracks[i].wheels;
    desc.tracks[i].inertia = component.tracks[i].inertia;
    desc.tracks[i].angular_damping = component.tracks[i].angular_damping;
    desc.tracks[i].max_brake_torque = component.tracks[i].max_brake_torque;
    desc.tracks[i].differential_ratio = component.tracks[i].differential_ratio;
  }

  return desc;
}

PhysicsVehicleInput buildVehicleInput(const components::PhysicsVehicleInputState& input) {
  return {
      .forward = input.forward,
      .right = input.right,
      .brake = input.brake,
      .hand_brake = input.hand_brake,
      .left_ratio = input.left_ratio,
      .right_ratio = input.right_ratio,
      .current_gear = input.current_gear,
      .clutch_friction = input.clutch_friction,
  };
}

void hashCurve(std::size_t& seed, const std::vector<PhysicsVehicleCurvePoint>& curve) {
  hashCombine(seed, curve.size());
  for (const auto& point : curve) {
    hashCombine(seed, point.x);
    hashCombine(seed, point.y);
  }
}

void hashVehicleDesc(std::size_t& seed, const PhysicsVehicleDesc& desc) {
  hashCombine(seed, static_cast<uint8_t>(desc.controller));
  hashCombine(seed, static_cast<uint8_t>(desc.collision_tester));
  hashVec3(seed, desc.up);
  hashVec3(seed, desc.forward);
  hashCombine(seed, desc.max_pitch_roll_angle);
  hashCombine(seed, desc.collision_test_sphere_radius);
  hashCombine(seed, desc.collision_test_cylinder_convex_radius_fraction);
  hashCombine(seed, desc.collision_test_max_slope_angle);
  hashCombine(seed, desc.collision_test_layer);
  hashCombine(seed, desc.num_steps_between_collision_test_active);
  hashCombine(seed, desc.num_steps_between_collision_test_inactive);
  hashCombine(seed, desc.override_gravity);
  hashVec3(seed, desc.gravity);
  hashCombine(seed, desc.priority);
  hashCombine(seed, desc.velocity_solver_steps);
  hashCombine(seed, desc.position_solver_steps);
  hashCombine(seed, desc.draw_size);
  hashCombine(seed, desc.user_data);

  hashCombine(seed, desc.wheels.size());
  for (const auto& wheel : desc.wheels) {
    hashVec3(seed, wheel.position);
    hashVec3(seed, wheel.suspension_force_point);
    hashVec3(seed, wheel.suspension_direction);
    hashVec3(seed, wheel.steering_axis);
    hashVec3(seed, wheel.wheel_up);
    hashVec3(seed, wheel.wheel_forward);
    hashCombine(seed, wheel.suspension_min_length);
    hashCombine(seed, wheel.suspension_max_length);
    hashCombine(seed, wheel.suspension_preload_length);
    hashCombine(seed, static_cast<uint8_t>(wheel.suspension_spring.mode));
    hashCombine(seed, wheel.suspension_spring.frequency_or_stiffness);
    hashCombine(seed, wheel.suspension_spring.damping);
    hashCombine(seed, wheel.radius);
    hashCombine(seed, wheel.width);
    hashCombine(seed, wheel.enable_suspension_force_point);
    hashCombine(seed, wheel.inertia);
    hashCombine(seed, wheel.angular_damping);
    hashCombine(seed, wheel.max_steer_angle);
    hashCurve(seed, wheel.longitudinal_friction);
    hashCurve(seed, wheel.lateral_friction);
    hashCombine(seed, wheel.max_brake_torque);
    hashCombine(seed, wheel.max_hand_brake_torque);
    hashCombine(seed, wheel.tracked_longitudinal_friction);
    hashCombine(seed, wheel.tracked_lateral_friction);
  }

  for (const auto& anti_roll_bar : desc.anti_roll_bars) {
    hashCombine(seed, anti_roll_bar.left_wheel);
    hashCombine(seed, anti_roll_bar.right_wheel);
    hashCombine(seed, anti_roll_bar.stiffness);
  }

  hashCombine(seed, desc.engine.max_torque);
  hashCombine(seed, desc.engine.min_rpm);
  hashCombine(seed, desc.engine.max_rpm);
  hashCombine(seed, desc.engine.inertia);
  hashCombine(seed, desc.engine.angular_damping);
  hashCurve(seed, desc.engine.normalized_torque);

  hashCombine(seed, static_cast<uint8_t>(desc.transmission.mode));
  for (float ratio : desc.transmission.gear_ratios) hashCombine(seed, ratio);
  for (float ratio : desc.transmission.reverse_gear_ratios) hashCombine(seed, ratio);
  hashCombine(seed, desc.transmission.switch_time);
  hashCombine(seed, desc.transmission.clutch_release_time);
  hashCombine(seed, desc.transmission.switch_latency);
  hashCombine(seed, desc.transmission.shift_up_rpm);
  hashCombine(seed, desc.transmission.shift_down_rpm);
  hashCombine(seed, desc.transmission.clutch_strength);

  for (const auto& differential : desc.differentials) {
    hashCombine(seed, differential.left_wheel);
    hashCombine(seed, differential.right_wheel);
    hashCombine(seed, differential.differential_ratio);
    hashCombine(seed, differential.left_right_split);
    hashCombine(seed, differential.limited_slip_ratio);
    hashCombine(seed, differential.engine_torque_ratio);
  }
  hashCombine(seed, desc.differential_limited_slip_ratio);

  hashCombine(seed, desc.motorcycle.max_lean_angle);
  hashCombine(seed, desc.motorcycle.lean_spring_constant);
  hashCombine(seed, desc.motorcycle.lean_spring_damping);
  hashCombine(seed, desc.motorcycle.lean_spring_integration_coefficient);
  hashCombine(seed, desc.motorcycle.lean_spring_integration_decay);
  hashCombine(seed, desc.motorcycle.lean_smoothing_factor);
  hashCombine(seed, desc.motorcycle.enable_lean_controller);
  hashCombine(seed, desc.motorcycle.enable_lean_steering_limit);

  for (const auto& track : desc.tracks) {
    hashCombine(seed, track.driven_wheel);
    for (uint32_t wheel : track.wheels) hashCombine(seed, wheel);
    hashCombine(seed, track.inertia);
    hashCombine(seed, track.angular_damping);
    hashCombine(seed, track.max_brake_torque);
    hashCombine(seed, track.differential_ratio);
  }
}

std::size_t vehicleSignature(const PhysicsVehicleDesc& desc, std::uintptr_t body) {
  std::size_t seed = 0;
  hashCombine(seed, body);
  hashVehicleDesc(seed, desc);
  return seed;
}

PhysicsSoftBodyPreset toPhysicsSoftBodyPreset(components::PhysicsSoftBodyPresetKind preset) {
  switch (preset) {
    case components::PhysicsSoftBodyPresetKind::Custom:
      return PhysicsSoftBodyPreset::Custom;
    case components::PhysicsSoftBodyPresetKind::Cloth:
      return PhysicsSoftBodyPreset::Cloth;
    case components::PhysicsSoftBodyPresetKind::Cube:
      return PhysicsSoftBodyPreset::Cube;
    case components::PhysicsSoftBodyPresetKind::Sphere:
      return PhysicsSoftBodyPreset::Sphere;
  }
  return PhysicsSoftBodyPreset::Custom;
}

PhysicsSoftBodyBendType toPhysicsSoftBodyBend(components::PhysicsSoftBodyBendKind bend) {
  switch (bend) {
    case components::PhysicsSoftBodyBendKind::None:
      return PhysicsSoftBodyBendType::None;
    case components::PhysicsSoftBodyBendKind::Distance:
      return PhysicsSoftBodyBendType::Distance;
    case components::PhysicsSoftBodyBendKind::Dihedral:
      return PhysicsSoftBodyBendType::Dihedral;
  }
  return PhysicsSoftBodyBendType::Distance;
}

PhysicsSoftBodyLraType toPhysicsSoftBodyLra(components::PhysicsSoftBodyLraKind lra) {
  switch (lra) {
    case components::PhysicsSoftBodyLraKind::None:
      return PhysicsSoftBodyLraType::None;
    case components::PhysicsSoftBodyLraKind::EuclideanDistance:
      return PhysicsSoftBodyLraType::EuclideanDistance;
    case components::PhysicsSoftBodyLraKind::GeodesicDistance:
      return PhysicsSoftBodyLraType::GeodesicDistance;
  }
  return PhysicsSoftBodyLraType::None;
}

PhysicsSoftBodyDesc buildSoftBodyDesc(const components::PhysicsSoftBodyComponent& component,
                                      const components::TransformComponent* transform) {
  PhysicsSoftBodyDesc desc;
  desc.preset = toPhysicsSoftBodyPreset(component.preset);
  desc.user_data = component.user_data;
  if (transform != nullptr) {
    desc.position = math::toGlm(transform->getPosition());
    desc.rotation = math::toGlm(transform->getRotation());
  }

  desc.vertices.reserve(component.vertices.size());
  for (const auto& vertex : component.vertices) {
    desc.vertices.push_back({
        .position = math::toGlm(vertex.position),
        .velocity = math::toGlm(vertex.velocity),
        .inverse_mass = vertex.inverse_mass,
    });
  }
  desc.faces.reserve(component.faces.size());
  for (const auto& face : component.faces) {
    desc.faces.push_back({face.vertex0, face.vertex1, face.vertex2, face.material_index});
  }
  desc.edges.reserve(component.edges.size());
  for (const auto& edge : component.edges) {
    desc.edges.push_back({edge.vertex0, edge.vertex1, edge.compliance});
  }
  desc.volumes.reserve(component.volumes.size());
  for (const auto& volume : component.volumes) {
    desc.volumes.push_back({volume.vertex0, volume.vertex1, volume.vertex2, volume.vertex3,
                            volume.compliance});
  }
  desc.pinned_vertices = component.pinned_vertices;

  desc.grid_size_x = component.grid_size_x;
  desc.grid_size_y = component.grid_size_y;
  desc.grid_size_z = component.grid_size_z;
  desc.grid_spacing = component.grid_spacing;
  desc.radius = component.radius;
  desc.sphere_theta = component.sphere_theta;
  desc.sphere_phi = component.sphere_phi;
  desc.pin_cloth_corners = component.pin_cloth_corners;
  desc.create_constraints = component.create_constraints;
  desc.optimize = component.optimize;
  desc.bend_type = toPhysicsSoftBodyBend(component.bend_type);
  desc.vertex_attributes.compliance = component.vertex_attributes.compliance;
  desc.vertex_attributes.shear_compliance = component.vertex_attributes.shear_compliance;
  desc.vertex_attributes.bend_compliance = component.vertex_attributes.bend_compliance;
  desc.vertex_attributes.lra_type = toPhysicsSoftBodyLra(component.vertex_attributes.lra_type);
  desc.vertex_attributes.lra_max_distance_multiplier =
      component.vertex_attributes.lra_max_distance_multiplier;
  desc.angle_tolerance = component.angle_tolerance;
  desc.vertex_radius = component.vertex_radius;
  desc.material.friction = component.friction;
  desc.material.restitution = component.restitution;
  desc.collision_filter.layers = component.collision_layers;
  desc.collision_filter.collides_with = component.collides_with;
  desc.solver_iterations = component.solver_iterations;
  desc.linear_damping = component.linear_damping;
  desc.max_linear_velocity = component.max_linear_velocity;
  desc.pressure = component.pressure;
  desc.gravity_factor = component.gravity_factor;
  desc.update_position = component.update_position;
  desc.make_rotation_identity = component.make_rotation_identity;
  desc.allow_sleeping = component.allow_sleeping;
  desc.activate = component.activate;
  return desc;
}

std::size_t softBodySignature(const PhysicsSoftBodyDesc& desc) {
  std::size_t seed = 0;
  hashCombine(seed, static_cast<uint8_t>(desc.preset));
  hashCombine(seed, desc.user_data);
  for (const auto& vertex : desc.vertices) {
    hashVec3(seed, vertex.position);
    hashVec3(seed, vertex.velocity);
    hashCombine(seed, vertex.inverse_mass);
  }
  for (const auto& face : desc.faces) {
    hashCombine(seed, face.vertex0);
    hashCombine(seed, face.vertex1);
    hashCombine(seed, face.vertex2);
    hashCombine(seed, face.material_index);
  }
  for (const auto& edge : desc.edges) {
    hashCombine(seed, edge.vertex0);
    hashCombine(seed, edge.vertex1);
    hashCombine(seed, edge.compliance);
  }
  for (const auto& volume : desc.volumes) {
    hashCombine(seed, volume.vertex0);
    hashCombine(seed, volume.vertex1);
    hashCombine(seed, volume.vertex2);
    hashCombine(seed, volume.vertex3);
    hashCombine(seed, volume.compliance);
  }
  for (uint32_t vertex : desc.pinned_vertices) hashCombine(seed, vertex);
  hashCombine(seed, desc.grid_size_x);
  hashCombine(seed, desc.grid_size_y);
  hashCombine(seed, desc.grid_size_z);
  hashCombine(seed, desc.grid_spacing);
  hashCombine(seed, desc.radius);
  hashCombine(seed, desc.sphere_theta);
  hashCombine(seed, desc.sphere_phi);
  hashCombine(seed, desc.pin_cloth_corners);
  hashCombine(seed, desc.create_constraints);
  hashCombine(seed, desc.optimize);
  hashCombine(seed, static_cast<uint8_t>(desc.bend_type));
  hashCombine(seed, desc.vertex_attributes.compliance);
  hashCombine(seed, desc.vertex_attributes.shear_compliance);
  hashCombine(seed, desc.vertex_attributes.bend_compliance);
  hashCombine(seed, static_cast<uint8_t>(desc.vertex_attributes.lra_type));
  hashCombine(seed, desc.vertex_attributes.lra_max_distance_multiplier);
  hashCombine(seed, desc.angle_tolerance);
  hashCombine(seed, desc.vertex_radius);
  hashCombine(seed, desc.material.friction);
  hashCombine(seed, desc.material.restitution);
  hashCombine(seed, desc.collision_filter.layers);
  hashCombine(seed, desc.collision_filter.collides_with);
  hashCombine(seed, desc.solver_iterations);
  hashCombine(seed, desc.linear_damping);
  hashCombine(seed, desc.max_linear_velocity);
  hashCombine(seed, desc.gravity_factor);
  hashCombine(seed, desc.make_rotation_identity);
  hashCombine(seed, desc.allow_sleeping);
  return seed;
}

PhysicsShapeDesc buildShapeDesc(const ecs::World& world,
                                ecs::Entity entity,
                                const components::TransformComponent& transform,
                                const MeshColliderGeometryResolver& resolve_mesh_geometry) {
  const math::Vec3 scale = transform.getScale();
  PhysicsShapeDesc shape;
  if (!world.has<components::ColliderComponent>(entity)) {
    return shape;
  }

  const auto& collider = world.get<components::ColliderComponent>(entity);
  std::visit(
      [&](const auto& collider_shape) {
        using Shape = std::decay_t<decltype(collider_shape)>;
        if constexpr (std::is_same_v<Shape, components::BoxColliderShape>) {
          shape.type = PhysicsShapeType::Box;
          shape.center = math::toGlm(scaledLocal(collider_shape.center, scale));
          shape.half_extents = math::toGlm(absScaledLocal(collider_shape.half_extents, scale));
        } else if constexpr (std::is_same_v<Shape, components::SphereColliderShape>) {
          shape.type = PhysicsShapeType::Sphere;
          shape.center = math::toGlm(scaledLocal(collider_shape.center, scale));
          shape.radius = std::abs(collider_shape.radius) * maxAbsScale(scale);
        } else if constexpr (std::is_same_v<Shape, components::CapsuleColliderShape>) {
          shape.type = PhysicsShapeType::Capsule;
          shape.center = math::toGlm(scaledLocal(collider_shape.center, scale));
          shape.radius = std::abs(collider_shape.radius) * maxAbsScaleXZ(scale);
          shape.height = std::abs(collider_shape.height * scale.y);
        } else if constexpr (std::is_same_v<Shape, components::CylinderColliderShape>) {
          shape.type = PhysicsShapeType::Cylinder;
          shape.center = math::toGlm(scaledLocal(collider_shape.center, scale));
          shape.radius = std::abs(collider_shape.radius) * maxAbsScaleXZ(scale);
          shape.height = std::abs(collider_shape.height * scale.y);
          shape.convex_radius = collider_shape.convex_radius;
        } else if constexpr (std::is_same_v<Shape, components::TaperedCapsuleColliderShape>) {
          shape.type = PhysicsShapeType::TaperedCapsule;
          shape.center = math::toGlm(scaledLocal(collider_shape.center, scale));
          shape.top_radius = std::abs(collider_shape.top_radius) * maxAbsScaleXZ(scale);
          shape.bottom_radius = std::abs(collider_shape.bottom_radius) * maxAbsScaleXZ(scale);
          shape.height = std::abs(collider_shape.height * scale.y);
        } else if constexpr (std::is_same_v<Shape, components::ConvexHullColliderShape>) {
          shape.type = PhysicsShapeType::ConvexHull;
          shape.center = math::toGlm(scaledLocal(collider_shape.center, scale));
          shape.convex_radius = collider_shape.convex_radius;
          shape.points.reserve(collider_shape.points.size());
          for (const math::Vec3& point : collider_shape.points) {
            shape.points.push_back(math::toGlm(scaledLocal(point, scale)));
          }
        } else if constexpr (std::is_same_v<Shape, components::TriangleColliderShape>) {
          shape.type = PhysicsShapeType::Triangle;
          shape.convex_radius = collider_shape.convex_radius;
          for (size_t i = 0; i < collider_shape.points.size(); ++i) {
            shape.triangle[i] = math::toGlm(scaledLocal(collider_shape.points[i], scale));
          }
        } else if constexpr (std::is_same_v<Shape, components::HeightFieldColliderShape>) {
          shape.type = PhysicsShapeType::HeightField;
          shape.height_samples = collider_shape.samples;
          shape.height_sample_count = collider_shape.sample_count;
          shape.height_offset = math::toGlm(scaledLocal(collider_shape.offset, scale));
          shape.height_scale = math::toGlm(absScaledLocal(collider_shape.scale, scale));
          shape.height_block_size = collider_shape.block_size;
          shape.height_bits_per_sample = collider_shape.bits_per_sample;
        } else if constexpr (std::is_same_v<Shape, components::MeshColliderShape>) {
          shape.type = PhysicsShapeType::Mesh;
          shape.mesh_asset_key = collider_shape.mesh_asset_key;
          if (shape.mesh_asset_key.empty() && world.has<components::MeshComponent>(entity)) {
            shape.mesh_asset_key = world.get<components::MeshComponent>(entity).mesh_asset_key;
          }

          const std::vector<math::Vec3>* source_vertices = &collider_shape.vertices;
          const std::vector<uint32_t>* source_indices = &collider_shape.indices;
          if ((source_vertices->empty() || source_indices->empty()) &&
              !shape.mesh_asset_key.empty() &&
              resolve_mesh_geometry) {
            if (const MeshColliderGeometry* resolved = resolve_mesh_geometry(shape.mesh_asset_key)) {
              source_vertices = &resolved->vertices;
              source_indices = &resolved->indices;
            }
          }

          shape.mesh_vertices.reserve(source_vertices->size());
          for (const math::Vec3& vertex : *source_vertices) {
            shape.mesh_vertices.push_back(math::toGlm(scaledLocal(vertex, scale)));
          }
          shape.mesh_indices = *source_indices;
        }
      },
      collider.shape);
  return shape;
}

PhysicsBodyDesc buildBodyDesc(const ecs::World& world,
                              ecs::Entity entity,
                              const components::TransformComponent& transform,
                              const components::RigidbodyComponent* rigidbody,
                              PhysicsMotionType fallback_motion,
                              const MeshColliderGeometryResolver& resolve_mesh_geometry) {
  PhysicsBodyDesc desc;
  desc.shape = buildShapeDesc(world, entity, transform, resolve_mesh_geometry);
  desc.position = math::toGlm(transform.getPosition());
  desc.rotation = math::toGlm(transform.getRotation());
  desc.material = materialForEntity(world, entity);
  desc.collision_filter = collisionFilterForEntity(world, entity);
  desc.motion = fallback_motion;
  desc.sensor = colliderIsTrigger(world, entity);

  if (rigidbody != nullptr) {
    desc.mass = rigidbody->mass;
    desc.linear_velocity = math::toGlm(rigidbody->velocity);
    desc.angular_velocity = math::toGlm(rigidbody->angular_velocity);
    desc.motion = toPhysicsMotion(*rigidbody);
    desc.motion_quality = toPhysicsQuality(rigidbody->motion_quality);
    desc.allowed_dofs = rigidbody->allowed_dofs;
    desc.gravity_factor = rigidbody->use_gravity ? rigidbody->gravity_factor : 0.0f;
    desc.linear_damping = rigidbody->linear_damping;
    desc.angular_damping = rigidbody->angular_damping;
    desc.max_linear_velocity = rigidbody->max_linear_velocity;
    desc.max_angular_velocity = rigidbody->max_angular_velocity;
    desc.inertia_multiplier = rigidbody->inertia_multiplier;
    desc.velocity_solver_steps = rigidbody->velocity_solver_steps;
    desc.position_solver_steps = rigidbody->position_solver_steps;
    desc.allow_sleeping = rigidbody->allow_sleeping;
    desc.allow_dynamic_or_kinematic = rigidbody->allow_dynamic_or_kinematic;
    desc.collide_kinematic_vs_non_dynamic = rigidbody->collide_kinematic_vs_non_dynamic;
    desc.use_manifold_reduction = rigidbody->use_manifold_reduction;
    desc.apply_gyroscopic_force = rigidbody->apply_gyroscopic_force;
    desc.enhanced_internal_edge_removal = rigidbody->enhanced_internal_edge_removal;
    desc.sensor = desc.sensor || rigidbody->is_trigger;
  }

  return desc;
}

glm::vec3 groundProbeDimensions(const ecs::World& world,
                                ecs::Entity entity,
                                const components::TransformComponent& transform) {
  const math::Vec3 scale = transform.getScale();
  if (!world.has<components::ColliderComponent>(entity)) {
    return {1.0f, 1.0f, 1.0f};
  }

  const auto& collider = world.get<components::ColliderComponent>(entity);
  return std::visit(
      [&](const auto& shape) -> glm::vec3 {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, components::BoxColliderShape>) {
          const math::Vec3 extents = absScaledLocal(shape.half_extents, scale);
          return {extents.x * 2.0f, extents.y * 2.0f, extents.z * 2.0f};
        } else if constexpr (std::is_same_v<Shape, components::SphereColliderShape>) {
          const float radius = std::abs(shape.radius) * maxAbsScale(scale);
          return {radius * 2.0f, radius * 2.0f, radius * 2.0f};
        } else if constexpr (std::is_same_v<Shape, components::CapsuleColliderShape>) {
          const float radius = std::abs(shape.radius) * maxAbsScaleXZ(scale);
          return {radius * 2.0f, std::abs(shape.height * scale.y), radius * 2.0f};
        } else if constexpr (std::is_same_v<Shape, components::CylinderColliderShape>) {
          const float radius = std::abs(shape.radius) * maxAbsScaleXZ(scale);
          return {radius * 2.0f, std::abs(shape.height * scale.y), radius * 2.0f};
        } else if constexpr (std::is_same_v<Shape, components::TaperedCapsuleColliderShape>) {
          const float radius =
              std::max(std::abs(shape.top_radius), std::abs(shape.bottom_radius)) *
              maxAbsScaleXZ(scale);
          return {radius * 2.0f, std::abs(shape.height * scale.y), radius * 2.0f};
        } else {
          return {1.0f, 1.0f, 1.0f};
        }
      },
      collider.shape);
}

ControllerShapeInfo controllerShapeInfo(const ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::ColliderComponent>(entity)) {
    return {};
  }
  const auto& collider = world.get<components::ColliderComponent>(entity);
  ControllerShapeInfo info{};
  info.shape_kind = colliderShapeKind(world, entity);
  if (const auto* box = std::get_if<components::BoxColliderShape>(&collider.shape)) {
    info.half_extents = math::toGlm(box->half_extents);
    info.center = box->center;
    info.valid = true;
  }
  return info;
}

}  // namespace karma::physics::system_internal
