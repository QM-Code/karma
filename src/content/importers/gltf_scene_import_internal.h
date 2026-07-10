#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/rendering.h"
#include "karma/world.h"

namespace karma::world {

namespace detail {

/// Converts Assimp's top-down BGRA texels to renderer-order RGBA8 bytes.
bool canonicalizeAssimpEmbeddedTexture(std::span<const uint8_t> bgra,
                                       uint32_t width,
                                       uint32_t height,
                                       std::vector<uint8_t>& rgba);

/// Returns true for conventional glTF-channel packed roughness/metallic names.
bool isPackedMetallicRoughnessTextureName(std::string_view name);

}  // namespace detail

/// Sentinel material index for imported glTF scene data.
constexpr uint32_t kInvalidGltfSceneMaterial = std::numeric_limits<uint32_t>::max();

/// Controls which glTF scene data is loaded by the package importer.
struct GltfSceneLoadOptions {
  enum class AlphaModePolicy : uint32_t {
    Authored = 0,
    AutoCutout = 1,
  };

  struct MaterialOverride {
    uint32_t material_index = kInvalidGltfSceneMaterial;
    std::string material_name;
    bool all_materials = false;
    float normal_scale = 1.0f;
    bool has_normal_scale = false;
    bool casts_shadows = true;
    bool has_casts_shadows = false;
    bool diffuse_only = false;
    bool has_diffuse_only = false;
    bool keep_normal_maps = false;
    bool has_keep_normal_maps = false;
    bool disable_metallic_roughness = false;
    bool has_disable_metallic_roughness = false;
  };

  bool import_meshes = true;
  bool import_lights = true;
  AlphaModePolicy alpha_mode_policy = AlphaModePolicy::Authored;
  std::vector<MaterialOverride> material_overrides;
};

/// Controls how an in-memory glTF prefab is instantiated during source import.
struct GltfScenePrefabInstantiateOptions {
  bool create_synthetic_root = false;
  bool autoplay_animations = true;
  std::string asset_key_prefix;
};

/// Combined glTF load and instantiate options for internal importer tests.
struct GltfSceneImportOptions {
  GltfSceneLoadOptions load{};
  GltfScenePrefabInstantiateOptions instantiate{};
};

/// One renderable primitive inside an imported glTF node.
struct GltfScenePrefabPrimitive {
  std::string name;
  world::MeshData mesh;
  rendering::MaterialDesc material;
  /// Renderer-facing material index in the Assimp material table.
  uint32_t source_material_index = kInvalidGltfSceneMaterial;
  /// Raw glTF primitive material index before backend/importer material remapping.
  uint32_t source_gltf_material_index = kInvalidGltfSceneMaterial;
  uint32_t source_mesh_index = kInvalidGltfSceneNode;
  bool casts_shadows = true;
  uint32_t skin_index = world::kInvalidAnimationIndex;
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
  std::vector<std::shared_ptr<const rendering::ImportedMaterialData>> imported_materials;
  std::vector<world::Skeleton> skeletons;
  std::vector<world::Skin> skins;
  std::vector<world::HumanoidRig> humanoid_rigs;
  std::vector<world::AnimationClip> animations;
  std::vector<std::string> diagnostics;

  bool valid() const {
    return root_node != kInvalidGltfSceneNode && root_node < nodes.size();
  }
};

world::Skeleton makeNodeHierarchySkeleton(const GltfScenePrefab& prefab,
                                          std::string_view name);

GltfScenePrefab loadGltfScenePrefab(const std::filesystem::path& path,
                                    const GltfSceneLoadOptions& options = {});

GltfSceneImportResult instantiateGltfScenePrefab(
    world::World& world,
    world::Scene& scene,
    assets::AssetRegistry& assets,
    const GltfScenePrefab& prefab,
    const GltfScenePrefabInstantiateOptions& options = {});

GltfSceneImportResult importGltfScene(world::World& world,
                                      world::Scene& scene,
                                      assets::AssetRegistry& assets,
                                      const std::filesystem::path& path,
                                      const GltfSceneImportOptions& options = {});

}  // namespace karma::world
