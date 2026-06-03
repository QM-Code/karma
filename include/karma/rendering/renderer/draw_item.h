#pragma once

#include "karma/rendering/renderer/ids.h"

#include <glm/glm.hpp>
#include <vector>

namespace karma::renderer {

/// \ingroup karma_rendering
/// One renderable mesh submission.
///
/// `RenderSystem` builds draw items from ECS mesh/skinned-mesh data. Runtime
/// modules can submit draw items directly when they own renderer resources.
struct DrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  MaterialSetId material_set = kInvalidMaterialSet;
  glm::mat4 transform{1.0f};
  std::vector<glm::mat4> skinning_palette;
  LayerId layer = 0;
  bool visible = true;
  bool shadow_visible = true;
  bool skinning_enabled = false;
};

}  // namespace karma::renderer
