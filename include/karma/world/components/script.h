#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

struct ScriptComponent : ecs::ComponentTag {
  std::string script_key;
  bool enabled = true;
};

}  // namespace karma::components
