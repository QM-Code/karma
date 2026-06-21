#include "detail/navigation_system_helpers.h"

#include <algorithm>

#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::navigation::detail {

void rebuildCrowds(world::World& world) {
  world.forEach<components::NavMeshComponent, components::NavCrowdComponent>(
      [&](world::Entity entity) {
        auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
        auto& crowd = world.get<components::NavCrowdComponent>(entity);
        if (!crowd.enabled) {
          if (crowd.built) {
            crowd.crowd.reset();
            crowd.built = false;
            invalidateCrowdAgentsForCrowd(world, entity);
          }
          return;
        }
        if (!navMeshUsable(nav_mesh)) {
          return;
        }

        const bool should_build = crowd.rebuild_requested ||
                                  (crowd.build_on_start && !crowd.built) ||
                                  crowd.nav_mesh_build_version != nav_mesh.build_version;
        if (!should_build) {
          return;
        }

        NavCrowdBuildResult result;
        crowd.built = crowd.crowd.init(nav_mesh.nav_mesh, crowd.config, &result);
        crowd.last_build_result = result;
        crowd.rebuild_requested = false;
        crowd.nav_mesh_build_version = nav_mesh.build_version;
        invalidateCrowdAgentsForCrowd(world, entity);
      });
}

void syncCrowdAgents(world::World& world) {
  world.forEach<components::NavCrowdAgentComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        auto& agent = world.get<components::NavCrowdAgentComponent>(entity);
        const auto& transform = world.get<components::TransformComponent>(entity);

        auto remove_from_crowd = [&](world::Entity preferred) {
          const CrowdSelection old_crowd = findCrowd(world, preferred);
          if (old_crowd.crowd != nullptr && agent.agent_id >= 0) {
            old_crowd.crowd->crowd.removeAgent(agent.agent_id);
          }
          agent.agent_id = -1;
          agent.cached_crowd_entity = {};
        };

        const world::Entity current_crowd = agent.cached_crowd_entity.isValid()
            ? agent.cached_crowd_entity
            : agent.crowd_entity;
        if (!agent.enabled || agent.remove_requested) {
          remove_from_crowd(current_crowd);
          agent.remove_requested = false;
          agent.state = NavCrowdAgentState::Invalid;
          agent.target_state = NavCrowdTargetState::None;
          agent.current_velocity = {};
          return;
        }

        const CrowdSelection target_crowd = findCrowd(world, agent.crowd_entity);
        if (target_crowd.crowd == nullptr) {
          return;
        }

        if (agent.agent_id >= 0 && agent.cached_crowd_entity != target_crowd.entity) {
          remove_from_crowd(current_crowd);
        }

        if (agent.agent_id < 0) {
          agent.agent_id = target_crowd.crowd->crowd.addAgent(
              crowdSpacePosition(transform.getPosition(), agent),
              agent.params);
          if (agent.agent_id < 0) {
            agent.last_request_status = NavStatus::BuildFailed;
            return;
          }
          agent.cached_crowd_entity = target_crowd.entity;
          agent.params_dirty = false;
          if (agent.has_destination) {
            agent.destination_requested = true;
          }
        } else if (agent.params_dirty) {
          if (target_crowd.crowd->crowd.updateAgentParams(agent.agent_id, agent.params)) {
            agent.params_dirty = false;
          }
        }

        if (agent.destination_requested && agent.has_destination) {
          agent.last_request_status =
              target_crowd.crowd->crowd.requestMoveTarget(agent.agent_id,
                                                          agent.destination,
                                                          agent.search_extents)
                  ? NavStatus::Success
                  : NavStatus::InvalidEnd;
          agent.destination_requested = false;
          agent.reached_destination = false;
        }
        if (agent.velocity_requested) {
          agent.last_request_status =
              target_crowd.crowd->crowd.requestMoveVelocity(agent.agent_id,
                                                            agent.requested_velocity)
                  ? NavStatus::Success
                  : NavStatus::QueryFailed;
          agent.velocity_requested = false;
          agent.reached_destination = false;
        }
      });
}

void updateCrowds(world::World& world, float dt) {
  world.forEach<components::NavMeshComponent, components::NavCrowdComponent>(
      [&](world::Entity entity) {
        auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
        auto& crowd = world.get<components::NavCrowdComponent>(entity);
        if (!crowdUsable(nav_mesh, crowd)) {
          return;
        }

        float update_dt = dt * std::max(0.0f, crowd.time_scale);
        if (crowd.simulation_paused) {
          update_dt = crowd.step_requested ? crowd.step_dt : 0.0f;
        }
        crowd.step_requested = false;
        if (update_dt <= 0.0f) {
          return;
        }

        crowd.crowd.update(update_dt);
        crowd.debug_snapshot = crowd.debug_request.enabled
            ? crowd.crowd.debugSnapshot(crowd.debug_request)
            : NavCrowdDebugSnapshot{};
        world.forEach<components::NavCrowdAgentComponent, components::TransformComponent>(
            [&](world::Entity agent_entity) {
              auto& agent = world.get<components::NavCrowdAgentComponent>(agent_entity);
              if (agent.cached_crowd_entity != entity || agent.agent_id < 0) {
                return;
              }

              NavCrowdAgentInfo info;
              if (!crowd.crowd.agentInfo(agent.agent_id, info)) {
                agent.agent_id = -1;
                agent.state = NavCrowdAgentState::Invalid;
                agent.target_state = NavCrowdTargetState::None;
                agent.current_velocity = {};
                return;
              }

              agent.state = info.state;
              agent.target_state = info.target_state;
              agent.partial = info.partial;
              agent.current_velocity = info.velocity;
              if (agent.has_destination) {
                agent.reached_destination =
                    horizontalDistance(info.position, agent.destination) <= agent.stopping_distance;
              }

              if (agent.movement_mode ==
                  components::NavCrowdMovementMode::CharacterControllerVelocity) {
                if (world.has<components::CharacterControllerComponent>(agent_entity)) {
                  auto& controller = world.get<components::CharacterControllerComponent>(agent_entity);
                  controller.setDesiredVelocity({info.velocity.x, 0.0f, info.velocity.z});
                }
              } else {
                auto& transform = world.get<components::TransformComponent>(agent_entity);
                transform.setPosition(crowdWorldPosition(info.position, agent));
              }
            });
      });
}

void syncCrowds(world::World& world, float dt) {
  rebuildCrowds(world);
  syncCrowdAgents(world);
  updateCrowds(world, dt);
}

}  // namespace karma::navigation::detail
