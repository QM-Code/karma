#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "karma/rendering/renderer/ids.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::content {
class AssetRegistry;
}  // namespace karma::content

namespace karma::particles {

/// \ingroup karma_particles
/// Runtime particle binding and renderer submission system.
///
/// The system consumes `ParticleEmitterComponent`, `ParticleEffectComponent`,
/// and `ParticleEffectOverrideComponent`. Live particle state is owned by the
/// renderer backend for GPU-first effects.
class ParticleSystem {
 public:
  explicit ParticleSystem(renderer::GraphicsDevice* device,
                          const content::AssetRegistry* assets = nullptr)
      : device_(device), assets_(assets) {}
  ~ParticleSystem();

  /// Updates effect bindings and submits emitter descriptors.
  void update(ecs::World& world, float dt, float interpolation_alpha);
  /// Returns current feature-owned live particle count for one entity.
  std::size_t liveParticleCount(ecs::Entity entity) const;

 private:
  uint32_t syncEffectBindings(ecs::World& world);
  renderer::TextureId resolveTextureAsset(const std::string& texture_key);
  void releaseTextureCache();

  renderer::GraphicsDevice* device_ = nullptr;
  const content::AssetRegistry* assets_ = nullptr;
  std::unordered_map<std::string, renderer::MeshId> mesh_asset_cache_;
  std::unordered_map<std::string, renderer::TextureId> texture_asset_cache_;
  uint64_t last_texture_version_ = 0;
};

}  // namespace karma::particles
