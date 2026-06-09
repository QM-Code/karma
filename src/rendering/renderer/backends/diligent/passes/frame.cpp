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

namespace karma::renderer_backend {

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
void accumulateParticlePassStats(renderer::ParticlePassStats& stats, const BatchT& batch) {
  const uint32_t particle_count = static_cast<uint32_t>(std::min<std::size_t>(
      batch.particles.size(),
      static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
  stats.submitted_batches += 1u;
  stats.submitted_particles += particle_count;
  switch (batch.blend_mode) {
    case renderer::ParticleBlendMode::Additive:
      stats.additive_batches += 1u;
      stats.additive_particles += particle_count;
      break;
    case renderer::ParticleBlendMode::Alpha:
      stats.alpha_batches += 1u;
      stats.alpha_particles += particle_count;
      break;
    case renderer::ParticleBlendMode::Distortion:
      stats.distortion_batches += 1u;
      stats.distortion_particles += particle_count;
      stats.distortion_present = true;
      break;
  }
}

renderer::ParticlePackedInstance packParticleInstance(const renderer::ParticleInstance& particle) {
  renderer::ParticlePackedInstance packed{};
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

void DiligentBackend::beginFrame(const renderer::FrameInfo& frame) {
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
}

void DiligentBackend::endFrame() {
  if (swap_chain_) {
    swap_chain_->Present(vsync_enabled_ ? 1u : 0u);
  }
  if (!line_vertices_depth_.empty()) {
    line_vertices_depth_.clear();
  }
  if (!line_vertices_no_depth_.empty()) {
    line_vertices_no_depth_.clear();
  }
}

void DiligentBackend::resize(int width, int height) {
  if (!isValidSize(width, height)) {
    return;
  }

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
  device_->CreateTexture(color_desc, nullptr, &default_scene_color_tex_);
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
  device_->CreateTexture(depth_desc, nullptr, &default_scene_depth_tex_);
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
  device_->CreateTexture(copy_desc, nullptr, &particle_scene_color_copy_tex_);
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
  device_->CreateTexture(alpha_desc, nullptr, &particle_half_res_alpha_tex_);
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
  device_->CreateTexture(desc, &init_data, &particle_fallback_depth_tex_);
  if (!particle_fallback_depth_tex_) {
    return;
  }

  particle_fallback_depth_srv_ =
      particle_fallback_depth_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
}

void DiligentBackend::submit(const renderer::DrawItem& item) {
  if (item.instance == renderer::kInvalidInstance) {
    return;
  }

  if (meshes_.find(item.mesh) == meshes_.end()) {
    return;
  }

  auto it = instances_.find(item.instance);
  if (it == instances_.end()) {
    it = instances_.emplace(item.instance, InstanceRecord{}).first;
  }
  auto& record = it->second;
  const bool mesh_changed = record.mesh != item.mesh;
  record.transform_changed =
      mesh_changed || matrixChangedBeyondEpsilon(record.transform, item.transform) ||
      (item.skinning_enabled && !item.skinning_palette.empty());
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.material_set = item.material_set;
  record.transform = item.transform;
  record.skinning_palette = item.skinning_palette;
  record.skinning_enabled = item.skinning_enabled && !record.skinning_palette.empty();
  record.visible = item.visible;
  record.shadow_visible = item.shadow_visible;
}

void DiligentBackend::submitParticles(renderer::ParticleBatch batch) {
  if (batch.particles.empty()) {
    return;
  }
  renderer::PackedParticleBatch packed_batch{};
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

void DiligentBackend::submitPackedParticles(renderer::PackedParticleBatch batch) {
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

void DiligentBackend::submitParticleEmitter(const renderer::ParticleEmitterGpuDesc& emitter) {
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

void DiligentBackend::retireInstance(renderer::InstanceId instance) {
  if (instance == renderer::kInvalidInstance) {
    return;
  }
  instances_.erase(instance);
  auto particle_it = particle_emitter_runtime_states_.find(static_cast<uint64_t>(instance));
  if (particle_it == particle_emitter_runtime_states_.end()) {
    return;
  }
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

unsigned int DiligentBackend::getRenderTargetTextureId(renderer::RenderTargetId target) const {
  if (target == renderer::kDefaultRenderTarget) {
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

}  // namespace karma::renderer_backend
