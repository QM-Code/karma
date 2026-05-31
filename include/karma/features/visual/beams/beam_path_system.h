#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::beams {

class BeamPathSystem {
 public:
  explicit BeamPathSystem(renderer::GraphicsDevice* device);
  ~BeamPathSystem();

  BeamPathSystem(const BeamPathSystem&) = delete;
  BeamPathSystem& operator=(const BeamPathSystem&) = delete;

  void update(ecs::World& world, float dt, float interpolation_alpha);

 private:
  struct RuntimeState {
    std::vector<ecs::Entity> light_entities;
  };

  void ensureSharedResources();
  void destroySharedResources();
  void destroyRuntimeState(RuntimeState& state);
  RuntimeState& ensureRuntimeState(uint64_t beam_key);
  void resizeLightEntities(ecs::World& world, RuntimeState& state, std::size_t desired_count);

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  renderer::GraphicsDevice* device_ = nullptr;
  renderer::TextureId endpoint_texture_ = renderer::kInvalidTexture;
  renderer::TextureId electric_texture_ = renderer::kInvalidTexture;
  renderer::TextureId distortion_texture_ = renderer::kInvalidTexture;
  std::vector<std::uint8_t> endpoint_texture_pixels_;
  std::vector<std::uint8_t> electric_texture_pixels_;
  std::vector<std::uint8_t> distortion_texture_pixels_;
  std::unordered_map<uint64_t, RuntimeState> beams_;
  double time_ = 0.0;
};

}  // namespace karma::beams
