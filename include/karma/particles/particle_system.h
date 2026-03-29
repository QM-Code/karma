#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "karma/ecs/entity.h"
#include "karma/ecs/world.h"
#include "karma/math/types.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::particles {

class ParticleLibrary;

class ParticleSystem {
 public:
  explicit ParticleSystem(renderer::GraphicsDevice* device,
                          ParticleLibrary* library = nullptr)
      : device_(device), library_(library) {}

  void update(ecs::World& world, float dt, float interpolation_alpha);

 private:
  uint32_t syncEffectBindings(ecs::World& world);

  struct Particle {
    math::Vec3 position{};
    math::Vec3 velocity{};
    float age = 0.0f;
    float lifetime = 1.0f;
    float start_size = 0.1f;
    float end_size = 0.0f;
    math::Color start_color{};
    math::Color end_color{};
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    uint32_t frame_offset = 0u;
    bool resting_on_ground = false;
  };

  struct EmitterState {
    std::vector<Particle> particles;
    float spawn_accumulator = 0.0f;
    float elapsed = 0.0f;
    uint32_t rng_state = 1u;
    uint32_t max_particles = 0u;
    bool burst_emitted = false;
    bool initialized = false;
  };

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  renderer::GraphicsDevice* device_ = nullptr;
  ParticleLibrary* library_ = nullptr;
  std::unordered_map<uint64_t, EmitterState> emitters_;
};

}  // namespace karma::particles
