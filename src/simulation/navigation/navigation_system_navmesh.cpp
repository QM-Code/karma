#include "detail/navigation_system_helpers.h"

#include <algorithm>
#include <cmath>

#include "karma/math.h"
#include "karma/navigation.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::navigation::detail {

void rebuildNavMeshes(world::World& world, const assets::AssetRegistry* assets) {
  world.forEach<components::NavMeshComponent>([&](world::Entity entity) {
    auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
    if (!nav_mesh.enabled) {
      return;
    }

    components::NavTileCacheComponent* tile_cache = nullptr;
    if (world.has<components::NavTileCacheComponent>(entity)) {
      tile_cache = &world.get<components::NavTileCacheComponent>(entity);
    }

    const bool should_build = nav_mesh.rebuild_requested ||
                              (nav_mesh.build_on_start && !nav_mesh.built);
    const bool should_build_cache =
        tile_cache != nullptr &&
        tile_cache->enabled &&
        (tile_cache->rebuild_requested ||
         (tile_cache->build_on_start && !tile_cache->built));
    if (!should_build && !should_build_cache) {
      return;
    }

    incrementBuildVersion(nav_mesh);

    const NavMeshInputGeometry geometry =
        collectNavMeshGeometry(world, assets, nav_mesh.source_mask);
    if (tile_cache != nullptr && tile_cache->enabled) {
      NavTileCacheBuildResult cache_result;
      tile_cache->built = tile_cache->tile_cache.build(nav_mesh.nav_mesh,
                                                       geometry,
                                                       nav_mesh.build_config,
                                                       tile_cache->build_config,
                                                       &cache_result);
      tile_cache->last_build_result = cache_result;
      tile_cache->rebuild_requested = false;
      tile_cache->updates_pending = false;
      nav_mesh.built = tile_cache->built && nav_mesh.nav_mesh.isValid();
      nav_mesh.last_build_result = nav_mesh.nav_mesh.lastBuildResult();
      if (!nav_mesh.built) {
        nav_mesh.last_build_result.status = cache_result.status;
        nav_mesh.last_build_result.message = cache_result.message;
      }
      invalidateObstacleRefsForCache(world, entity);
    } else {
      NavMeshBuildResult result;
      nav_mesh.built = nav_mesh.nav_mesh.build(geometry, nav_mesh.build_config, &result);
      nav_mesh.last_build_result = result;
    }
    nav_mesh.rebuild_requested = false;
  });
}

void moveAgents(world::World& world, float dt) {
  if (dt <= 0.0f) {
    return;
  }

  world.forEach<components::NavMeshAgentComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        auto& agent = world.get<components::NavMeshAgentComponent>(entity);
        if (!agent.enabled ||
            agent.path.empty() ||
            agent.next_waypoint >= agent.path.size() ||
            agent.speed <= 0.0f) {
          agent.current_velocity = {};
          return;
        }

        auto& transform = world.get<components::TransformComponent>(entity);
        const math::Vec3 previous_world = transform.getPosition();
        math::Vec3 current = navSpacePosition(previous_world, agent);
        float remaining = agent.speed * dt;
        while (remaining > 0.0f && agent.next_waypoint < agent.path.size()) {
          const math::Vec3 target = agent.path[agent.next_waypoint];
          const math::Vec3 delta = math::subtract(target, current);
          const float distance = math::length(delta);
          if (distance <= agent.stopping_distance) {
            current = target;
            ++agent.next_waypoint;
            continue;
          }

          const float step = std::min(remaining, distance);
          current = math::add(current, math::scale(delta, step / distance));
          remaining -= step;
          if (step < distance) {
            break;
          }
        }

        const math::Vec3 next_world = worldSpacePosition(current, agent, previous_world.y);
        transform.setPosition(next_world);
        agent.current_velocity =
            math::scale(math::subtract(next_world, previous_world), 1.0f / dt);

        if (agent.next_waypoint >= agent.path.size()) {
          agent.status = agent.current_path_partial
              ? components::NavMeshAgentStatus::PartialPath
              : components::NavMeshAgentStatus::Arrived;
          agent.path_resolved = false;
          agent.current_velocity = {};
        } else {
          agent.status = components::NavMeshAgentStatus::Moving;
          agent.path_resolved = false;
        }
      });
}

}  // namespace karma::navigation::detail
