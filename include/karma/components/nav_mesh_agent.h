#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "karma/ecs/component.h"
#include "karma/ecs/entity.h"
#include "karma/math/types.h"
#include "karma/navigation/nav_mesh.h"

namespace karma::components {

enum class NavMeshAgentStatus {
  Idle,
  Requested,
  PathPending,
  PathResolved,
  Moving,
  Arrived,
  Failed,
  PartialPath,
};

struct NavMeshAgentComponent : ecs::ComponentTag {
  bool enabled = true;
  float speed = 3.0f;
  float stopping_distance = 0.15f;
  float height_offset = 0.0f;
  bool update_vertical_position = true;
  bool accept_partial_paths = true;
  math::Vec3 destination{};
  math::Vec3 search_extents{2.0f, 4.0f, 2.0f};
  math::Vec3 current_velocity{};
  ecs::Entity nav_mesh_entity{};
  navigation::NavQueryFilter query_filter{};
  navigation::NavStatus last_path_status = navigation::NavStatus::QueryFailed;
  NavMeshAgentStatus status = NavMeshAgentStatus::Idle;
  bool has_destination = false;
  bool path_requested = false;
  bool path_pending = false;
  bool path_resolved = false;
  bool current_path_partial = false;
  uint64_t path_request_id = 0;
  std::vector<math::Vec3> path;
  std::vector<uint8_t> path_point_flags;
  size_t next_waypoint = 0;
};

}  // namespace karma::components
