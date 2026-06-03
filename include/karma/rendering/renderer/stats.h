#pragma once

#include <cstdint>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Forward+ light-culling diagnostics.
struct ForwardPlusStats {
  uint32_t tile_size = 16;
  uint32_t max_lights_per_tile = 128;
  uint32_t max_local_lights = 4096;
  uint32_t tiles_x = 0;
  uint32_t tiles_y = 0;
  uint32_t local_light_count = 0;
  bool active = false;
  bool cpu_fallback = false;
  bool overflow_risk = false;
};

/// \ingroup karma_rendering
/// Particle simulation and renderer-pass diagnostics.
///
/// These fields are intentionally public so examples and perf logs can report
/// comparable counters without parsing backend internals.
struct ParticlePassStats {
  uint32_t effect_binding_updates = 0;
  uint32_t simulated_emitters = 0;
  uint32_t visible_emitters = 0;
  uint32_t culled_emitters = 0;
  uint32_t submitted_emitters = 0;
  uint32_t simulated_particles = 0;
  uint32_t packed_particles = 0;
  uint32_t culled_particles = 0;
  uint32_t ground_collision_particles = 0;
  uint32_t submitted_batches = 0;
  uint32_t submitted_particles = 0;
  uint32_t additive_batches = 0;
  uint32_t additive_particles = 0;
  uint32_t alpha_batches = 0;
  uint32_t alpha_particles = 0;
  uint32_t distortion_batches = 0;
  uint32_t distortion_particles = 0;
  uint32_t additive_draw_calls = 0;
  uint32_t alpha_draw_calls = 0;
  uint32_t distortion_draw_calls = 0;
  uint32_t alpha_sorted_particles = 0;
  uint32_t distortion_sorted_particles = 0;
  uint32_t alpha_invalid_depth_particles = 0;
  uint32_t distortion_invalid_depth_particles = 0;
  uint32_t pre_particle_scene_sample_draws = 0;
  uint32_t post_particle_scene_sample_draws = 0;
  float sync_effect_bindings_ms = 0.0f;
  float simulation_ms = 0.0f;
  float packing_ms = 0.0f;
  float additive_grouping_ms = 0.0f;
  float alpha_sort_ms = 0.0f;
  float distortion_sort_ms = 0.0f;
  float alpha_collect_ms = 0.0f;
  float alpha_sort_only_ms = 0.0f;
  float alpha_span_ms = 0.0f;
  float distortion_collect_ms = 0.0f;
  float distortion_sort_only_ms = 0.0f;
  float distortion_span_ms = 0.0f;
  float draw_submission_ms = 0.0f;
  bool scene_color_copy = false;
  bool post_particle_scene_color_copy = false;
  bool alpha_half_res = false;
  bool distortion_present = false;
};

}  // namespace karma::renderer
