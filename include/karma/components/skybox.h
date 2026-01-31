#pragma once

#include <string>

#include "karma/ecs/component.h"

namespace karma::components {

struct SkyboxComponent : ecs::ComponentTag {
  std::string environment_map;
  float intensity = 1.0f;
  bool enabled = true;
};

}  // namespace karma::components
