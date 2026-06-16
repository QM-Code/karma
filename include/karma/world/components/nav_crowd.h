#pragma once

#include <cstdint>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_crowd.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

/// \ingroup karma_components
/// How `NavigationSystem` applies DetourCrowd movement to an entity.
enum class NavCrowdMovementMode {
  Transform,
  PlayerControllerVelocity,
};

/// \ingroup karma_components
/// Owns a DetourCrowd instance for a navmesh entity.
struct NavCrowdComponent : ecs::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  bool simulation_paused = false;
  bool step_requested = false;
  float step_dt = 1.0f / 20.0f;
  float time_scale = 1.0f;
  uint64_t nav_mesh_build_version = 0;
  navigation::NavCrowdConfig config{};
  navigation::NavCrowdBuildResult last_build_result{};
  navigation::NavCrowdDebugRequest debug_request{};
  navigation::NavCrowdDebugSnapshot debug_snapshot{};
  navigation::NavCrowd crowd{};
};

/// \ingroup karma_components
/// ECS-authored DetourCrowd agent.
struct NavCrowdAgentComponent : ecs::ComponentTag {
  bool enabled = true;
  ecs::Entity crowd_entity{};
  ecs::Entity cached_crowd_entity{};
  navigation::NavCrowdAgentParams params{};
  math::Vec3 destination{};
  math::Vec3 requested_velocity{};
  math::Vec3 current_velocity{};
  math::Vec3 search_extents{2.0f, 4.0f, 2.0f};
  float height_offset = 0.0f;
  float stopping_distance = 0.2f;
  int agent_id = -1;
  bool has_destination = false;
  bool destination_requested = false;
  bool velocity_requested = false;
  bool params_dirty = false;
  bool remove_requested = false;
  NavCrowdMovementMode movement_mode = NavCrowdMovementMode::Transform;
  navigation::NavCrowdAgentState state = navigation::NavCrowdAgentState::Invalid;
  navigation::NavCrowdTargetState target_state = navigation::NavCrowdTargetState::None;
  navigation::NavStatus last_request_status = navigation::NavStatus::QueryFailed;
  bool partial = false;
  bool reached_destination = false;
};

}  // namespace karma::components
