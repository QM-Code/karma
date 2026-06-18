#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// Assigned material asset or variant for one mesh material slot.
struct MeshMaterialAssignment {
  uint32_t slot = 0;
  std::string material_key;

  bool operator==(const MeshMaterialAssignment&) const = default;
};

/// \ingroup karma_components
/// Mesh/material assignment data extracted by `RenderSystem`.
///
/// Mesh assets may provide default material slots. Entries in `materials`
/// assign replacement material keys to individual slots for this object.
/// Key fields refer to normalized assets registered in `content::AssetRegistry`.
struct MeshComponent : ecs::ComponentTag {
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components
