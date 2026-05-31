#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

struct TagComponent : ecs::ComponentTag {
  std::string name;
};

}  // namespace karma::components
