#pragma once

#include <string>
#include <vector>

#include "karma/world/components/particle_emitter.h"

namespace karma::particles {

/// \ingroup karma_particles
/// Canonical v3 emitter authoring record parsed from `.kpeffect` JSON.
struct ParticleEmitterDesc {
  components::ParticleEmitterComponent emitter{};
  std::string texture_key;
};

/// \ingroup karma_particles
/// Canonical v3 particle effect asset.
struct ParticleEffectAsset {
  std::vector<ParticleEmitterDesc> emitters;

  const ParticleEmitterDesc* primaryEmitter() const {
    return emitters.empty() ? nullptr : &emitters.front();
  }
};

using ParticleEffectDesc = ParticleEffectAsset;

}  // namespace karma::particles
