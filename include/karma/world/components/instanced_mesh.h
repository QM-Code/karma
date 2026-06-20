#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/instance_data.h"
#include "karma/world/components/mesh.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// One authored instance inside an `InstancedMeshComponent`.
struct MeshInstance {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  std::array<float, 4> params{0.0f, 0.0f, 0.0f, 0.0f};
};

/// One planar/yaw-only authored instance for compact GPU instancing.
struct PlanarMeshInstance {
  math::Vec3 position{};
  float yaw_radians = 0.0f;
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  std::array<float, 4> params{0.0f, 0.0f, 0.0f, 0.0f};
};

inline constexpr size_t kMaxInstancedMeshLodLevels = 3u;

/// Optional alternate mesh/material used after an instance reaches a distance.
struct InstancedMeshLodLevel {
  float start_distance = 0.0f;
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  renderer::InstanceLodRenderMode render_mode = renderer::InstanceLodRenderMode::Mesh;
  bool shadow_visible = false;
};

/// \ingroup karma_components
/// Shared mesh/material binding plus many per-instance transforms.
///
/// Instance data is authored in world-layer types. `RenderSystem` translates it
/// into renderer instance buffers and keeps material slot fallback behavior the
/// same as `MeshComponent`.
struct InstancedMeshComponent : ecs::ComponentTag {
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  std::vector<InstancedMeshLodLevel> lods;
  renderer::InstanceGpuLayout gpu_layout = renderer::InstanceGpuLayout::Matrix4x4Params;
  std::vector<MeshInstance> instances;
  std::vector<PlanarMeshInstance> planar_instances;
  uint64_t instance_revision = 0;
  bool dynamic = false;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components
