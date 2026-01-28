#pragma once

#include <string>

#include "karma/ecs/component.h"

namespace karma::components {

struct SkyboxComponent : ecs::ComponentTag {
  std::string skybox_key;
  bool enabled = true;
};

}  // namespace karma::components
