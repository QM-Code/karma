#pragma once

#include <cstdint>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_tile_cache.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

/// \ingroup karma_components
/// Builds a Detour tile cache for a `NavMeshComponent` so obstacle entities can update tiles incrementally.
struct NavTileCacheComponent : ecs::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  navigation::NavTileCacheBuildConfig build_config{};
  navigation::NavTileCacheBuildResult last_build_result{};
  navigation::NavTileCache tile_cache{};
  bool updates_pending = false;
};

/// \ingroup karma_components
/// Temporary navigation obstacle authored in ECS and synchronized into a `NavTileCacheComponent`.
struct NavTileCacheObstacleComponent : ecs::ComponentTag {
  bool enabled = true;
  ecs::Entity nav_mesh_entity{};
  navigation::NavTileCacheObstacleShape shape = navigation::NavTileCacheObstacleShape::Cylinder;
  math::Vec3 offset{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
  math::Vec3 bounds_min{-0.5f, -0.5f, -0.5f};
  math::Vec3 bounds_max{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  float height = 2.0f;
  float yaw_radians = 0.0f;
  uint64_t obstacle_ref = 0;
  ecs::Entity cached_nav_mesh_entity{};
  bool dirty = true;
  bool remove_requested = false;
};

}  // namespace karma::components
