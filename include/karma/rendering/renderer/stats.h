#pragma once

#include <cstdint>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Renderer command counters reported by the active graphics backend.
///
/// Counters are backend lifetime totals unless a backend explicitly documents a
/// different reset policy. They are intended for diagnostics and performance
/// overlays, not gameplay decisions.
struct RendererCommandStats {
  uint32_t set_pipeline_state = 0;
  uint32_t commit_shader_resources = 0;
  uint32_t set_vertex_buffers = 0;
  uint32_t set_index_buffer = 0;
  uint32_t set_render_targets = 0;
  uint32_t set_viewports = 0;
  uint32_t set_scissor_rects = 0;
  uint32_t clear_render_target = 0;
  uint32_t clear_depth_stencil = 0;
  uint32_t draw = 0;
  uint32_t draw_indexed = 0;
  uint32_t draw_indirect = 0;
  uint32_t draw_indexed_indirect = 0;
  uint32_t multi_draw = 0;
  uint32_t multi_draw_indexed = 0;
  uint32_t dispatch_compute = 0;
  uint32_t dispatch_compute_indirect = 0;
  uint32_t draw_mesh = 0;
  uint32_t draw_mesh_indirect = 0;
  uint32_t trace_rays = 0;
  uint32_t trace_rays_indirect = 0;
  uint32_t update_buffer = 0;
  uint32_t copy_buffer = 0;
  uint32_t map_buffer = 0;
  uint32_t update_texture = 0;
  uint32_t copy_texture = 0;
  uint32_t map_texture_subresource = 0;
  uint32_t begin_query = 0;
  uint32_t generate_mips = 0;
  uint32_t resolve_texture_subresource = 0;
  uint32_t total_triangles = 0;
  uint32_t total_lines = 0;
  uint32_t total_points = 0;
};

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
  uint32_t gpu_particle_capacity = 0;
  uint32_t gpu_alive_particles = 0;
  uint32_t gpu_dead_particles = 0;
  uint32_t gpu_spawned_particles = 0;
  uint32_t gpu_killed_particles = 0;
  uint32_t gpu_compacted_particles = 0;
  uint32_t gpu_compute_dispatches = 0;
  uint32_t gpu_indirect_draws = 0;
  uint32_t gpu_indirect_dispatches = 0;
  uint32_t gpu_sort_key_count = 0;
  uint32_t gpu_sort_passes = 0;
  uint32_t gpu_buffer_resizes = 0;
  uint32_t gpu_stats_readback_age = 0;
  uint32_t gpu_allocator_live_emitters = 0;
  uint32_t gpu_allocator_free_ranges = 0;
  uint32_t gpu_allocator_active_capacity = 0;
  uint32_t gpu_allocator_high_water_capacity = 0;
  uint32_t gpu_allocator_retired_emitters = 0;
  uint32_t gpu_allocator_reused_slots = 0;
  uint32_t gpu_allocator_allocation_failures = 0;
  uint32_t gpu_culled_emitters = 0;
  uint32_t gpu_culled_particles = 0;
  uint32_t gpu_culling_dispatches = 0;
  uint32_t cpu_fallback_particles = 0;
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
  bool gpu_sort_overflow = false;
  bool gpu_fallback_active = false;
  bool gpu_global_sort_active = false;
  bool gpu_grouped_sort_fallback = false;
};

}  // namespace karma::renderer
