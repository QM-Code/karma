#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "karma/content/importers/gltf_scene_import.h"
#include "karma/rendering/renderer/material.h"
#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/components/deformable_mesh.h"
#include "karma/world/components/light.h"
#include "karma/world/geometry/mesh_data.h"
#include "karma/world/scene/scene.h"

namespace karma::scene {

/// Sentinel material index for imported glTF scene data.
constexpr uint32_t kInvalidGltfSceneMaterial = std::numeric_limits<uint32_t>::max();

/// Controls which glTF scene data is loaded by the package importer.
struct GltfSceneLoadOptions {
  bool import_meshes = true;
  bool import_lights = true;
};

/// Combined glTF load and instantiate options for internal importer tests.
struct GltfSceneImportOptions {
  GltfSceneLoadOptions load{};
  GltfSceneInstantiateOptions instantiate{};
};

/// One renderable primitive inside an imported glTF node.
struct GltfScenePrefabPrimitive {
  std::string name;
  geometry::MeshData mesh;
  renderer::MaterialDesc material;
  /// Renderer-facing material index in the Assimp material table.
  uint32_t source_material_index = kInvalidGltfSceneMaterial;
  /// Raw glTF primitive material index before backend/importer material remapping.
  uint32_t source_gltf_material_index = kInvalidGltfSceneMaterial;
  uint32_t source_mesh_index = kInvalidGltfSceneNode;
  uint32_t skin_index = animation::kInvalidAnimationIndex;
  /// Default morph target weights authored on the source glTF mesh.
  std::vector<float> morph_weights;
  std::vector<components::VertexSkinInfluence> vertex_influences;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;

  bool skinned() const {
    return !joint_node_indices.empty() && vertex_influences.size() == mesh.vertices.size();
  }

  bool morphable() const {
    return !mesh.morph_targets.empty();
  }
};

/// Imported glTF node with transforms, light, mesh primitives, and children.
struct GltfScenePrefabNode {
  std::string name;
  math::Vec3 local_position{};
  math::Quat local_rotation{};
  math::Vec3 local_scale{1.0f, 1.0f, 1.0f};
  math::Vec3 world_position{};
  math::Quat world_rotation{};
  math::Vec3 world_scale{1.0f, 1.0f, 1.0f};
  bool has_light = false;
  components::LightComponent light{};
  std::vector<GltfScenePrefabPrimitive> primitives;
  std::vector<uint32_t> children;
};

/// In-memory glTF scene prefab before conversion to registered assets.
struct GltfScenePrefab {
  std::filesystem::path source_path;
  uint32_t root_node = kInvalidGltfSceneNode;
  std::vector<GltfScenePrefabNode> nodes;
  std::vector<std::shared_ptr<const renderer::ImportedMaterialData>> imported_materials;
  std::vector<animation::Skeleton> skeletons;
  std::vector<animation::Skin> skins;
  std::vector<animation::AnimationClip> animations;
  std::vector<std::string> diagnostics;

  bool valid() const {
    return root_node != kInvalidGltfSceneNode && root_node < nodes.size();
  }
};

GltfScenePrefab loadGltfScenePrefab(const std::filesystem::path& path,
                                    const GltfSceneLoadOptions& options = {});

GltfSceneImportResult instantiateGltfScenePrefab(
    ecs::World& world,
    scene::Scene& scene,
    content::AssetRegistry& assets,
    const GltfScenePrefab& prefab,
    const GltfSceneInstantiateOptions& options = {});

GltfSceneImportResult importGltfScene(ecs::World& world,
                                      scene::Scene& scene,
                                      content::AssetRegistry& assets,
                                      const std::filesystem::path& path,
                                      const GltfSceneImportOptions& options = {});

}  // namespace karma::scene
