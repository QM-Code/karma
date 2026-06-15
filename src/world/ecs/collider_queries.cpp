#include "karma/world/ecs/collider_queries.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "karma/world/components/collider.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"

namespace karma::ecs::queries {

namespace {

constexpr float kContainmentEpsilon = 1e-5f;

struct OrientedBox {
  math::Vec3 center{};
  std::array<math::Vec3, 3> axes{};
  math::Vec3 extents{};
};

struct WorldSphere {
  math::Vec3 center{};
  float radius = 0.0f;
};

struct WorldCapsule {
  math::Vec3 a{};
  math::Vec3 b{};
  float radius = 0.0f;
};

math::Vec3 scaledLocalPoint(const math::Vec3& point, const math::Vec3& scale) {
  return math::multiply(point, scale);
}

float maxAbs3(const math::Vec3& v) {
  return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
}

float component(const math::Vec3& v, int axis) {
  switch (axis) {
    case 0:
      return v.x;
    case 1:
      return v.y;
    default:
      return v.z;
  }
}

math::Vec3 worldPointFromLocal(const components::TransformComponent& transform,
                               const math::Vec3& local_point) {
  return math::add(transform.getPosition(),
                   math::rotateVec(transform.getRotation(),
                                   scaledLocalPoint(local_point, transform.getScale())));
}

bool matchesCollisionLayerMask(const ecs::World& world,
                               ecs::Entity entity,
                               uint32_t collision_layer_mask) {
  if (!world.has<components::VisibilityComponent>(entity)) {
    return true;
  }
  const auto& visibility = world.get<components::VisibilityComponent>(entity);
  return (visibility.collision_layer_mask & collision_layer_mask) != 0u;
}

std::optional<ColliderShape> colliderShapeForEntity(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return ColliderShape::Box;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return ColliderShape::Sphere;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return ColliderShape::Capsule;
  }
  if (world.has<components::MeshColliderComponent>(entity)) {
    return ColliderShape::Mesh;
  }
  return std::nullopt;
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
  if (world.has<components::MeshColliderComponent>(entity)) {
    return world.get<components::MeshColliderComponent>(entity).is_trigger;
  }
  return false;
}

OrientedBox makeWorldBox(const components::TransformComponent& transform,
                         const components::BoxColliderComponent& collider) {
  const math::Quat rotation = transform.getRotation();
  const math::Vec3 scale = transform.getScale();
  return OrientedBox{
      .center = worldPointFromLocal(transform, collider.center),
      .axes = {
          math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}),
          math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}),
          math::rotateVec(rotation, {0.0f, 0.0f, 1.0f}),
      },
      .extents = {
          std::abs(scale.x * collider.half_extents.x),
          std::abs(scale.y * collider.half_extents.y),
          std::abs(scale.z * collider.half_extents.z),
      }};
}

WorldSphere makeWorldSphere(const components::TransformComponent& transform,
                            const components::SphereColliderComponent& collider) {
  return WorldSphere{
      .center = worldPointFromLocal(transform, collider.center),
      .radius = collider.radius * maxAbs3(transform.getScale()),
  };
}

WorldCapsule makeWorldCapsule(const components::TransformComponent& transform,
                              const components::CapsuleColliderComponent& collider) {
  const math::Vec3 center = worldPointFromLocal(transform, collider.center);
  const math::Vec3 scale = transform.getScale();
  const math::Vec3 axis =
      math::normalize(math::rotateVec(transform.getRotation(), {0.0f, 1.0f, 0.0f}));
  const float half_height = std::abs(scale.y) * collider.height * 0.5f;
  const float radius = std::max(std::abs(scale.x), std::abs(scale.z)) * collider.radius;
  return WorldCapsule{
      .a = math::add(center, math::scale(axis, half_height)),
      .b = math::subtract(center, math::scale(axis, half_height)),
      .radius = radius,
  };
}

math::Vec3 pointInBoxLocalSpace(const OrientedBox& box, const math::Vec3& point) {
  const math::Vec3 rel = math::subtract(point, box.center);
  return {
      math::dot(rel, box.axes[0]),
      math::dot(rel, box.axes[1]),
      math::dot(rel, box.axes[2]),
  };
}

float pointAabbDistanceSquared(const math::Vec3& point, const math::Vec3& extents) {
  float distance_sq = 0.0f;
  for (int axis = 0; axis < 3; ++axis) {
    const float p = component(point, axis);
    const float e = component(extents, axis);
    const float excess = std::max(std::abs(p) - e, 0.0f);
    distance_sq += excess * excess;
  }
  return distance_sq;
}

float segmentAabbDistanceSquared(const math::Vec3& a,
                                 const math::Vec3& b,
                                 const math::Vec3& extents) {
  const math::Vec3 d = math::subtract(b, a);
  std::array<float, 8> t_values{};
  size_t count = 0;
  t_values[count++] = 0.0f;
  t_values[count++] = 1.0f;

  for (int axis = 0; axis < 3; ++axis) {
    const float da = component(d, axis);
    if (std::abs(da) <= kContainmentEpsilon) {
      continue;
    }
    const float pa = component(a, axis);
    const float e = component(extents, axis);
    const float t0 = (-e - pa) / da;
    const float t1 = (e - pa) / da;
    if (t0 > 0.0f && t0 < 1.0f) {
      t_values[count++] = t0;
    }
    if (t1 > 0.0f && t1 < 1.0f) {
      t_values[count++] = t1;
    }
  }

  std::sort(t_values.begin(), t_values.begin() + static_cast<std::ptrdiff_t>(count));
  size_t unique_count = 0;
  for (size_t i = 0; i < count; ++i) {
    if (unique_count == 0 ||
        std::abs(t_values[i] - t_values[unique_count - 1]) > kContainmentEpsilon) {
      t_values[unique_count++] = t_values[i];
    }
  }

  auto evaluate = [&](float t) {
    const math::Vec3 point = math::add(a, math::scale(d, t));
    return pointAabbDistanceSquared(point, extents);
  };

  float best = std::numeric_limits<float>::max();
  for (size_t i = 0; i + 1 < unique_count; ++i) {
    const float start = t_values[i];
    const float end = t_values[i + 1];
    best = std::min(best, evaluate(start));
    best = std::min(best, evaluate(end));
    if (end - start <= kContainmentEpsilon) {
      continue;
    }

    const float mid = 0.5f * (start + end);
    float coeff_a = 0.0f;
    float coeff_b = 0.0f;
    float coeff_c = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
      const float mid_value = component(a, axis) + component(d, axis) * mid;
      const float slope = component(d, axis);
      const float e = component(extents, axis);
      float offset = 0.0f;
      if (mid_value < -e) {
        offset = component(a, axis) + e;
      } else if (mid_value > e) {
        offset = component(a, axis) - e;
      } else {
        continue;
      }
      coeff_a += slope * slope;
      coeff_b += 2.0f * slope * offset;
      coeff_c += offset * offset;
    }

    if (coeff_a > kContainmentEpsilon) {
      const float candidate = math::clamp(-coeff_b / (2.0f * coeff_a), start, end);
      best = std::min(best, coeff_a * candidate * candidate + coeff_b * candidate + coeff_c);
    }
  }

  return best;
}

float pointSegmentDistanceSquared(const math::Vec3& point,
                                  const math::Vec3& a,
                                  const math::Vec3& b) {
  const math::Vec3 ab = math::subtract(b, a);
  const float ab_len_sq = math::lengthSquared(ab);
  float t = 0.0f;
  if (ab_len_sq > kContainmentEpsilon) {
    t = math::clamp01(math::dot(math::subtract(point, a), ab) / ab_len_sq);
  }
  const math::Vec3 closest = math::add(a, math::scale(ab, t));
  return math::lengthSquared(math::subtract(point, closest));
}

float segmentSegmentDistanceSquared(const math::Vec3& p1,
                                    const math::Vec3& q1,
                                    const math::Vec3& p2,
                                    const math::Vec3& q2) {
  const math::Vec3 d1 = math::subtract(q1, p1);
  const math::Vec3 d2 = math::subtract(q2, p2);
  const math::Vec3 r = math::subtract(p1, p2);
  const float a = math::dot(d1, d1);
  const float e = math::dot(d2, d2);
  const float f = math::dot(d2, r);

  float s = 0.0f;
  float t = 0.0f;

  if (a <= kContainmentEpsilon && e <= kContainmentEpsilon) {
    return math::lengthSquared(math::subtract(p1, p2));
  }

  if (a <= kContainmentEpsilon) {
    t = math::clamp01(f / e);
  } else {
    const float c = math::dot(d1, r);
    if (e <= kContainmentEpsilon) {
      s = math::clamp01(-c / a);
    } else {
      const float b_dot = math::dot(d1, d2);
      const float denom = a * e - b_dot * b_dot;
      if (std::abs(denom) > kContainmentEpsilon) {
        s = math::clamp01((b_dot * f - c * e) / denom);
      }
      t = (b_dot * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f;
        s = math::clamp01(-c / a);
      } else if (t > 1.0f) {
        t = 1.0f;
        s = math::clamp01((b_dot - c) / a);
      }
    }
  }

  const math::Vec3 c1 = math::add(p1, math::scale(d1, s));
  const math::Vec3 c2 = math::add(p2, math::scale(d2, t));
  return math::lengthSquared(math::subtract(c1, c2));
}

bool overlapsSphereSphere(const WorldSphere& a, const WorldSphere& b) {
  const float radius = a.radius + b.radius;
  return math::lengthSquared(math::subtract(a.center, b.center)) <= radius * radius + kContainmentEpsilon;
}

bool overlapsSphereBox(const WorldSphere& sphere, const OrientedBox& box) {
  const math::Vec3 local_center = pointInBoxLocalSpace(box, sphere.center);
  return pointAabbDistanceSquared(local_center, box.extents) <=
         sphere.radius * sphere.radius + kContainmentEpsilon;
}

bool overlapsSphereCapsule(const WorldSphere& sphere, const WorldCapsule& capsule) {
  const float radius = sphere.radius + capsule.radius;
  return pointSegmentDistanceSquared(sphere.center, capsule.a, capsule.b) <=
         radius * radius + kContainmentEpsilon;
}

bool overlapsCapsuleCapsule(const WorldCapsule& a, const WorldCapsule& b) {
  const float radius = a.radius + b.radius;
  return segmentSegmentDistanceSquared(a.a, a.b, b.a, b.b) <=
         radius * radius + kContainmentEpsilon;
}

bool overlapsBoxCapsule(const OrientedBox& box, const WorldCapsule& capsule) {
  const math::Vec3 local_a = pointInBoxLocalSpace(box, capsule.a);
  const math::Vec3 local_b = pointInBoxLocalSpace(box, capsule.b);
  return segmentAabbDistanceSquared(local_a, local_b, box.extents) <=
         capsule.radius * capsule.radius + kContainmentEpsilon;
}

bool overlapsBoxBox(const OrientedBox& a, const OrientedBox& b) {
  const float a_ext[3] = {a.extents.x, a.extents.y, a.extents.z};
  const float b_ext[3] = {b.extents.x, b.extents.y, b.extents.z};
  float r[3][3];
  float abs_r[3][3];
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      r[i][j] = math::dot(a.axes[i], b.axes[j]);
      abs_r[i][j] = std::abs(r[i][j]) + kContainmentEpsilon;
    }
  }

  const math::Vec3 t_world = math::subtract(b.center, a.center);
  const float t[3] = {
      math::dot(t_world, a.axes[0]),
      math::dot(t_world, a.axes[1]),
      math::dot(t_world, a.axes[2]),
  };

  for (int i = 0; i < 3; ++i) {
    const float ra = a_ext[i];
    const float rb = b_ext[0] * abs_r[i][0] + b_ext[1] * abs_r[i][1] + b_ext[2] * abs_r[i][2];
    if (std::abs(t[i]) > ra + rb) {
      return false;
    }
  }

  for (int j = 0; j < 3; ++j) {
    const float ra = a_ext[0] * abs_r[0][j] + a_ext[1] * abs_r[1][j] + a_ext[2] * abs_r[2][j];
    const float rb = b_ext[j];
    const float distance = std::abs(t[0] * r[0][j] + t[1] * r[1][j] + t[2] * r[2][j]);
    if (distance > ra + rb) {
      return false;
    }
  }

  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const float ra =
          a_ext[(i + 1) % 3] * abs_r[(i + 2) % 3][j] +
          a_ext[(i + 2) % 3] * abs_r[(i + 1) % 3][j];
      const float rb =
          b_ext[(j + 1) % 3] * abs_r[i][(j + 2) % 3] +
          b_ext[(j + 2) % 3] * abs_r[i][(j + 1) % 3];
      const float distance = std::abs(
          t[(i + 2) % 3] * r[(i + 1) % 3][j] -
          t[(i + 1) % 3] * r[(i + 2) % 3][j]);
      if (distance > ra + rb) {
        return false;
      }
    }
  }

  return true;
}

bool containsPointBox(const components::TransformComponent& transform,
                      const components::BoxColliderComponent& collider,
                      const math::Vec3& world_point) {
  const math::Vec3 center = worldPointFromLocal(transform, collider.center);
  const math::Quat rotation = transform.getRotation();
  const math::Vec3 rel = math::subtract(world_point, center);
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
  const math::Vec3 delta = math::subtract(world_point, center);
  return math::lengthSquared(delta) <= radius * radius + kContainmentEpsilon;
}

bool containsPointCapsule(const components::TransformComponent& transform,
                          const components::CapsuleColliderComponent& collider,
                          const math::Vec3& world_point) {
  const WorldCapsule capsule = makeWorldCapsule(transform, collider);
  return pointSegmentDistanceSquared(world_point, capsule.a, capsule.b) <=
         capsule.radius * capsule.radius + kContainmentEpsilon;
}

bool overlapsColliderPair(const ecs::World& world, ecs::Entity a_entity, ecs::Entity b_entity) {
  if (!world.isAlive(a_entity) || !world.isAlive(b_entity) || a_entity == b_entity ||
      !world.has<components::TransformComponent>(a_entity) ||
      !world.has<components::TransformComponent>(b_entity)) {
    return false;
  }

  const auto a_shape = colliderShapeForEntity(world, a_entity);
  const auto b_shape = colliderShapeForEntity(world, b_entity);
  if (!a_shape || !b_shape || *a_shape == ColliderShape::Mesh || *b_shape == ColliderShape::Mesh) {
    return false;
  }

  const auto& a_transform = world.get<components::TransformComponent>(a_entity);
  const auto& b_transform = world.get<components::TransformComponent>(b_entity);

  if (*a_shape == ColliderShape::Sphere && *b_shape == ColliderShape::Sphere) {
    return overlapsSphereSphere(
        makeWorldSphere(a_transform, world.get<components::SphereColliderComponent>(a_entity)),
        makeWorldSphere(b_transform, world.get<components::SphereColliderComponent>(b_entity)));
  }

  if (*a_shape == ColliderShape::Sphere && *b_shape == ColliderShape::Box) {
    return overlapsSphereBox(
        makeWorldSphere(a_transform, world.get<components::SphereColliderComponent>(a_entity)),
        makeWorldBox(b_transform, world.get<components::BoxColliderComponent>(b_entity)));
  }
  if (*a_shape == ColliderShape::Box && *b_shape == ColliderShape::Sphere) {
    return overlapsSphereBox(
        makeWorldSphere(b_transform, world.get<components::SphereColliderComponent>(b_entity)),
        makeWorldBox(a_transform, world.get<components::BoxColliderComponent>(a_entity)));
  }

  if (*a_shape == ColliderShape::Sphere && *b_shape == ColliderShape::Capsule) {
    return overlapsSphereCapsule(
        makeWorldSphere(a_transform, world.get<components::SphereColliderComponent>(a_entity)),
        makeWorldCapsule(b_transform, world.get<components::CapsuleColliderComponent>(b_entity)));
  }
  if (*a_shape == ColliderShape::Capsule && *b_shape == ColliderShape::Sphere) {
    return overlapsSphereCapsule(
        makeWorldSphere(b_transform, world.get<components::SphereColliderComponent>(b_entity)),
        makeWorldCapsule(a_transform, world.get<components::CapsuleColliderComponent>(a_entity)));
  }

  if (*a_shape == ColliderShape::Capsule && *b_shape == ColliderShape::Capsule) {
    return overlapsCapsuleCapsule(
        makeWorldCapsule(a_transform, world.get<components::CapsuleColliderComponent>(a_entity)),
        makeWorldCapsule(b_transform, world.get<components::CapsuleColliderComponent>(b_entity)));
  }

  if (*a_shape == ColliderShape::Box && *b_shape == ColliderShape::Box) {
    return overlapsBoxBox(
        makeWorldBox(a_transform, world.get<components::BoxColliderComponent>(a_entity)),
        makeWorldBox(b_transform, world.get<components::BoxColliderComponent>(b_entity)));
  }

  if (*a_shape == ColliderShape::Box && *b_shape == ColliderShape::Capsule) {
    return overlapsBoxCapsule(
        makeWorldBox(a_transform, world.get<components::BoxColliderComponent>(a_entity)),
        makeWorldCapsule(b_transform, world.get<components::CapsuleColliderComponent>(b_entity)));
  }
  if (*a_shape == ColliderShape::Capsule && *b_shape == ColliderShape::Box) {
    return overlapsBoxCapsule(
        makeWorldBox(b_transform, world.get<components::BoxColliderComponent>(b_entity)),
        makeWorldCapsule(a_transform, world.get<components::CapsuleColliderComponent>(a_entity)));
  }

  return false;
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
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
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
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
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
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
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
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
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
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
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
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
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

bool overlaps(const ecs::World& world, ecs::Entity a, ecs::Entity b) {
  return overlapsColliderPair(world, a, b);
}

std::optional<OverlapHit> findOverlappingCollider(
    const ecs::World& world,
    ecs::Entity query_entity,
    const OverlapFilter& filter) {
  if (!world.isAlive(query_entity) || !colliderShapeForEntity(world, query_entity)) {
    return std::nullopt;
  }

  for (const ecs::Entity entity : world.entities()) {
    if (!world.isAlive(entity)) {
      continue;
    }
    if (filter.skip_self && entity == query_entity) {
      continue;
    }
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
      continue;
    }
    if (filter.only_triggers && !colliderIsTrigger(world, entity)) {
      continue;
    }
    if (!overlapsColliderPair(world, query_entity, entity)) {
      continue;
    }
    return OverlapHit{.entity = entity,
                      .shape = colliderShapeForEntity(world, entity).value_or(ColliderShape::Box)};
  }

  return std::nullopt;
}

std::vector<OverlapHit> findOverlappingColliders(
    const ecs::World& world,
    ecs::Entity query_entity,
    const OverlapFilter& filter) {
  std::vector<OverlapHit> hits;
  if (!world.isAlive(query_entity) || !colliderShapeForEntity(world, query_entity)) {
    return hits;
  }

  for (const ecs::Entity entity : world.entities()) {
    if (!world.isAlive(entity)) {
      continue;
    }
    if (filter.skip_self && entity == query_entity) {
      continue;
    }
    if (!matchesCollisionLayerMask(world, entity, filter.collision_layer_mask)) {
      continue;
    }
    if (filter.only_triggers && !colliderIsTrigger(world, entity)) {
      continue;
    }
    if (!overlapsColliderPair(world, query_entity, entity)) {
      continue;
    }
    hits.push_back(
        OverlapHit{.entity = entity,
                   .shape = colliderShapeForEntity(world, entity).value_or(ColliderShape::Box)});
  }

  return hits;
}

}  // namespace karma::ecs::queries
