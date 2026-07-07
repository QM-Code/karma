#include "karma/navigation.h"

#include "karma/core.h"
#include "karma/components.h"
#include "karma/world.h"
#include "detail/navigation_system_helpers.h"

namespace karma::navigation {

using detail::clearStoredPath;
using detail::alignPathToCurrentPosition;
using detail::hasActivePath;
using detail::moveAgents;
using detail::navSpacePosition;
using detail::rebuildNavMeshes;
using detail::syncCrowds;
using detail::syncTileCaches;

void NavigationSystem::update(world::World& world, float dt) {
  const auto update_start = core::SteadyClock::now();
  auto section_start = update_start;
  rebuildNavMeshes(world, assets_, &stats_);
  syncTileCaches(world, dt);
  syncCrowds(world, dt);
  auto section_end = core::SteadyClock::now();
  stats_.last_rebuild_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  submitPathRequests(world);
  section_end = core::SteadyClock::now();
  stats_.last_submit_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  moveAgents(world, dt);
  section_end = core::SteadyClock::now();
  stats_.last_move_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  applyCompletedPaths(world);
  section_end = core::SteadyClock::now();
  stats_.last_apply_ms = core::elapsedMilliseconds(section_start, section_end);
  stats_.last_update_ms = core::elapsedMilliseconds(update_start, section_end);
}

bool NavigationSystem::requestMoveTo(world::World& world,
                                     world::Entity agent_entity,
                                     const math::Vec3& destination,
                                     std::shared_ptr<const NavTraversalCostProvider> traversal_cost_provider) {
  if (!world.isAlive(agent_entity) ||
      !world.has<components::NavMeshAgentComponent>(agent_entity)) {
    return false;
  }

  auto& agent = world.get<components::NavMeshAgentComponent>(agent_entity);
  const bool active_path = hasActivePath(agent);
  agent.destination = destination;
  agent.has_destination = true;
  agent.path_requested = true;
  agent.path_resolved = false;
  agent.traversal_cost_provider = std::move(traversal_cost_provider);
  if (!active_path) {
    agent.current_path_partial = false;
    agent.status = components::NavMeshAgentStatus::Requested;
    agent.current_velocity = {};
  }
  return true;
}

bool NavigationSystem::requestFollowPath(world::World& world,
                                         world::Entity agent_entity,
                                         const NavPath& path) {
  if (!world.isAlive(agent_entity) ||
      !world.has<components::NavMeshAgentComponent>(agent_entity) ||
      !path.success() ||
      path.points.empty()) {
    return false;
  }

  auto& agent = world.get<components::NavMeshAgentComponent>(agent_entity);
  if (path.partial && !agent.accept_partial_paths) {
    return false;
  }

  agent.destination = path.points.back();
  agent.has_destination = true;
  agent.path_requested = false;
  agent.path_pending = false;
  agent.path_resolved = true;
  agent.current_path_partial = path.partial;
  agent.path_request_id = 0;
  agent.traversal_cost_provider.reset();
  agent.last_path_status = path.status;
  agent.path = path.points;
  agent.path_point_flags = path.point_flags;
  if (agent.path_point_flags.size() != agent.path.size()) {
    agent.path_point_flags.clear();
  }
  agent.path_point_speed_multipliers = path.point_speed_multipliers;
  if (agent.path_point_speed_multipliers.size() != agent.path.size()) {
    agent.path_point_speed_multipliers.clear();
  }
  if (world.has<components::TransformComponent>(agent_entity)) {
    const auto& transform =
        world.get<components::TransformComponent>(agent_entity);
    alignPathToCurrentPosition(agent, navSpacePosition(transform.getPosition(), agent));
  } else {
    agent.next_waypoint = agent.path.size() > 1u ? 1u : 0u;
  }
  agent.status = components::NavMeshAgentStatus::PathResolved;
  agent.current_velocity = {};
  return true;
}

void NavigationSystem::clearPath(world::World& world, world::Entity agent_entity) {
  if (!world.isAlive(agent_entity) ||
      !world.has<components::NavMeshAgentComponent>(agent_entity)) {
    return;
  }

  auto& agent = world.get<components::NavMeshAgentComponent>(agent_entity);
  clearStoredPath(agent);
  agent.next_waypoint = 0;
  agent.path_requested = false;
  agent.path_pending = false;
  agent.path_resolved = false;
  agent.current_path_partial = false;
  agent.path_request_id = 0;
  agent.traversal_cost_provider.reset();
  agent.has_destination = false;
  agent.status = components::NavMeshAgentStatus::Idle;
  agent.current_velocity = {};
}

bool NavigationSystem::requestCrowdMoveTo(world::World& world,
                                          world::Entity agent_entity,
                                          const math::Vec3& destination) {
  if (!world.isAlive(agent_entity) ||
      !world.has<components::NavCrowdAgentComponent>(agent_entity)) {
    return false;
  }

  auto& agent = world.get<components::NavCrowdAgentComponent>(agent_entity);
  agent.destination = destination;
  agent.has_destination = true;
  agent.destination_requested = true;
  agent.velocity_requested = false;
  agent.reached_destination = false;
  agent.last_request_status = NavStatus::InProgress;
  return true;
}

bool NavigationSystem::requestCrowdVelocity(world::World& world,
                                            world::Entity agent_entity,
                                            const math::Vec3& velocity) {
  if (!world.isAlive(agent_entity) ||
      !world.has<components::NavCrowdAgentComponent>(agent_entity)) {
    return false;
  }

  auto& agent = world.get<components::NavCrowdAgentComponent>(agent_entity);
  agent.requested_velocity = velocity;
  agent.velocity_requested = true;
  agent.destination_requested = false;
  agent.reached_destination = false;
  agent.last_request_status = NavStatus::InProgress;
  return true;
}

void NavigationSystem::clearCrowdTarget(world::World& world, world::Entity agent_entity) {
  if (!world.isAlive(agent_entity) ||
      !world.has<components::NavCrowdAgentComponent>(agent_entity)) {
    return;
  }

  auto& agent = world.get<components::NavCrowdAgentComponent>(agent_entity);
  if (agent.cached_crowd_entity.isValid() &&
      world.isAlive(agent.cached_crowd_entity) &&
      world.has<components::NavCrowdComponent>(agent.cached_crowd_entity)) {
    auto& crowd = world.get<components::NavCrowdComponent>(agent.cached_crowd_entity);
    if (crowd.built && crowd.crowd.isValid() && agent.agent_id >= 0) {
      crowd.crowd.resetMoveTarget(agent.agent_id);
    }
  }
  agent.has_destination = false;
  agent.destination_requested = false;
  agent.velocity_requested = false;
  agent.target_state = NavCrowdTargetState::None;
  agent.reached_destination = false;
  agent.last_request_status = NavStatus::Success;
}

bool NavigationSystem::requestBuildDebugDraw(world::World& world, world::Entity nav_entity) {
  if (!world.isAlive(nav_entity) ||
      !world.has<components::NavMeshComponent>(nav_entity)) {
    return false;
  }
  auto& nav_mesh = world.get<components::NavMeshComponent>(nav_entity);
  nav_mesh.rebuild_requested = true;
  nav_mesh.build_debug_draw_requested = true;
  return true;
}

}  // namespace karma::navigation
