#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/TextureView.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

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

bool materialBindingsEqual(const std::vector<rendering::DrawMaterialBinding>& a,
                           const std::vector<rendering::DrawMaterialBinding>& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](const auto& lhs, const auto& rhs) {
           return lhs.slot == rhs.slot && lhs.material == rhs.material;
         });
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

struct DepthResolveConstants {
  float params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

struct SsaaDownsampleConstants {
  float source_size[4] = {};
  float output_size[4] = {};
};
}  // namespace

void DiligentBackend::beginFrame(const rendering::FrameInfo& frame) {
  auto graph_pass_timings = std::move(current_frame_timing_stats_.graph_pass_timings);
  graph_pass_timings.clear();
  current_frame_timing_stats_ = {};
  current_frame_timing_stats_.graph_pass_timings = std::move(graph_pass_timings);
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
  const Diligent::TEXTURE_FORMAT color_format = sceneColorFormat();

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

  Diligent::TextureDesc color_desc{};
  color_desc.Name = "Karma Default Scene Color";
  color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  color_desc.Width = static_cast<Diligent::Uint32>(width);
  color_desc.Height = static_cast<Diligent::Uint32>(height);
  color_desc.MipLevels = 1;
  color_desc.Format = sceneColorFormat();
  color_desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  Diligent::RefCntAutoPtr<Diligent::ITexture> color_texture;
  const auto color_start = core::SteadyClock::now();
  device_->CreateTexture(color_desc, nullptr, &color_texture);
  recordResourceCreation("default_scene", "color texture", color_start, core::SteadyClock::now());
  if (!color_texture) {
    return;
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> color_srv;
  color_srv = color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  Diligent::RefCntAutoPtr<Diligent::ITextureView> color_rtv;
  color_rtv = color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!color_srv || !color_rtv) {
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
  Diligent::RefCntAutoPtr<Diligent::ITexture> depth_texture;
  const auto depth_start = core::SteadyClock::now();
  device_->CreateTexture(depth_desc, nullptr, &depth_texture);
  recordResourceCreation("default_scene", "depth texture", depth_start, core::SteadyClock::now());
  if (!depth_texture) {
    return;
  }

  Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_srv;
  Diligent::TextureViewDesc depth_srv_desc{};
  depth_srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
  depth_srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
  depth_srv_desc.Format = resolveDepthSrvFormat(depth_desc.Format);
  if (depth_srv_desc.Format != Diligent::TEX_FORMAT_UNKNOWN) {
    depth_texture->CreateView(depth_srv_desc, &depth_srv);
  }
  if (!depth_srv) {
    depth_srv = depth_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_dsv;
  depth_dsv = depth_texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
  if (!depth_srv || !depth_dsv) {
    return;
  }

  Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_read_only_dsv;
  Diligent::TextureViewDesc read_only_dsv_desc{};
  read_only_dsv_desc.ViewType = Diligent::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
  read_only_dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
  depth_texture->CreateView(read_only_dsv_desc, &depth_read_only_dsv);

  default_scene_color_tex_ = std::move(color_texture);
  default_scene_color_srv_ = std::move(color_srv);
  default_scene_color_rtv_ = std::move(color_rtv);
  default_scene_depth_tex_ = std::move(depth_texture);
  default_scene_depth_srv_ = std::move(depth_srv);
  default_scene_depth_dsv_ = std::move(depth_dsv);
  default_scene_depth_read_only_dsv_ = std::move(depth_read_only_dsv);

  default_scene_width_ = width;
  default_scene_height_ = height;
}

uint32_t DiligentBackend::activeRasterSampleCount() const {
  return std::max(active_raster_sample_count_, 1u);
}

void DiligentBackend::releaseRasterSampleDependentResources() {
  pipeline_state_.Release();
  opaque_double_sided_pipeline_state_.Release();
  depth_prepass_pipeline_state_.Release();
  transparent_pipeline_state_.Release();
  transparent_double_sided_pipeline_state_.Release();
  additive_pipeline_state_.Release();
  additive_double_sided_pipeline_state_.Release();
  for (auto& pso : compact_forward_pipeline_states_) {
    pso.Release();
  }
  for (auto& pso : editor_wireframe_forward_pipeline_states_) {
    pso.Release();
  }
  custom_forward_pipelines_.clear();

  camera_override_pipeline_state_.Release();
  camera_override_srb_.Release();
  camera_override_vertex_path_.clear();
  camera_override_fragment_path_.clear();

  shader_resources_.Release();
  depth_prepass_srb_.Release();
  default_material_srb_.Release();
  opaque_double_sided_default_material_srb_.Release();
  transparent_default_material_srb_.Release();
  transparent_double_sided_default_material_srb_.Release();
  additive_default_material_srb_.Release();
  additive_double_sided_default_material_srb_.Release();
  for (auto& srb : compact_default_material_srbs_) {
    srb.Release();
  }
  for (auto& srb : editor_wireframe_default_material_srbs_) {
    srb.Release();
  }
  for (auto& [id, material] : materials_) {
    (void)id;
    material.srb.Release();
    material.transparent_srb.Release();
    material.transparent_double_sided_srb.Release();
    material.additive_srb.Release();
    material.additive_double_sided_srb.Release();
    material.custom_srb.Release();
    material.custom_transparent_srb.Release();
    material.custom_transparent_double_sided_srb.Release();
    material.custom_additive_srb.Release();
    material.custom_additive_double_sided_srb.Release();
    for (auto& srb : material.layout_srbs) {
      srb.Release();
    }
    for (auto& srb : material.layout_custom_srbs) {
      srb.Release();
    }
    for (auto& srb : material.editor_wireframe_srbs) {
      srb.Release();
    }
  }

  line_pipeline_state_depth_.Release();
  line_pipeline_state_no_depth_.Release();
  line_srb_depth_.Release();
  line_srb_no_depth_.Release();

  particle_beam_pipeline_additive_depth_.Release();
  particle_beam_pipeline_additive_no_depth_.Release();
  particle_beam_pipeline_alpha_depth_.Release();
  particle_beam_pipeline_alpha_no_depth_.Release();
  particle_beam_srb_additive_depth_.Release();
  particle_beam_srb_additive_no_depth_.Release();
  particle_beam_srb_alpha_depth_.Release();
  particle_beam_srb_alpha_no_depth_.Release();
  particle_beam_texture_var_additive_depth_ = nullptr;
  particle_beam_texture_var_additive_no_depth_ = nullptr;
  particle_beam_texture_var_alpha_depth_ = nullptr;
  particle_beam_texture_var_alpha_no_depth_ = nullptr;

  terrain_pipeline_sets_.clear();

  particle_pipeline_state_additive_depth_.Release();
  particle_pipeline_state_additive_no_depth_.Release();
  particle_pipeline_state_alpha_depth_.Release();
  particle_pipeline_state_alpha_no_depth_.Release();
  particle_pipeline_state_alpha_half_res_.Release();
  particle_pipeline_state_distortion_depth_.Release();
  particle_pipeline_state_distortion_no_depth_.Release();
  particle_half_res_composite_pipeline_state_.Release();
  particle_global_alpha_depth_ = {};
  particle_global_alpha_no_depth_ = {};
  particle_global_alpha_half_res_ = {};
  particle_global_distortion_depth_ = {};
  particle_global_distortion_no_depth_ = {};
  particle_srb_additive_depth_.Release();
  particle_srb_additive_no_depth_.Release();
  particle_srb_alpha_depth_.Release();
  particle_srb_alpha_no_depth_.Release();
  particle_srb_alpha_half_res_.Release();
  particle_srb_distortion_depth_.Release();
  particle_srb_distortion_no_depth_.Release();
  particle_half_res_composite_srb_.Release();
  particle_texture_var_additive_depth_ = nullptr;
  particle_texture_var_additive_no_depth_ = nullptr;
  particle_texture_var_alpha_depth_ = nullptr;
  particle_texture_var_alpha_no_depth_ = nullptr;
  particle_texture_var_alpha_half_res_ = nullptr;
  particle_texture_var_distortion_depth_ = nullptr;
  particle_texture_var_distortion_no_depth_ = nullptr;
  particle_scene_color_var_additive_depth_ = nullptr;
  particle_scene_color_var_additive_no_depth_ = nullptr;
  particle_scene_color_var_alpha_depth_ = nullptr;
  particle_scene_color_var_alpha_no_depth_ = nullptr;
  particle_scene_color_var_alpha_half_res_ = nullptr;
  particle_scene_color_var_distortion_depth_ = nullptr;
  particle_scene_color_var_distortion_no_depth_ = nullptr;
  particle_scene_depth_var_additive_depth_ = nullptr;
  particle_scene_depth_var_additive_no_depth_ = nullptr;
  particle_scene_depth_var_alpha_depth_ = nullptr;
  particle_scene_depth_var_alpha_no_depth_ = nullptr;
  particle_scene_depth_var_alpha_half_res_ = nullptr;
  particle_scene_depth_var_distortion_depth_ = nullptr;
  particle_scene_depth_var_distortion_no_depth_ = nullptr;
  particle_half_res_alpha_var_ = nullptr;

  skybox_pso_.Release();
  skybox_srb_.Release();
  skybox_texture_var_ = nullptr;
}

void DiligentBackend::setActiveRasterSampleCount(uint32_t sample_count) {
  sample_count = std::max(sample_count, 1u);
  if (active_raster_sample_count_ == sample_count) {
    return;
  }
  active_raster_sample_count_ = sample_count;
  releaseRasterSampleDependentResources();
}

uint32_t DiligentBackend::effectiveMsaaSampleCount(Diligent::TEXTURE_FORMAT color_format,
                                                   Diligent::TEXTURE_FORMAT depth_format,
                                                   uint32_t requested_samples) {
  if (!device_ || color_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return 1u;
  }
  const uint32_t requested = rendering::clampRequestedMsaaSamples(requested_samples);
  auto supports_sample = [&](Diligent::TEXTURE_FORMAT format, uint32_t sample) {
    if (format == Diligent::TEX_FORMAT_UNKNOWN) {
      return true;
    }
    const auto info = device_->GetTextureFormatInfoExt(format);
    return (info.SampleCounts & static_cast<Diligent::SAMPLE_COUNT>(sample)) !=
           Diligent::SAMPLE_COUNT_NONE;
  };

  const uint32_t candidates[] = {8u, 4u, 2u};
  for (uint32_t sample : candidates) {
    if (sample > requested) {
      continue;
    }
    if (supports_sample(color_format, sample) && supports_sample(depth_format, sample)) {
      if (sample != requested && !warned_msaa_downgrade_) {
        warned_msaa_downgrade_ = true;
        spdlog::warn("Requested MSAA {}x is not supported for the camera target; using {}x",
                     requested,
                     sample);
      }
      return sample;
    }
  }

  if (!warned_msaa_downgrade_) {
    warned_msaa_downgrade_ = true;
    spdlog::warn("Requested MSAA {}x is not supported for the camera target; disabling MSAA",
                 requested);
  }
  return 1u;
}

bool DiligentBackend::ensureCameraRasterResources(int width,
                                                  int height,
                                                  Diligent::TEXTURE_FORMAT color_format,
                                                  Diligent::TEXTURE_FORMAT depth_format,
                                                  uint32_t sample_count) {
  if (!device_ || width <= 0 || height <= 0 ||
      color_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return false;
  }
  sample_count = std::max(sample_count, 1u);
  const bool need_depth = depth_format != Diligent::TEX_FORMAT_UNKNOWN;
  const bool valid_color =
      camera_raster_color_.texture &&
      camera_raster_color_.rtv &&
      camera_raster_color_.width == width &&
      camera_raster_color_.height == height &&
      camera_raster_color_.format == color_format &&
      camera_raster_color_.sample_count == sample_count &&
      (sample_count > 1u || camera_raster_color_.srv);
  const bool valid_depth =
      !need_depth ||
      (camera_raster_depth_.texture &&
       camera_raster_depth_.dsv &&
       camera_raster_depth_.srv &&
       camera_raster_depth_.read_only_dsv &&
       camera_raster_depth_.width == width &&
       camera_raster_depth_.height == height &&
       camera_raster_depth_.format == depth_format &&
       camera_raster_depth_.sample_count == sample_count);
  if (valid_color && valid_depth) {
    return true;
  }

  camera_raster_color_ = {};
  camera_raster_depth_ = {};

  Diligent::TextureDesc color_desc{};
  color_desc.Name = sample_count > 1u ? "Karma Camera MSAA Color" : "Karma Camera SSAA Color";
  color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  color_desc.Width = static_cast<Diligent::Uint32>(width);
  color_desc.Height = static_cast<Diligent::Uint32>(height);
  color_desc.MipLevels = 1;
  color_desc.Format = color_format;
  color_desc.SampleCount = static_cast<Diligent::Uint8>(sample_count);
  color_desc.BindFlags = Diligent::BIND_RENDER_TARGET;
  if (sample_count == 1u) {
    color_desc.BindFlags |= Diligent::BIND_SHADER_RESOURCE;
  }
  const auto color_start = core::SteadyClock::now();
  device_->CreateTexture(color_desc, nullptr, &camera_raster_color_.texture);
  recordResourceCreation("camera_aa", "raster color", color_start, core::SteadyClock::now());
  if (!camera_raster_color_.texture) {
    return false;
  }
  camera_raster_color_.rtv =
      camera_raster_color_.texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (sample_count == 1u) {
    camera_raster_color_.srv =
        camera_raster_color_.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  }
  if (!camera_raster_color_.rtv || (sample_count == 1u && !camera_raster_color_.srv)) {
    camera_raster_color_ = {};
    return false;
  }
  camera_raster_color_.width = width;
  camera_raster_color_.height = height;
  camera_raster_color_.format = color_format;
  camera_raster_color_.sample_count = sample_count;

  if (!need_depth) {
    return true;
  }

  Diligent::TextureDesc depth_desc{};
  depth_desc.Name = sample_count > 1u ? "Karma Camera MSAA Depth" : "Karma Camera SSAA Depth";
  depth_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  depth_desc.Width = static_cast<Diligent::Uint32>(width);
  depth_desc.Height = static_cast<Diligent::Uint32>(height);
  depth_desc.MipLevels = 1;
  depth_desc.Format = depth_format;
  depth_desc.SampleCount = static_cast<Diligent::Uint8>(sample_count);
  depth_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
  const auto depth_start = core::SteadyClock::now();
  device_->CreateTexture(depth_desc, nullptr, &camera_raster_depth_.texture);
  recordResourceCreation("camera_aa", "raster depth", depth_start, core::SteadyClock::now());
  if (!camera_raster_depth_.texture) {
    camera_raster_color_ = {};
    return false;
  }
  Diligent::TextureViewDesc depth_srv_desc{};
  depth_srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
  depth_srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
  depth_srv_desc.Format = resolveDepthSrvFormat(depth_format);
  if (depth_srv_desc.Format != Diligent::TEX_FORMAT_UNKNOWN) {
    camera_raster_depth_.texture->CreateView(depth_srv_desc, &camera_raster_depth_.srv);
  }
  if (!camera_raster_depth_.srv) {
    camera_raster_depth_.srv =
        camera_raster_depth_.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  }
  camera_raster_depth_.dsv =
      camera_raster_depth_.texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
  Diligent::TextureViewDesc read_only_dsv_desc{};
  read_only_dsv_desc.ViewType = Diligent::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
  read_only_dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
  camera_raster_depth_.texture->CreateView(read_only_dsv_desc,
                                           &camera_raster_depth_.read_only_dsv);
  if (!camera_raster_depth_.srv || !camera_raster_depth_.dsv ||
      !camera_raster_depth_.read_only_dsv) {
    camera_raster_color_ = {};
    camera_raster_depth_ = {};
    return false;
  }
  camera_raster_depth_.width = width;
  camera_raster_depth_.height = height;
  camera_raster_depth_.format = depth_format;
  camera_raster_depth_.sample_count = sample_count;
  return true;
}

bool DiligentBackend::ensureResolvedDepthResource(int width, int height) {
  if (!device_ || width <= 0 || height <= 0) {
    return false;
  }
  if (camera_resolved_depth_.texture &&
      camera_resolved_depth_.srv &&
      camera_resolved_depth_.rtv &&
      camera_resolved_depth_.width == width &&
      camera_resolved_depth_.height == height) {
    return true;
  }

  camera_resolved_depth_ = {};
  Diligent::TextureDesc desc{};
  desc.Name = "Karma Camera Resolved Depth";
  desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width = static_cast<Diligent::Uint32>(width);
  desc.Height = static_cast<Diligent::Uint32>(height);
  desc.MipLevels = 1;
  desc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
  desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  const auto create_start = core::SteadyClock::now();
  device_->CreateTexture(desc, nullptr, &camera_resolved_depth_.texture);
  recordResourceCreation("camera_aa", "resolved depth", create_start, core::SteadyClock::now());
  if (!camera_resolved_depth_.texture) {
    return false;
  }
  camera_resolved_depth_.srv =
      camera_resolved_depth_.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  camera_resolved_depth_.rtv =
      camera_resolved_depth_.texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!camera_resolved_depth_.srv || !camera_resolved_depth_.rtv) {
    camera_resolved_depth_ = {};
    return false;
  }
  camera_resolved_depth_.width = width;
  camera_resolved_depth_.height = height;
  camera_resolved_depth_.format = Diligent::TEX_FORMAT_R32_FLOAT;
  camera_resolved_depth_.sample_count = 1u;
  return true;
}

bool DiligentBackend::ensureDepthResolvePipeline(Diligent::TEXTURE_FORMAT output_depth_format) {
  const bool writes_output_depth = output_depth_format != Diligent::TEX_FORMAT_UNKNOWN;
  if (depth_resolve_pso_ && depth_resolve_srb_ && depth_resolve_cb_ &&
      depth_resolve_source_var_ &&
      depth_resolve_pipeline_depth_format_ == output_depth_format) {
    return true;
  }
  if (!device_) {
    return false;
  }

  depth_resolve_pso_.Release();
  depth_resolve_srb_.Release();
  depth_resolve_source_var_ = nullptr;
  depth_resolve_pipeline_depth_format_ = Diligent::TEX_FORMAT_UNKNOWN;

  static constexpr const char* kDepthResolveVS = R"(
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput main(uint VertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((VertexId << 1) & 2, VertexId & 2);
    output.UV = uv;
    output.Pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}
)";

  static constexpr const char* kDepthResolveColorPS = R"(
Texture2DMS<float> g_DepthMS;

cbuffer DepthResolveConstants
{
    float4 g_DepthResolveParams;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float main(PSInput input) : SV_TARGET
{
    uint sample_count = max(1u, (uint)round(g_DepthResolveParams.x));
    int2 coord = int2(input.Pos.xy);
    float depth = 1.0;
    [loop]
    for (uint sample_index = 0u; sample_index < sample_count; ++sample_index)
    {
        depth = min(depth, g_DepthMS.Load(coord, sample_index));
    }
    return depth;
}
)";

  static constexpr const char* kDepthResolveColorDepthPS = R"(
Texture2DMS<float> g_DepthMS;

cbuffer DepthResolveConstants
{
    float4 g_DepthResolveParams;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

struct PSOutput
{
    float DepthValue : SV_TARGET;
    float Depth : SV_Depth;
};

PSOutput main(PSInput input)
{
    uint sample_count = max(1u, (uint)round(g_DepthResolveParams.x));
    int2 coord = int2(input.Pos.xy);
    float depth = 1.0;
    [loop]
    for (uint sample_index = 0u; sample_index < sample_count; ++sample_index)
    {
        depth = min(depth, g_DepthMS.Load(coord, sample_index));
    }

    PSOutput output;
    output.DepthValue = depth;
    output.Depth = depth;
    return output;
}
)";

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};
  shader_ci.EntryPoint = "main";

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Depth Resolve VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.Source = kDepthResolveVS;
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name =
      writes_output_depth ? "Karma Depth Resolve Color Depth PS" : "Karma Depth Resolve PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.Source = writes_output_depth ? kDepthResolveColorDepthPS : kDepthResolveColorPS;
  ps = device_with_cache_.CreateShader(shader_ci);
  if (!vs || !ps) {
    return false;
  }

  if (!depth_resolve_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Depth Resolve Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(DepthResolveConstants);
    const auto cb_start = core::SteadyClock::now();
    device_->CreateBuffer(cb_desc, nullptr, &depth_resolve_cb_);
    recordResourceCreation("camera_aa", "depth resolve constants", cb_start,
                           core::SteadyClock::now());
    if (!depth_resolve_cb_) {
      return false;
    }
  }

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_PIXEL,
       "DepthResolveConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL,
       "g_DepthMS",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
  };

  Diligent::GraphicsPipelineStateCreateInfo pso{};
  pso.PSODesc.Name = "Karma Depth Resolve Pipeline";
  pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso.PSODesc.ResourceLayout.Variables = vars;
  pso.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
  pso.pVS = vs;
  pso.pPS = ps;
  auto& graphics = pso.GraphicsPipeline;
  graphics.NumRenderTargets = 1;
  graphics.RTVFormats[0] = Diligent::TEX_FORMAT_R32_FLOAT;
  graphics.DSVFormat = writes_output_depth ? output_depth_format : Diligent::TEX_FORMAT_UNKNOWN;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
  graphics.DepthStencilDesc.DepthEnable = writes_output_depth;
  graphics.DepthStencilDesc.DepthWriteEnable = writes_output_depth;
  graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_ALWAYS;
  graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

  const auto pso_start = core::SteadyClock::now();
  depth_resolve_pso_ = createGraphicsPipelineState(pso);
  recordPipelineCreation("camera_aa", "depth resolve", pso_start, core::SteadyClock::now());
  if (!depth_resolve_pso_) {
    return false;
  }
  if (auto* var = depth_resolve_pso_->GetStaticVariableByName(
          Diligent::SHADER_TYPE_PIXEL, "DepthResolveConstants")) {
    var->Set(depth_resolve_cb_);
  }
  const auto srb_start = core::SteadyClock::now();
  depth_resolve_pso_->CreateShaderResourceBinding(&depth_resolve_srb_, true);
  recordResourceCreation("camera_aa", "depth resolve SRB", srb_start,
                         core::SteadyClock::now());
  if (!depth_resolve_srb_) {
    return false;
  }
  depth_resolve_source_var_ =
      depth_resolve_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_DepthMS");
  if (!depth_resolve_source_var_) {
    depth_resolve_pso_.Release();
    depth_resolve_srb_.Release();
    return false;
  }
  depth_resolve_pipeline_depth_format_ = output_depth_format;
  return true;
}

bool DiligentBackend::resolveMsaaCameraResources(Diligent::ITexture* msaa_color_texture,
                                                 Diligent::ITexture* resolved_color_texture,
                                                 Diligent::ITextureView* resolved_depth_dsv,
                                                 Diligent::TEXTURE_FORMAT color_format,
                                                 Diligent::TEXTURE_FORMAT depth_format,
                                                 uint32_t sample_count,
                                                 int width,
                                                 int height) {
  if (!context_ || !msaa_color_texture || !resolved_color_texture ||
      color_format == Diligent::TEX_FORMAT_UNKNOWN || sample_count <= 1u) {
    return false;
  }

  Diligent::ResolveTextureSubresourceAttribs resolve_attribs{};
  resolve_attribs.SrcTextureTransitionMode =
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
  resolve_attribs.DstTextureTransitionMode =
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
  resolve_attribs.Format = color_format;
  context_->ResolveTextureSubresource(msaa_color_texture,
                                      resolved_color_texture,
                                      resolve_attribs);

  if (!camera_raster_depth_.srv) {
    return true;
  }
  Diligent::ITextureView* output_depth_dsv =
      (resolved_depth_dsv && depth_format != Diligent::TEX_FORMAT_UNKNOWN) ? resolved_depth_dsv
                                                                           : nullptr;
  const Diligent::TEXTURE_FORMAT resolve_depth_format =
      output_depth_dsv ? depth_format : Diligent::TEX_FORMAT_UNKNOWN;
  if (!ensureResolvedDepthResource(width, height) ||
      !ensureDepthResolvePipeline(resolve_depth_format)) {
    return false;
  }

  DepthResolveConstants constants{};
  constants.params[0] = static_cast<float>(sample_count);
  {
    Diligent::MapHelper<DepthResolveConstants> mapped(context_,
                                                      depth_resolve_cb_,
                                                      Diligent::MAP_WRITE,
                                                      Diligent::MAP_FLAG_DISCARD);
    auto* data = static_cast<DepthResolveConstants*>(mapped);
    if (!data) {
      return false;
    }
    *data = constants;
  }
  depth_resolve_source_var_->Set(camera_raster_depth_.srv);
  context_->SetPipelineState(depth_resolve_pso_);
  context_->CommitShaderResources(depth_resolve_srb_,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::ITextureView* rtv = camera_resolved_depth_.rtv;
  context_->SetRenderTargets(1, &rtv, output_depth_dsv,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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

bool DiligentBackend::ensureSsaaDownsamplePipeline(Diligent::TEXTURE_FORMAT color_format,
                                                   Diligent::TEXTURE_FORMAT depth_format) {
  const bool has_depth = depth_format != Diligent::TEX_FORMAT_UNKNOWN;
  if (!device_ || color_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return false;
  }
  if (ssaa_downsample_pass_.pso &&
      ssaa_downsample_pass_.srb &&
      ssaa_downsample_cb_ &&
      ssaa_downsample_pass_.source_var &&
      (!has_depth || ssaa_downsample_pass_.depth_var) &&
      ssaa_downsample_color_format_ == color_format &&
      ssaa_downsample_depth_format_ == depth_format) {
    return true;
  }

  ssaa_downsample_pass_.pso.Release();
  ssaa_downsample_pass_.srb.Release();
  ssaa_downsample_pass_.source_var = nullptr;
  ssaa_downsample_pass_.depth_var = nullptr;
  ssaa_downsample_pass_.bloom_var = nullptr;
  ssaa_downsample_pass_.history_var = nullptr;
  ssaa_downsample_pass_.sampler_var = nullptr;
  ssaa_downsample_color_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  ssaa_downsample_depth_format_ = Diligent::TEX_FORMAT_UNKNOWN;

  if (!ssaa_downsample_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma SSAA Downsample Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(SsaaDownsampleConstants);
    const auto cb_start = core::SteadyClock::now();
    device_->CreateBuffer(cb_desc, nullptr, &ssaa_downsample_cb_);
    recordResourceCreation("camera_aa", "ssaa downsample constants", cb_start,
                           core::SteadyClock::now());
    if (!ssaa_downsample_cb_) {
      return false;
    }
  }

  static constexpr const char* kSsaaDownsampleVS = R"(
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput main(uint VertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((VertexId << 1) & 2, VertexId & 2);
    output.UV = uv;
    output.Pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}
)";

  static constexpr const char* kSsaaDownsampleColorPS = R"(
Texture2D<float4> g_SourceColor;

cbuffer SsaaDownsampleConstants
{
    float4 g_SourceSize;
    float4 g_OutputSize;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

uint2 SourceStart(uint2 output_coord, float2 scale, uint2 source_size)
{
    float2 output_coord_f = float2((float)output_coord.x, (float)output_coord.y);
    uint2 start_coord = uint2((uint)floor(output_coord_f.x * scale.x),
                              (uint)floor(output_coord_f.y * scale.y));
    return min(start_coord, source_size - uint2(1u, 1u));
}

uint2 SourceEnd(uint2 output_coord, float2 scale, uint2 source_size, uint2 start_coord)
{
    float2 output_next_f = float2((float)output_coord.x + 1.0,
                                  (float)output_coord.y + 1.0);
    uint2 end_coord = uint2((uint)ceil(output_next_f.x * scale.x),
                            (uint)ceil(output_next_f.y * scale.y));
    return min(max(end_coord, start_coord + uint2(1u, 1u)), source_size);
}

float4 main(PSInput input) : SV_TARGET
{
    uint2 source_size = uint2(max(1u, (uint)g_SourceSize.x),
                              max(1u, (uint)g_SourceSize.y));
    uint2 output_size = uint2(max(1u, (uint)g_OutputSize.x),
                              max(1u, (uint)g_OutputSize.y));
    uint2 output_coord = min(uint2(input.Pos.xy), output_size - uint2(1u, 1u));
    float2 scale = float2(g_SourceSize.x * g_OutputSize.z,
                          g_SourceSize.y * g_OutputSize.w);
    uint2 start_coord = SourceStart(output_coord, scale, source_size);
    uint2 end_coord = SourceEnd(output_coord, scale, source_size, start_coord);

    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    uint sample_count = 0u;
    [loop]
    for (uint y = start_coord.y; y < end_coord.y; ++y)
    {
        [loop]
        for (uint x = start_coord.x; x < end_coord.x; ++x)
        {
            color += g_SourceColor.Load(int3((int)x, (int)y, 0));
            ++sample_count;
        }
    }
    return color * rcp((float)max(sample_count, 1u));
}
)";

  static constexpr const char* kSsaaDownsampleColorDepthPS = R"(
Texture2D<float4> g_SourceColor;
Texture2D<float> g_SourceDepth;

cbuffer SsaaDownsampleConstants
{
    float4 g_SourceSize;
    float4 g_OutputSize;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
    float Depth : SV_Depth;
};

uint2 SourceStart(uint2 output_coord, float2 scale, uint2 source_size)
{
    float2 output_coord_f = float2((float)output_coord.x, (float)output_coord.y);
    uint2 start_coord = uint2((uint)floor(output_coord_f.x * scale.x),
                              (uint)floor(output_coord_f.y * scale.y));
    return min(start_coord, source_size - uint2(1u, 1u));
}

uint2 SourceEnd(uint2 output_coord, float2 scale, uint2 source_size, uint2 start_coord)
{
    float2 output_next_f = float2((float)output_coord.x + 1.0,
                                  (float)output_coord.y + 1.0);
    uint2 end_coord = uint2((uint)ceil(output_next_f.x * scale.x),
                            (uint)ceil(output_next_f.y * scale.y));
    return min(max(end_coord, start_coord + uint2(1u, 1u)), source_size);
}

PSOutput main(PSInput input)
{
    uint2 source_size = uint2(max(1u, (uint)g_SourceSize.x),
                              max(1u, (uint)g_SourceSize.y));
    uint2 output_size = uint2(max(1u, (uint)g_OutputSize.x),
                              max(1u, (uint)g_OutputSize.y));
    uint2 output_coord = min(uint2(input.Pos.xy), output_size - uint2(1u, 1u));
    float2 scale = float2(g_SourceSize.x * g_OutputSize.z,
                          g_SourceSize.y * g_OutputSize.w);
    uint2 start_coord = SourceStart(output_coord, scale, source_size);
    uint2 end_coord = SourceEnd(output_coord, scale, source_size, start_coord);

    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float depth = 1.0;
    uint sample_count = 0u;
    [loop]
    for (uint y = start_coord.y; y < end_coord.y; ++y)
    {
        [loop]
        for (uint x = start_coord.x; x < end_coord.x; ++x)
        {
            int3 coord = int3((int)x, (int)y, 0);
            color += g_SourceColor.Load(coord);
            depth = min(depth, g_SourceDepth.Load(coord));
            ++sample_count;
        }
    }

    PSOutput output;
    output.Color = color * rcp((float)max(sample_count, 1u));
    output.Depth = depth;
    return output;
}
)";

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};
  shader_ci.EntryPoint = "main";

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma SSAA Downsample VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.Source = kSsaaDownsampleVS;
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name =
      has_depth ? "Karma SSAA Downsample Color Depth PS" : "Karma SSAA Downsample Color PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.Source = has_depth ? kSsaaDownsampleColorDepthPS : kSsaaDownsampleColorPS;
  ps = device_with_cache_.CreateShader(shader_ci);
  if (!vs || !ps) {
    return false;
  }

  Diligent::ShaderResourceVariableDesc vars_color_only[] = {
      {Diligent::SHADER_TYPE_PIXEL,
       "SsaaDownsampleConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL,
       "g_SourceColor",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
  };
  Diligent::ShaderResourceVariableDesc vars_with_depth[] = {
      {Diligent::SHADER_TYPE_PIXEL,
       "SsaaDownsampleConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL,
       "g_SourceColor",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL,
       "g_SourceDepth",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
  };

  Diligent::GraphicsPipelineStateCreateInfo pso{};
  pso.PSODesc.Name = has_depth ? "Karma SSAA Downsample Pipeline (Depth)"
                               : "Karma SSAA Downsample Pipeline";
  pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso.PSODesc.ResourceLayout.Variables = has_depth ? vars_with_depth : vars_color_only;
  pso.PSODesc.ResourceLayout.NumVariables =
      has_depth
          ? static_cast<Diligent::Uint32>(sizeof(vars_with_depth) / sizeof(vars_with_depth[0]))
          : static_cast<Diligent::Uint32>(sizeof(vars_color_only) / sizeof(vars_color_only[0]));
  pso.pVS = vs;
  pso.pPS = ps;

  auto& graphics = pso.GraphicsPipeline;
  graphics.NumRenderTargets = 1;
  graphics.RTVFormats[0] = color_format;
  graphics.DSVFormat = has_depth ? depth_format : Diligent::TEX_FORMAT_UNKNOWN;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
  graphics.DepthStencilDesc.DepthEnable = has_depth;
  graphics.DepthStencilDesc.DepthWriteEnable = has_depth;
  graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_ALWAYS;
  graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

  const auto pso_start = core::SteadyClock::now();
  ssaa_downsample_pass_.pso = createGraphicsPipelineState(pso);
  recordPipelineCreation("camera_aa", "ssaa downsample", pso_start,
                         core::SteadyClock::now());
  if (!ssaa_downsample_pass_.pso) {
    return false;
  }
  if (auto* var = ssaa_downsample_pass_.pso->GetStaticVariableByName(
          Diligent::SHADER_TYPE_PIXEL, "SsaaDownsampleConstants")) {
    var->Set(ssaa_downsample_cb_);
  }

  const auto srb_start = core::SteadyClock::now();
  ssaa_downsample_pass_.pso->CreateShaderResourceBinding(&ssaa_downsample_pass_.srb, true);
  recordResourceCreation("camera_aa", "ssaa downsample SRB", srb_start,
                         core::SteadyClock::now());
  if (!ssaa_downsample_pass_.srb) {
    ssaa_downsample_pass_.pso.Release();
    return false;
  }
  ssaa_downsample_pass_.source_var =
      ssaa_downsample_pass_.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                   "g_SourceColor");
  ssaa_downsample_pass_.depth_var =
      ssaa_downsample_pass_.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                   "g_SourceDepth");
  if (!ssaa_downsample_pass_.source_var ||
      (has_depth && !ssaa_downsample_pass_.depth_var)) {
    ssaa_downsample_pass_.pso.Release();
    ssaa_downsample_pass_.srb.Release();
    ssaa_downsample_pass_.source_var = nullptr;
    ssaa_downsample_pass_.depth_var = nullptr;
    return false;
  }

  ssaa_downsample_color_format_ = color_format;
  ssaa_downsample_depth_format_ = depth_format;
  return true;
}

bool DiligentBackend::runSsaaDownsample(Diligent::ITextureView* source_color_srv,
                                        Diligent::ITextureView* source_depth_srv,
                                        Diligent::ITextureView* output_rtv,
                                        Diligent::ITextureView* output_dsv,
                                        Diligent::TEXTURE_FORMAT color_format,
                                        Diligent::TEXTURE_FORMAT depth_format,
                                        int source_width,
                                        int source_height,
                                        int output_width,
                                        int output_height) {
  const bool has_depth = depth_format != Diligent::TEX_FORMAT_UNKNOWN;
  if (!context_ || !source_color_srv || !output_rtv ||
      color_format == Diligent::TEX_FORMAT_UNKNOWN ||
      source_width <= 0 || source_height <= 0 ||
      output_width <= 0 || output_height <= 0 ||
      (has_depth && (!source_depth_srv || !output_dsv)) ||
      !ensureSsaaDownsamplePipeline(color_format, depth_format)) {
    return false;
  }

  SsaaDownsampleConstants constants{};
  constants.source_size[0] = static_cast<float>(source_width);
  constants.source_size[1] = static_cast<float>(source_height);
  constants.source_size[2] = 1.0f / static_cast<float>(std::max(source_width, 1));
  constants.source_size[3] = 1.0f / static_cast<float>(std::max(source_height, 1));
  constants.output_size[0] = static_cast<float>(output_width);
  constants.output_size[1] = static_cast<float>(output_height);
  constants.output_size[2] = 1.0f / static_cast<float>(std::max(output_width, 1));
  constants.output_size[3] = 1.0f / static_cast<float>(std::max(output_height, 1));
  {
    Diligent::MapHelper<SsaaDownsampleConstants> mapped(context_,
                                                        ssaa_downsample_cb_,
                                                        Diligent::MAP_WRITE,
                                                        Diligent::MAP_FLAG_DISCARD);
    auto* data = static_cast<SsaaDownsampleConstants*>(mapped);
    if (!data) {
      return false;
    }
    *data = constants;
  }

  ssaa_downsample_pass_.source_var->Set(source_color_srv);
  if (has_depth) {
    ssaa_downsample_pass_.depth_var->Set(source_depth_srv);
  }

  context_->SetPipelineState(ssaa_downsample_pass_.pso);
  context_->CommitShaderResources(ssaa_downsample_pass_.srb,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::ITextureView* rtvs[] = {output_rtv};
  context_->SetRenderTargets(1,
                             rtvs,
                             has_depth ? output_dsv : nullptr,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(output_width);
  viewport.Height = static_cast<float>(output_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(output_width),
                         static_cast<Diligent::Uint32>(output_height));

  Diligent::DrawAttribs draw{};
  draw.NumVertices = 3;
  draw.Flags = Diligent::DRAW_FLAG_NONE;
  context_->Draw(draw);
  return true;
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
  const bool shadow_material_changed =
      record.material != item.material ||
      !materialBindingsEqual(record.materials, item.materials);
  const bool shadow_scene_changed =
      new_record ? item.shadow_visible
                 : ((record.layer != item.layer ||
                     mesh_changed ||
                     shadow_material_changed ||
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
  record.render_tags = item.render_tags;
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
  const bool shadow_material_changed =
      record.material != item.material ||
      !materialBindingsEqual(record.materials, item.materials);
  const bool shadow_scene_changed =
      new_record ? item.shadow_visible
                 : ((record.layer != item.layer ||
                     record.mesh != item.mesh ||
                     shadow_material_changed ||
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
  if (payload_changed &&
      (item.shadow_visible || (!new_record && record.shadow_visible))) {
    directional_shadow_scene_dirty_ = true;
    point_shadow_scene_dirty_ = true;
  }
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.materials = item.materials;
  record.render_tags = item.render_tags;
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
