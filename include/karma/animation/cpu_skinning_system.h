#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/components/skinned_mesh.h"
#include "karma/ecs/world.h"
#include "karma/renderer/device.h"

namespace karma::animation {

renderer::MeshData skinMesh(const renderer::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices);

class CpuSkinningSystem {
 public:
  void update(ecs::World& world, renderer::GraphicsDevice& device);
};

}  // namespace karma::animation
