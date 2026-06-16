#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/core/math/types.h"

namespace karma::ecs::queries {

/// \ingroup karma_world_ecs
/// Collider shape kind exposed by ECS query helpers.
enum class ColliderShape {
  Box,
  Sphere,
  Capsule,
  Cylinder,
  TaperedCapsule,
  ConvexHull,
  Triangle,
  HeightField,
  Mesh,
};

/// Result from point containment tests.
struct PointContainmentHit {
  ecs::Entity entity{};
  ColliderShape shape = ColliderShape::Box;
};

/// Filter for point containment queries.
struct PointContainmentFilter {
  bool only_triggers = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

/// Result from overlap tests.
struct OverlapHit {
  ecs::Entity entity{};
  ColliderShape shape = ColliderShape::Box;
};

/// Filter for overlap queries.
struct OverlapFilter {
  bool only_triggers = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
  bool skip_self = true;
};

/// Returns true when `world_point` lies inside `entity`'s collider.
bool containsPoint(const ecs::World& world, ecs::Entity entity, const math::Vec3& world_point);

/// Returns true when two entities' colliders overlap.
bool overlaps(const ecs::World& world, ecs::Entity a, ecs::Entity b);

/// Finds the first collider containing `world_point`.
std::optional<PointContainmentHit> findContainingCollider(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

/// Finds all colliders containing `world_point`.
std::vector<PointContainmentHit> findContainingColliders(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

/// Finds the first collider overlapping `query_entity`.
std::optional<OverlapHit> findOverlappingCollider(
    const ecs::World& world,
    ecs::Entity query_entity,
    const OverlapFilter& filter = {});

/// Finds all colliders overlapping `query_entity`.
std::vector<OverlapHit> findOverlappingColliders(
    const ecs::World& world,
    ecs::Entity query_entity,
    const OverlapFilter& filter = {});

}  // namespace karma::ecs::queries
