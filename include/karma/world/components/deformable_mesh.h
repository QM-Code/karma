#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "karma/rendering/renderer/ids.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::components {

/// \ingroup karma_components
/// Selected deformation execution path.
enum class DeformationPath {
  Gpu,
  CpuReference,
};

/// \ingroup karma_components
/// Four-joint skin influence payload for one vertex.
struct VertexSkinInfluence {
  glm::uvec4 joints{0u, 0u, 0u, 0u};
  glm::vec4 weights{0.0f, 0.0f, 0.0f, 0.0f};
};

/// \ingroup karma_components
/// Unified runtime deformation state for one renderable mesh.
///
/// glTF import fills bind mesh, skin binding, morph weights, joint entities, and
/// inverse bind matrices. `DeformationSystem` builds joint palettes, updates the
/// renderer-owned deformation resource, and only uploads CPU-deformed meshes for
/// explicit reference/diagnostic paths.
struct DeformableMeshComponent : ecs::ComponentTag {
  geometry::MeshData bind_mesh;
  geometry::MeshData cpu_deformed_mesh;

  std::vector<VertexSkinInfluence> vertex_influences;
  std::vector<ecs::Entity> joint_entities;
  std::vector<glm::mat4> inverse_bind_matrices;
  std::vector<glm::mat4> joint_palette;

  std::vector<float> base_morph_weights;
  std::vector<float> morph_weights;

  ecs::Entity render_transform_entity{};
  renderer::DeformationId deformation = renderer::kInvalidDeformation;
  uint32_t skin_index = 0;
  DeformationPath path = DeformationPath::Gpu;
  std::string diagnostic;

  bool palette_valid = false;
  bool morph_weights_dirty = true;
  bool override_render_transform = false;
  bool renderer_mesh_is_cpu_deformed = false;
  bool enabled = true;

  bool skinned() const {
    return !joint_entities.empty() &&
           vertex_influences.size() == bind_mesh.vertices.size();
  }

  bool morphable() const {
    return !bind_mesh.morph_targets.empty();
  }
};

}  // namespace karma::components
