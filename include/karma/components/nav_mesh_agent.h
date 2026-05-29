#pragma once

#include <cstddef>
#include <vector>

#include "karma/ecs/component.h"
#include "karma/math/types.h"

namespace karma::components {

struct NavMeshAgentComponent : ecs::ComponentTag {
  bool enabled = true;
  float speed = 3.0f;
  float stopping_distance = 0.15f;
  std::vector<math::Vec3> path;
  size_t next_waypoint = 0;
};

}  // namespace karma::components
