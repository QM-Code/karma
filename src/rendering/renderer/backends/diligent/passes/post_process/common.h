#pragma once

#include "../../backend.hpp"

namespace karma::renderer_backend::post_process {

struct alignas(16) PostProcessConstants {
  float screen_params[4] = {};
  float bloom_params[4] = {};
  float tone_params[4] = {};
  float ssao_params[4] = {};
  float ssr_params[4] = {};
  float taa_params[4] = {};
  float dof_params[4] = {};
  float camera_params[4] = {};
  float mode_params[4] = {};
};

bool hasActiveEffect(const renderer::PostProcessSettings& settings);
void releasePass(PostProcessPassResources& pass);
bool passReady(const PostProcessPassResources& pass);
PostProcessConstants makeConstants(const renderer::PostProcessSettings& settings,
                                   const renderer::CameraData& camera,
                                   int width,
                                   int height,
                                   bool history_valid,
                                   bool bloom_available,
                                   double accumulated_time_seconds);

}  // namespace karma::renderer_backend::post_process
