#include "karma/ecs/collider_queries.h"

#include <algorithm>
#include <cmath>

#include "karma/components/collider.h"
#include "karma/components/transform.h"
#include "karma/components/visibility.h"
#include "karma/math/quat.h"
#include "karma/math/vec3.h"

namespace karma::ecs::queries {

namespace {

constexpr float kContainmentEpsilon = 1e-5f;

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 subVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

math::Vec3 scaleVec3(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

math::Vec3 scaledLocalPoint(const math::Vec3& point, const math::Vec3& scale) {
  return {point.x * scale.x, point.y * scale.y, point.z * scale.z};
}

float maxAbs3(const math::Vec3& v) {
  return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
}

math::Vec3 worldPointFromLocal(const components::TransformComponent& transform,
                               const math::Vec3& local_point) {
  return addVec3(transform.getPosition(),
                 math::rotateVec(transform.getRotation(),
                                 scaledLocalPoint(local_point, transform.getScale())));
}

bool matchesCollisionLayerMask(const ecs::World& world,
                               ecs::Entity entity,
                               const PointContainmentFilter& filter) {
  if (!world.has<components::VisibilityComponent>(entity)) {
    return true;
  }
  const auto& visibility = world.get<components::VisibilityComponent>(entity);
  return (visibility.collision_layer_mask & filter.collision_layer_mask) != 0u;
}

bool containsPointBox(const components::TransformComponent& transform,
                      const components::BoxColliderComponent& collider,
                      const math::Vec3& world_point) {
  const math::Vec3 center = worldPointFromLocal(transform, collider.center);
  const math::Quat rotation = transform.getRotation();
  const math::Vec3 rel = subVec3(world_point, center);
  const math::Vec3 right = math::rotateVec(rotation, {1.0f, 0.0f, 0.0f});
  const math::Vec3 up = math::rotateVec(rotation, {0.0f, 1.0f, 0.0f});
  const math::Vec3 forward = math::rotateVec(rotation, {0.0f, 0.0f, 1.0f});
  const math::Vec3 scale = transform.getScale();
  const math::Vec3 extents{
      std::abs(scale.x * collider.half_extents.x),
      std::abs(scale.y * collider.half_extents.y),
      std::abs(scale.z * collider.half_extents.z),
  };
  return std::abs(math::dot(rel, right)) <= extents.x + kContainmentEpsilon &&
         std::abs(math::dot(rel, up)) <= extents.y + kContainmentEpsilon &&
         std::abs(math::dot(rel, forward)) <= extents.z + kContainmentEpsilon;
}

bool containsPointSphere(const components::TransformComponent& transform,
                         const components::SphereColliderComponent& collider,
                         const math::Vec3& world_point) {
  const math::Vec3 center = worldPointFromLocal(transform, collider.center);
  const float radius = collider.radius * maxAbs3(transform.getScale());
  const math::Vec3 delta = subVec3(world_point, center);
  return math::lengthSquared(delta) <= radius * radius + kContainmentEpsilon;
}

bool containsPointCapsule(const components::TransformComponent& transform,
                          const components::CapsuleColliderComponent& collider,
                          const math::Vec3& world_point) {
  const math::Vec3 center = worldPointFromLocal(transform, collider.center);
  const math::Quat rotation = transform.getRotation();
  const math::Vec3 axis = math::normalize(math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}));
  const math::Vec3 scale = transform.getScale();
  const float half_height = std::abs(scale.y) * collider.height * 0.5f;
  const float radius = std::max(std::abs(scale.x), std::abs(scale.z)) * collider.radius;
  const math::Vec3 a = addVec3(center, scaleVec3(axis, half_height));
  const math::Vec3 b = subVec3(center, scaleVec3(axis, half_height));
  const math::Vec3 ab = subVec3(b, a);
  const float ab_len_sq = math::lengthSquared(ab);
  float t = 0.0f;
  if (ab_len_sq > kContainmentEpsilon) {
    t = std::clamp(math::dot(subVec3(world_point, a), ab) / ab_len_sq, 0.0f, 1.0f);
  }
  const math::Vec3 closest = addVec3(a, scaleVec3(ab, t));
  const math::Vec3 delta = subVec3(world_point, closest);
  return math::lengthSquared(delta) <= radius * radius + kContainmentEpsilon;
}

}  // namespace

bool containsPoint(const ecs::World& world, ecs::Entity entity, const math::Vec3& world_point) {
  if (!world.isAlive(entity) || !world.has<components::TransformComponent>(entity)) {
    return false;
  }

  const auto& transform = world.get<components::TransformComponent>(entity);
  if (world.has<components::BoxColliderComponent>(entity) &&
      containsPointBox(transform, world.get<components::BoxColliderComponent>(entity), world_point)) {
    return true;
  }
  if (world.has<components::SphereColliderComponent>(entity) &&
      containsPointSphere(transform, world.get<components::SphereColliderComponent>(entity), world_point)) {
    return true;
  }
  if (world.has<components::CapsuleColliderComponent>(entity) &&
      containsPointCapsule(transform, world.get<components::CapsuleColliderComponent>(entity), world_point)) {
    return true;
  }
  return false;
}

std::optional<PointContainmentHit> findContainingCollider(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter) {
  std::optional<PointContainmentHit> hit;

  world.forEach<components::BoxColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!matchesCollisionLayerMask(world, entity, filter)) {
      return true;
    }
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    if (filter.only_triggers && !collider.is_trigger) {
      return true;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (!containsPointBox(transform, collider, world_point)) {
      return true;
    }
    hit = PointContainmentHit{.entity = entity, .shape = ColliderShape::Box};
    return false;
  });
  if (hit) {
    return hit;
  }

  world.forEach<components::SphereColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!matchesCollisionLayerMask(world, entity, filter)) {
      return true;
    }
    const auto& collider = world.get<components::SphereColliderComponent>(entity);
    if (filter.only_triggers && !collider.is_trigger) {
      return true;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (!containsPointSphere(transform, collider, world_point)) {
      return true;
    }
    hit = PointContainmentHit{.entity = entity, .shape = ColliderShape::Sphere};
    return false;
  });
  if (hit) {
    return hit;
  }

  world.forEach<components::CapsuleColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!matchesCollisionLayerMask(world, entity, filter)) {
      return true;
    }
    const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
    if (filter.only_triggers && !collider.is_trigger) {
      return true;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (!containsPointCapsule(transform, collider, world_point)) {
      return true;
    }
    hit = PointContainmentHit{.entity = entity, .shape = ColliderShape::Capsule};
    return false;
  });

  return hit;
}

std::vector<PointContainmentHit> findContainingColliders(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter) {
  std::vector<PointContainmentHit> hits;

  world.forEach<components::BoxColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!matchesCollisionLayerMask(world, entity, filter)) {
      return;
    }
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    if (filter.only_triggers && !collider.is_trigger) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (containsPointBox(transform, collider, world_point)) {
      hits.push_back(PointContainmentHit{.entity = entity, .shape = ColliderShape::Box});
    }
  });

  world.forEach<components::SphereColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!matchesCollisionLayerMask(world, entity, filter)) {
      return;
    }
    const auto& collider = world.get<components::SphereColliderComponent>(entity);
    if (filter.only_triggers && !collider.is_trigger) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (containsPointSphere(transform, collider, world_point)) {
      hits.push_back(PointContainmentHit{.entity = entity, .shape = ColliderShape::Sphere});
    }
  });

  world.forEach<components::CapsuleColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (!matchesCollisionLayerMask(world, entity, filter)) {
      return;
    }
    const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
    if (filter.only_triggers && !collider.is_trigger) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (containsPointCapsule(transform, collider, world_point)) {
      hits.push_back(PointContainmentHit{.entity = entity, .shape = ColliderShape::Capsule});
    }
  });

  return hits;
}

}  // namespace karma::ecs::queries
