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

constexpr uint32_t kInvalidGlbSceneNode = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInvalidGlbSceneMaterial = std::numeric_limits<uint32_t>::max();

struct GlbSceneLoadOptions {
  bool import_meshes = true;
  bool import_lights = true;
};

struct GlbSceneInstantiateOptions {
  bool create_synthetic_root = false;
  bool autoplay_animations = true;
};

struct GlbSceneImportOptions {
  GlbSceneLoadOptions load{};
  GlbSceneInstantiateOptions instantiate{};
};

struct GlbScenePrefabPrimitive {
  std::string name;
  renderer::MeshData mesh;
  renderer::MaterialDesc material;
  uint32_t source_material_index = kInvalidGlbSceneMaterial;
  uint32_t source_mesh_index = kInvalidGlbSceneNode;
  std::vector<components::VertexSkinInfluence> vertex_influences;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;

  bool skinned() const {
    return !joint_node_indices.empty() && vertex_influences.size() == mesh.vertices.size();
  }
};

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

struct GlbScenePrefab {
  std::filesystem::path source_path;
  uint32_t root_node = kInvalidGlbSceneNode;
  std::vector<GlbScenePrefabNode> nodes;
  std::vector<animation::AnimationClip> animations;

  bool valid() const {
    return root_node != kInvalidGlbSceneNode && root_node < nodes.size();
  }
};

struct GlbSceneImportResult {
  ecs::Entity root_entity{};
  scene::NodeId root_node = scene::Node::kInvalidId;
  std::vector<ecs::Entity> entities;
  std::vector<ecs::Entity> node_entities_by_index;

  bool valid() const {
    return root_entity.isValid() && root_node != scene::Node::kInvalidId;
  }
};

GlbScenePrefab loadGlbScenePrefab(const std::filesystem::path& path,
                                  const GlbSceneLoadOptions& options = {});

GlbSceneImportResult instantiateGlbScenePrefab(
    ecs::World& world,
    scene::Scene& scene,
    renderer::GraphicsDevice& device,
    const GlbScenePrefab& prefab,
    const GlbSceneInstantiateOptions& options = {});

GlbSceneImportResult importGlbScene(ecs::World& world,
                                    scene::Scene& scene,
                                    renderer::GraphicsDevice& device,
                                    const std::filesystem::path& path,
                                    const GlbSceneImportOptions& options = {});

}  // namespace karma::scene
