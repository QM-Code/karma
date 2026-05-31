#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

struct EnvironmentComponent : ecs::ComponentTag {
  std::string environment_map;
  float intensity = 1.0f;
  bool draw_skybox = true;
  bool enabled = true;
};

}  // namespace karma::components
