#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace karma::renderer {

/// GPU vertex-input layout used by instanced mesh batches.
enum class InstanceGpuLayout : uint32_t {
  Matrix4x4Params = 0,
  PositionYawScaleParams = 1,
};

/// Draw-time orientation behavior for an instanced LOD mesh.
enum class InstanceLodRenderMode : uint32_t {
  Mesh = 0,
  UprightBillboard = 1,
};

/// Per-instance GPU payload for the default matrix layout.
struct alignas(16) InstanceData {
  glm::mat4 transform{1.0f};
  glm::vec4 params{0.0f};
};

/// Compact planar/yaw-only instance payload.
struct alignas(16) PlanarInstanceData {
  glm::vec4 position_yaw{0.0f};
  glm::vec4 scale_pad{1.0f, 1.0f, 1.0f, 0.0f};
  glm::vec4 params{0.0f};
};

inline constexpr size_t instanceGpuLayoutStride(InstanceGpuLayout layout) {
  switch (layout) {
    case InstanceGpuLayout::Matrix4x4Params:
      return sizeof(InstanceData);
    case InstanceGpuLayout::PositionYawScaleParams:
      return sizeof(PlanarInstanceData);
  }
  return sizeof(InstanceData);
}

}  // namespace karma::renderer
