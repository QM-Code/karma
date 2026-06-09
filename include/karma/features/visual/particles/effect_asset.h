#pragma once

#include <string>
#include <vector>

#include "karma/world/components/particle_emitter.h"

namespace karma::particles {

/// \ingroup karma_particles
/// Canonical v2 emitter authoring record parsed from `.kpeffect` JSON.
struct ParticleEmitterDesc {
  components::ParticleEmitterComponent emitter{};
  std::string texture_key;
};

/// \ingroup karma_particles
/// Canonical v2 particle effect asset.
///
/// The current gameplay binding path consumes the first emitter as the primary
/// emitter. The vector keeps the asset model ready for multi-emitter effects
/// without serializing emitter internals into prefabs.
struct ParticleEffectAsset {
  std::vector<ParticleEmitterDesc> emitters;

  const ParticleEmitterDesc* primaryEmitter() const {
    return emitters.empty() ? nullptr : &emitters.front();
  }
};

using ParticleEffectDesc = ParticleEmitterDesc;

}  // namespace karma::particles
