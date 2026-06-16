#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::components {

/// \ingroup karma_components
/// Selected skinning execution path.
enum class SkinningPath {
  Cpu,
  Gpu,
  GpuUnavailableCpuFallback,
};

/// Maximum joint matrices currently supported by one renderer draw.
constexpr uint32_t kMaxSkinningJointsPerDraw = 128;

/// \ingroup karma_components
/// Four-joint skin influence payload for one vertex.
struct VertexSkinInfluence {
  glm::uvec4 joints{0u, 0u, 0u, 0u};
  glm::vec4 weights{0.0f, 0.0f, 0.0f, 0.0f};
};

/// \ingroup karma_components
/// Skinning data for a renderable mesh.
///
/// GLB import fills bind mesh, influences, joint entities, and inverse bind
/// matrices. `CpuSkinningSystem` builds `joint_palette` for GPU skinning and
/// updates `skinned_mesh` only for CPU fallback paths.
struct SkinnedMeshComponent : ecs::ComponentTag {
  geometry::MeshData bind_mesh;
  geometry::MeshData skinned_mesh;
  std::vector<VertexSkinInfluence> vertex_influences;
  std::vector<ecs::Entity> joint_entities;
  std::vector<glm::mat4> inverse_bind_matrices;
  std::vector<glm::mat4> joint_palette;
  ecs::Entity render_transform_entity{};
  uint32_t skin_index = 0;
  SkinningPath skinning_path = SkinningPath::GpuUnavailableCpuFallback;
  std::string diagnostic;
  bool palette_valid = false;
  bool override_render_transform = false;
  bool renderer_mesh_is_bind_pose = true;
  bool enabled = true;
};

}  // namespace karma::components
