#pragma once

#include <optional>

#include "karma/ecs/component.h"
#include "karma/renderer/types.h"

namespace karma::components {

struct ParticleEffectOverrideComponent : ecs::ComponentTag {
  bool active = true;
  float time_scale = 1.0f;
  float spawn_rate_scale = 1.0f;
  float lifetime_scale = 1.0f;
  float size_scale = 1.0f;
  float radius_scale = 1.0f;
  float velocity_scale = 1.0f;
  float angular_velocity_scale = 1.0f;
  float alpha_scale = 1.0f;
  std::optional<math::Color> start_color;
  std::optional<math::Color> end_color;
  std::optional<renderer::TextureId> texture;
};

}  // namespace karma::components
