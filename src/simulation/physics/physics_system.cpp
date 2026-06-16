#include "karma/simulation/physics/physics_system.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "karma/core/math/glm.h"
#include "karma/core/math/vec3.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/visibility.h"

namespace karma::physics {

namespace {

bool nearlyEqualVec3(const math::Vec3& a, const math::Vec3& b, float eps = 1e-4f) {
  return std::abs(a.x - b.x) <= eps &&
         std::abs(a.y - b.y) <= eps &&
         std::abs(a.z - b.z) <= eps;
}

math::Vec3 negateVec3(const math::Vec3& v) {
  return {-v.x, -v.y, -v.z};
}

int colliderShapeKind(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return 0;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return 1;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return 2;
  }
  return -1;
}

ecs::queries::ColliderShape colliderShape(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Box;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Sphere;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Capsule;
  }
  if (world.has<components::CylinderColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Cylinder;
  }
  if (world.has<components::TaperedCapsuleColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::TaperedCapsule;
  }
  if (world.has<components::ConvexHullColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::ConvexHull;
  }
  if (world.has<components::TriangleColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Triangle;
  }
  if (world.has<components::HeightFieldColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::HeightField;
  }
  return ecs::queries::ColliderShape::Mesh;
}

bool colliderIsTrigger(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return world.get<components::BoxColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return world.get<components::SphereColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return world.get<components::CapsuleColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::CylinderColliderComponent>(entity)) {
    return world.get<components::CylinderColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::TaperedCapsuleColliderComponent>(entity)) {
    return world.get<components::TaperedCapsuleColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::ConvexHullColliderComponent>(entity)) {
    return world.get<components::ConvexHullColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::TriangleColliderComponent>(entity)) {
    return world.get<components::TriangleColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::HeightFieldColliderComponent>(entity)) {
    return world.get<components::HeightFieldColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::MeshColliderComponent>(entity)) {
    return world.get<components::MeshColliderComponent>(entity).is_trigger;
  }
  return false;
}

bool hasPhysicsCollider(const ecs::World& world, ecs::Entity entity) {
  return world.has<components::BoxColliderComponent>(entity) ||
         world.has<components::SphereColliderComponent>(entity) ||
         world.has<components::CapsuleColliderComponent>(entity) ||
         world.has<components::CylinderColliderComponent>(entity) ||
         world.has<components::TaperedCapsuleColliderComponent>(entity) ||
         world.has<components::ConvexHullColliderComponent>(entity) ||
         world.has<components::TriangleColliderComponent>(entity) ||
         world.has<components::HeightFieldColliderComponent>(entity) ||
         world.has<components::MeshColliderComponent>(entity);
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
  hashCombine(seed, shape.mesh_path);
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

PhysicsShapeDesc buildShapeDesc(const ecs::World& world,
                                ecs::Entity entity,
                                const components::TransformComponent& transform) {
  const math::Vec3 scale = transform.getScale();
  std::vector<PhysicsShapeDesc> shapes;

  if (world.has<components::BoxColliderComponent>(entity)) {
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::Box;
    shape.center = math::toGlm(scaledLocal(collider.center, scale));
    shape.half_extents = math::toGlm(absScaledLocal(collider.half_extents, scale));
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::SphereColliderComponent>(entity)) {
    const auto& collider = world.get<components::SphereColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::Sphere;
    shape.center = math::toGlm(scaledLocal(collider.center, scale));
    shape.radius = std::abs(collider.radius) * maxAbsScale(scale);
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::CapsuleColliderComponent>(entity)) {
    const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::Capsule;
    shape.center = math::toGlm(scaledLocal(collider.center, scale));
    shape.radius = std::abs(collider.radius) * maxAbsScaleXZ(scale);
    shape.height = std::abs(collider.height * scale.y);
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::CylinderColliderComponent>(entity)) {
    const auto& collider = world.get<components::CylinderColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::Cylinder;
    shape.center = math::toGlm(scaledLocal(collider.center, scale));
    shape.radius = std::abs(collider.radius) * maxAbsScaleXZ(scale);
    shape.height = std::abs(collider.height * scale.y);
    shape.convex_radius = collider.convex_radius;
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::TaperedCapsuleColliderComponent>(entity)) {
    const auto& collider = world.get<components::TaperedCapsuleColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::TaperedCapsule;
    shape.center = math::toGlm(scaledLocal(collider.center, scale));
    shape.top_radius = std::abs(collider.top_radius) * maxAbsScaleXZ(scale);
    shape.bottom_radius = std::abs(collider.bottom_radius) * maxAbsScaleXZ(scale);
    shape.height = std::abs(collider.height * scale.y);
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::ConvexHullColliderComponent>(entity)) {
    const auto& collider = world.get<components::ConvexHullColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::ConvexHull;
    shape.center = math::toGlm(scaledLocal(collider.center, scale));
    shape.convex_radius = collider.convex_radius;
    shape.points.reserve(collider.points.size());
    for (const math::Vec3& point : collider.points) {
      shape.points.push_back(math::toGlm(scaledLocal(point, scale)));
    }
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::TriangleColliderComponent>(entity)) {
    const auto& collider = world.get<components::TriangleColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::Triangle;
    shape.convex_radius = collider.convex_radius;
    for (size_t i = 0; i < collider.points.size(); ++i) {
      shape.triangle[i] = math::toGlm(scaledLocal(collider.points[i], scale));
    }
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::HeightFieldColliderComponent>(entity)) {
    const auto& collider = world.get<components::HeightFieldColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::HeightField;
    shape.height_samples = collider.samples;
    shape.height_sample_count = collider.sample_count;
    shape.height_offset = math::toGlm(scaledLocal(collider.offset, scale));
    shape.height_scale = math::toGlm(absScaledLocal(collider.scale, scale));
    shape.height_block_size = collider.block_size;
    shape.height_bits_per_sample = collider.bits_per_sample;
    shapes.push_back(std::move(shape));
  }

  if (world.has<components::MeshColliderComponent>(entity)) {
    const auto& collider = world.get<components::MeshColliderComponent>(entity);
    PhysicsShapeDesc shape;
    shape.type = PhysicsShapeType::Mesh;
    shape.mesh_path = collider.mesh_path;
    if (shape.mesh_path.empty() && world.has<components::MeshComponent>(entity)) {
      shape.mesh_path = world.get<components::MeshComponent>(entity).mesh_key;
    }
    shape.mesh_vertices.reserve(collider.vertices.size());
    for (const math::Vec3& vertex : collider.vertices) {
      shape.mesh_vertices.push_back(math::toGlm(scaledLocal(vertex, scale)));
    }
    shape.mesh_indices = collider.indices;
    shapes.push_back(std::move(shape));
  }

  if (shapes.size() == 1) {
    return std::move(shapes.front());
  }

  PhysicsShapeDesc compound;
  compound.type = PhysicsShapeType::Compound;
  compound.children = std::move(shapes);
  return compound;
}

PhysicsBodyDesc buildBodyDesc(const ecs::World& world,
                              ecs::Entity entity,
                              const components::TransformComponent& transform,
                              const components::RigidbodyComponent* rigidbody,
                              PhysicsMotionType fallback_motion) {
  PhysicsBodyDesc desc;
  desc.shape = buildShapeDesc(world, entity, transform);
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
  if (world.has<components::BoxColliderComponent>(entity)) {
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    const math::Vec3 extents = absScaledLocal(collider.half_extents, scale);
    return {extents.x * 2.0f, extents.y * 2.0f, extents.z * 2.0f};
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    const auto& collider = world.get<components::SphereColliderComponent>(entity);
    const float radius = std::abs(collider.radius) * maxAbsScale(scale);
    return {radius * 2.0f, radius * 2.0f, radius * 2.0f};
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
    const float radius = std::abs(collider.radius) * maxAbsScaleXZ(scale);
    return {radius * 2.0f, std::abs(collider.height * scale.y), radius * 2.0f};
  }
  if (world.has<components::CylinderColliderComponent>(entity)) {
    const auto& collider = world.get<components::CylinderColliderComponent>(entity);
    const float radius = std::abs(collider.radius) * maxAbsScaleXZ(scale);
    return {radius * 2.0f, std::abs(collider.height * scale.y), radius * 2.0f};
  }
  if (world.has<components::TaperedCapsuleColliderComponent>(entity)) {
    const auto& collider = world.get<components::TaperedCapsuleColliderComponent>(entity);
    const float radius =
        std::max(std::abs(collider.top_radius), std::abs(collider.bottom_radius)) *
        maxAbsScaleXZ(scale);
    return {radius * 2.0f, std::abs(collider.height * scale.y), radius * 2.0f};
  }
  return {1.0f, 1.0f, 1.0f};
}

}

void PhysicsSystem::update(ecs::World& world, float dt) {
  syncRigidBodies(world);
  syncConstraints(world);
  syncPlayerController(world, dt);
  applyBodyForces(world);
  physics_.update(dt);
  syncDynamicBodies(world);
  syncContactEvents(world);
  syncGroundContacts(world);
  cleanupStale(world);
}

void PhysicsSystem::syncRigidBodies(ecs::World& world) {
  world.forEach<components::RigidbodyComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!hasPhysicsCollider(world, entity)) {
      return;
    }
    if (!collisionEnabled(world, entity)) {
      return;
    }
    auto& transform = world.get<components::TransformComponent>(entity);
    auto& body = world.get<components::RigidbodyComponent>(entity);
    PhysicsBodyDesc desc = buildBodyDesc(world, entity, transform, &body, PhysicsMotionType::Dynamic);
    const std::size_t signature = bodySignature(desc);

    const uint64_t key = entityKey(entity);
    auto static_it = static_bodies_.find(key);
    if (static_it != static_bodies_.end()) {
      physics_entities_by_handle_.erase(static_it->second.nativeHandle());
      static_it->second.destroy();
      static_bodies_.erase(static_it);
      body_state_.erase(key);
    }

    auto it = rigid_bodies_.find(key);
    auto state_it = body_state_.find(key);
    const bool needs_recreate =
        it == rigid_bodies_.end() ||
        state_it == body_state_.end() ||
        state_it->second.signature != signature;

    if (needs_recreate) {
      if (it != rigid_bodies_.end() && it->second.isValid()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        desc.rotation = it->second.getRotation();
        desc.linear_velocity = it->second.getVelocity();
        desc.angular_velocity = it->second.getAngularVelocity();
        it->second.destroy();
      }
      RigidBody rigid = physics_.createBody(desc);
      it = rigid_bodies_.insert_or_assign(key, std::move(rigid)).first;
      if (it->second.isValid()) {
        physics_entities_by_handle_[it->second.nativeHandle()] = entity;
      }
      body_state_[key] = BodyState{.signature = signature};
    }

    const bool position_dirty = transform.position_dirty_;
    const bool rotation_dirty = transform.rotation_dirty_;
    if (position_dirty || rotation_dirty) {
      if (it->second.isValid()) {
        if (position_dirty) {
          it->second.setPosition(math::toGlm(transform.getPosition()));
        }
        if (rotation_dirty) {
          it->second.setRotation(math::toGlm(transform.getRotation()));
        }
      }
      transform.position_dirty_ = false;
      transform.rotation_dirty_ = false;
    }

    if (it->second.isValid()) {
      if (desc.motion == PhysicsMotionType::Kinematic) {
        it->second.setKinematic(true);
      } else if (desc.motion == PhysicsMotionType::Dynamic) {
        it->second.setKinematic(false);
      }
      if (!body.use_gravity) {
        it->second.setUseGravity(false);
      } else if (std::abs(body.gravity_factor - 1.0f) <= 0.0001f) {
        it->second.setUseGravity(true);
      }
      it->second.setTrigger(body.is_trigger || colliderIsTrigger(world, entity));
      if (desc.motion == PhysicsMotionType::Kinematic) {
        it->second.setVelocity(math::toGlm(body.velocity));
        it->second.setAngularVelocity(math::toGlm(body.angular_velocity));
      }
    }
  });

  world.forEach<components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (world.has<components::RigidbodyComponent>(entity)) {
      return;
    }
    if (!hasPhysicsCollider(world, entity)) {
      return;
    }
    if (!collisionEnabled(world, entity)) {
      return;
    }
    auto& transform = world.get<components::TransformComponent>(entity);
    PhysicsBodyDesc desc = buildBodyDesc(world, entity, transform, nullptr, PhysicsMotionType::Static);
    desc.motion = PhysicsMotionType::Static;
    desc.mass = 0.0f;
    const std::size_t signature = bodySignature(desc);

    const uint64_t key = entityKey(entity);
    auto it = static_bodies_.find(key);
    auto state_it = body_state_.find(key);
    const bool needs_recreate =
        it == static_bodies_.end() ||
        state_it == body_state_.end() ||
        state_it->second.signature != signature;

    if (needs_recreate) {
      if (it != static_bodies_.end() && it->second.isValid()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        it->second.destroy();
      }
      RigidBody body = physics_.createBody(desc);
      it = static_bodies_.insert_or_assign(key, std::move(body)).first;
      if (it->second.isValid()) {
        physics_entities_by_handle_[it->second.nativeHandle()] = entity;
      }
      body_state_[key] = BodyState{.signature = signature};
    }

    if (it->second.isValid()) {
      if (transform.position_dirty_) {
        it->second.setPosition(math::toGlm(transform.getPosition()));
      }
      if (transform.rotation_dirty_) {
        it->second.setRotation(math::toGlm(transform.getRotation()));
      }
      transform.position_dirty_ = false;
      transform.rotation_dirty_ = false;
    }
  });
}

void PhysicsSystem::syncConstraints(ecs::World& world) {
  auto handle_for_entity = [&](ecs::Entity entity) -> std::uintptr_t {
    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it != rigid_bodies_.end() && body_it->second.isValid()) {
      return body_it->second.nativeHandle();
    }
    auto static_it = static_bodies_.find(key);
    if (static_it != static_bodies_.end() && static_it->second.isValid()) {
      return static_it->second.nativeHandle();
    }
    return 0;
  };

  world.forEach<components::PhysicsConstraintComponent>(
      [&](const ecs::Entity entity) {
    const auto& component = world.get<components::PhysicsConstraintComponent>(entity);
    if (!world.isAlive(component.body_a) || !world.isAlive(component.body_b)) {
      return;
    }

    const std::uintptr_t body_a = handle_for_entity(component.body_a);
    const std::uintptr_t body_b = handle_for_entity(component.body_b);
    if (body_a == 0 || body_b == 0 || body_a == body_b) {
      return;
    }

    PhysicsConstraintDesc desc = buildConstraintDesc(component);
    const std::size_t signature = constraintSignature(desc, body_a, body_b);
    const uint64_t key = entityKey(entity);

    auto it = constraints_.find(key);
    auto signature_it = constraint_signatures_.find(key);
    const bool needs_recreate =
        it == constraints_.end() ||
        signature_it == constraint_signatures_.end() ||
        signature_it->second != signature;

    if (!needs_recreate) {
      if (it->second.isValid()) {
        it->second.setEnabled(component.enabled);
      }
      return;
    }

    if (it != constraints_.end()) {
      it->second.destroy();
    }

    Constraint constraint = physics_.createConstraint(desc, body_a, body_b);
    it = constraints_.insert_or_assign(key, std::move(constraint)).first;
    constraint_signatures_[key] = signature;
    if (it->second.isValid()) {
      it->second.setEnabled(component.enabled);
    }
  });
}

void PhysicsSystem::applyBodyForces(ecs::World& world) {
  auto non_zero = [](const math::Vec3& value) {
    return std::abs(value.x) > 0.000001f ||
           std::abs(value.y) > 0.000001f ||
           std::abs(value.z) > 0.000001f;
  };

  world.forEach<components::PhysicsBodyForcesComponent>(
      [&](const ecs::Entity entity) {
    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it == rigid_bodies_.end() || !body_it->second.isValid()) {
      return;
    }

    auto& forces = world.get<components::PhysicsBodyForcesComponent>(entity);
    if (non_zero(forces.force)) {
      if (forces.force_at_position) {
        body_it->second.addForceAtPosition(math::toGlm(forces.force),
                                           math::toGlm(forces.force_position));
      } else {
        body_it->second.addForce(math::toGlm(forces.force));
      }
    }
    if (non_zero(forces.torque)) {
      body_it->second.addTorque(math::toGlm(forces.torque));
    }
    if (non_zero(forces.impulse)) {
      if (forces.impulse_at_position) {
        body_it->second.addImpulseAtPosition(math::toGlm(forces.impulse),
                                             math::toGlm(forces.impulse_position));
      } else {
        body_it->second.addImpulse(math::toGlm(forces.impulse));
      }
    }
    if (non_zero(forces.angular_impulse)) {
      body_it->second.addAngularImpulse(math::toGlm(forces.angular_impulse));
    }

    if (forces.clear_after_step) {
      forces.clearTransient();
    }
  });
}

void PhysicsSystem::syncDynamicBodies(ecs::World& world) {
  world.forEach<components::RigidbodyComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      return;
    }
    if (!hasPhysicsCollider(world, entity)) {
      return;
    }
    auto& body = world.get<components::RigidbodyComponent>(entity);
    if (toPhysicsMotion(body) != PhysicsMotionType::Dynamic) {
      return;
    }
    const uint64_t key = entityKey(entity);
    auto it = rigid_bodies_.find(key);
    if (it == rigid_bodies_.end()) {
      return;
    }
    if (!it->second.isValid()) {
      return;
    }
    auto& transform = world.get<components::TransformComponent>(entity);
    const math::Vec3 body_pos = math::fromGlm(it->second.getPosition());
    transform.setPositionFromPhysics(body_pos);
    transform.setRotationFromPhysics(math::fromGlm(it->second.getRotation()));
    body.velocity = math::fromGlm(it->second.getVelocity());
    body.angular_velocity = math::fromGlm(it->second.getAngularVelocity());
  });
}

void PhysicsSystem::syncContactEvents(ecs::World& world) {
  auto handle_for_entity = [&](ecs::Entity entity) -> std::uintptr_t {
    const uint64_t key = entityKey(entity);
    if (has_player_ && entity == player_entity_ && physics_.playerController()) {
      return player_native_handle_;
    }
    auto body_it = rigid_bodies_.find(key);
    if (body_it != rigid_bodies_.end() && body_it->second.isValid()) {
      return body_it->second.nativeHandle();
    }
    auto static_it = static_bodies_.find(key);
    if (static_it != static_bodies_.end() && static_it->second.isValid()) {
      return static_it->second.nativeHandle();
    }
    return 0;
  };

  auto to_contact_event = [](const TrackedContact& tracked) {
    return components::ContactEvent{
        .other = tracked.other,
        .other_shape = tracked.other_shape,
        .point = tracked.point,
        .normal = tracked.normal,
    };
  };

  std::vector<PhysicsContact> contacts;
  physics_.collectContacts(contacts);
  if (has_player_ && physics_.playerController()) {
    physics_.playerController()->collectContacts(contacts);
  }

  for (const ecs::Entity entity : world.view<components::ContactListenerComponent>()) {
    auto& listener = world.get<components::ContactListenerComponent>(entity);
    if (!world.has<components::ContactEventsComponent>(entity)) {
      world.add(entity, components::ContactEventsComponent{});
    }
    auto& events = world.get<components::ContactEventsComponent>(entity);
    events.clearTransient();

    const uint64_t key = entityKey(entity);
    if (!listener.enabled || !collisionEnabled(world, entity)) {
      previous_contacts_.erase(key);
      continue;
    }

    const std::uintptr_t self_handle = handle_for_entity(entity);
    if (self_handle == 0) {
      previous_contacts_.erase(key);
      continue;
    }

    ContactMap current_contacts;
    auto previous_it = previous_contacts_.find(key);
    const ContactMap* previous = previous_it != previous_contacts_.end() ? &previous_it->second : nullptr;

    for (const PhysicsContact& contact : contacts) {
      const bool self_is_a = contact.handle_a == self_handle;
      const bool self_is_b = contact.handle_b == self_handle;
      if (!self_is_a && !self_is_b) {
        continue;
      }

      const std::uintptr_t other_handle = self_is_a ? contact.handle_b : contact.handle_a;
      auto other_it = physics_entities_by_handle_.find(other_handle);
      if (other_it == physics_entities_by_handle_.end()) {
        continue;
      }

      const ecs::Entity other = other_it->second;
      if (!world.isAlive(other) || other == entity) {
        continue;
      }
      if (!collisionEnabled(world, other) ||
          !matchesCollisionLayerMask(world, other, listener.collision_layer_mask)) {
        continue;
      }
      if (colliderIsTrigger(world, entity) || colliderIsTrigger(world, other)) {
        continue;
      }

      const uint64_t other_key = entityKey(other);
      TrackedContact tracked{
          .other = other,
          .other_shape = colliderShape(world, other),
          .point = self_is_a ? math::fromGlm(contact.point_a) : math::fromGlm(contact.point_b),
          .normal = self_is_a ? negateVec3(math::fromGlm(contact.normal_a_to_b))
                              : math::fromGlm(contact.normal_a_to_b),
      };

      current_contacts[other_key] = tracked;
      events.active.push_back(to_contact_event(tracked));

      if (previous == nullptr || previous->find(other_key) == previous->end()) {
        events.entered.push_back(to_contact_event(tracked));
      } else if (listener.emit_stay) {
        events.stayed.push_back(to_contact_event(tracked));
      }
    }

    if (previous != nullptr) {
      for (const auto& [other_key, tracked] : *previous) {
        if (current_contacts.find(other_key) == current_contacts.end()) {
          events.exited.push_back(to_contact_event(tracked));
        }
      }
    }

    if (current_contacts.empty()) {
      previous_contacts_.erase(key);
    } else {
      previous_contacts_[key] = std::move(current_contacts);
    }
  }
}

void PhysicsSystem::syncPlayerController(ecs::World& world, float dt) {
  (void)dt;
  if (!has_player_) {
    world.forEach<components::PlayerControllerComponent, components::TransformComponent>(
        [&](const ecs::Entity entity) {
      if (!collisionEnabled(world, entity)) {
        return true;
      }
      glm::vec3 half_extents{};
      math::Vec3 center{};
      const int shape_kind = colliderShapeKind(world, entity);
      if (world.has<components::BoxColliderComponent>(entity)) {
        const auto& collider = world.get<components::BoxColliderComponent>(entity);
        half_extents = math::toGlm(collider.half_extents);
        center = collider.center;
      } else if (world.has<components::SphereColliderComponent>(entity)) {
        const auto& collider = world.get<components::SphereColliderComponent>(entity);
        half_extents = glm::vec3(collider.radius);
        center = collider.center;
      } else if (world.has<components::CapsuleColliderComponent>(entity)) {
        const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
        half_extents = glm::vec3(collider.radius, collider.height * 0.5f, collider.radius);
        center = collider.center;
      } else {
        return true;
      }
      player_half_extents_ = {half_extents.x, half_extents.y, half_extents.z};
      player_center_ = center;
      player_shape_kind_ = shape_kind;
      auto& controller = physics_.createPlayer(half_extents * 2.0f);
      player_entity_ = entity;
      has_player_ = true;
      player_native_handle_ = controller.nativeHandle();
      if (player_native_handle_ != 0) {
        physics_entities_by_handle_[player_native_handle_] = entity;
      }
      auto& transform = world.get<components::TransformComponent>(entity);
      controller.setCenter(math::toGlm(center));
      controller.setPosition(math::toGlm(transform.getPosition()));
      return false;
    });
  }

  if (!has_player_) {
    return;
  }

  if (!physics_.playerController()) {
    return;
  }
  auto* controller = physics_.playerController();
  if (!controller) {
    return;
  }
  auto& transform = world.get<components::TransformComponent>(player_entity_);
  auto& input = world.get<components::PlayerControllerComponent>(player_entity_);

  glm::vec3 half_extents{};
  math::Vec3 center{};
  bool has_collider = true;
  const int shape_kind = colliderShapeKind(world, player_entity_);
  if (world.has<components::BoxColliderComponent>(player_entity_)) {
    const auto& collider = world.get<components::BoxColliderComponent>(player_entity_);
    half_extents = math::toGlm(collider.half_extents);
    center = collider.center;
  } else if (world.has<components::SphereColliderComponent>(player_entity_)) {
    const auto& collider = world.get<components::SphereColliderComponent>(player_entity_);
    half_extents = glm::vec3(collider.radius);
    center = collider.center;
  } else if (world.has<components::CapsuleColliderComponent>(player_entity_)) {
    const auto& collider = world.get<components::CapsuleColliderComponent>(player_entity_);
    half_extents = glm::vec3(collider.radius, collider.height * 0.5f, collider.radius);
    center = collider.center;
  } else {
    has_collider = false;
  }

  if (!has_collider) {
    return;
  }

  const math::Vec3 current_half_extents{half_extents.x, half_extents.y, half_extents.z};
  const bool size_changed = !nearlyEqualVec3(current_half_extents, player_half_extents_);
  const bool center_changed = !nearlyEqualVec3(center, player_center_);
  const bool shape_changed = shape_kind != player_shape_kind_;
  if (size_changed || shape_changed) {
    if (player_native_handle_ != 0) {
      physics_entities_by_handle_.erase(player_native_handle_);
    }
    const glm::quat current_rot = controller->getRotation();
    const glm::vec3 current_vel = controller->getVelocity();
    const glm::vec3 current_ang_vel = controller->getAngularVelocity();
    controller->destroy();
    auto& new_controller = physics_.createPlayer(half_extents * 2.0f);
    controller = &new_controller;
    controller->setCenter(math::toGlm(center));
    controller->setPosition(math::toGlm(transform.getPosition()));
    controller->setRotation(current_rot);
    controller->setVelocity(current_vel);
    controller->setAngularVelocity(current_ang_vel);
    player_native_handle_ = controller->nativeHandle();
    if (player_native_handle_ != 0) {
      physics_entities_by_handle_[player_native_handle_] = player_entity_;
    }
    player_half_extents_ = current_half_extents;
    player_center_ = center;
    player_shape_kind_ = shape_kind;
  }

  if (center_changed && !size_changed && !shape_changed) {
    controller->setCenter(math::toGlm(center));
    controller->setPosition(math::toGlm(transform.getPosition()));
    player_center_ = center;
  }

  const math::Vec3 desired = input.desiredVelocity();
  const math::Vec3 impulse = input.addVelocity();
  glm::vec3 velocity = math::toGlm(desired) + math::toGlm(impulse);
  controller->setVelocity(velocity);
  input.clearImpulse();

  transform.setPositionFromPhysics(
      math::fromGlm(controller->getPosition()));
  transform.setRotationFromPhysics({controller->getRotation().x, controller->getRotation().y,
                                    controller->getRotation().z, controller->getRotation().w});
}

void PhysicsSystem::cleanupStale(ecs::World& world) {
  for (auto it = constraints_.begin(); it != constraints_.end();) {
    const ecs::Entity entity = entityFromKey(it->first);
    bool remove = !world.isAlive(entity) ||
                  !world.has<components::PhysicsConstraintComponent>(entity);
    if (!remove) {
      const auto& component = world.get<components::PhysicsConstraintComponent>(entity);
      remove = !world.isAlive(component.body_a) || !world.isAlive(component.body_b) ||
               !hasPhysicsCollider(world, component.body_a) ||
               !hasPhysicsCollider(world, component.body_b);
    }
    if (remove) {
      it->second.destroy();
      constraint_signatures_.erase(it->first);
      it = constraints_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = rigid_bodies_.begin(); it != rigid_bodies_.end();) {
    ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        !world.has<components::RigidbodyComponent>(entity) ||
        !hasPhysicsCollider(world, entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      it = rigid_bodies_.erase(it);
      body_state_.erase(entityKey(entity));
    } else {
      ++it;
    }
  }

  for (auto it = static_bodies_.begin(); it != static_bodies_.end();) {
    ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        world.has<components::RigidbodyComponent>(entity) ||
        !hasPhysicsCollider(world, entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      it = static_bodies_.erase(it);
      body_state_.erase(entityKey(entity));
    } else {
      ++it;
    }
  }

  if (has_player_ &&
      (!world.isAlive(player_entity_) ||
       !world.has<components::PlayerControllerComponent>(player_entity_))) {
    if (physics_.playerController()) {
      physics_.playerController()->destroy();
    }
    if (player_native_handle_ != 0) {
      physics_entities_by_handle_.erase(player_native_handle_);
      player_native_handle_ = 0;
    }
    has_player_ = false;
  }

  for (auto it = previous_contacts_.begin(); it != previous_contacts_.end();) {
    const ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) || !world.has<components::ContactListenerComponent>(entity)) {
      it = previous_contacts_.erase(it);
    } else {
      ++it;
    }
  }
}

void PhysicsSystem::syncGroundContacts(ecs::World& world) {
  constexpr float kGroundNormalThreshold = 0.7f;
  constexpr float kGroundProbeInset = 0.02f;
  constexpr float kGroundProbeDistance = 0.2f;

  auto apply_ground_state = [&](ecs::Entity entity,
                                const PhysicsGroundContact* hit,
                                ecs::Entity support_entity) {
    auto& contact = world.get<components::GroundContactComponent>(entity);
    const bool was_grounded = contact.grounded;
    contact.clearTransient();
    contact.grounded = hit != nullptr && hit->grounded;
    contact.entered = contact.grounded && !was_grounded;
    contact.exited = !contact.grounded && was_grounded;
    contact.has_support = hit != nullptr && hit->grounded;
    contact.support_entity = support_entity;
    contact.point = hit != nullptr ? math::fromGlm(hit->point) : math::Vec3{};
    contact.normal = hit != nullptr ? math::fromGlm(hit->normal) : math::Vec3{0.0f, 1.0f, 0.0f};
  };

  world.forEach<components::GroundContactComponent>([&](const ecs::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    if (world.has<components::PlayerControllerComponent>(entity)) {
      PhysicsGroundContact hit{};
      const bool grounded = has_player_ && entity == player_entity_ &&
                            physics_.playerController() != nullptr &&
                            physics_.playerController()->getGroundContact(hit);
      ecs::Entity support_entity{};
      if (grounded) {
        auto support_it = physics_entities_by_handle_.find(hit.support_handle);
        if (support_it != physics_entities_by_handle_.end()) {
          support_entity = support_it->second;
        }
      }
      apply_ground_state(entity, grounded ? &hit : nullptr, support_entity);
      return;
    }

    if (!world.has<components::RigidbodyComponent>(entity) ||
        !hasPhysicsCollider(world, entity) ||
        !world.has<components::TransformComponent>(entity)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it == rigid_bodies_.end() || !body_it->second.isValid()) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    const auto& transform = world.get<components::TransformComponent>(entity);
    const glm::vec3 full_dimensions = groundProbeDimensions(world, entity, transform);
    if (!body_it->second.isGrounded(full_dimensions)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    PhysicsGroundContact hit{};
    bool resolved_support = false;
    const std::uintptr_t self_handle = body_it->second.nativeHandle();
    const glm::vec3 body_position = body_it->second.getPosition();
    const float half_height = full_dimensions.y * 0.5f;

    const glm::vec3 probe_from = body_position + glm::vec3(0.0f, half_height - kGroundProbeInset, 0.0f);
    const glm::vec3 probe_to = body_position - glm::vec3(0.0f, half_height + kGroundProbeDistance, 0.0f);

    if (physics_.raycastDetailed(probe_from, probe_to, hit)) {
      if (hit.support_handle == self_handle) {
        const glm::vec3 retry_from = hit.point - glm::vec3(0.0f, kGroundProbeInset * 2.0f, 0.0f);
        const glm::vec3 retry_to = retry_from - glm::vec3(0.0f, kGroundProbeDistance, 0.0f);
        PhysicsGroundContact retry_hit{};
        if (physics_.raycastDetailed(retry_from, retry_to, retry_hit)) {
          hit = retry_hit;
        }
      }

      resolved_support = hit.support_handle != 0 &&
                         hit.support_handle != self_handle &&
                         hit.normal.y > kGroundNormalThreshold;
    }

    ecs::Entity support_entity{};
    if (resolved_support) {
      auto support_it = physics_entities_by_handle_.find(hit.support_handle);
      if (support_it != physics_entities_by_handle_.end() && support_it->second != entity) {
        support_entity = support_it->second;
      } else {
        resolved_support = false;
      }
    }

    if (resolved_support) {
      apply_ground_state(entity, &hit, support_entity);
      return;
    }

    PhysicsGroundContact grounded_hit{};
    grounded_hit.grounded = true;
    apply_ground_state(entity, &grounded_hit, {});
  });
}

}  // namespace karma::physics
