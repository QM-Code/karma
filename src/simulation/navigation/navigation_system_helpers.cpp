#include "detail/navigation_system_helpers.h"

#include <algorithm>
#include <cmath>

#include "karma/components.h"
#include "karma/world.h"

namespace karma::navigation::detail {


uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

void incrementBuildVersion(components::NavMeshComponent& nav_mesh) {
  ++nav_mesh.build_version;
  if (nav_mesh.build_version == 0) {
    ++nav_mesh.build_version;
  }
}

math::Vec3 navSpacePosition(const math::Vec3& world_position,
                            const components::NavMeshAgentComponent& agent) {
  return {world_position.x, world_position.y - agent.height_offset, world_position.z};
}

math::Vec3 worldSpacePosition(const math::Vec3& nav_position,
                              const components::NavMeshAgentComponent& agent,
                              float fallback_y) {
  if (!agent.update_vertical_position) {
    return {nav_position.x, fallback_y, nav_position.z};
  }
  return {nav_position.x, nav_position.y + agent.height_offset, nav_position.z};
}

void clearStoredPath(components::NavMeshAgentComponent& agent) {
  agent.path.clear();
  agent.path_point_flags.clear();
  agent.next_waypoint = 0;
}

bool hasActivePath(const components::NavMeshAgentComponent& agent) {
  return !agent.path.empty() && agent.next_waypoint < agent.path.size();
}

void failPathRequest(components::NavMeshAgentComponent& agent, NavStatus status) {
  agent.path_requested = false;
  agent.path_pending = false;
  agent.path_resolved = false;
  agent.last_path_status = status;
  if (!hasActivePath(agent)) {
    clearStoredPath(agent);
    agent.current_path_partial = false;
    agent.status = components::NavMeshAgentStatus::Failed;
    agent.current_velocity = {};
  }
}

bool navMeshUsable(const components::NavMeshComponent& nav_mesh) {
  return nav_mesh.enabled && nav_mesh.built && nav_mesh.nav_mesh.isValid() &&
         nav_mesh.nav_mesh.snapshot() != nullptr;
}

bool tileCacheUsable(const components::NavMeshComponent& nav_mesh,
                     const components::NavTileCacheComponent& tile_cache) {
  return navMeshUsable(nav_mesh) &&
         tile_cache.enabled &&
         tile_cache.built &&
         tile_cache.tile_cache.isValid();
}

bool crowdUsable(const components::NavMeshComponent& nav_mesh,
                 const components::NavCrowdComponent& crowd) {
  return navMeshUsable(nav_mesh) &&
         crowd.enabled &&
         crowd.built &&
         crowd.crowd.isValid();
}

NavMeshSelection findNavMesh(world::World& world, world::Entity preferred) {
  if (preferred.isValid() &&
      world.isAlive(preferred) &&
      world.has<components::NavMeshComponent>(preferred)) {
    auto& nav_mesh = world.get<components::NavMeshComponent>(preferred);
    if (navMeshUsable(nav_mesh)) {
      return {preferred, &nav_mesh};
    }
  }

  NavMeshSelection found{};
  world.forEach<components::NavMeshComponent>([&](world::Entity entity) -> bool {
    auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
    if (navMeshUsable(nav_mesh)) {
      found = {entity, &nav_mesh};
      return false;
    }
    return true;
  });
  return found;
}

CrowdSelection findCrowd(world::World& world, world::Entity preferred) {
  if (preferred.isValid() &&
      world.isAlive(preferred) &&
      world.has<components::NavMeshComponent>(preferred) &&
      world.has<components::NavCrowdComponent>(preferred)) {
    auto& nav_mesh = world.get<components::NavMeshComponent>(preferred);
    auto& crowd = world.get<components::NavCrowdComponent>(preferred);
    if (crowdUsable(nav_mesh, crowd)) {
      return {preferred, &nav_mesh, &crowd};
    }
  }

  CrowdSelection found{};
  world.forEach<components::NavMeshComponent, components::NavCrowdComponent>(
      [&](world::Entity entity) -> bool {
        auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
        auto& crowd = world.get<components::NavCrowdComponent>(entity);
        if (crowdUsable(nav_mesh, crowd)) {
          found = {entity, &nav_mesh, &crowd};
          return false;
        }
        return true;
      });
  return found;
}

TileCacheSelection findTileCache(world::World& world, world::Entity preferred) {
  if (preferred.isValid() &&
      world.isAlive(preferred) &&
      world.has<components::NavMeshComponent>(preferred) &&
      world.has<components::NavTileCacheComponent>(preferred)) {
    auto& nav_mesh = world.get<components::NavMeshComponent>(preferred);
    auto& tile_cache = world.get<components::NavTileCacheComponent>(preferred);
    if (tileCacheUsable(nav_mesh, tile_cache)) {
      return {preferred, &nav_mesh, &tile_cache};
    }
  }

  TileCacheSelection found{};
  world.forEach<components::NavMeshComponent, components::NavTileCacheComponent>(
      [&](world::Entity entity) -> bool {
        auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
        auto& tile_cache = world.get<components::NavTileCacheComponent>(entity);
        if (tileCacheUsable(nav_mesh, tile_cache)) {
          found = {entity, &nav_mesh, &tile_cache};
          return false;
        }
        return true;
      });
  return found;
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 scaledByAbs(const math::Vec3& value, const math::Vec3& scale) {
  return {
      value.x * std::abs(scale.x),
      value.y * std::abs(scale.y),
      value.z * std::abs(scale.z),
  };
}

float horizontalScale(const math::Vec3& scale) {
  return std::max(std::abs(scale.x), std::abs(scale.z));
}

void invalidateObstacleRefsForCache(world::World& world, world::Entity nav_mesh_entity) {
  world.forEach<components::NavTileCacheObstacleComponent>([&](world::Entity obstacle_entity) {
    auto& obstacle = world.get<components::NavTileCacheObstacleComponent>(obstacle_entity);
    const bool explicitly_targets_cache = obstacle.nav_mesh_entity == nav_mesh_entity;
    const bool cached_on_cache = obstacle.cached_nav_mesh_entity == nav_mesh_entity;
    const bool auto_targeted = !obstacle.nav_mesh_entity.isValid() &&
                               obstacle.cached_nav_mesh_entity.isValid();
    if (explicitly_targets_cache || cached_on_cache || auto_targeted) {
      obstacle.obstacle_ref = 0;
      obstacle.cached_nav_mesh_entity = {};
      obstacle.dirty = true;
    }
  });
}

void invalidateCrowdAgentsForCrowd(world::World& world, world::Entity crowd_entity) {
  world.forEach<components::NavCrowdAgentComponent>([&](world::Entity agent_entity) {
    auto& agent = world.get<components::NavCrowdAgentComponent>(agent_entity);
    const bool explicitly_targets_crowd = agent.crowd_entity == crowd_entity;
    const bool cached_on_crowd = agent.cached_crowd_entity == crowd_entity;
    const bool auto_targeted = !agent.crowd_entity.isValid() && agent.cached_crowd_entity.isValid();
    if (explicitly_targets_crowd || cached_on_crowd || auto_targeted) {
      agent.agent_id = -1;
      agent.cached_crowd_entity = {};
      agent.params_dirty = true;
      if (agent.has_destination) {
        agent.destination_requested = true;
      }
    }
  });
}

math::Vec3 crowdSpacePosition(const math::Vec3& world_position,
                              const components::NavCrowdAgentComponent& agent) {
  return {world_position.x, world_position.y - agent.height_offset, world_position.z};
}

math::Vec3 crowdWorldPosition(const math::Vec3& crowd_position,
                              const components::NavCrowdAgentComponent& agent) {
  return {crowd_position.x, crowd_position.y + agent.height_offset, crowd_position.z};
}

float horizontalDistance(const math::Vec3& a, const math::Vec3& b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dz * dz);
}

}  // namespace karma::navigation::detail
