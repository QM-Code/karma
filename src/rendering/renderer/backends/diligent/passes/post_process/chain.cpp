#include "common.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <cmath>

namespace karma::rendering::backend {
namespace {

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

bool temporalCameraChanged(const rendering::CameraData& previous,
                           const rendering::CameraData& current) {
  const auto changed = [](float a, float b, float epsilon = 1.0e-5f) {
    return std::abs(a - b) > epsilon;
  };
  const glm::vec3 position_delta = previous.position - current.position;
  const float rotation_dot =
      std::min(std::abs(glm::dot(previous.rotation, current.rotation)), 1.0f);
  return glm::dot(position_delta, position_delta) > 1.0e-8f ||
         1.0f - rotation_dot > 1.0e-6f ||
         previous.perspective != current.perspective ||
         changed(previous.fov_y_degrees, current.fov_y_degrees) ||
         changed(previous.aspect, current.aspect) ||
         changed(previous.near_clip, current.near_clip) ||
         changed(previous.far_clip, current.far_clip) ||
         changed(previous.ortho_left, current.ortho_left) ||
         changed(previous.ortho_right, current.ortho_right) ||
         changed(previous.ortho_top, current.ortho_top) ||
         changed(previous.ortho_bottom, current.ortho_bottom);
}

}  // namespace

bool DiligentBackend::runPostProcessPass(PostProcessPassResources& pass,
                                         Diligent::ITextureView* source_srv,
                                         Diligent::ITextureView* depth_srv,
                                         Diligent::ITextureView* bloom_srv,
                                         Diligent::ITextureView* history_srv,
                                         Diligent::ITextureView* target_rtv,
                                         int width,
                                         int height,
                                         bool history_valid) {
  if (!context_ || !pass.pso || !pass.srb || !post_process_cb_ ||
      !source_srv || !target_rtv ||
      width <= 0 || height <= 0) {
    return false;
  }

  ensureParticleFallbackDepthResource();
  Diligent::ITextureView* active_depth_srv = depth_srv ? depth_srv : particle_fallback_depth_srv_;
  Diligent::ITextureView* active_bloom_srv = bloom_srv ? bloom_srv : source_srv;
  Diligent::ITextureView* active_history_srv = history_srv ? history_srv : source_srv;
  if ((pass.depth_var && !active_depth_srv) ||
      (pass.bloom_var && !active_bloom_srv) ||
      (pass.history_var && !active_history_srv)) {
    return false;
  }

  const bool bloom_available = bloom_srv != nullptr || pass.bloom_var == nullptr;
  post_process::PostProcessConstants constants =
      post_process::makeConstants(post_process_settings_,
                                  camera_,
                                  width,
                                  height,
                                  history_valid,
                                  bloom_available,
                                  accumulated_time_seconds_);
  {
    Diligent::MapHelper<post_process::PostProcessConstants> cb_map(context_,
                                                                   post_process_cb_,
                                                                   Diligent::MAP_WRITE,
                                                                   Diligent::MAP_FLAG_DISCARD);
    auto* mapped_constants = getMappedData(cb_map);
    if (mapped_constants == nullptr) {
      return false;
    }
    *mapped_constants = constants;
  }

  if (pass.source_var) {
    pass.source_var->Set(source_srv);
  }
  if (pass.depth_var) {
    pass.depth_var->Set(active_depth_srv);
  }
  if (pass.bloom_var) {
    pass.bloom_var->Set(active_bloom_srv);
  }
  if (pass.history_var) {
    pass.history_var->Set(active_history_srv);
  }
  if (pass.sampler_var && sampler_color_) {
    pass.sampler_var->Set(sampler_color_);
  }

  context_->SetPipelineState(pass.pso);
  context_->CommitShaderResources(pass.srb,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::ITextureView* rtvs[] = {target_rtv};
  context_->SetRenderTargets(1, rtvs, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(width),
                         static_cast<Diligent::Uint32>(height));

  Diligent::DrawAttribs draw{};
  draw.NumVertices = 3;
  draw.Flags = Diligent::DRAW_FLAG_NONE;
  context_->Draw(draw);
  return true;
}

bool DiligentBackend::runFullscreenBlit(Diligent::ITextureView* source_srv,
                                        Diligent::ITextureView* target_rtv,
                                        int target_width,
                                        int target_height,
                                        Diligent::TEXTURE_FORMAT format) {
  if (!source_srv || !target_rtv || target_width <= 0 || target_height <= 0 ||
      format == Diligent::TEX_FORMAT_UNKNOWN ||
      !ensureFullscreenBlitPipeline(format)) {
    return false;
  }
  return runPostProcessPass(fullscreen_blit_pass_,
                            source_srv,
                            nullptr,
                            nullptr,
                            nullptr,
                            target_rtv,
                            target_width,
                            target_height,
                            false);
}

bool DiligentBackend::runPresentBlit(Diligent::ITextureView* source_srv,
                                     Diligent::ITextureView* target_rtv,
                                     int target_width,
                                     int target_height,
                                     Diligent::TEXTURE_FORMAT format) {
  if (!source_srv || !target_rtv || target_width <= 0 || target_height <= 0 ||
      format == Diligent::TEX_FORMAT_UNKNOWN ||
      !ensurePresentBlitPipeline(format)) {
    return false;
  }
  return runPostProcessPass(present_blit_pass_,
                            source_srv,
                            nullptr,
                            nullptr,
                            nullptr,
                            target_rtv,
                            target_width,
                            target_height,
                            false);
}

Diligent::ITextureView* DiligentBackend::runBloomChain(Diligent::ITextureView* source_srv,
                                                       int width,
                                                       int height,
                                                       Diligent::TEXTURE_FORMAT format) {
  (void)format;
  if (!post_process_settings_.bloom_enabled ||
      !source_srv ||
      post_process_bloom_mips_.empty() ||
      post_process_bloom_scratch_mips_.empty() ||
      !post_process::passReady(post_process_bloom_prefilter_pass_) ||
      !post_process::passReady(post_process_bloom_downsample_pass_) ||
      !post_process::passReady(post_process_bloom_upsample_pass_)) {
    return nullptr;
  }

  auto& first_mip = post_process_bloom_mips_.front();
  if (!first_mip.rtv ||
      !runPostProcessPass(post_process_bloom_prefilter_pass_,
                          source_srv,
                          nullptr,
                          nullptr,
                          nullptr,
                          first_mip.rtv,
                          first_mip.width,
                          first_mip.height,
                          false)) {
    return nullptr;
  }

  for (size_t i = 1; i < post_process_bloom_mips_.size(); ++i) {
    auto& previous = post_process_bloom_mips_[i - 1];
    auto& current = post_process_bloom_mips_[i];
    if (!previous.srv || !current.rtv ||
        !runPostProcessPass(post_process_bloom_downsample_pass_,
                            previous.srv,
                            nullptr,
                            nullptr,
                            nullptr,
                            current.rtv,
                            current.width,
                            current.height,
                            false)) {
      return post_process_bloom_mips_.front().srv;
    }
  }

  for (size_t level = post_process_bloom_mips_.size() - 1; level > 0; --level) {
    auto& lower = post_process_bloom_mips_[level];
    auto& higher = post_process_bloom_mips_[level - 1];
    auto& scratch = post_process_bloom_scratch_mips_[level - 1];
    if (!lower.srv || !higher.srv || !scratch.rtv || !scratch.texture ||
        !runPostProcessPass(post_process_bloom_upsample_pass_,
                            lower.srv,
                            nullptr,
                            higher.srv,
                            nullptr,
                            scratch.rtv,
                            scratch.width,
                            scratch.height,
                            false)) {
      return post_process_bloom_mips_.front().srv;
    }

    copyTextureAfterRender(scratch.texture, higher.texture);
  }

  return post_process_bloom_mips_.front().srv;
}

void DiligentBackend::applyPostProcessChain(Diligent::ITexture* scene_texture,
                                            Diligent::ITextureView* scene_rtv,
                                            Diligent::ITextureView* scene_depth_srv,
                                            int width,
                                            int height,
                                            Diligent::TEXTURE_FORMAT format,
                                            rendering::RenderTargetId target) {
  if (!context_ || !scene_texture || !scene_rtv ||
      width <= 0 || height <= 0 ||
      format == Diligent::TEX_FORMAT_UNKNOWN) {
    return;
  }

  const bool taa_requested =
      post_process_settings_.temporal_antialiasing_enabled;
  if (!taa_requested) {
    if (auto history = post_process_histories_.find(target);
        history != post_process_histories_.end()) {
      history->second.valid = false;
    }
  }
  if (!post_process::hasActiveEffect(post_process_settings_)) {
    return;
  }

  ensurePostProcessResources(width, height, format);
  if (!post_process_ping_tex_ ||
      !post_process_ping_srv_ ||
      !post_process_pong_srv_ ||
      !post_process_pong_rtv_ ||
      !ensurePostProcessPipelines(format)) {
    return;
  }

  PostProcessHistoryResources* history = nullptr;
  if (taa_requested) {
    history = ensurePostProcessHistoryResources(target, width, height, format);
    if (history) {
      if (!history->camera_initialized ||
          temporalCameraChanged(history->camera, camera_)) {
        history->valid = false;
      }
      history->camera = camera_;
      history->camera_initialized = true;
    }
  }

  copyTextureAfterRender(scene_texture, post_process_ping_tex_);

  Diligent::ITextureView* bloom_srv = nullptr;
  if (post_process_settings_.bloom_enabled) {
    bloom_srv = runBloomChain(post_process_ping_srv_, width, height, format);
  }

  const bool taa_enabled = taa_requested && history != nullptr;
  Diligent::ITextureView* composite_target = scene_rtv;
  if (taa_enabled) {
    composite_target = post_process_pong_rtv_;
  }
  if (!runPostProcessPass(post_process_composite_pass_,
                          post_process_ping_srv_,
                          scene_depth_srv,
                          bloom_srv,
                          nullptr,
                          composite_target,
                          width,
                          height,
                          false)) {
    return;
  }

  if (!taa_enabled) {
    return;
  }

  const int read_history_index = std::clamp(history->index, 0, 1);
  const int write_history_index = 1 - read_history_index;
  Diligent::ITextureView* history_srv = post_process_pong_srv_;
  if (history->valid) {
    history_srv = history->srvs[read_history_index];
  }
  if (!runPostProcessPass(post_process_temporal_pass_,
                          post_process_pong_srv_,
                          nullptr,
                          nullptr,
                          history_srv,
                          scene_rtv,
                          width,
                          height,
                          history->valid)) {
    return;
  }

  if (history->textures[write_history_index]) {
    copyTextureAfterRender(scene_texture,
                           history->textures[write_history_index]);
    history->index = write_history_index;
    history->valid = true;
  }
}

}  // namespace karma::rendering::backend
