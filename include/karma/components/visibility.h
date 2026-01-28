#pragma once

#include <cstdint>

#include "karma/ecs/component.h"

namespace karma::components {

struct VisibilityComponent : ecs::ComponentTag {
  bool visible = true;
  uint32_t render_layer_mask = 0xFFFFFFFFu;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

}  // namespace karma::components
