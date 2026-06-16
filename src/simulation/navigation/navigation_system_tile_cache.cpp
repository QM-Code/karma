#include "detail/navigation_system_helpers.h"

#include <algorithm>
#include <cmath>

#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"

namespace karma::navigation::detail {

void syncTileCacheObstacles(ecs::World& world) {
  world.forEach<components::NavTileCacheObstacleComponent, components::TransformComponent>(
      [&](ecs::Entity entity) {
        auto& obstacle = world.get<components::NavTileCacheObstacleComponent>(entity);
        const auto& transform = world.get<components::TransformComponent>(entity);

        auto remove_from_cache = [&](ecs::Entity preferred) {
          const TileCacheSelection old_cache = findTileCache(world, preferred);
          if (old_cache.tile_cache == nullptr || obstacle.obstacle_ref == 0) {
            obstacle.obstacle_ref = 0;
            return;
          }
          if (old_cache.tile_cache->tile_cache.removeObstacle(obstacle.obstacle_ref)) {
            old_cache.tile_cache->updates_pending = true;
          }
          obstacle.obstacle_ref = 0;
          obstacle.cached_nav_mesh_entity = {};
        };

        const ecs::Entity current_cache = obstacle.cached_nav_mesh_entity.isValid()
            ? obstacle.cached_nav_mesh_entity
            : obstacle.nav_mesh_entity;

        if (!obstacle.enabled || obstacle.remove_requested) {
          remove_from_cache(current_cache);
          obstacle.remove_requested = false;
          return;
        }

        const TileCacheSelection target_cache = findTileCache(world, obstacle.nav_mesh_entity);
        if (target_cache.tile_cache == nullptr) {
          return;
        }

        if (obstacle.obstacle_ref != 0 &&
            (obstacle.dirty || obstacle.cached_nav_mesh_entity != target_cache.entity)) {
          remove_from_cache(current_cache);
        }

        if (obstacle.obstacle_ref != 0 && !obstacle.dirty) {
          return;
        }

        const math::Vec3 position = addVec3(transform.getPosition(), obstacle.offset);
        const math::Vec3 scale = transform.getScale();
        uint64_t ref = 0;
        bool added = false;
        switch (obstacle.shape) {
          case NavTileCacheObstacleShape::Box: {
            const math::Vec3 scaled_min = scaledByAbs(obstacle.bounds_min, scale);
            const math::Vec3 scaled_max = scaledByAbs(obstacle.bounds_max, scale);
            const math::Vec3 bounds_min{
                position.x + std::min(scaled_min.x, scaled_max.x),
                position.y + std::min(scaled_min.y, scaled_max.y),
                position.z + std::min(scaled_min.z, scaled_max.z),
            };
            const math::Vec3 bounds_max{
                position.x + std::max(scaled_min.x, scaled_max.x),
                position.y + std::max(scaled_min.y, scaled_max.y),
                position.z + std::max(scaled_min.z, scaled_max.z),
            };
            added = target_cache.tile_cache->tile_cache.addBoxObstacle(bounds_min, bounds_max, &ref);
            break;
          }
          case NavTileCacheObstacleShape::OrientedBox: {
            const math::Vec3 half_extents = scaledByAbs(obstacle.half_extents, scale);
            added = target_cache.tile_cache->tile_cache.addOrientedBoxObstacle(position,
                                                                               half_extents,
                                                                               obstacle.yaw_radians,
                                                                               &ref);
            break;
          }
          case NavTileCacheObstacleShape::Cylinder:
          default: {
            const float radius = obstacle.radius * horizontalScale(scale);
            const float height = obstacle.height * std::abs(scale.y);
            added = target_cache.tile_cache->tile_cache.addCylinderObstacle(position,
                                                                            radius,
                                                                            height,
                                                                            &ref);
            break;
          }
        }

        if (added) {
          obstacle.obstacle_ref = ref;
          obstacle.cached_nav_mesh_entity = target_cache.entity;
          obstacle.dirty = false;
          target_cache.tile_cache->updates_pending = true;
        }
      });
}

void updateTileCaches(ecs::World& world, float dt) {
  world.forEach<components::NavMeshComponent, components::NavTileCacheComponent>(
      [&](ecs::Entity entity) {
        auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
        auto& tile_cache = world.get<components::NavTileCacheComponent>(entity);
        if (!tileCacheUsable(nav_mesh, tile_cache)) {
          return;
        }

        const bool was_pending = tile_cache.updates_pending;
        bool up_to_date = true;
        for (int i = 0; i < 8; ++i) {
          if (!tile_cache.tile_cache.update(dt, nav_mesh.nav_mesh, &up_to_date)) {
            break;
          }
          if (up_to_date) {
            break;
          }
        }
        tile_cache.updates_pending = !up_to_date;
        if (was_pending || !up_to_date) {
          incrementBuildVersion(nav_mesh);
        }
      });
}

void syncTileCaches(ecs::World& world, float dt) {
  syncTileCacheObstacles(world);
  updateTileCaches(world, dt);
}

}  // namespace karma::navigation::detail
