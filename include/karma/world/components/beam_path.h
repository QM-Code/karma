#pragma once

#include <cstdint>
#include <vector>

#include "karma/world/ecs/component.h"
#include "karma/core/math/types.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::components {

struct BeamPathComponent : ecs::ComponentTag {
  std::vector<math::Vec3> points;
  math::Color core_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color glow_color{1.0f, 0.18f, 0.16f, 1.0f};
  float core_radius = 0.05f;
  float glow_radius = 0.14f;
  float core_intensity = 2.0f;
  float glow_intensity = 1.35f;
  float endpoint_core_size = 0.32f;
  float endpoint_glow_size = 0.78f;
  uint32_t light_count = 0;
  float light_intensity = 1.4f;
  float light_range = 3.0f;
  float light_spacing = 0.0f;
  float electric_intensity = 0.0f;
  float electric_size = 0.12f;
  float electric_spacing = 0.34f;
  float electric_jitter_radius = 0.06f;
  float electric_speed = 1.0f;
  float distortion_intensity = 0.0f;
  float distortion_size = 0.22f;
  float distortion_spacing = 0.40f;
  float distortion_jitter_radius = 0.05f;
  float distortion_strength = 3.0f;
  float distortion_soft_particle_distance = 0.0f;
  float distortion_speed = 1.0f;
  renderer::LayerId layer = 0;
  bool visible = true;
  bool depth_test = true;
  bool closed_loop = false;
  bool world_space = false;
  bool endpoint_flares = true;
};

}  // namespace karma::components
