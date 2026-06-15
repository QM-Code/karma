#pragma once

#include <cstddef>
#include <cstdint>

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::particles {

class ParticleLibrary;

/// \ingroup karma_particles
/// Runtime particle binding and renderer submission system.
///
/// The system consumes `ParticleEmitterComponent`, `ParticleEffectComponent`,
/// and `ParticleEffectOverrideComponent`. Live particle state is owned by the
/// renderer backend for GPU-first effects.
class ParticleSystem {
 public:
  explicit ParticleSystem(renderer::GraphicsDevice* device,
                          ParticleLibrary* library = nullptr)
      : device_(device), library_(library) {}

  /// Updates effect bindings and submits emitter descriptors.
  void update(ecs::World& world, float dt, float interpolation_alpha);
  /// Returns current feature-owned live particle count for one entity.
  std::size_t liveParticleCount(ecs::Entity entity) const;

 private:
  uint32_t syncEffectBindings(ecs::World& world);

  renderer::GraphicsDevice* device_ = nullptr;
  ParticleLibrary* library_ = nullptr;
};

}  // namespace karma::particles
