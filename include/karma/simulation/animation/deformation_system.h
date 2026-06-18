#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/rendering/renderer/device.h"
#include "karma/simulation/animation/pose.h"
#include "karma/world/components/deformable_mesh.h"
#include "karma/world/ecs/world.h"

namespace karma::scene {
class Scene;
}

namespace karma::content {
class AssetRegistry;
}  // namespace karma::content

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
    const components::DeformableMeshComponent& deformation,
    const ecs::World& world,
    const glm::mat4& mesh_world);

/// Builds a skinning palette from scene hierarchy and world transforms.
SkinningPalette buildSkinningPaletteFromScene(
    const components::DeformableMeshComponent& deformation,
    const ecs::World& world,
    const scene::Scene& scene,
    const glm::mat4& mesh_world);

/// \ingroup karma_animation
/// Updates renderer-owned deformation resources for skinned/morphed meshes.
///
/// GPU mode updates joint palettes and morph weights without rewriting mesh
/// vertex buffers. CPU reference mode remains available for validation and
/// diagnostics.
class DeformationSystem {
 public:
  void update(ecs::World& world,
              const scene::Scene& scene,
              renderer::GraphicsDevice& device,
              const content::AssetRegistry* assets = nullptr);
};

}  // namespace karma::animation
