#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/morph_target.h"
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
/// Applies morph target weights to `bind_mesh` on the CPU.
///
/// Position, normal, and tangent deltas are accumulated from
/// `MeshData::morph_targets`; normals and tangents are renormalized.
geometry::MeshData morphMesh(const geometry::MeshData& bind_mesh,
                             const std::vector<float>& weights);

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
/// CPU mesh deformation correctness/fallback path.
///
/// The system applies morph targets, builds skinning palettes for
/// `SkinnedMeshComponent`, and uploads CPU-deformed meshes when the renderer
/// cannot consume the full deformation directly.
class CpuSkinningSystem {
 public:
  void update(ecs::World& world, const scene::Scene& scene, renderer::GraphicsDevice& device);
};

}  // namespace karma::animation
