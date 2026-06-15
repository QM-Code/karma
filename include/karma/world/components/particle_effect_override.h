#pragma once

#include <optional>
#include <string>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/world/ecs/component.h"
#include "karma/world/components/particle_emitter.h"

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
  std::optional<std::string> texture_key;
  std::optional<ParticleSourceShape> source_shape;
  std::optional<math::Vec3> source_box_extents;
  std::optional<math::Vec3> source_dimensions;
  std::optional<float> source_radius_min;
  std::optional<float> source_radius_max;
  std::optional<float> source_inner_radius;
  std::optional<float> source_outer_radius;
  std::optional<float> source_height;
  std::optional<float> source_angle;
  std::optional<std::vector<math::Vec3>> source_path_points;
  std::optional<bool> source_closed_loop;
  std::optional<ParticleSourceSamplingMode> source_sampling;
  std::optional<float> source_jitter_radius;
  std::optional<std::string> source_mesh_key;
  std::optional<std::string> source_mesh_path;
  std::optional<ParticleSourceDistribution> source_distribution;
};

}  // namespace karma::components
