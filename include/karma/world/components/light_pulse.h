#pragma once

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Time-based light intensity/range envelope.
///
/// `LightPulseSystem` updates the paired `LightComponent` and can hide the
/// entity when the pulse completes.
struct LightPulseComponent : ecs::ComponentTag {
  bool enabled = true;
  bool active = true;
  float start_delay = 0.0f;
  float duration = 0.64f;
  float peak_intensity = 0.0f;
  float peak_range = 0.0f;
  float off_intensity = 0.0f;
  float off_range = 0.1f;
  float intensity_power = 2.0f;
  float range_power = 0.5f;
  float range_floor_factor = 0.35f;
  bool hide_after_completion = true;
  float elapsed = 0.0f;
};

}  // namespace karma::components
