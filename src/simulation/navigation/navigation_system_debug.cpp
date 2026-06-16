#include "karma/simulation/navigation/navigation_system.h"
#include "karma/simulation/navigation/nav_query.h"

#include "karma/rendering/renderer/device.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "detail/navigation_system_helpers.h"

namespace karma::navigation {

using detail::navSpacePosition;

void NavigationSystem::debugDraw(ecs::World& world,
                                 renderer::GraphicsDevice& graphics,
                                 bool depth_test) const {
  world.forEach<components::NavMeshComponent>([&](ecs::Entity entity) {
    const auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
    if (nav_mesh.enabled && nav_mesh.debug_draw && nav_mesh.nav_mesh.isValid()) {
      nav_mesh.nav_mesh.debugDraw(graphics,
                                  nav_mesh.debug_draw_mode,
                                  depth_test,
                                  {0.05f, 0.95f, 0.48f, 1.0f});
    }
  });

  world.forEach<components::NavTileCacheComponent>([&](ecs::Entity entity) {
    const auto& tile_cache = world.get<components::NavTileCacheComponent>(entity);
    if (tile_cache.enabled && tile_cache.built && tile_cache.tile_cache.isValid()) {
      tile_cache.tile_cache.debugDraw(graphics, {0.96f, 0.45f, 0.12f, 1.0f}, depth_test);
    }
  });

  world.forEach<components::NavCrowdComponent>([&](ecs::Entity entity) {
    const auto& crowd = world.get<components::NavCrowdComponent>(entity);
    if (crowd.enabled && crowd.built && crowd.crowd.isValid()) {
      crowd.crowd.debugDraw(graphics,
                            {0.2f, 0.65f, 1.0f, 1.0f},
                            {0.95f, 0.95f, 0.15f, 1.0f},
                            depth_test);
    }
  });

  world.forEach<components::NavMeshAgentComponent, components::TransformComponent>(
      [&](ecs::Entity entity) {
        const auto& agent = world.get<components::NavMeshAgentComponent>(entity);
        if (!agent.enabled || agent.path.empty() || agent.next_waypoint >= agent.path.size()) {
          return;
        }

        const auto& transform = world.get<components::TransformComponent>(entity);
        NavPath path;
        path.status = NavStatus::Success;
        path.points.push_back(navSpacePosition(transform.getPosition(), agent));
        for (size_t i = agent.next_waypoint; i < agent.path.size(); ++i) {
          path.points.push_back(agent.path[i]);
        }
        NavQuery::debugDrawPath(graphics, path, {1.0f, 0.82f, 0.08f, 1.0f}, depth_test);
      });
}

}  // namespace karma::navigation
