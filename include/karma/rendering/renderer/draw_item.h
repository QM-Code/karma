#pragma once

#include "karma/rendering/renderer/ids.h"

#include <glm/glm.hpp>

namespace karma::renderer {

struct DrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  MaterialSetId material_set = kInvalidMaterialSet;
  glm::mat4 transform{1.0f};
  LayerId layer = 0;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::renderer
