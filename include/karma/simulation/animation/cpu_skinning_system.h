#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/world/components/skinned_mesh.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/device.h"
#include "karma/simulation/animation/pose.h"

namespace karma::scene {
class Scene;
}

namespace karma::animation {

/// \ingroup karma_animation
/// Skins `bind_mesh` on the CPU using final skin matrices.
geometry::MeshData skinMesh(const geometry::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices);

/// Builds a skinning palette from ECS world transforms.
SkinningPalette buildSkinningPaletteFromWorld(
    const components::SkinnedMeshComponent& skin,
    const ecs::World& world,
    const glm::mat4& mesh_world);

/// Builds a skinning palette from scene hierarchy and world transforms.
SkinningPalette buildSkinningPaletteFromScene(
    const components::SkinnedMeshComponent& skin,
    const ecs::World& world,
    const scene::Scene& scene,
    const glm::mat4& mesh_world);

/// \ingroup karma_animation
/// CPU skinning correctness/fallback path.
///
/// The system builds joint palettes for `SkinnedMeshComponent`, deforms bind
/// meshes on CPU, and uploads the result through `GraphicsDevice::updateMesh`.
class CpuSkinningSystem {
 public:
  void update(ecs::World& world, const scene::Scene& scene, renderer::GraphicsDevice& device);
};

}  // namespace karma::animation
