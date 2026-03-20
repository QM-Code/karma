#pragma once

#include <cstdint>
#include <string>

#include "karma/ecs/component.h"

namespace karma::components {

struct ParticleEffectComponent : ecs::ComponentTag {
  std::string effect_key;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  uint32_t restart_count = 0;
  uint64_t applied_version = 0;
  uint64_t applied_override_hash = 0;
  uint32_t applied_restart_count = 0;
  std::string applied_effect_key;
};

}  // namespace karma::components
