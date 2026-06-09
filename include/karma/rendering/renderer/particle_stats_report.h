#pragma once

#include "karma/rendering/renderer/stats.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Accumulated particle statistics over a reporting window.
struct ParticleStatsReport {
  ParticlePassStats totals{};
  uint32_t frame_count = 0;
  double elapsed_seconds = 0.0;
};

/// \ingroup karma_rendering
/// Adds one frame of particle statistics into an aggregate report.
inline void accumulateParticleStats(ParticlePassStats& totals,
                                    const ParticlePassStats& frame) {
  totals.effect_binding_updates += frame.effect_binding_updates;
  totals.simulated_emitters += frame.simulated_emitters;
  totals.visible_emitters += frame.visible_emitters;
  totals.culled_emitters += frame.culled_emitters;
  totals.submitted_emitters += frame.submitted_emitters;
  totals.simulated_particles += frame.simulated_particles;
  totals.packed_particles += frame.packed_particles;
  totals.culled_particles += frame.culled_particles;
  totals.ground_collision_particles += frame.ground_collision_particles;
  totals.submitted_batches += frame.submitted_batches;
  totals.submitted_particles += frame.submitted_particles;
  totals.additive_batches += frame.additive_batches;
  totals.additive_particles += frame.additive_particles;
  totals.alpha_batches += frame.alpha_batches;
  totals.alpha_particles += frame.alpha_particles;
  totals.distortion_batches += frame.distortion_batches;
  totals.distortion_particles += frame.distortion_particles;
  totals.additive_draw_calls += frame.additive_draw_calls;
  totals.alpha_draw_calls += frame.alpha_draw_calls;
  totals.distortion_draw_calls += frame.distortion_draw_calls;
  totals.alpha_sorted_particles += frame.alpha_sorted_particles;
  totals.distortion_sorted_particles += frame.distortion_sorted_particles;
  totals.alpha_invalid_depth_particles += frame.alpha_invalid_depth_particles;
  totals.distortion_invalid_depth_particles += frame.distortion_invalid_depth_particles;
  totals.pre_particle_scene_sample_draws += frame.pre_particle_scene_sample_draws;
  totals.post_particle_scene_sample_draws += frame.post_particle_scene_sample_draws;
  totals.gpu_particle_capacity += frame.gpu_particle_capacity;
  totals.gpu_alive_particles += frame.gpu_alive_particles;
  totals.gpu_dead_particles += frame.gpu_dead_particles;
  totals.gpu_spawned_particles += frame.gpu_spawned_particles;
  totals.gpu_killed_particles += frame.gpu_killed_particles;
  totals.gpu_compacted_particles += frame.gpu_compacted_particles;
  totals.gpu_compute_dispatches += frame.gpu_compute_dispatches;
  totals.gpu_indirect_draws += frame.gpu_indirect_draws;
  totals.gpu_indirect_dispatches += frame.gpu_indirect_dispatches;
  totals.gpu_sort_key_count += frame.gpu_sort_key_count;
  totals.gpu_sort_passes += frame.gpu_sort_passes;
  totals.gpu_buffer_resizes += frame.gpu_buffer_resizes;
  totals.gpu_stats_readback_age += frame.gpu_stats_readback_age;
  totals.gpu_allocator_live_emitters += frame.gpu_allocator_live_emitters;
  totals.gpu_allocator_free_ranges += frame.gpu_allocator_free_ranges;
  totals.gpu_allocator_active_capacity += frame.gpu_allocator_active_capacity;
  totals.gpu_allocator_high_water_capacity += frame.gpu_allocator_high_water_capacity;
  totals.gpu_allocator_retired_emitters += frame.gpu_allocator_retired_emitters;
  totals.gpu_allocator_reused_slots += frame.gpu_allocator_reused_slots;
  totals.gpu_allocator_allocation_failures += frame.gpu_allocator_allocation_failures;
  totals.gpu_culled_emitters += frame.gpu_culled_emitters;
  totals.gpu_culled_particles += frame.gpu_culled_particles;
  totals.gpu_culling_dispatches += frame.gpu_culling_dispatches;
  totals.cpu_fallback_particles += frame.cpu_fallback_particles;
  totals.sync_effect_bindings_ms += frame.sync_effect_bindings_ms;
  totals.simulation_ms += frame.simulation_ms;
  totals.packing_ms += frame.packing_ms;
  totals.additive_grouping_ms += frame.additive_grouping_ms;
  totals.alpha_sort_ms += frame.alpha_sort_ms;
  totals.distortion_sort_ms += frame.distortion_sort_ms;
  totals.alpha_collect_ms += frame.alpha_collect_ms;
  totals.alpha_sort_only_ms += frame.alpha_sort_only_ms;
  totals.alpha_span_ms += frame.alpha_span_ms;
  totals.distortion_collect_ms += frame.distortion_collect_ms;
  totals.distortion_sort_only_ms += frame.distortion_sort_only_ms;
  totals.distortion_span_ms += frame.distortion_span_ms;
  totals.draw_submission_ms += frame.draw_submission_ms;
  totals.scene_color_copy = totals.scene_color_copy || frame.scene_color_copy;
  totals.post_particle_scene_color_copy =
      totals.post_particle_scene_color_copy || frame.post_particle_scene_color_copy;
  totals.alpha_half_res = totals.alpha_half_res || frame.alpha_half_res;
  totals.distortion_present = totals.distortion_present || frame.distortion_present;
  totals.gpu_sort_overflow = totals.gpu_sort_overflow || frame.gpu_sort_overflow;
  totals.gpu_fallback_active = totals.gpu_fallback_active || frame.gpu_fallback_active;
  totals.gpu_global_sort_active =
      totals.gpu_global_sort_active || frame.gpu_global_sort_active;
  totals.gpu_grouped_sort_fallback =
      totals.gpu_grouped_sort_fallback || frame.gpu_grouped_sort_fallback;
}

/// \ingroup karma_rendering
/// Formats a stable one-line terminal report matching the debug particle tab.
inline std::string formatParticleStatsReport(const ParticleStatsReport& report) {
  std::ostringstream stream;
  const double inv_frames =
      report.frame_count > 0u ? 1.0 / static_cast<double>(report.frame_count) : 0.0;
  const double fps = report.elapsed_seconds > 0.0
                         ? static_cast<double>(report.frame_count) / report.elapsed_seconds
                         : 0.0;
  const auto avg = [inv_frames](uint32_t value) {
    return static_cast<double>(value) * inv_frames;
  };
  const auto avg_ms = [inv_frames](float value) {
    return static_cast<double>(value) * inv_frames;
  };

  stream << std::fixed << std::setprecision(2)
         << "Particle stats: seconds=" << report.elapsed_seconds
         << " frames=" << report.frame_count
         << " fps=" << std::setprecision(1) << fps
         << " effect_binding_updates=" << avg(report.totals.effect_binding_updates)
         << " simulated_emitters=" << avg(report.totals.simulated_emitters)
         << " visible_emitters=" << avg(report.totals.visible_emitters)
         << " culled_emitters=" << avg(report.totals.culled_emitters)
         << " submitted_emitters=" << avg(report.totals.submitted_emitters)
         << " simulated_particles=" << avg(report.totals.simulated_particles)
         << " packed_particles=" << avg(report.totals.packed_particles)
         << " culled_particles=" << avg(report.totals.culled_particles)
         << " ground_collision_particles=" << avg(report.totals.ground_collision_particles)
         << " submitted_batches=" << avg(report.totals.submitted_batches)
         << " submitted_particles=" << avg(report.totals.submitted_particles)
         << " additive_batches=" << avg(report.totals.additive_batches)
         << " alpha_batches=" << avg(report.totals.alpha_batches)
         << " distortion_batches=" << avg(report.totals.distortion_batches)
         << " additive_particles=" << avg(report.totals.additive_particles)
         << " alpha_particles=" << avg(report.totals.alpha_particles)
         << " distortion_particles=" << avg(report.totals.distortion_particles)
         << " additive_draw_calls=" << avg(report.totals.additive_draw_calls)
         << " alpha_draw_calls=" << avg(report.totals.alpha_draw_calls)
         << " distortion_draw_calls=" << avg(report.totals.distortion_draw_calls)
         << " alpha_sorted_particles=" << avg(report.totals.alpha_sorted_particles)
         << " distortion_sorted_particles=" << avg(report.totals.distortion_sorted_particles)
         << " alpha_invalid_depth_particles=" << avg(report.totals.alpha_invalid_depth_particles)
         << " distortion_invalid_depth_particles="
         << avg(report.totals.distortion_invalid_depth_particles)
         << " pre_particle_scene_sample_draws="
         << avg(report.totals.pre_particle_scene_sample_draws)
         << " post_particle_scene_sample_draws="
         << avg(report.totals.post_particle_scene_sample_draws)
         << " gpu_particle_capacity=" << avg(report.totals.gpu_particle_capacity)
         << " gpu_alive_particles=" << avg(report.totals.gpu_alive_particles)
         << " gpu_dead_particles=" << avg(report.totals.gpu_dead_particles)
         << " gpu_spawned_particles=" << avg(report.totals.gpu_spawned_particles)
         << " gpu_killed_particles=" << avg(report.totals.gpu_killed_particles)
         << " gpu_compacted_particles=" << avg(report.totals.gpu_compacted_particles)
         << " gpu_compute_dispatches=" << avg(report.totals.gpu_compute_dispatches)
         << " gpu_indirect_draws=" << avg(report.totals.gpu_indirect_draws)
         << " gpu_indirect_dispatches=" << avg(report.totals.gpu_indirect_dispatches)
         << " gpu_sort_key_count=" << avg(report.totals.gpu_sort_key_count)
         << " gpu_sort_passes=" << avg(report.totals.gpu_sort_passes)
         << " gpu_buffer_resizes=" << avg(report.totals.gpu_buffer_resizes)
         << " gpu_stats_readback_age=" << avg(report.totals.gpu_stats_readback_age)
         << " gpu_allocator_live_emitters="
         << avg(report.totals.gpu_allocator_live_emitters)
         << " gpu_allocator_free_ranges="
         << avg(report.totals.gpu_allocator_free_ranges)
         << " gpu_allocator_active_capacity="
         << avg(report.totals.gpu_allocator_active_capacity)
         << " gpu_allocator_high_water_capacity="
         << avg(report.totals.gpu_allocator_high_water_capacity)
         << " gpu_allocator_retired_emitters="
         << avg(report.totals.gpu_allocator_retired_emitters)
         << " gpu_allocator_reused_slots="
         << avg(report.totals.gpu_allocator_reused_slots)
         << " gpu_allocator_allocation_failures="
         << avg(report.totals.gpu_allocator_allocation_failures)
         << " gpu_culled_emitters=" << avg(report.totals.gpu_culled_emitters)
         << " gpu_culled_particles=" << avg(report.totals.gpu_culled_particles)
         << " gpu_culling_dispatches=" << avg(report.totals.gpu_culling_dispatches)
         << " cpu_fallback_particles=" << avg(report.totals.cpu_fallback_particles)
         << std::setprecision(3)
         << " sync_effect_bindings_ms=" << avg_ms(report.totals.sync_effect_bindings_ms)
         << " simulation_ms=" << avg_ms(report.totals.simulation_ms)
         << " packing_ms=" << avg_ms(report.totals.packing_ms)
         << " additive_grouping_ms=" << avg_ms(report.totals.additive_grouping_ms)
         << " alpha_collect_ms=" << avg_ms(report.totals.alpha_collect_ms)
         << " alpha_sort_only_ms=" << avg_ms(report.totals.alpha_sort_only_ms)
         << " alpha_span_ms=" << avg_ms(report.totals.alpha_span_ms)
         << " alpha_sort_ms=" << avg_ms(report.totals.alpha_sort_ms)
         << " distortion_collect_ms=" << avg_ms(report.totals.distortion_collect_ms)
         << " distortion_sort_only_ms=" << avg_ms(report.totals.distortion_sort_only_ms)
         << " distortion_span_ms=" << avg_ms(report.totals.distortion_span_ms)
         << " distortion_sort_ms=" << avg_ms(report.totals.distortion_sort_ms)
         << " draw_submission_ms=" << avg_ms(report.totals.draw_submission_ms)
         << " scene_color_copy=" << (report.totals.scene_color_copy ? "true" : "false")
         << " post_particle_scene_color_copy="
         << (report.totals.post_particle_scene_color_copy ? "true" : "false")
         << " alpha_half_res=" << (report.totals.alpha_half_res ? "true" : "false")
         << " distortion_present=" << (report.totals.distortion_present ? "true" : "false")
         << " gpu_sort_overflow=" << (report.totals.gpu_sort_overflow ? "true" : "false")
         << " gpu_fallback_active=" << (report.totals.gpu_fallback_active ? "true" : "false")
         << " gpu_global_sort_active="
         << (report.totals.gpu_global_sort_active ? "true" : "false")
         << " gpu_grouped_sort_fallback="
         << (report.totals.gpu_grouped_sort_fallback ? "true" : "false");
  return stream.str();
}

}  // namespace karma::renderer
