#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// Material asset binding for one mesh material slot.
struct MeshMaterialBinding {
  uint32_t slot = 0;
  std::string material_key;

  bool operator==(const MeshMaterialBinding&) const = default;
};

/// \ingroup karma_components
/// Mesh/material binding data extracted by `RenderSystem`.
///
/// Key fields refer to shared assets or runtime renderer resources registered by key.
struct MeshComponent : ecs::ComponentTag {
  std::string mesh_key;
  std::vector<MeshMaterialBinding> materials;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components
