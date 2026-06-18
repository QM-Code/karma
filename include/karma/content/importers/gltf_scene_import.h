#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/components/light.h"
#include "karma/world/components/deformable_mesh.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/material_library.h"
#include "karma/world/scene/scene.h"

namespace karma::scene {

/// \ingroup karma_content
/// Sentinel node id for imported glTF scene data.
constexpr uint32_t kInvalidGltfSceneNode = std::numeric_limits<uint32_t>::max();
/// Sentinel material index for imported glTF scene data.
constexpr uint32_t kInvalidGltfSceneMaterial = std::numeric_limits<uint32_t>::max();

/// \ingroup karma_content
/// Controls which glTF scene data is loaded.
struct GltfSceneLoadOptions {
  bool import_meshes = true;
  bool import_lights = true;
};

/// \ingroup karma_content
/// Controls how a loaded glTF scene prefab is instantiated into ECS/scene data.
struct GltfSceneInstantiateOptions {
  bool create_synthetic_root = false;
  bool autoplay_animations = true;
};

/// \ingroup karma_content
/// Combined glTF load and instantiate options for `importGltfScene`.
struct GltfSceneImportOptions {
  GltfSceneLoadOptions load{};
  GltfSceneInstantiateOptions instantiate{};
};

/// \ingroup karma_content
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

  /// Returns true when the primitive has skinning payloads.
  bool skinned() const {
    return !joint_node_indices.empty() && vertex_influences.size() == mesh.vertices.size();
  }

  /// Returns true when the primitive has morph target payloads.
  bool morphable() const {
    return !mesh.morph_targets.empty();
  }
};

/// \ingroup karma_content
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

/// \ingroup karma_content
/// In-memory glTF scene prefab before ECS instantiation.
struct GltfScenePrefab {
  std::filesystem::path source_path;
  uint32_t root_node = kInvalidGltfSceneNode;
  std::vector<GltfScenePrefabNode> nodes;
  std::vector<std::shared_ptr<const renderer::ImportedMaterialData>> imported_materials;
  std::vector<animation::Skeleton> skeletons;
  std::vector<animation::Skin> skins;
  std::vector<animation::AnimationClip> animations;
  std::vector<std::string> diagnostics;

  /// Returns true when root node metadata is valid.
  bool valid() const {
    return root_node != kInvalidGltfSceneNode && root_node < nodes.size();
  }
};

/// \ingroup karma_content
/// ECS/scene entities created from a glTF scene prefab.
struct GltfSceneImportResult {
  ecs::Entity root_entity{};
  scene::NodeId root_node = scene::Node::kInvalidId;
  std::vector<ecs::Entity> entities;
  std::vector<ecs::Entity> node_entities_by_index;
  /// Renderable morph primitive entities keyed by imported glTF node index.
  std::vector<std::vector<ecs::Entity>> morph_entities_by_node_index;

  /// Returns true when a root ECS entity and scene node were created.
  bool valid() const {
    return root_entity.isValid() && root_node != scene::Node::kInvalidId;
  }
};

/// Loads a glTF scene into an in-memory prefab.
GltfScenePrefab loadGltfScenePrefab(const std::filesystem::path& path,
                                    const GltfSceneLoadOptions& options = {});

/// Instantiates a loaded glTF scene prefab into world and scene data.
GltfSceneImportResult instantiateGltfScenePrefab(
    ecs::World& world,
    scene::Scene& scene,
    renderer::GraphicsDevice& device,
    const GltfScenePrefab& prefab,
    const GltfSceneInstantiateOptions& options = {},
    renderer::MaterialLibrary* materials = nullptr);

/// Loads and instantiates a glTF scene in one call.
GltfSceneImportResult importGltfScene(ecs::World& world,
                                      scene::Scene& scene,
                                      renderer::GraphicsDevice& device,
                                      const std::filesystem::path& path,
                                      const GltfSceneImportOptions& options = {},
                                      renderer::MaterialLibrary* materials = nullptr);

}  // namespace karma::scene
