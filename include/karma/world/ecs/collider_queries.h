#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/core/math/types.h"

namespace karma::ecs::queries {

enum class ColliderShape {
  Box,
  Sphere,
  Capsule,
  Mesh,
};

struct PointContainmentHit {
  ecs::Entity entity{};
  ColliderShape shape = ColliderShape::Box;
};

struct PointContainmentFilter {
  bool only_triggers = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

struct OverlapHit {
  ecs::Entity entity{};
  ColliderShape shape = ColliderShape::Box;
};

struct OverlapFilter {
  bool only_triggers = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
  bool skip_self = true;
};

bool containsPoint(const ecs::World& world, ecs::Entity entity, const math::Vec3& world_point);

bool overlaps(const ecs::World& world, ecs::Entity a, ecs::Entity b);

std::optional<PointContainmentHit> findContainingCollider(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

std::vector<PointContainmentHit> findContainingColliders(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

std::optional<OverlapHit> findOverlappingCollider(
    const ecs::World& world,
    ecs::Entity query_entity,
    const OverlapFilter& filter = {});

std::vector<OverlapHit> findOverlappingColliders(
    const ecs::World& world,
    ecs::Entity query_entity,
    const OverlapFilter& filter = {});

}  // namespace karma::ecs::queries
