#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Script binding placeholder for higher-level scripting integrations.
struct ScriptComponent : ecs::ComponentTag {
  std::string script_key;
  bool enabled = true;
};

}  // namespace karma::components
