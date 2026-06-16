#include "common.h"

#include <algorithm>

namespace karma::renderer_backend::post_process {

bool hasActiveEffect(const renderer::PostProcessSettings& settings) {
  return settings.enabled &&
         (settings.bloom_enabled ||
          settings.tone_mapping_enabled ||
          settings.ssao_enabled ||
          settings.screen_space_reflections_enabled ||
          settings.temporal_antialiasing_enabled ||
          settings.depth_of_field_enabled);
}

void releasePass(PostProcessPassResources& pass) {
  pass.pso.Release();
  pass.srb.Release();
  pass.source_var = nullptr;
  pass.depth_var = nullptr;
  pass.bloom_var = nullptr;
  pass.history_var = nullptr;
  pass.sampler_var = nullptr;
}

bool passReady(const PostProcessPassResources& pass) {
  return pass.pso && pass.srb;
}

PostProcessConstants makeConstants(const renderer::PostProcessSettings& settings,
                                   const renderer::CameraData& camera,
                                   int width,
                                   int height,
                                   bool history_valid,
                                   bool bloom_available,
                                   double accumulated_time_seconds) {
  PostProcessConstants constants{};
  constants.screen_params[0] = static_cast<float>(width);
  constants.screen_params[1] = static_cast<float>(height);
  constants.screen_params[2] = 1.0f / static_cast<float>(std::max(width, 1));
  constants.screen_params[3] = 1.0f / static_cast<float>(std::max(height, 1));
  constants.bloom_params[0] = settings.bloom_threshold;
  constants.bloom_params[1] = settings.bloom_intensity;
  constants.bloom_params[2] = settings.bloom_radius;
  constants.bloom_params[3] =
      settings.bloom_enabled && bloom_available ? 1.0f : 0.0f;
  constants.tone_params[0] = settings.tone_exposure;
  constants.tone_params[1] = settings.tone_contrast;
  constants.tone_params[2] = settings.tone_saturation;
  constants.tone_params[3] = settings.tone_mapping_enabled ? 1.0f : 0.0f;
  constants.ssao_params[0] = settings.ssao_radius;
  constants.ssao_params[1] = settings.ssao_intensity;
  constants.ssao_params[2] = settings.ssao_power;
  constants.ssao_params[3] = settings.ssao_enabled ? 1.0f : 0.0f;
  constants.ssr_params[0] = settings.ssr_intensity;
  constants.ssr_params[1] = settings.ssr_max_roughness;
  constants.ssr_params[2] = settings.ssr_thickness;
  constants.ssr_params[3] = settings.screen_space_reflections_enabled ? 1.0f : 0.0f;
  constants.taa_params[0] = settings.taa_feedback;
  constants.taa_params[1] = settings.taa_sharpening;
  constants.taa_params[2] = history_valid ? 1.0f : 0.0f;
  constants.taa_params[3] = settings.temporal_antialiasing_enabled ? 1.0f : 0.0f;
  constants.dof_params[0] = settings.dof_focus_depth;
  constants.dof_params[1] = settings.dof_focus_range;
  constants.dof_params[2] = settings.dof_intensity;
  constants.dof_params[3] = settings.depth_of_field_enabled ? 1.0f : 0.0f;
  constants.camera_params[0] = camera.near_clip;
  constants.camera_params[1] = camera.far_clip;
  constants.camera_params[2] = camera.perspective ? 1.0f : 0.0f;
  constants.camera_params[3] = 0.0f;
  constants.mode_params[0] = 0.0f;
  constants.mode_params[1] = 0.0f;
  constants.mode_params[2] = static_cast<float>(accumulated_time_seconds);
  constants.mode_params[3] = 0.0f;
  return constants;
}

}  // namespace karma::renderer_backend::post_process
