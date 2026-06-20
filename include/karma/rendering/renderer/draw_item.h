#pragma once

#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/instance_data.h"

#include <span>
#include <vector>

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
  glm::vec4 instance_params{0.0f};
  LayerId layer = 0;
  bool visible = true;
  bool shadow_visible = true;
};

struct InstancedLodDrawDesc {
  float start_distance = 0.0f;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  InstanceLodRenderMode render_mode = InstanceLodRenderMode::Mesh;
  glm::vec3 bounds_center{0.0f};
  float bounds_radius = 0.0f;
  bool bounds_valid = false;
  bool shadow_visible = false;
};

/// \ingroup karma_rendering
/// One batch of repeated mesh instances with shared mesh/material bindings.
struct InstancedDrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  std::vector<InstancedLodDrawDesc> lods;
  InstanceGpuLayout gpu_layout = InstanceGpuLayout::Matrix4x4Params;
  std::span<const InstanceData> instances;
  std::span<const PlanarInstanceData> planar_instances;
  bool payload_changed = true;
  uint64_t revision = 0;
  glm::vec3 bounds_center{0.0f};
  float bounds_radius = 0.0f;
  bool bounds_valid = false;
  LayerId layer = 0;
  bool dynamic = false;
  bool visible = true;
  bool shadow_visible = true;

  size_t instanceCount() const {
    switch (gpu_layout) {
      case InstanceGpuLayout::Matrix4x4Params:
        return instances.size();
      case InstanceGpuLayout::PositionYawScaleParams:
        return planar_instances.size();
    }
    return 0u;
  }
};

}  // namespace karma::renderer
