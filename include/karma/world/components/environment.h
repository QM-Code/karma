#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Environment map and skybox settings extracted by `RenderSystem`.
struct EnvironmentComponent : ecs::ComponentTag {
  std::string environment_map_asset_key;
  float intensity = 1.0f;
  bool draw_skybox = true;
  bool enabled = true;
};

}  // namespace karma::components
