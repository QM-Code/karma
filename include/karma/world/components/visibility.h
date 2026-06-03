#pragma once

#include <cstdint>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Shared visibility and layer-mask component.
///
/// Render and collision systems both honor the relevant masks when extracting
/// scene data or processing queries.
struct VisibilityComponent : ecs::ComponentTag {
  bool visible = true;
  uint32_t render_layer_mask = 0xFFFFFFFFu;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

}  // namespace karma::components
