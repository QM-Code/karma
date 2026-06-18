#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "karma/world/components/mesh.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/material_library.h"
#include "karma/rendering/renderer/post_process_profile_library.h"
#include "karma/world/scene/scene.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// Extracts ECS render data and submits it to `GraphicsDevice`.
///
/// `RenderSystem` resolves mesh/material keys, maintains shared renderer
/// resources, extracts cameras/lights/environment, resolves camera-selected
/// post-process profiles, submits offscreen camera passes, submits the primary
/// camera pass, and cleans up renderer resources for destroyed entities.
class RenderSystem {
 public:
  /// Binds renderer extraction to the device and shared resource registries.
  ///
  /// `post_process_profiles` must outlive the render system; `EngineApp` owns
  /// both and wires this dependency during startup.
  RenderSystem(GraphicsDevice& device,
               const MaterialLibrary& material_library,
               const PostProcessProfileLibrary& post_process_profiles)
      : device_(device),
        material_library_(&material_library),
        post_process_profiles_(&post_process_profiles) {}

  /// Extracts the world/scene for one frame and submits render data.
  void update(ecs::World& world, scene::Scene& scene, float dt, float interpolation_alpha);

 private:
  struct RenderRecord {
    std::string mesh_key;
    std::vector<geometry::MeshMaterialSlot> material_slots;
    std::vector<components::MeshMaterialBinding> component_materials;
    std::vector<std::string> acquired_material_keys;
    std::vector<renderer::DrawMaterialBinding> material_bindings;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
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
  };

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }
  static ecs::Entity entityFromKey(uint64_t key) {
    ecs::Entity entity{};
    entity.index = static_cast<uint32_t>(key >> 32);
    entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
    return entity;
  }

  void releaseRecord(uint64_t key, RenderRecord& record);
  void cleanupStaleRecords(ecs::World& world);
  void releaseMeshBinding(RenderRecord& record);
  void releaseMaterialBinding(RenderRecord& record);
  void bindMesh(const components::MeshComponent& mesh, RenderRecord& record);
  void bindMaterial(const components::MeshComponent& mesh, RenderRecord& record);
  void acquireSharedMesh(const std::string& mesh_key, RenderRecord& record);
  void releaseSharedMesh(const std::string& mesh_key);
  renderer::MaterialId acquireSharedMaterial(const std::string& material_key);
  void releaseSharedMaterial(const std::string& material_key);

  GraphicsDevice& device_;
  const MaterialLibrary* material_library_ = nullptr;
  const PostProcessProfileLibrary* post_process_profiles_ = nullptr;
  std::unordered_map<uint64_t, RenderRecord> records_;
  std::unordered_map<std::string, SharedMeshResource> shared_meshes_;
  std::unordered_map<std::string, SharedMaterialResource> shared_materials_;
  std::unordered_map<std::string, renderer::RenderTargetId> render_targets_by_key_;
  std::unordered_map<std::string, bool> warned_missing_material_keys_;
  uint64_t last_material_library_version_ = 0;
  std::string last_env_path_;
  float last_env_intensity_ = -1.0f;
  bool last_env_draw_skybox_ = false;
  bool warned_no_camera_ = false;
};

}  // namespace karma::renderer
