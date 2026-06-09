#pragma once

#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Mesh/material binding data extracted by `RenderSystem`.
///
/// Key fields refer to shared assets or runtime renderer resources registered by key.
struct MeshComponent : ecs::ComponentTag {
  std::string mesh_key;
  std::string material_key;
  std::string texture_key;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components
