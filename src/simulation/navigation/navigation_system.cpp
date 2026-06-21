#include "karma/navigation.h"

#include "karma/core.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"
#include "detail/navigation_system_helpers.h"

namespace karma::navigation {

using detail::clearStoredPath;
using detail::hasActivePath;
using detail::moveAgents;
using detail::rebuildNavMeshes;
using detail::syncCrowds;
using detail::syncTileCaches;

void NavigationSystem::update(world::World& world, float dt) {
  const auto update_start = core::SteadyClock::now();
  auto section_start = update_start;
  rebuildNavMeshes(world, assets_);
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
                                     const math::Vec3& destination) {
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
  if (!active_path) {
    agent.current_path_partial = false;
    agent.status = components::NavMeshAgentStatus::Requested;
    agent.current_velocity = {};
  }
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

}  // namespace karma::navigation
