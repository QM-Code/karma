#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/components/light.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/device.h"
#include "karma/world/scene/scene.h"

namespace karma::scene {

/// \ingroup karma_content
/// Sentinel node id for imported GLB scene data.
constexpr uint32_t kInvalidGlbSceneNode = std::numeric_limits<uint32_t>::max();
/// Sentinel material index for imported GLB scene data.
constexpr uint32_t kInvalidGlbSceneMaterial = std::numeric_limits<uint32_t>::max();

/// \ingroup karma_content
/// Controls which GLB scene data is loaded.
struct GlbSceneLoadOptions {
  bool import_meshes = true;
  bool import_lights = true;
};

/// \ingroup karma_content
/// Controls how a loaded GLB prefab is instantiated into ECS/scene data.
struct GlbSceneInstantiateOptions {
  bool create_synthetic_root = false;
  bool autoplay_animations = true;
};

/// \ingroup karma_content
/// Combined GLB load and instantiate options for `importGlbScene`.
struct GlbSceneImportOptions {
  GlbSceneLoadOptions load{};
  GlbSceneInstantiateOptions instantiate{};
};

/// \ingroup karma_content
/// One renderable primitive inside an imported GLB node.
struct GlbScenePrefabPrimitive {
  std::string name;
  renderer::MeshData mesh;
  renderer::MaterialDesc material;
  uint32_t source_material_index = kInvalidGlbSceneMaterial;
  uint32_t source_mesh_index = kInvalidGlbSceneNode;
  uint32_t skin_index = animation::kInvalidAnimationIndex;
  std::vector<components::VertexSkinInfluence> vertex_influences;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;

  /// Returns true when the primitive has skinning payloads.
  bool skinned() const {
    return !joint_node_indices.empty() && vertex_influences.size() == mesh.vertices.size();
  }
};

/// \ingroup karma_content
/// Imported GLB node with transforms, light, mesh primitives, and children.
struct GlbScenePrefabNode {
  std::string name;
  math::Vec3 local_position{};
  math::Quat local_rotation{};
  math::Vec3 local_scale{1.0f, 1.0f, 1.0f};
  math::Vec3 world_position{};
  math::Quat world_rotation{};
  math::Vec3 world_scale{1.0f, 1.0f, 1.0f};
  bool has_light = false;
  components::LightComponent light{};
  std::vector<GlbScenePrefabPrimitive> primitives;
  std::vector<uint32_t> children;
};

/// \ingroup karma_content
/// In-memory GLB scene prefab before ECS instantiation.
struct GlbScenePrefab {
  std::filesystem::path source_path;
  uint32_t root_node = kInvalidGlbSceneNode;
  std::vector<GlbScenePrefabNode> nodes;
  std::vector<animation::Skeleton> skeletons;
  std::vector<animation::Skin> skins;
  std::vector<animation::AnimationClip> animations;
  std::vector<std::string> diagnostics;

  /// Returns true when root node metadata is valid.
  bool valid() const {
    return root_node != kInvalidGlbSceneNode && root_node < nodes.size();
  }
};

/// \ingroup karma_content
/// ECS/scene entities created from a GLB scene prefab.
struct GlbSceneImportResult {
  ecs::Entity root_entity{};
  scene::NodeId root_node = scene::Node::kInvalidId;
  std::vector<ecs::Entity> entities;
  std::vector<ecs::Entity> node_entities_by_index;

  /// Returns true when a root ECS entity and scene node were created.
  bool valid() const {
    return root_entity.isValid() && root_node != scene::Node::kInvalidId;
  }
};

/// Loads a GLB scene into an in-memory prefab.
GlbScenePrefab loadGlbScenePrefab(const std::filesystem::path& path,
                                  const GlbSceneLoadOptions& options = {});

/// Instantiates a loaded GLB scene prefab into world and scene data.
GlbSceneImportResult instantiateGlbScenePrefab(
    ecs::World& world,
    scene::Scene& scene,
    renderer::GraphicsDevice& device,
    const GlbScenePrefab& prefab,
    const GlbSceneInstantiateOptions& options = {});

/// Loads and instantiates a GLB scene in one call.
GlbSceneImportResult importGlbScene(ecs::World& world,
                                    scene::Scene& scene,
                                    renderer::GraphicsDevice& device,
                                    const std::filesystem::path& path,
                                    const GlbSceneImportOptions& options = {});

}  // namespace karma::scene
