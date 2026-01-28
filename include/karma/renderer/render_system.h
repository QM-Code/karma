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

  struct MeshBounds {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    bool valid = false;
  };

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  GraphicsDevice& device_;
  std::unordered_map<uint64_t, RenderRecord> records_;
  std::unordered_map<std::string, MeshBounds> bounds_cache_;
  std::string last_env_path_;
  float last_env_intensity_ = -1.0f;
  bool warned_no_camera_ = false;
};

}  // namespace karma::renderer
