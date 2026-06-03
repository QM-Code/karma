#pragma once

#include <optional>

#include "karma/world/ecs/component.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::components {

/// \ingroup karma_components
/// Per-instance multipliers and replacements applied to a particle effect.
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
