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
#include "karma/scene/scene.h"

namespace karma::renderer {

class RenderSystem {
 public:
  explicit RenderSystem(GraphicsDevice& device) : device_(device) {}

  void update(ecs::World& world, scene::Scene& scene, float dt);

 private:
  struct RenderRecord {
    std::string mesh_key;
    std::string material_key;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    renderer::MaterialId material = renderer::kInvalidMaterial;
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

  GraphicsDevice& device_;
  std::unordered_map<uint64_t, RenderRecord> records_;
  std::unordered_map<std::string, SharedMeshResource> shared_meshes_;
  std::unordered_map<std::string, renderer::RenderTargetId> render_targets_by_key_;
  std::string last_env_path_;
  float last_env_intensity_ = -1.0f;
  bool last_env_draw_skybox_ = false;
  bool warned_no_camera_ = false;
};

}  // namespace karma::renderer
