#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "karma/components/mesh.h"
#include "karma/components/transform.h"
#include "karma/components/visibility.h"
#include "karma/ecs/world.h"
#include "karma/renderer/device.h"
#include "karma/renderer/material_library.h"
#include "karma/scene/scene.h"

namespace karma::renderer {

class RenderSystem {
 public:
  RenderSystem(GraphicsDevice& device, const MaterialLibrary& material_library)
      : device_(device), material_library_(&material_library) {}

  void update(ecs::World& world, scene::Scene& scene, float dt, float interpolation_alpha);

 private:
  struct RenderRecord {
    std::string mesh_key;
    std::string material_key;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    renderer::MaterialId material = renderer::kInvalidMaterial;
    renderer::MaterialSetId material_set = renderer::kInvalidMaterialSet;
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
  };

  struct SharedMaterialVariant {
    renderer::MaterialSetId material_set = renderer::kInvalidMaterialSet;
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
  void acquireSharedMesh(const std::string& mesh_key, RenderRecord& record);
  void releaseSharedMesh(const std::string& mesh_key);
  void acquireSharedMaterialVariant(const std::string& mesh_key,
                                    const std::string& material_key,
                                    RenderRecord& record);
  void releaseSharedMaterialVariant(const std::string& mesh_key,
                                    const std::string& material_key);

  GraphicsDevice& device_;
  const MaterialLibrary* material_library_ = nullptr;
  std::unordered_map<uint64_t, RenderRecord> records_;
  std::unordered_map<std::string, SharedMeshResource> shared_meshes_;
  std::unordered_map<std::string, SharedMaterialVariant> shared_material_variants_;
  std::unordered_map<std::string, renderer::RenderTargetId> render_targets_by_key_;
  std::unordered_map<std::string, bool> warned_missing_material_keys_;
  std::unordered_map<std::string, bool> warned_material_mesh_mismatch_keys_;
  uint64_t last_material_library_version_ = 0;
  std::string last_env_path_;
  float last_env_intensity_ = -1.0f;
  bool last_env_draw_skybox_ = false;
  bool warned_no_camera_ = false;
};

}  // namespace karma::renderer
