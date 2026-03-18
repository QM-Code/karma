#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "karma/ecs/entity.h"
#include "karma/ecs/world.h"
#include "karma/math/types.h"

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

bool containsPoint(const ecs::World& world, ecs::Entity entity, const math::Vec3& world_point);

std::optional<PointContainmentHit> findContainingCollider(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

std::vector<PointContainmentHit> findContainingColliders(
    const ecs::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

}  // namespace karma::ecs::queries
