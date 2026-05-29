#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/ecs/component.h"
#include "karma/ecs/entity.h"
#include "karma/renderer/types.h"

namespace karma::components {

struct VertexSkinInfluence {
  glm::uvec4 joints{0u, 0u, 0u, 0u};
  glm::vec4 weights{0.0f, 0.0f, 0.0f, 0.0f};
};

struct SkinnedMeshComponent : ecs::ComponentTag {
  renderer::MeshData bind_mesh;
  renderer::MeshData skinned_mesh;
  std::vector<VertexSkinInfluence> vertex_influences;
  std::vector<ecs::Entity> joint_entities;
  std::vector<glm::mat4> inverse_bind_matrices;
  bool enabled = true;
};

}  // namespace karma::components
