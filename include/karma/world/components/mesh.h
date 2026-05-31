#pragma once

#include <string>

#include "karma/world/ecs/component.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::components {

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
