#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "karma/content/assets/asset_registry.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/deformable_mesh.h"
#include "karma/world/components/instanced_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/device.h"
#include "karma/world/scene/scene.h"

namespace karma::content {
struct AssetPackageHandle;
}

namespace karma::renderer {

/// \ingroup karma_rendering
/// Opaque handle for renderer assets pinned by explicit prewarm.
struct RenderPrewarmHandle {
  uint64_t id = 0u;
  bool valid() const { return id != 0u; }
};

/// \ingroup karma_rendering
/// Extracts ECS render data and submits it to `GraphicsDevice`.
///
/// `RenderSystem` resolves mesh/material keys, maintains shared renderer
/// resources, extracts cameras/lights/environment, resolves camera-selected
/// post-process profiles, submits offscreen camera passes, submits the primary
/// camera pass, and cleans up renderer resources for destroyed entities.
class RenderSystem {
 public:
  /// Binds renderer extraction to the device and normalized runtime assets.
  RenderSystem(GraphicsDevice& device, const content::AssetRegistry& assets)
      : device_(device), assets_(&assets) {}

  /// Extracts the world/scene for one frame and submits render data.
  void update(ecs::World& world, scene::Scene& scene, float dt, float interpolation_alpha);
  /// Uploads and pins selected assets until `releasePrewarm` is called.
  RenderPrewarmHandle prewarmAssets(const std::vector<std::string>& mesh_keys,
                                    const std::vector<std::string>& material_keys,
                                    const std::vector<std::string>& texture_keys = {});
  /// Uploads and pins renderer-facing assets from a loaded package handle.
  RenderPrewarmHandle prewarmPackage(const karma::content::AssetPackageHandle& package);
  /// Releases assets pinned by a previous prewarm call.
  bool releasePrewarm(RenderPrewarmHandle handle);

 private:
  struct InstancedLodRecord {
    float start_distance = 0.0f;
    std::string mesh_asset_key;
    std::vector<geometry::MeshMaterialSlot> material_slots;
    std::vector<components::MeshMaterialAssignment> component_materials;
    std::vector<std::string> acquired_material_keys;
    std::vector<renderer::DrawMaterialBinding> material_bindings;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    renderer::InstanceLodRenderMode render_mode = renderer::InstanceLodRenderMode::Mesh;
    bool shadow_visible = false;
  };

  struct RenderRecord {
    std::string mesh_asset_key;
    std::vector<geometry::MeshMaterialSlot> material_slots;
    std::vector<components::MeshMaterialAssignment> component_materials;
    std::vector<std::string> acquired_material_keys;
    std::vector<renderer::DrawMaterialBinding> material_bindings;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    renderer::InstanceGpuLayout cached_instance_layout =
        renderer::InstanceGpuLayout::Matrix4x4Params;
    uint64_t cached_instance_revision = UINT64_MAX;
    size_t cached_instance_count = 0;
    bool cached_instance_dynamic = false;
    bool cached_instance_bounds_valid = false;
    glm::vec3 cached_instance_bounds_center{0.0f};
    float cached_instance_bounds_radius = 0.0f;
    std::vector<renderer::InstanceData> cached_instances;
    std::vector<renderer::PlanarInstanceData> cached_planar_instances;
    std::vector<InstancedLodRecord> instanced_lods;
  };

  struct SharedMeshResource {
    renderer::MeshId mesh = renderer::kInvalidMesh;
    uint32_t ref_count = 0;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    bool owned_by_render_system = false;
  };

  struct SharedMaterialResource {
    renderer::MaterialId material = renderer::kInvalidMaterial;
    uint32_t ref_count = 0;
    std::vector<std::string> texture_asset_keys;
  };

  struct SharedMaterialAlias {
    std::string fingerprint;
    uint32_t ref_count = 0;
  };

  struct SharedTextureResource {
    renderer::TextureId texture = renderer::kInvalidTexture;
    uint32_t ref_count = 0;
  };

  struct PrewarmRecord {
    std::vector<std::string> mesh_asset_keys;
    std::vector<std::string> material_keys;
    std::vector<std::string> texture_keys;
  };

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }
  static uint64_t instancedEntityKey(ecs::Entity entity) {
    return entityKey(entity) | (uint64_t{1} << 63);
  }
  static ecs::Entity entityFromKey(uint64_t key) {
    ecs::Entity entity{};
    entity.index = static_cast<uint32_t>(key >> 32);
    entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
    return entity;
  }
  static ecs::Entity entityFromInstancedKey(uint64_t key) {
    return entityFromKey(key & ~(uint64_t{1} << 63));
  }

  void releaseRecord(uint64_t key, RenderRecord& record);
  void cleanupStaleRecords(ecs::World& world);
  void cleanupStaleInstancedRecords(ecs::World& world);
  void releaseMeshBinding(RenderRecord& record);
  void releaseMaterialBinding(RenderRecord& record);
  void releaseInstancedLodBindings(RenderRecord& record);
  bool syncInstancedLodBindings(const components::InstancedMeshComponent& instanced,
                                RenderRecord& record);
  void bindMesh(const components::MeshComponent& mesh, RenderRecord& record);
  void bindMesh(const std::string& mesh_asset_key, RenderRecord& record);
  void bindMaterial(const components::MeshComponent& mesh, RenderRecord& record);
  void bindMaterial(const std::vector<components::MeshMaterialAssignment>& materials,
                    RenderRecord& record);
  void acquireSharedMesh(const std::string& mesh_asset_key, RenderRecord& record);
  void releaseSharedMesh(const std::string& mesh_asset_key);
  renderer::MaterialId acquireSharedMaterial(const std::string& material_key);
  void releaseSharedMaterial(const std::string& material_key);
  renderer::TextureId acquireSharedTexture(const std::string& texture_key);
  void releaseSharedTexture(const std::string& texture_key);

  GraphicsDevice& device_;
  const content::AssetRegistry* assets_ = nullptr;
  std::unordered_map<uint64_t, RenderRecord> records_;
  std::unordered_map<uint64_t, RenderRecord> instanced_records_;
  std::unordered_map<std::string, SharedMeshResource> shared_meshes_;
  std::unordered_map<std::string, SharedMaterialResource> shared_materials_;
  std::unordered_map<std::string, SharedMaterialAlias> shared_material_aliases_;
  std::unordered_map<std::string, SharedTextureResource> shared_textures_;
  std::unordered_map<uint64_t, PrewarmRecord> prewarm_records_;
  std::unordered_map<std::string, renderer::RenderTargetId> render_targets_by_key_;
  std::unordered_map<std::string, bool> warned_missing_mesh_asset_keys_;
  std::unordered_map<std::string, bool> warned_missing_material_keys_;
  std::unordered_map<std::string, bool> warned_missing_environment_map_keys_;
  uint64_t last_asset_registry_version_ = 0;
  std::string last_env_path_;
  float last_env_intensity_ = -1.0f;
  bool last_env_draw_skybox_ = false;
  bool warned_no_camera_ = false;
  uint64_t next_prewarm_id_ = 1u;
};

}  // namespace karma::renderer
