#pragma once

#include "karma/ecs/component.h"
#include "karma/math/types.h"

namespace karma::components {

struct VolumeSphereComponent : ecs::ComponentTag {
  math::Color color{0.18f, 0.82f, 1.0f, 1.0f};
  math::Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
  float radius = 1.0f;
  float center_opacity = 0.5f;
  float distortion_strength = 0.0f;
  float noise_strength = 1.0f;
  float overlay_depth = 0.12f;
  bool visible = true;
  bool scale_with_transform = true;
};

}  // namespace karma::components
