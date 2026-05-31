#pragma once

#include <cstdint>
#include <unordered_map>

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::volumes {

class VolumeSphereSystem {
 public:
  explicit VolumeSphereSystem(renderer::GraphicsDevice* device);
  ~VolumeSphereSystem();

  VolumeSphereSystem(const VolumeSphereSystem&) = delete;
  VolumeSphereSystem& operator=(const VolumeSphereSystem&) = delete;

  void update(ecs::World& world, float dt, float interpolation_alpha);

 private:
  struct RuntimeState {
    ecs::Entity proxy{};
    renderer::MaterialId material = renderer::kInvalidMaterial;
  };

  void ensureSharedResources();
  void destroySharedResources();
  void destroyRuntimeState(ecs::World& world, RuntimeState& state);
  RuntimeState& ensureRuntimeState(ecs::World& world, ecs::Entity source);

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  renderer::GraphicsDevice* device_ = nullptr;
  renderer::MeshId overlay_mesh_ = renderer::kInvalidMesh;
  std::unordered_map<uint64_t, RuntimeState> runtime_;
};

}  // namespace karma::volumes
