#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/world/components/skinned_mesh.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/device.h"

namespace karma::animation {

renderer::MeshData skinMesh(const renderer::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices);

class CpuSkinningSystem {
 public:
  void update(ecs::World& world, renderer::GraphicsDevice& device);
};

}  // namespace karma::animation
