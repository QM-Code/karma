#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

namespace karma::rendering::backend {

namespace {
constexpr double kWrappedShaderTimeSeconds = 4096.0;

bool envFlagEnabled(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  return value != "0" && value != "false" && value != "FALSE" && value != "off" &&
         value != "OFF";
}

template <typename RecordT, typename BatchT>
void copyParticleBatchMetadata(RecordT& record, const BatchT& batch) {
  record.layer = batch.layer;
  record.depth_test = batch.depth_test;
  record.texture = batch.texture;
  record.blend_mode = batch.blend_mode;
  record.alignment = batch.alignment;
  record.shading_mode = batch.shading_mode;
  record.presentation_mode = batch.presentation_mode;
  record.use_soft_mask = batch.use_soft_mask;
  record.soft_particle_distance = batch.soft_particle_distance;
  record.distortion_strength = batch.distortion_strength;
  record.fresnel_power = batch.fresnel_power;
  record.fresnel_strength = batch.fresnel_strength;
  record.refraction_strength = batch.refraction_strength;
  record.interior_glow = batch.interior_glow;
  record.size_curve_exponent = batch.size_curve_exponent;
  record.alpha_curve_exponent = batch.alpha_curve_exponent;
  record.atlas_columns = batch.atlas_columns;
  record.atlas_rows = batch.atlas_rows;
  record.atlas_frame_count = batch.atlas_frame_count;
  record.animate_over_lifetime = batch.animate_over_lifetime;
  record.atlas_frame_width = batch.atlas_frame_width;
  record.atlas_frame_height = batch.atlas_frame_height;
  record.atlas_border_x = batch.atlas_border_x;
  record.atlas_border_y = batch.atlas_border_y;
  record.atlas_spacing_x = batch.atlas_spacing_x;
  record.atlas_spacing_y = batch.atlas_spacing_y;
  record.animation_fps = batch.animation_fps;
}

template <typename BatchT>
void accumulateParticlePassStats(rendering::ParticlePassStats& stats, const BatchT& batch) {
  const uint32_t particle_count = static_cast<uint32_t>(std::min<std::size_t>(
      batch.particles.size(),
      static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
  stats.submitted_batches += 1u;
  stats.submitted_particles += particle_count;
  switch (batch.blend_mode) {
    case rendering::ParticleBlendMode::Additive:
      stats.additive_batches += 1u;
      stats.additive_particles += particle_count;
      break;
    case rendering::ParticleBlendMode::Alpha:
      stats.alpha_batches += 1u;
      stats.alpha_particles += particle_count;
      break;
    case rendering::ParticleBlendMode::Distortion:
      stats.distortion_batches += 1u;
      stats.distortion_particles += particle_count;
      stats.distortion_present = true;
      break;
  }
}

rendering::ParticlePackedInstance packParticleInstance(const rendering::ParticleInstance& particle) {
  rendering::ParticlePackedInstance packed{};
  packed.position_age[0] = particle.position.x;
  packed.position_age[1] = particle.position.y;
  packed.position_age[2] = particle.position.z;
  packed.position_age[3] = particle.normalized_age;
  packed.color_start[0] = particle.color.r;
  packed.color_start[1] = particle.color.g;
  packed.color_start[2] = particle.color.b;
  packed.color_start[3] = particle.color.a;
  packed.color_end[0] = particle.color_end.r;
  packed.color_end[1] = particle.color_end.g;
  packed.color_end[2] = particle.color_end.b;
  packed.color_end[3] = particle.color_end.a;
  packed.rotation_size[0] = std::cos(particle.rotation_radians);
  packed.rotation_size[1] = std::sin(particle.rotation_radians);
  packed.rotation_size[2] = particle.size;
  packed.rotation_size[3] = particle.size_end;
  packed.uv_rect[0] = particle.uv_min.x;
  packed.uv_rect[1] = particle.uv_min.y;
  packed.uv_rect[2] = particle.uv_max.x;
  packed.uv_rect[3] = particle.uv_max.y;
  packed.uv_rect_next[0] = particle.uv_min_next.x;
  packed.uv_rect_next[1] = particle.uv_min_next.y;
  packed.uv_rect_next[2] = particle.uv_max_next.x;
  packed.uv_rect_next[3] = particle.uv_max_next.y;
  packed.params[0] = particle.frame_blend;
  packed.params[1] = static_cast<float>(particle.frame_offset);
  packed.params[2] = particle.age_seconds;
  return packed;
}

bool matrixChangedBeyondEpsilon(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      if (std::abs(a[col][row] - b[col][row]) > eps) {
        return true;
      }
    }
  }
  return false;
}

Diligent::TEXTURE_FORMAT resolveDepthSrvFormat(Diligent::TEXTURE_FORMAT depth_format) {
  switch (depth_format) {
    case Diligent::TEX_FORMAT_D32_FLOAT:
      return Diligent::TEX_FORMAT_R32_FLOAT;
    case Diligent::TEX_FORMAT_D24_UNORM_S8_UINT:
      return Diligent::TEX_FORMAT_R24_UNORM_X8_TYPELESS;
    case Diligent::TEX_FORMAT_D32_FLOAT_S8X24_UINT:
      return Diligent::TEX_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
      return Diligent::TEX_FORMAT_UNKNOWN;
  }
}
}  // namespace

void DiligentBackend::beginFrame(const rendering::FrameInfo& frame) {
  current_frame_timing_stats_ = {};
  frame_active_ = true;
  present_frame_ = frame.present;

  if (!particle_stats_log_initialized_) {
    particle_stats_log_initialized_ = true;
    if (const char* value = std::getenv("KARMA_PARTICLE_STATS")) {
      particle_stats_log_enabled_ = envFlagEnabled(value);
    }
    if (particle_stats_log_enabled_) {
      spdlog::info(
          "KARMA_PARTICLE_STATS enabled; logging averaged final particle pass stats once per second");
    }
  }

  if (isValidSize(frame.width, frame.height) &&
      (frame.width != current_width_ || frame.height != current_height_)) {
    resize(frame.width, frame.height);
  }
  particle_pass_stats_ = {};
  instancing_stats_ = {};
  last_frame_delta_seconds_ = std::max(frame.delta_time, 0.0f);
  accumulated_time_seconds_ += static_cast<double>(std::max(frame.delta_time, 0.0f));
  if (accumulated_time_seconds_ >= kWrappedShaderTimeSeconds) {
    accumulated_time_seconds_ =
        std::fmod(accumulated_time_seconds_, kWrappedShaderTimeSeconds);
  }
  if (!particle_batches_.empty()) {
    particle_batches_.clear();
  }
  if (!particle_emitter_submissions_.empty()) {
    particle_emitter_submissions_.clear();
  }
  if (!particle_beam_submissions_.empty()) {
    particle_beam_submissions_.clear();
  }
  if (!terrain_submissions_.empty()) {
    terrain_submissions_.clear();
  }
  terrain_stats_ = {};
}

void DiligentBackend::endFrame() {
  if (swap_chain_ && present_frame_) {
    const auto present_start = core::SteadyClock::now();
    swap_chain_->Present(vsync_enabled_ ? 1u : 0u);
    const auto present_end = core::SteadyClock::now();
    current_frame_timing_stats_.swapchain_present_ms +=
        static_cast<float>(core::elapsedMilliseconds(present_start, present_end));
  } else if (swap_chain_) {
    if (context_) {
      const auto flush_start = core::SteadyClock::now();
      context_->Flush();
      const auto flush_end = core::SteadyClock::now();
      current_frame_timing_stats_.skipped_present_flush_ms +=
          static_cast<float>(core::elapsedMilliseconds(flush_start, flush_end));
    }
    current_frame_timing_stats_.skipped_presents += 1u;
  }
  if (!line_vertices_depth_.empty()) {
    line_vertices_depth_.clear();
  }
  if (!line_vertices_no_depth_.empty()) {
    line_vertices_no_depth_.clear();
  }
  last_frame_timing_stats_ = current_frame_timing_stats_;
  frame_active_ = false;
}

void DiligentBackend::resize(int width, int height) {
  if (!isValidSize(width, height)) {
    return;
  }

  const auto resize_start = core::SteadyClock::now();
  current_width_ = width;
  current_height_ = height;
  if (swap_chain_) {
    swap_chain_->Resize(static_cast<Diligent::Uint32>(width),
                        static_cast<Diligent::Uint32>(height));
  }
  ensureDefaultSceneResources(width, height);
  for (auto& [id, target] : targets_) {
    (void)id;
    if (target.desc.width <= 0 || target.desc.height <= 0) {
      recreateRenderTargetResources(target, width, height);
    }
  }
  const auto resize_end = core::SteadyClock::now();
  if (frame_active_) {
    current_frame_timing_stats_.resize_events += 1u;
    current_frame_timing_stats_.resize_ms +=
        static_cast<float>(core::elapsedMilliseconds(resize_start, resize_end));
  }
}

void DiligentBackend::prewarmRendererResources(bool include_ui) {
  if (!device_) {
    return;
  }

  const int width = std::max(current_width_, 1);
  const int height = std::max(current_height_, 1);
  const Diligent::TEXTURE_FORMAT color_format =
      swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                  : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;

  ensureDefaultSceneResources(width, height);
  ensureForwardPipeline(ForwardPipelineVariant::Opaque,
                        rendering::InstanceGpuLayout::Matrix4x4Params);
  ensureForwardPipeline(ForwardPipelineVariant::Opaque,
                        rendering::InstanceGpuLayout::PositionYawScaleParams);
  ensureForwardPipeline(ForwardPipelineVariant::OpaqueDoubleSided,
                        rendering::InstanceGpuLayout::Matrix4x4Params);
  ensureForwardPipeline(ForwardPipelineVariant::OpaqueDoubleSided,
                        rendering::InstanceGpuLayout::PositionYawScaleParams);
  ensureForwardPipeline(ForwardPipelineVariant::DepthPrepass,
                        rendering::InstanceGpuLayout::Matrix4x4Params);
  ensureForwardPipeline(ForwardPipelineVariant::DepthPrepass,
                        rendering::InstanceGpuLayout::PositionYawScaleParams);
  ensureInstancedGpuCullingResources();
  ensureInstancedGpuLodCullingResources();
  if (!shadow_pipeline_state_) {
    recreateShadowPipeline();
  }
  ensurePostProcessResources(width, height, color_format);
  ensurePostProcessPipelines(color_format);
  ensureLineResources();
  if (include_ui) {
    ensureUiResources();
  }
}

void DiligentBackend::ensureDefaultSceneResources(int width, int height) {
  if (!device_ || width <= 0 || height <= 0) {
    return;
  }
  if (default_scene_color_tex_ &&
      default_scene_color_rtv_ &&
      default_scene_depth_tex_ &&
      default_scene_depth_dsv_ &&
      default_scene_depth_srv_ &&
      default_scene_width_ == width &&
      default_scene_height_ == height) {
    return;
  }

  default_scene_color_tex_.Release();
  default_scene_color_srv_.Release();
  default_scene_color_rtv_.Release();
  default_scene_depth_tex_.Release();
  default_scene_depth_srv_.Release();
  default_scene_depth_dsv_.Release();
  default_scene_depth_read_only_dsv_.Release();
  default_scene_width_ = 0;
  default_scene_height_ = 0;

  Diligent::TextureDesc color_desc{};
  color_desc.Name = "Karma Default Scene Color";
  color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  color_desc.Width = static_cast<Diligent::Uint32>(width);
  color_desc.Height = static_cast<Diligent::Uint32>(height);
  color_desc.MipLevels = 1;
  color_desc.Format = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                  : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
  color_desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  const auto color_start = core::SteadyClock::now();
  device_->CreateTexture(color_desc, nullptr, &default_scene_color_tex_);
  recordResourceCreation("default_scene", "color texture", color_start, core::SteadyClock::now());
  if (!default_scene_color_tex_) {
    return;
  }
  default_scene_color_srv_ =
      default_scene_color_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  default_scene_color_rtv_ =
      default_scene_color_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!default_scene_color_srv_ || !default_scene_color_rtv_) {
    default_scene_color_tex_.Release();
    default_scene_color_srv_.Release();
    default_scene_color_rtv_.Release();
    return;
  }

  Diligent::TextureDesc depth_desc{};
  depth_desc.Name = "Karma Default Scene Depth";
  depth_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  depth_desc.Width = static_cast<Diligent::Uint32>(width);
  depth_desc.Height = static_cast<Diligent::Uint32>(height);
  depth_desc.MipLevels = 1;
  depth_desc.Format = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                  : Diligent::TEX_FORMAT_D32_FLOAT;
  depth_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
  const auto depth_start = core::SteadyClock::now();
  device_->CreateTexture(depth_desc, nullptr, &default_scene_depth_tex_);
  recordResourceCreation("default_scene", "depth texture", depth_start, core::SteadyClock::now());
  if (!default_scene_depth_tex_) {
    default_scene_color_tex_.Release();
    default_scene_color_srv_.Release();
    default_scene_color_rtv_.Release();
    return;
  }

  Diligent::TextureViewDesc depth_srv_desc{};
  depth_srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
  depth_srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
  depth_srv_desc.Format = resolveDepthSrvFormat(depth_desc.Format);
  if (depth_srv_desc.Format != Diligent::TEX_FORMAT_UNKNOWN) {
    default_scene_depth_tex_->CreateView(depth_srv_desc, &default_scene_depth_srv_);
  }
  if (!default_scene_depth_srv_) {
    default_scene_depth_srv_ =
        default_scene_depth_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  }
  default_scene_depth_dsv_ =
      default_scene_depth_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
  if (!default_scene_depth_srv_ || !default_scene_depth_dsv_) {
    default_scene_color_tex_.Release();
    default_scene_color_srv_.Release();
    default_scene_color_rtv_.Release();
    default_scene_depth_tex_.Release();
    default_scene_depth_srv_.Release();
    default_scene_depth_dsv_.Release();
    return;
  }

  Diligent::TextureViewDesc read_only_dsv_desc{};
  read_only_dsv_desc.ViewType = Diligent::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
  read_only_dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
  default_scene_depth_tex_->CreateView(read_only_dsv_desc, &default_scene_depth_read_only_dsv_);

  default_scene_width_ = width;
  default_scene_height_ = height;
}

void DiligentBackend::ensureParticleSceneCopyResources(int width,
                                                       int height,
                                                       Diligent::TEXTURE_FORMAT format) {
  if (!device_ || width <= 0 || height <= 0 || format == Diligent::TEX_FORMAT_UNKNOWN) {
    return;
  }
  if (particle_scene_color_copy_tex_ &&
      particle_scene_color_copy_srv_ &&
      particle_scene_color_copy_width_ == width &&
      particle_scene_color_copy_height_ == height &&
      particle_scene_color_copy_format_ == format) {
    return;
  }

  particle_scene_color_copy_tex_.Release();
  particle_scene_color_copy_srv_.Release();
  particle_scene_color_copy_width_ = 0;
  particle_scene_color_copy_height_ = 0;
  particle_scene_color_copy_format_ = Diligent::TEX_FORMAT_UNKNOWN;

  Diligent::TextureDesc copy_desc{};
  copy_desc.Name = "Karma Particle Scene Copy";
  copy_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  copy_desc.Width = static_cast<Diligent::Uint32>(width);
  copy_desc.Height = static_cast<Diligent::Uint32>(height);
  copy_desc.MipLevels = 1;
  copy_desc.Format = format;
  copy_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
  const auto copy_start = core::SteadyClock::now();
  device_->CreateTexture(copy_desc, nullptr, &particle_scene_color_copy_tex_);
  recordResourceCreation("particle_scene_copy", "color texture", copy_start, core::SteadyClock::now());
  if (!particle_scene_color_copy_tex_) {
    return;
  }

  particle_scene_color_copy_srv_ =
      particle_scene_color_copy_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  if (!particle_scene_color_copy_srv_) {
    particle_scene_color_copy_tex_.Release();
    return;
  }

  particle_scene_color_copy_width_ = width;
  particle_scene_color_copy_height_ = height;
  particle_scene_color_copy_format_ = format;
}

void DiligentBackend::ensureParticleHalfResAlphaResources(int width,
                                                          int height,
                                                          Diligent::TEXTURE_FORMAT format) {
  if (!device_ || width <= 0 || height <= 0 || format == Diligent::TEX_FORMAT_UNKNOWN) {
    return;
  }
  if (particle_half_res_alpha_tex_ &&
      particle_half_res_alpha_srv_ &&
      particle_half_res_alpha_rtv_ &&
      particle_half_res_alpha_width_ == width &&
      particle_half_res_alpha_height_ == height &&
      particle_half_res_alpha_format_ == format) {
    return;
  }

  particle_half_res_alpha_tex_.Release();
  particle_half_res_alpha_srv_.Release();
  particle_half_res_alpha_rtv_.Release();
  particle_half_res_alpha_width_ = 0;
  particle_half_res_alpha_height_ = 0;
  particle_half_res_alpha_format_ = Diligent::TEX_FORMAT_UNKNOWN;

  Diligent::TextureDesc alpha_desc{};
  alpha_desc.Name = "Karma Particle Half Res Alpha";
  alpha_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  alpha_desc.Width = static_cast<Diligent::Uint32>(width);
  alpha_desc.Height = static_cast<Diligent::Uint32>(height);
  alpha_desc.MipLevels = 1;
  alpha_desc.Format = format;
  alpha_desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  const auto alpha_start = core::SteadyClock::now();
  device_->CreateTexture(alpha_desc, nullptr, &particle_half_res_alpha_tex_);
  recordResourceCreation("particle_scene_copy", "half res alpha texture", alpha_start, core::SteadyClock::now());
  if (!particle_half_res_alpha_tex_) {
    return;
  }

  particle_half_res_alpha_srv_ =
      particle_half_res_alpha_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  particle_half_res_alpha_rtv_ =
      particle_half_res_alpha_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!particle_half_res_alpha_srv_ || !particle_half_res_alpha_rtv_) {
    particle_half_res_alpha_tex_.Release();
    particle_half_res_alpha_srv_.Release();
    particle_half_res_alpha_rtv_.Release();
    return;
  }

  particle_half_res_alpha_width_ = width;
  particle_half_res_alpha_height_ = height;
  particle_half_res_alpha_format_ = format;
}

void DiligentBackend::ensureParticleFallbackDepthResource() {
  if (!device_ || particle_fallback_depth_tex_ || particle_fallback_depth_srv_) {
    return;
  }

  const float depth_value = 1.0f;
  Diligent::TextureSubResData subres{};
  subres.pData = &depth_value;
  subres.Stride = sizeof(depth_value);
  Diligent::TextureData init_data{};
  init_data.pSubResources = &subres;
  init_data.NumSubresources = 1;

  Diligent::TextureDesc desc{};
  desc.Name = "Karma Particle Fallback Depth";
  desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width = 1;
  desc.Height = 1;
  desc.MipLevels = 1;
  desc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
  const auto fallback_start = core::SteadyClock::now();
  device_->CreateTexture(desc, &init_data, &particle_fallback_depth_tex_);
  recordResourceCreation("particle_scene_copy", "fallback depth texture", fallback_start, core::SteadyClock::now());
  if (!particle_fallback_depth_tex_) {
    return;
  }

  particle_fallback_depth_srv_ =
      particle_fallback_depth_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
}

void DiligentBackend::submit(const rendering::DrawItem& item) {
  if (item.instance == rendering::kInvalidInstance) {
    return;
  }

  if (meshes_.find(item.mesh) == meshes_.end()) {
    auto stale_it = instances_.find(item.instance);
    if (stale_it != instances_.end()) {
      if (stale_it->second.shadow_visible) {
        directional_shadow_scene_dirty_ = true;
        point_shadow_scene_dirty_ = true;
      }
      instances_.erase(stale_it);
    }
    return;
  }

  auto it = instances_.find(item.instance);
  const bool new_record = it == instances_.end();
  if (new_record) {
    it = instances_.emplace(item.instance, InstanceRecord{}).first;
  }
  auto& record = it->second;
  const bool mesh_changed = record.mesh != item.mesh;
  const bool shadow_scene_changed =
      new_record ? item.shadow_visible
                 : ((record.layer != item.layer ||
                     mesh_changed ||
                     record.deformation != item.deformation ||
                     record.shadow_visible != item.shadow_visible) &&
                    (record.shadow_visible || item.shadow_visible));
  if (shadow_scene_changed) {
    directional_shadow_scene_dirty_ = true;
    point_shadow_scene_dirty_ = true;
  }
  record.transform_changed =
      mesh_changed || matrixChangedBeyondEpsilon(record.transform, item.transform) ||
      record.deformation != item.deformation;
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.materials = item.materials;
  record.deformation = item.deformation;
  record.transform = item.transform;
  record.params = item.instance_params;
  record.volume_params = item.volume_params;
  record.has_volume_params = item.has_volume_params;
  record.requires_scene_sample = item.requires_scene_sample;
  record.post_particle_scene_sample = item.post_particle_scene_sample;
  record.visible = item.visible;
  record.shadow_visible = item.shadow_visible;
}

void DiligentBackend::submitInstanced(const rendering::InstancedDrawItem& item) {
  const size_t item_instance_count = item.instanceCount();
  if (item.instance == rendering::kInvalidInstance) {
    return;
  }

  if (item_instance_count == 0u || meshes_.find(item.mesh) == meshes_.end()) {
    auto stale_it = instanced_records_.find(item.instance);
    if (stale_it != instanced_records_.end()) {
      if (stale_it->second.shadow_visible) {
        directional_shadow_scene_dirty_ = true;
        point_shadow_scene_dirty_ = true;
      }
      instanced_records_.erase(stale_it);
    }
    return;
  }

  auto it = instanced_records_.find(item.instance);
  const bool new_record = it == instanced_records_.end();
  if (new_record) {
    it = instanced_records_.emplace(item.instance, InstancedRecord{}).first;
  }
  auto& record = it->second;
  const bool shadow_scene_changed =
      new_record ? item.shadow_visible
                 : ((record.layer != item.layer ||
                     record.mesh != item.mesh ||
                     record.shadow_visible != item.shadow_visible) &&
                    (record.shadow_visible || item.shadow_visible));
  if (shadow_scene_changed) {
    directional_shadow_scene_dirty_ = true;
    point_shadow_scene_dirty_ = true;
  }
  const bool payload_changed = item.payload_changed ||
                               item.dynamic ||
                               record.mesh != item.mesh ||
                               record.revision != item.revision ||
                               record.gpu_layout != item.gpu_layout ||
                               record.instanceCount() != item_instance_count;
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.materials = item.materials;
  if (record.lods.size() > item.lods.size()) {
    record.lods.resize(item.lods.size());
  }
  record.lods.reserve(item.lods.size());
  for (size_t lod_index = 0; lod_index < item.lods.size(); ++lod_index) {
    if (lod_index >= record.lods.size()) {
      record.lods.emplace_back();
    }
    const rendering::InstancedLodDrawDesc& item_lod = item.lods[lod_index];
    InstancedRecord::LodRecord& lod = record.lods[lod_index];
    lod.start_distance = item_lod.start_distance;
    lod.mesh = item_lod.mesh;
    lod.material = item_lod.material;
    lod.materials = item_lod.materials;
    lod.render_mode = item_lod.render_mode;
    lod.bounds_center = item_lod.bounds_center;
    lod.bounds_radius = item_lod.bounds_radius;
    lod.bounds_valid = item_lod.bounds_valid;
    lod.shadow_visible = item_lod.shadow_visible;
  }
  record.gpu_layout = item.gpu_layout;
  record.revision = item.revision;
  record.bounds_center = item.bounds_center;
  record.bounds_radius = item.bounds_radius;
  record.bounds_valid = item.bounds_valid;
  record.dynamic = item.dynamic;
  record.visible = item.visible;
  record.shadow_visible = item.shadow_visible;
  if (payload_changed) {
    record.instances.clear();
    record.planar_instances.clear();
    if (item.gpu_layout == rendering::InstanceGpuLayout::PositionYawScaleParams) {
      record.planar_instances.assign(item.planar_instances.begin(), item.planar_instances.end());
    } else {
      record.instances.assign(item.instances.begin(), item.instances.end());
    }
    record.instance_buffer_dirty = true;
    if (!record.dynamic) {
      ensureInstancedRecordBuffer(record);
    }
  }
  instancing_stats_.submitted_batches += 1u;
  instancing_stats_.submitted_instances +=
      static_cast<uint32_t>(std::min<size_t>(item_instance_count,
                                             std::numeric_limits<uint32_t>::max()));
}

bool DiligentBackend::ensureInstancedRecordBuffer(InstancedRecord& record) {
  if (!device_ || !context_ || record.instanceCount() == 0u) {
    return false;
  }

  const void* payload_data = nullptr;
  size_t payload_bytes = 0u;
  const size_t payload_stride = rendering::instanceGpuLayoutStride(record.gpu_layout);
  if (record.gpu_layout == rendering::InstanceGpuLayout::PositionYawScaleParams) {
    payload_data = record.planar_instances.data();
    payload_bytes = record.planar_instances.size() * sizeof(rendering::PlanarInstanceData);
  } else {
    payload_data = record.instances.data();
    payload_bytes = record.instances.size() * sizeof(rendering::InstanceData);
  }
  if (!payload_data || payload_bytes == 0u ||
      payload_bytes > static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max())) {
    return false;
  }

  if (!record.instance_buffer || record.instance_buffer_capacity_bytes < payload_bytes) {
    const size_t next_capacity =
        std::max(payload_bytes,
                 record.instance_buffer_capacity_bytes > 0u
                     ? record.instance_buffer_capacity_bytes * 2u
                     : static_cast<size_t>(128u) *
                           rendering::instanceGpuLayoutStride(record.gpu_layout));
    Diligent::BufferDesc desc{};
    desc.Name = "Karma Persistent Instance Buffer";
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_VERTEX_BUFFER | Diligent::BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = static_cast<Diligent::Uint32>(payload_stride);
    desc.Size = static_cast<Diligent::Uint64>(next_capacity);
    record.instance_buffer.Release();
    record.instance_srv.Release();
    const auto buffer_start = core::SteadyClock::now();
    device_->CreateBuffer(desc, nullptr, &record.instance_buffer);
    recordResourceCreation("instancing", "persistent instance buffer", buffer_start, core::SteadyClock::now());
    if (!record.instance_buffer) {
      record.instance_buffer_capacity_bytes = 0u;
      return false;
    }
    record.instance_srv = record.instance_buffer->GetDefaultView(
        Diligent::BUFFER_VIEW_SHADER_RESOURCE);
    if (!record.instance_srv) {
      record.instance_buffer.Release();
      record.instance_buffer_capacity_bytes = 0u;
      return false;
    }
    record.instance_buffer_capacity_bytes = next_capacity;
    record.instance_buffer_dirty = true;
  }

  if (record.instance_buffer_dirty) {
    const auto upload_start = core::SteadyClock::now();
    context_->UpdateBuffer(record.instance_buffer,
                           0,
                           static_cast<Diligent::Uint32>(payload_bytes),
                           payload_data,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const auto upload_end = core::SteadyClock::now();
    instancing_stats_.instance_buffer_updates += 1u;
    instancing_stats_.instance_upload_bytes += static_cast<uint64_t>(payload_bytes);
    instancing_stats_.instance_upload_ms +=
        static_cast<float>(core::elapsedMilliseconds(upload_start, upload_end));
    record.instance_buffer_dirty = false;
  }
  if (!record.dynamic &&
      record.gpu_layout == rendering::InstanceGpuLayout::PositionYawScaleParams &&
      instancedGpuCullingEnabled()) {
    ensureInstancedGpuCullingRecordBuffers(record);
  }
  return record.instance_buffer != nullptr && record.instance_srv != nullptr;
}

void DiligentBackend::submitParticles(rendering::ParticleBatch batch) {
  if (batch.particles.empty()) {
    return;
  }
  rendering::PackedParticleBatch packed_batch{};
  packed_batch.layer = batch.layer;
  packed_batch.depth_test = batch.depth_test;
  packed_batch.texture = batch.texture;
  packed_batch.blend_mode = batch.blend_mode;
  packed_batch.alignment = batch.alignment;
  packed_batch.shading_mode = batch.shading_mode;
  packed_batch.presentation_mode = batch.presentation_mode;
  packed_batch.use_soft_mask = batch.use_soft_mask;
  packed_batch.soft_particle_distance = batch.soft_particle_distance;
  packed_batch.distortion_strength = batch.distortion_strength;
  packed_batch.fresnel_power = batch.fresnel_power;
  packed_batch.fresnel_strength = batch.fresnel_strength;
  packed_batch.refraction_strength = batch.refraction_strength;
  packed_batch.interior_glow = batch.interior_glow;
  packed_batch.size_curve_exponent = batch.size_curve_exponent;
  packed_batch.alpha_curve_exponent = batch.alpha_curve_exponent;
  packed_batch.atlas_columns = batch.atlas_columns;
  packed_batch.atlas_rows = batch.atlas_rows;
  packed_batch.atlas_frame_count = batch.atlas_frame_count;
  packed_batch.animate_over_lifetime = batch.animate_over_lifetime;
  packed_batch.atlas_frame_width = batch.atlas_frame_width;
  packed_batch.atlas_frame_height = batch.atlas_frame_height;
  packed_batch.atlas_border_x = batch.atlas_border_x;
  packed_batch.atlas_border_y = batch.atlas_border_y;
  packed_batch.atlas_spacing_x = batch.atlas_spacing_x;
  packed_batch.atlas_spacing_y = batch.atlas_spacing_y;
  packed_batch.animation_fps = batch.animation_fps;
  packed_batch.particles.reserve(batch.particles.size());
  for (const auto& particle : batch.particles) {
    packed_batch.particles.push_back(packParticleInstance(particle));
  }
  submitPackedParticles(std::move(packed_batch));
}

void DiligentBackend::submitPackedParticles(rendering::PackedParticleBatch batch) {
  if (batch.particles.empty()) {
    return;
  }
  accumulateParticlePassStats(particle_pass_stats_, batch);
  particle_pass_stats_.cpu_fallback_particles += static_cast<uint32_t>(std::min<std::size_t>(
      batch.particles.size(),
      static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));

  ParticleBatchRecord record{};
  copyParticleBatchMetadata(record, batch);
  record.particles = std::move(batch.particles);
  particle_batches_.push_back(std::move(record));
}

void DiligentBackend::submitParticleEmitter(const rendering::ParticleEmitterGpuDesc& emitter) {
  if (emitter.instance_id == 0u) {
    return;
  }

  ParticleEmitterRuntimeState& state = particle_emitter_runtime_states_[emitter.instance_id];
  if (state.initialized && state.restart_count != emitter.restart_count) {
    state.elapsed_seconds = 0.0f;
    state.previous_elapsed_seconds = 0.0f;
    state.restart_count = emitter.restart_count;
    state.gpu_reset_pending = true;
  }
  if (!state.initialized) {
    state.restart_count = emitter.restart_count;
    state.initialized = true;
  }

  state.previous_elapsed_seconds = state.elapsed_seconds;
  if (emitter.enabled && emitter.playing) {
    state.elapsed_seconds += std::max(emitter.delta_seconds, 0.0f) *
                             std::max(emitter.time_scale, 0.0f);
  }

  particle_emitter_submissions_.push_back(ParticleEmitterSubmission{
      .desc = emitter,
      .elapsed_seconds = state.elapsed_seconds,
      .previous_elapsed_seconds = state.previous_elapsed_seconds,
  });
}

void DiligentBackend::submitParticleBeam(const rendering::ParticleBeamGpuDesc& beam) {
  if (beam.instance_id == 0u || beam.local_path_points.size() < 2u) {
    return;
  }
  if (beam.blend_mode == rendering::ParticleBlendMode::Distortion) {
    return;
  }

  ParticleBeamRuntimeState& state = particle_beam_runtime_states_[beam.instance_id];
  if (state.initialized && state.restart_count != beam.restart_count) {
    state.elapsed_seconds = 0.0f;
    state.restart_count = beam.restart_count;
  }
  if (!state.initialized) {
    state.restart_count = beam.restart_count;
    state.initialized = true;
  }

  if (beam.enabled && beam.visible) {
    state.elapsed_seconds += std::max(beam.delta_seconds, 0.0f) *
                             std::max(beam.time_scale, 0.0f);
  }

  particle_beam_submissions_.push_back(ParticleBeamSubmission{
      .desc = beam,
      .elapsed_seconds = state.elapsed_seconds,
  });
  particle_pass_stats_.submitted_beams += 1u;
  particle_pass_stats_.beam_segments += static_cast<uint32_t>(std::min<std::size_t>(
      beam.local_path_points.size() - 1u,
      static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
}

void DiligentBackend::retireInstance(rendering::InstanceId instance) {
  if (instance == rendering::kInvalidInstance) {
    return;
  }
  auto instance_it = instances_.find(instance);
  if (instance_it != instances_.end()) {
    if (instance_it->second.shadow_visible) {
      directional_shadow_scene_dirty_ = true;
      point_shadow_scene_dirty_ = true;
    }
    instances_.erase(instance_it);
  }
  auto instanced_it = instanced_records_.find(instance);
  if (instanced_it != instanced_records_.end()) {
    if (instanced_it->second.shadow_visible) {
      directional_shadow_scene_dirty_ = true;
      point_shadow_scene_dirty_ = true;
    }
    instanced_records_.erase(instanced_it);
  }
  auto particle_it = particle_emitter_runtime_states_.find(static_cast<uint64_t>(instance));
  if (particle_it != particle_emitter_runtime_states_.end()) {
    const ParticleEmitterRuntimeState state = particle_it->second;
    if (state.gpu_slot_capacity > 0u) {
      particle_gpu_free_particle_slots_.push_back(ParticleGpuSlotRange{
          .offset = state.gpu_slot_offset,
          .capacity = state.gpu_slot_capacity,
      });
    }
    if (state.gpu_emitter_state_allocated) {
      particle_gpu_free_emitter_state_slots_.push_back(state.gpu_emitter_state_index);
    }
    particle_emitter_runtime_states_.erase(particle_it);
  }
  particle_beam_runtime_states_.erase(static_cast<uint64_t>(instance));
}

void DiligentBackend::drawLine(const math::Vec3& start, const math::Vec3& end,
                               const math::Color& color, bool depth_test, float thickness) {
  if (!warned_line_thickness_ && thickness != 1.0f) {
    warned_line_thickness_ = true;
  }
  LineVertex a{};
  a.position[0] = start.x;
  a.position[1] = start.y;
  a.position[2] = start.z;
  a.position[3] = 1.0f;
  a.color[0] = color.r;
  a.color[1] = color.g;
  a.color[2] = color.b;
  a.color[3] = color.a;

  LineVertex b{};
  b.position[0] = end.x;
  b.position[1] = end.y;
  b.position[2] = end.z;
  b.position[3] = 1.0f;
  b.color[0] = color.r;
  b.color[1] = color.g;
  b.color[2] = color.b;
  b.color[3] = color.a;

  auto& bucket = depth_test ? line_vertices_depth_ : line_vertices_no_depth_;
  bucket.push_back(a);
  bucket.push_back(b);
}

unsigned int DiligentBackend::getRenderTargetTextureId(rendering::RenderTargetId target) const {
  if (target == rendering::kDefaultRenderTarget) {
    return 0u;
  }
  auto it = targets_.find(target);
  if (it == targets_.end() || !it->second.color_srv) {
    return 0u;
  }
  if ((target & kRenderTargetTextureHandleBit) != 0u) {
    return 0u;
  }
  return static_cast<unsigned int>(target | kRenderTargetTextureHandleBit);
}

void DiligentBackend::clearFrame(const float* color, bool clear_depth) {
  if (!context_ || !swap_chain_) {
    return;
  }

  auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
  auto* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context_->ClearRenderTarget(rtv, color, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  if (clear_depth && dsv) {
    context_->ClearDepthStencil(dsv,
                                Diligent::CLEAR_DEPTH_FLAG,
                                1.0f,
                                0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
}

}  // namespace karma::rendering::backend
