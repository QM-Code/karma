#pragma once

#include <string>

#include "karma/world/ecs/component.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::components {

/// \ingroup karma_components
/// Mesh/material binding data extracted by `RenderSystem`.
///
/// Key fields refer to shared assets. Direct ids can be used for runtime-created
/// resources and ownership flags tell the render system whether to destroy them.
struct MeshComponent : ecs::ComponentTag {
  std::string mesh_key;
  std::string material_key;
  std::string texture_key;
  renderer::MeshId mesh_id = renderer::kInvalidMesh;
  renderer::MaterialId material_id = renderer::kInvalidMaterial;
  bool owns_mesh_id = false;
  bool owns_material_id = false;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components
