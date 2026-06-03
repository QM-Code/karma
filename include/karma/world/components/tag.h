#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Human-readable entity name used by examples, prefabs, and debug UI.
struct TagComponent : ecs::ComponentTag {
  std::string name;
};

}  // namespace karma::components
