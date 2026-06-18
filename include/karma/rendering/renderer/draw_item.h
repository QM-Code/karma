#pragma once

#include "karma/rendering/renderer/ids.h"

#include <vector>

#include <glm/glm.hpp>

namespace karma::renderer {

/// Non-owning material binding for one mesh material slot.
struct DrawMaterialBinding {
  uint32_t slot = 0;
  MaterialId material = kInvalidMaterial;
};

/// \ingroup karma_rendering
/// One renderable mesh submission.
///
/// `RenderSystem` builds draw items from ECS mesh/deformation data. Runtime
/// modules can submit draw items directly when they own renderer resources.
struct DrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  DeformationId deformation = kInvalidDeformation;
  glm::mat4 transform{1.0f};
  LayerId layer = 0;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::renderer
