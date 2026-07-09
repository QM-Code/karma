#include "karma/components.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

#include "karma/math.h"

namespace karma::world::queries {
namespace {

constexpr float kGeometryEpsilon = 1.0e-6f;
constexpr float kLengthSquaredEpsilon = 1.0e-12f;
constexpr float kContainmentTolerance = 1.0e-5f;

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

using WorldShape = std::variant<OrientedBox, WorldSphere, WorldCapsule>;

float maxAbs3(const math::Vec3& value) {
  return std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
}

float maxAbsXZ(const math::Vec3& value) {
  return std::max(std::abs(value.x), std::abs(value.z));
}

float component(const math::Vec3& value, int axis) {
  switch (axis) {
    case 0:
      return value.x;
    case 1:
      return value.y;
    default:
      return value.z;
  }
}

bool isFiniteTransform(const components::TransformComponent& transform) {
  return math::isFinite(transform.getPosition()) &&
         math::isFinite(transform.getRotation()) &&
         math::isFinite(transform.getScale());
}

math::Vec3 worldPointFromLocal(const components::TransformComponent& transform,
                               const math::Quat& rotation,
                               const math::Vec3& local_point) {
  return math::add(transform.getPosition(),
                   math::rotateVec(rotation,
                                   math::multiply(local_point, transform.getScale())));
}

std::optional<WorldShape> makeWorldShape(
    const components::TransformComponent& transform,
    const components::ColliderComponent& collider) {
  if (!isFiniteTransform(transform)) {
    return std::nullopt;
  }

  const math::Quat rotation = math::normalize(transform.getRotation());
  const math::Vec3 scale = transform.getScale();

  if (const auto* box = std::get_if<components::BoxColliderShape>(&collider.shape)) {
    if (!math::isFinite(box->center) || !math::isFinite(box->half_extents)) {
      return std::nullopt;
    }
    OrientedBox result{
        .center = worldPointFromLocal(transform, rotation, box->center),
        .axes = {
            math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}),
            math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}),
            math::rotateVec(rotation, {0.0f, 0.0f, 1.0f}),
        },
        .extents = {
            std::abs(scale.x * box->half_extents.x),
            std::abs(scale.y * box->half_extents.y),
            std::abs(scale.z * box->half_extents.z),
        },
    };
    if (!math::isFinite(result.center) || !math::isFinite(result.extents)) {
      return std::nullopt;
    }
    return result;
  }

  if (const auto* sphere = std::get_if<components::SphereColliderShape>(&collider.shape)) {
    if (!math::isFinite(sphere->center) || !math::isFinite(sphere->radius)) {
      return std::nullopt;
    }
    WorldSphere result{
        .center = worldPointFromLocal(transform, rotation, sphere->center),
        .radius = std::abs(sphere->radius) * maxAbs3(scale),
    };
    if (!math::isFinite(result.center) || !math::isFinite(result.radius)) {
      return std::nullopt;
    }
    return result;
  }

  if (const auto* capsule = std::get_if<components::CapsuleColliderShape>(&collider.shape)) {
    if (!math::isFinite(capsule->center) || !math::isFinite(capsule->radius) ||
        !math::isFinite(capsule->height)) {
      return std::nullopt;
    }
    const math::Vec3 center =
        worldPointFromLocal(transform, rotation, capsule->center);
    const math::Vec3 axis = math::rotateVec(rotation, {0.0f, 1.0f, 0.0f});
    const float radius = std::abs(capsule->radius) * maxAbsXZ(scale);
    const float total_height = std::abs(capsule->height * scale.y);
    const float half_segment = std::max(total_height * 0.5f - radius, 0.0f);
    WorldCapsule result{
        .a = math::add(center, math::scale(axis, half_segment)),
        .b = math::subtract(center, math::scale(axis, half_segment)),
        .radius = radius,
    };
    if (!math::isFinite(result.a) || !math::isFinite(result.b) ||
        !math::isFinite(result.radius)) {
      return std::nullopt;
    }
    return result;
  }

  return std::nullopt;
}

std::optional<WorldShape> worldShapeForEntity(const world::World& world,
                                              world::Entity entity) {
  const auto* transform = world.tryGet<components::TransformComponent>(entity);
  const auto* collider = world.tryGet<components::ColliderComponent>(entity);
  if (transform == nullptr || collider == nullptr) {
    return std::nullopt;
  }
  return makeWorldShape(*transform, *collider);
}

bool matchesCollisionLayerMask(const world::World& world,
                               world::Entity entity,
                               uint32_t collision_layer_mask) {
  const auto* visibility = world.tryGet<components::VisibilityComponent>(entity);
  const uint32_t entity_mask = visibility == nullptr
                                   ? std::numeric_limits<uint32_t>::max()
                                   : visibility->collision_layer_mask;
  return (entity_mask & collision_layer_mask) != 0u;
}

math::Vec3 pointInBoxLocalSpace(const OrientedBox& box,
                                const math::Vec3& point) {
  const math::Vec3 relative = math::subtract(point, box.center);
  return {
      math::dot(relative, box.axes[0]),
      math::dot(relative, box.axes[1]),
      math::dot(relative, box.axes[2]),
  };
}

float pointAabbDistanceSquared(const math::Vec3& point,
                               const math::Vec3& extents) {
  float distance_squared = 0.0f;
  for (int axis = 0; axis < 3; ++axis) {
    const float excess =
        std::max(std::abs(component(point, axis)) - component(extents, axis),
                 0.0f);
    distance_squared += excess * excess;
  }
  return distance_squared;
}

float segmentAabbDistanceSquared(const math::Vec3& a,
                                 const math::Vec3& b,
                                 const math::Vec3& extents) {
  const math::Vec3 direction = math::subtract(b, a);
  std::array<float, 8> breakpoints{};
  size_t breakpoint_count = 0;
  breakpoints[breakpoint_count++] = 0.0f;
  breakpoints[breakpoint_count++] = 1.0f;

  for (int axis = 0; axis < 3; ++axis) {
    const float slope = component(direction, axis);
    if (std::abs(slope) <= kGeometryEpsilon) {
      continue;
    }
    const float start = component(a, axis);
    const float extent = component(extents, axis);
    const float t0 = (-extent - start) / slope;
    const float t1 = (extent - start) / slope;
    if (t0 > 0.0f && t0 < 1.0f) {
      breakpoints[breakpoint_count++] = t0;
    }
    if (t1 > 0.0f && t1 < 1.0f) {
      breakpoints[breakpoint_count++] = t1;
    }
  }

  std::sort(breakpoints.begin(),
            breakpoints.begin() + static_cast<std::ptrdiff_t>(breakpoint_count));
  size_t unique_count = 0;
  for (size_t i = 0; i < breakpoint_count; ++i) {
    if (unique_count == 0 ||
        std::abs(breakpoints[i] - breakpoints[unique_count - 1]) >
            kGeometryEpsilon) {
      breakpoints[unique_count++] = breakpoints[i];
    }
  }

  auto evaluate = [&](float t) {
    return pointAabbDistanceSquared(
        math::add(a, math::scale(direction, t)), extents);
  };

  float best = std::numeric_limits<float>::max();
  for (size_t i = 0; i + 1 < unique_count; ++i) {
    const float interval_start = breakpoints[i];
    const float interval_end = breakpoints[i + 1];
    best = std::min({best, evaluate(interval_start), evaluate(interval_end)});
    if (interval_end - interval_start <= kGeometryEpsilon) {
      continue;
    }

    const float midpoint = 0.5f * (interval_start + interval_end);
    float coefficient_a = 0.0f;
    float coefficient_b = 0.0f;
    float coefficient_c = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
      const float start = component(a, axis);
      const float slope = component(direction, axis);
      const float midpoint_value = start + slope * midpoint;
      const float extent = component(extents, axis);
      float offset = 0.0f;
      if (midpoint_value < -extent) {
        offset = start + extent;
      } else if (midpoint_value > extent) {
        offset = start - extent;
      } else {
        continue;
      }
      coefficient_a += slope * slope;
      coefficient_b += 2.0f * slope * offset;
      coefficient_c += offset * offset;
    }

    if (coefficient_a > kLengthSquaredEpsilon) {
      const float candidate = math::clamp(
          -coefficient_b / (2.0f * coefficient_a),
          interval_start,
          interval_end);
      best = std::min(best,
                      coefficient_a * candidate * candidate +
                          coefficient_b * candidate + coefficient_c);
    }
  }
  return best;
}

float pointSegmentDistanceSquared(const math::Vec3& point,
                                  const math::Vec3& a,
                                  const math::Vec3& b) {
  const math::Vec3 segment = math::subtract(b, a);
  const float segment_length_squared = math::lengthSquared(segment);
  float t = 0.0f;
  if (segment_length_squared > kLengthSquaredEpsilon) {
    t = math::clamp01(
        math::dot(math::subtract(point, a), segment) /
        segment_length_squared);
  }
  const math::Vec3 closest = math::add(a, math::scale(segment, t));
  return math::lengthSquared(math::subtract(point, closest));
}

float segmentSegmentDistanceSquared(const math::Vec3& p1,
                                    const math::Vec3& q1,
                                    const math::Vec3& p2,
                                    const math::Vec3& q2) {
  const math::Vec3 d1 = math::subtract(q1, p1);
  const math::Vec3 d2 = math::subtract(q2, p2);
  const math::Vec3 relative = math::subtract(p1, p2);
  const float a = math::dot(d1, d1);
  const float e = math::dot(d2, d2);
  const float f = math::dot(d2, relative);

  float s = 0.0f;
  float t = 0.0f;
  if (a <= kLengthSquaredEpsilon && e <= kLengthSquaredEpsilon) {
    return math::lengthSquared(relative);
  }
  if (a <= kLengthSquaredEpsilon) {
    t = math::clamp01(f / e);
  } else {
    const float c = math::dot(d1, relative);
    if (e <= kLengthSquaredEpsilon) {
      s = math::clamp01(-c / a);
    } else {
      const float b = math::dot(d1, d2);
      const float denominator = a * e - b * b;
      if (std::abs(denominator) > kLengthSquaredEpsilon) {
        s = math::clamp01((b * f - c * e) / denominator);
      }
      t = (b * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f;
        s = math::clamp01(-c / a);
      } else if (t > 1.0f) {
        t = 1.0f;
        s = math::clamp01((b - c) / a);
      }
    }
  }

  const math::Vec3 c1 = math::add(p1, math::scale(d1, s));
  const math::Vec3 c2 = math::add(p2, math::scale(d2, t));
  return math::lengthSquared(math::subtract(c1, c2));
}

bool withinRadius(float distance_squared, float radius) {
  const float tolerated_radius = radius + kContainmentTolerance;
  return distance_squared <= tolerated_radius * tolerated_radius;
}

bool overlapsBoxBox(const OrientedBox& a, const OrientedBox& b) {
  const float a_extents[3] = {a.extents.x, a.extents.y, a.extents.z};
  const float b_extents[3] = {b.extents.x, b.extents.y, b.extents.z};
  float rotation[3][3]{};
  float absolute_rotation[3][3]{};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      rotation[i][j] = math::dot(a.axes[i], b.axes[j]);
      absolute_rotation[i][j] =
          std::abs(rotation[i][j]) + kGeometryEpsilon;
    }
  }

  const math::Vec3 world_translation = math::subtract(b.center, a.center);
  const float translation[3] = {
      math::dot(world_translation, a.axes[0]),
      math::dot(world_translation, a.axes[1]),
      math::dot(world_translation, a.axes[2]),
  };

  for (int i = 0; i < 3; ++i) {
    const float rb = b_extents[0] * absolute_rotation[i][0] +
                     b_extents[1] * absolute_rotation[i][1] +
                     b_extents[2] * absolute_rotation[i][2];
    if (std::abs(translation[i]) > a_extents[i] + rb) {
      return false;
    }
  }

  for (int j = 0; j < 3; ++j) {
    const float ra = a_extents[0] * absolute_rotation[0][j] +
                     a_extents[1] * absolute_rotation[1][j] +
                     a_extents[2] * absolute_rotation[2][j];
    const float distance =
        std::abs(translation[0] * rotation[0][j] +
                 translation[1] * rotation[1][j] +
                 translation[2] * rotation[2][j]);
    if (distance > ra + b_extents[j]) {
      return false;
    }
  }

  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const float ra =
          a_extents[(i + 1) % 3] * absolute_rotation[(i + 2) % 3][j] +
          a_extents[(i + 2) % 3] * absolute_rotation[(i + 1) % 3][j];
      const float rb =
          b_extents[(j + 1) % 3] * absolute_rotation[i][(j + 2) % 3] +
          b_extents[(j + 2) % 3] * absolute_rotation[i][(j + 1) % 3];
      const float distance =
          std::abs(translation[(i + 2) % 3] *
                       rotation[(i + 1) % 3][j] -
                   translation[(i + 1) % 3] *
                       rotation[(i + 2) % 3][j]);
      if (distance > ra + rb) {
        return false;
      }
    }
  }
  return true;
}

bool worldShapesOverlap(const WorldShape& a, const WorldShape& b) {
  return std::visit(
      [](const auto& left, const auto& right) {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (std::is_same_v<Left, WorldSphere> &&
                      std::is_same_v<Right, WorldSphere>) {
          return withinRadius(
              math::lengthSquared(math::subtract(left.center, right.center)),
              left.radius + right.radius);
        } else if constexpr (std::is_same_v<Left, WorldSphere> &&
                             std::is_same_v<Right, OrientedBox>) {
          return withinRadius(
              pointAabbDistanceSquared(
                  pointInBoxLocalSpace(right, left.center), right.extents),
              left.radius);
        } else if constexpr (std::is_same_v<Left, OrientedBox> &&
                             std::is_same_v<Right, WorldSphere>) {
          return withinRadius(
              pointAabbDistanceSquared(
                  pointInBoxLocalSpace(left, right.center), left.extents),
              right.radius);
        } else if constexpr (std::is_same_v<Left, WorldSphere> &&
                             std::is_same_v<Right, WorldCapsule>) {
          return withinRadius(
              pointSegmentDistanceSquared(left.center, right.a, right.b),
              left.radius + right.radius);
        } else if constexpr (std::is_same_v<Left, WorldCapsule> &&
                             std::is_same_v<Right, WorldSphere>) {
          return withinRadius(
              pointSegmentDistanceSquared(right.center, left.a, left.b),
              left.radius + right.radius);
        } else if constexpr (std::is_same_v<Left, WorldCapsule> &&
                             std::is_same_v<Right, WorldCapsule>) {
          return withinRadius(
              segmentSegmentDistanceSquared(left.a, left.b, right.a, right.b),
              left.radius + right.radius);
        } else if constexpr (std::is_same_v<Left, OrientedBox> &&
                             std::is_same_v<Right, WorldCapsule>) {
          return withinRadius(
              segmentAabbDistanceSquared(
                  pointInBoxLocalSpace(left, right.a),
                  pointInBoxLocalSpace(left, right.b),
                  left.extents),
              right.radius);
        } else if constexpr (std::is_same_v<Left, WorldCapsule> &&
                             std::is_same_v<Right, OrientedBox>) {
          return withinRadius(
              segmentAabbDistanceSquared(
                  pointInBoxLocalSpace(right, left.a),
                  pointInBoxLocalSpace(right, left.b),
                  right.extents),
              left.radius);
        } else {
          return overlapsBoxBox(left, right);
        }
      },
      a,
      b);
}

bool worldShapeContainsPoint(const WorldShape& shape,
                             const math::Vec3& point) {
  if (!math::isFinite(point)) {
    return false;
  }
  return std::visit(
      [&](const auto& value) {
        using Shape = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Shape, OrientedBox>) {
          return pointAabbDistanceSquared(
                     pointInBoxLocalSpace(value, point), value.extents) <=
                 kContainmentTolerance * kContainmentTolerance;
        } else if constexpr (std::is_same_v<Shape, WorldSphere>) {
          return withinRadius(
              math::lengthSquared(math::subtract(point, value.center)),
              value.radius);
        } else {
          return withinRadius(
              pointSegmentDistanceSquared(point, value.a, value.b),
              value.radius);
        }
      },
      shape);
}

}  // namespace

bool containsPoint(const world::World& world,
                   world::Entity entity,
                   const math::Vec3& world_point) {
  const std::optional<WorldShape> shape = worldShapeForEntity(world, entity);
  return shape && worldShapeContainsPoint(*shape, world_point);
}

std::optional<PointContainmentHit> findContainingCollider(
    const world::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter) {
  if (!math::isFinite(world_point) || filter.collision_layer_mask == 0u) {
    return std::nullopt;
  }

  std::optional<PointContainmentHit> hit;
  world.forEach<components::ColliderComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        if (!matchesCollisionLayerMask(world, entity,
                                       filter.collision_layer_mask)) {
          return true;
        }
        const auto& collider = world.get<components::ColliderComponent>(entity);
        if (filter.only_triggers && !collider.is_trigger) {
          return true;
        }
        const auto& transform = world.get<components::TransformComponent>(entity);
        const std::optional<WorldShape> shape =
            makeWorldShape(transform, collider);
        if (!shape || !worldShapeContainsPoint(*shape, world_point)) {
          return true;
        }
        hit = PointContainmentHit{
            .entity = entity,
            .shape = components::colliderShapeType(collider.shape),
        };
        return false;
      });
  return hit;
}

std::vector<PointContainmentHit> findContainingColliders(
    const world::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter) {
  std::vector<PointContainmentHit> hits;
  if (!math::isFinite(world_point) || filter.collision_layer_mask == 0u) {
    return hits;
  }

  world.forEach<components::ColliderComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        if (!matchesCollisionLayerMask(world, entity,
                                       filter.collision_layer_mask)) {
          return;
        }
        const auto& collider = world.get<components::ColliderComponent>(entity);
        if (filter.only_triggers && !collider.is_trigger) {
          return;
        }
        const auto& transform = world.get<components::TransformComponent>(entity);
        const std::optional<WorldShape> shape =
            makeWorldShape(transform, collider);
        if (shape && worldShapeContainsPoint(*shape, world_point)) {
          hits.push_back(PointContainmentHit{
              .entity = entity,
              .shape = components::colliderShapeType(collider.shape),
          });
        }
      });
  return hits;
}

bool overlaps(const world::World& world, world::Entity a, world::Entity b) {
  const std::optional<WorldShape> a_shape = worldShapeForEntity(world, a);
  const std::optional<WorldShape> b_shape = worldShapeForEntity(world, b);
  return a_shape && b_shape && worldShapesOverlap(*a_shape, *b_shape);
}

std::optional<OverlapHit> findOverlappingCollider(
    const world::World& world,
    world::Entity query_entity,
    const OverlapFilter& filter) {
  const std::optional<WorldShape> query_shape =
      worldShapeForEntity(world, query_entity);
  if (!query_shape || filter.collision_layer_mask == 0u) {
    return std::nullopt;
  }

  std::optional<OverlapHit> hit;
  world.forEach<components::ColliderComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        if ((filter.skip_self && entity == query_entity) ||
            !matchesCollisionLayerMask(world, entity,
                                       filter.collision_layer_mask)) {
          return true;
        }
        const auto& collider = world.get<components::ColliderComponent>(entity);
        if (filter.only_triggers && !collider.is_trigger) {
          return true;
        }
        const auto& transform = world.get<components::TransformComponent>(entity);
        const std::optional<WorldShape> shape =
            makeWorldShape(transform, collider);
        if (!shape || !worldShapesOverlap(*query_shape, *shape)) {
          return true;
        }
        hit = OverlapHit{
            .entity = entity,
            .shape = components::colliderShapeType(collider.shape),
        };
        return false;
      });
  return hit;
}

std::vector<OverlapHit> findOverlappingColliders(
    const world::World& world,
    world::Entity query_entity,
    const OverlapFilter& filter) {
  std::vector<OverlapHit> hits;
  const std::optional<WorldShape> query_shape =
      worldShapeForEntity(world, query_entity);
  if (!query_shape || filter.collision_layer_mask == 0u) {
    return hits;
  }

  world.forEach<components::ColliderComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        if ((filter.skip_self && entity == query_entity) ||
            !matchesCollisionLayerMask(world, entity,
                                       filter.collision_layer_mask)) {
          return;
        }
        const auto& collider = world.get<components::ColliderComponent>(entity);
        if (filter.only_triggers && !collider.is_trigger) {
          return;
        }
        const auto& transform = world.get<components::TransformComponent>(entity);
        const std::optional<WorldShape> shape =
            makeWorldShape(transform, collider);
        if (shape && worldShapesOverlap(*query_shape, *shape)) {
          hits.push_back(OverlapHit{
              .entity = entity,
              .shape = components::colliderShapeType(collider.shape),
          });
        }
      });
  return hits;
}

}  // namespace karma::world::queries
