#include "backend.hpp"

#include "backend_internal.h"
#include "passes/pass_shared.h"
#include "karma/rendering.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::rendering::backend {

namespace {
struct alignas(16) ForwardPlusGpuLight {
  float position_range[4];
  float direction_type[4];
  float color_intensity[4];
  float spot_params[4];
  float screen_rect[4];
};

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

bool projectSphereToScreenRect(const glm::mat4& view,
                               const glm::mat4& view_proj,
                               const glm::vec3& center_ws,
                               float radius_ws,
                               float near_clip,
                               float screen_width,
                               float screen_height,
                               glm::vec4& out_rect) {
  if (!std::isfinite(center_ws.x) || !std::isfinite(center_ws.y) || !std::isfinite(center_ws.z) ||
      !std::isfinite(radius_ws) || radius_ws <= 0.0f ||
      !std::isfinite(near_clip) || near_clip <= 0.0f ||
      !std::isfinite(screen_width) || !std::isfinite(screen_height) ||
      screen_width <= 0.0f || screen_height <= 0.0f) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  const glm::vec3 center_vs = glm::vec3(view * glm::vec4(center_ws, 1.0f));
  if (!std::isfinite(center_vs.x) || !std::isfinite(center_vs.y) || !std::isfinite(center_vs.z)) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  const float max_screen_x = std::max(screen_width - 1.0f, 0.0f);
  const float max_screen_y = std::max(screen_height - 1.0f, 0.0f);
  const float center_depth = -center_vs.z;
  if (center_depth + radius_ws <= near_clip) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }
  // When the light sphere intersects the near plane, corner sampling tends to
  // underestimate the visible screen coverage and produces hard cut lines in
  // the tiled-light path as the camera moves through the light volume.
  if (center_depth - radius_ws <= near_clip) {
    out_rect = glm::vec4(0.0f, 0.0f, max_screen_x, max_screen_y);
    return true;
  }

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = -std::numeric_limits<float>::max();
  float max_y = -std::numeric_limits<float>::max();
  bool any_point = false;
  for (int z = -1; z <= 1; z += 2) {
    for (int y = -1; y <= 1; y += 2) {
      for (int x = -1; x <= 1; x += 2) {
        const glm::vec3 sample =
            center_ws + glm::vec3(static_cast<float>(x),
                                  static_cast<float>(y),
                                  static_cast<float>(z)) *
                            radius_ws;
        const glm::vec4 clip = view_proj * glm::vec4(sample, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
          continue;
        }
        if (std::abs(clip.w) <= 1e-6f) {
          continue;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) {
          continue;
        }
        const float sx = (ndc.x * 0.5f + 0.5f) * screen_width;
        const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * screen_height;
        if (!std::isfinite(sx) || !std::isfinite(sy)) {
          continue;
        }
        min_x = std::min(min_x, sx);
        min_y = std::min(min_y, sy);
        max_x = std::max(max_x, sx);
        max_y = std::max(max_y, sy);
        any_point = true;
      }
    }
  }

  if (!any_point) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  if (max_x < 0.0f || max_y < 0.0f || min_x > screen_width || min_y > screen_height) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  out_rect.x = std::clamp(min_x, 0.0f, max_screen_x);
  out_rect.y = std::clamp(min_y, 0.0f, max_screen_y);
  out_rect.z = std::clamp(max_x, 0.0f, max_screen_x);
  out_rect.w = std::clamp(max_y, 0.0f, max_screen_y);
  return out_rect.z >= out_rect.x && out_rect.w >= out_rect.y;
}

ForwardPlusGpuLight packForwardPlusLight(const rendering::LightData& light,
                                         const glm::vec4& screen_rect) {
  ForwardPlusGpuLight gpu{};
  gpu.position_range[0] = light.position.x;
  gpu.position_range[1] = light.position.y;
  gpu.position_range[2] = light.position.z;
  gpu.position_range[3] = std::max(light.range, 0.0f);
  gpu.direction_type[0] = light.direction.x;
  gpu.direction_type[1] = light.direction.y;
  gpu.direction_type[2] = light.direction.z;
  gpu.direction_type[3] = static_cast<float>(static_cast<uint32_t>(light.type));
  gpu.color_intensity[0] = light.color.r;
  gpu.color_intensity[1] = light.color.g;
  gpu.color_intensity[2] = light.color.b;
  gpu.color_intensity[3] = std::max(light.intensity, 0.0f);
  gpu.spot_params[0] = light.inner_cone_cos;
  gpu.spot_params[1] = light.outer_cone_cos;
  gpu.spot_params[2] = -1.0f;
  gpu.spot_params[3] = light.mixed_bake_mask_bit < 64u
                           ? static_cast<float>(light.mixed_bake_mask_bit)
                           : -1.0f;
  gpu.screen_rect[0] = screen_rect.x;
  gpu.screen_rect[1] = screen_rect.y;
  gpu.screen_rect[2] = screen_rect.z;
  gpu.screen_rect[3] = screen_rect.w;
  return gpu;
}

bool isFiniteVec3(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool renderLayerFrameDiagEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("KARMA_RENDER_LAYER_FRAME_DIAG");
    return value != nullptr && value[0] != '\0' &&
           std::strcmp(value, "0") != 0 &&
           std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "FALSE") != 0 &&
           std::strcmp(value, "off") != 0 &&
           std::strcmp(value, "OFF") != 0;
  }();
  return enabled;
}

float renderLayerFrameDiagFloat(const char* name, float fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const float parsed = std::strtof(value, &end);
  return end == value ? fallback : parsed;
}

struct RenderLayerStageTiming {
  const char* name = "";
  double ms = 0.0;
};

bool readBoolParam(const std::unordered_map<std::string, rendering::MaterialParameterValue>& params,
                   std::string_view name,
                   bool& out) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return false;
  }
  if (const auto* value = std::get_if<bool>(&it->second)) {
    out = *value;
    return true;
  }
  return false;
}

bool readFloatParam(const std::unordered_map<std::string, rendering::MaterialParameterValue>& params,
                    std::string_view name,
                    float& out) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return false;
  }
  if (const auto* value = std::get_if<float>(&it->second)) {
    out = *value;
    return true;
  }
  if (const auto* value = std::get_if<int32_t>(&it->second)) {
    out = static_cast<float>(*value);
    return true;
  }
  if (const auto* value = std::get_if<uint32_t>(&it->second)) {
    out = static_cast<float>(*value);
    return true;
  }
  return false;
}

rendering::PostProcessSettings postProcessSettingsFromFrameGraph(
    const rendering::FrameGraphDesc& graph) {
  rendering::PostProcessSettings settings{};
  settings.enabled = graph.enabled;
  if (!graph.enabled) {
    return settings;
  }

  for (const rendering::FrameGraphPassDesc& pass : graph.passes) {
    if (!pass.enabled) {
      continue;
    }
    if (pass.kind == rendering::FrameGraphPassKind::Builtin) {
      if (pass.builtin_pass == "bloom") {
        settings.bloom_enabled = true;
      } else if (pass.builtin_pass == "taa") {
        settings.temporal_antialiasing_enabled = true;
      }
    }

    readBoolParam(pass.params, "enabled", settings.enabled);
    readBoolParam(pass.params, "bloom_enabled", settings.bloom_enabled);
    readFloatParam(pass.params, "bloom_threshold", settings.bloom_threshold);
    readFloatParam(pass.params, "bloom_intensity", settings.bloom_intensity);
    readFloatParam(pass.params, "bloom_radius", settings.bloom_radius);
    readBoolParam(pass.params, "tone_mapping_enabled", settings.tone_mapping_enabled);
    readFloatParam(pass.params, "tone_exposure", settings.tone_exposure);
    readFloatParam(pass.params, "tone_contrast", settings.tone_contrast);
    readFloatParam(pass.params, "tone_saturation", settings.tone_saturation);
    readBoolParam(pass.params, "ssao_enabled", settings.ssao_enabled);
    readFloatParam(pass.params, "ssao_radius", settings.ssao_radius);
    readFloatParam(pass.params, "ssao_intensity", settings.ssao_intensity);
    readFloatParam(pass.params, "ssao_power", settings.ssao_power);
    readBoolParam(pass.params,
                  "screen_space_reflections_enabled",
                  settings.screen_space_reflections_enabled);
    readFloatParam(pass.params, "ssr_intensity", settings.ssr_intensity);
    readFloatParam(pass.params, "ssr_max_roughness", settings.ssr_max_roughness);
    readFloatParam(pass.params, "ssr_thickness", settings.ssr_thickness);
    readBoolParam(pass.params,
                  "temporal_antialiasing_enabled",
                  settings.temporal_antialiasing_enabled);
    readFloatParam(pass.params, "taa_feedback", settings.taa_feedback);
    readFloatParam(pass.params, "taa_sharpening", settings.taa_sharpening);
    readBoolParam(pass.params, "depth_of_field_enabled", settings.depth_of_field_enabled);
    readFloatParam(pass.params, "dof_focus_depth", settings.dof_focus_depth);
    readFloatParam(pass.params, "dof_focus_range", settings.dof_focus_range);
    readFloatParam(pass.params, "dof_intensity", settings.dof_intensity);
  }
  return settings;
}

rendering::FrameGraphValidationOptions validationOptionsForResolvedGraph(
    const rendering::FrameGraphDesc& graph) {
  rendering::FrameGraphValidationOptions options{};
  for (const rendering::FrameGraphPassDesc& pass : graph.passes) {
    if (pass.enabled && pass.kind == rendering::FrameGraphPassKind::Shader) {
      options.require_shader_pass_keys = true;
      break;
    }
  }
  if (!options.require_shader_pass_keys) {
    return options;
  }
  options.shader_pass_keys.reserve(graph.shader_pass_assets.size());
  for (const rendering::ShaderPassAssetDesc& shader_pass : graph.shader_pass_assets) {
    if (!shader_pass.shader_pass_key.empty()) {
      options.shader_pass_keys.push_back(shader_pass.shader_pass_key);
    }
  }
  return options;
}

}  // namespace

void DiligentBackend::renderLayer(rendering::LayerId layer,
                                  rendering::RenderTargetId target,
                                  const rendering::FrameGraphDesc& frame_graph) {
  const rendering::FrameGraphDesc* active_frame_graph = &frame_graph;
  const rendering::FrameGraphValidationOptions graph_validation_options =
      validationOptionsForResolvedGraph(frame_graph);
  const rendering::FrameGraphValidationResult graph_validation =
      rendering::validateFrameGraphDesc(frame_graph, graph_validation_options);
  if (!graph_validation.valid()) {
    const std::string graph_key =
        frame_graph.frame_graph_key.empty() ? std::string("<unnamed>")
                                            : frame_graph.frame_graph_key;
    static std::unordered_map<std::string, bool> warned_invalid_graphs;
    if (!warned_invalid_graphs[graph_key]) {
      warned_invalid_graphs[graph_key] = true;
      spdlog::warn("Invalid renderer frame graph '{}'; using default graph. {}",
                   graph_key,
                   graph_validation.diagnostics.empty()
                       ? std::string("No diagnostic reported.")
                       : graph_validation.diagnostics.front());
    }
    active_frame_graph = &rendering::defaultFrameGraphDesc();
  }
  const rendering::PostProcessSettings post_process =
      postProcessSettingsFromFrameGraph(*active_frame_graph);
  const bool frame_layer_diag = renderLayerFrameDiagEnabled();
  const float frame_layer_diag_threshold_ms =
      std::max(0.0f,
               renderLayerFrameDiagFloat("KARMA_RENDER_LAYER_FRAME_DIAG_THRESHOLD_MS", 20.0f));
  const float frame_layer_diag_stage_threshold_ms =
      std::max(0.0f,
               renderLayerFrameDiagFloat(
                   "KARMA_RENDER_LAYER_FRAME_DIAG_STAGE_THRESHOLD_MS",
                   renderLayerFrameDiagFloat("KARMA_RENDER_LAYER_STAGE_DIAG_THRESHOLD_MS",
                                             0.25f)));
  const bool startup_layer_diag = [] {
    if (!startupDiagnosticsEnabled()) {
      return false;
    }
    static int logged_layer_count = 0;
    if (logged_layer_count >= 4) {
      return false;
    }
    ++logged_layer_count;
    return true;
  }();
  const auto layer_start = core::SteadyClock::now();
  auto stage_start = layer_start;
  std::vector<RenderLayerStageTiming> stage_timings;
  if (frame_layer_diag) {
    stage_timings.reserve(32);
  }
  auto mark_stage = [&](const char* stage) {
    const auto stage_end = core::SteadyClock::now();
    const double stage_ms = core::elapsedMilliseconds(stage_start, stage_end);
    recordRenderLayerStageTiming(stage, stage_ms);
    if (frame_layer_diag) {
      stage_timings.push_back(RenderLayerStageTiming{stage, stage_ms});
    }
    if (startup_layer_diag) {
      logStartupDiag("diligent_render_layer", stage, stage_start, stage_end);
    }
    stage_start = stage_end;
  };

  prunePostProcessHistoryResources();
  applyPostProcessSettingsForPass(post_process);
  if (!context_ || !swap_chain_) {
    return;
  }

  const bool rendering_to_default_target = target == rendering::kDefaultRenderTarget;
  Diligent::ITextureView* active_rtv = swap_chain_->GetCurrentBackBufferRTV();
  Diligent::ITextureView* active_dsv = swap_chain_->GetDepthBufferDSV();
  Diligent::ITextureView* particle_dsv = active_dsv;
  Diligent::ITextureView* particle_scene_color_srv = nullptr;
  Diligent::ITextureView* particle_scene_depth_srv = nullptr;
  Diligent::ITexture* particle_scene_texture = nullptr;
  Diligent::ITextureView* present_source_srv = nullptr;
  Diligent::ITextureView* present_destination_rtv = nullptr;
  Diligent::ITexture* present_source_texture = nullptr;
  Diligent::ITexture* present_destination_texture = nullptr;
  Diligent::TEXTURE_FORMAT active_color_format = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::TEXTURE_FORMAT active_depth_format = Diligent::TEX_FORMAT_UNKNOWN;
  int render_width = current_width_;
  int render_height = current_height_;
  Diligent::ITextureView* output_rtv = active_rtv;
  Diligent::ITextureView* output_dsv = active_dsv;
  Diligent::ITextureView* output_read_only_dsv = nullptr;
  Diligent::ITextureView* output_color_srv = nullptr;
  Diligent::ITextureView* output_depth_srv = nullptr;
  Diligent::ITexture* output_color_texture = nullptr;
  if (rendering_to_default_target) {
    ensureDefaultSceneResources(render_width, render_height);
    active_rtv = default_scene_color_rtv_;
    active_dsv = default_scene_depth_dsv_;
    output_rtv = active_rtv;
    output_dsv = active_dsv;
    output_read_only_dsv = default_scene_depth_read_only_dsv_;
    output_color_srv = default_scene_color_srv_;
    output_depth_srv = default_scene_depth_srv_;
    output_color_texture = default_scene_color_tex_;
    particle_dsv = default_scene_depth_read_only_dsv_ ? default_scene_depth_read_only_dsv_
                                                      : active_dsv;
    particle_scene_color_srv = default_scene_color_srv_;
    particle_scene_depth_srv = default_scene_depth_srv_;
    particle_scene_texture = default_scene_color_tex_;
    present_source_srv = default_scene_color_srv_;
    present_source_texture = default_scene_color_tex_;
    if (default_scene_color_tex_) {
      active_color_format = default_scene_color_tex_->GetDesc().Format;
    }
    if (default_scene_depth_tex_) {
      active_depth_format = default_scene_depth_tex_->GetDesc().Format;
    }
    if (auto* backbuffer_rtv = swap_chain_->GetCurrentBackBufferRTV()) {
      present_destination_rtv = backbuffer_rtv;
      present_destination_texture = backbuffer_rtv->GetTexture();
    }
  } else {
    auto target_it = targets_.find(target);
    if (target_it == targets_.end() || !target_it->second.color_rtv) {
      return;
    }
    active_rtv = target_it->second.color_rtv;
    active_dsv = target_it->second.depth_dsv;
    output_rtv = active_rtv;
    output_dsv = active_dsv;
    output_read_only_dsv = target_it->second.depth_read_only_dsv;
    output_color_srv = target_it->second.color_srv;
    output_depth_srv = target_it->second.depth_srv;
    output_color_texture = target_it->second.color_texture;
    particle_dsv = target_it->second.depth_read_only_dsv ? target_it->second.depth_read_only_dsv
                                                         : active_dsv;
    particle_scene_color_srv = target_it->second.color_srv;
    particle_scene_depth_srv = target_it->second.depth_srv;
    particle_scene_texture = target_it->second.color_texture;
    if (target_it->second.color_texture) {
      active_color_format = target_it->second.color_texture->GetDesc().Format;
    }
    if (target_it->second.depth_texture) {
      active_depth_format = target_it->second.depth_texture->GetDesc().Format;
    }
    render_width = std::max(target_it->second.width, 1);
    render_height = std::max(target_it->second.height, 1);
  }
  if (!active_rtv || render_width <= 0 || render_height <= 0) {
    return;
  }
  const int output_width = render_width;
  const int output_height = render_height;

  rendering::AntiAliasingSettings effective_aa =
      rendering::clampAntiAliasingSettings(camera_.anti_aliasing);
  uint32_t raster_sample_count = 1u;
  float effective_ssaa_scale = 1.0f;
  bool msaa_enabled = false;
  bool ssaa_enabled = false;
  if (effective_aa.mode == rendering::AntiAliasingMode::MSAA) {
    raster_sample_count =
        effectiveMsaaSampleCount(active_color_format,
                                 active_depth_format,
                                 effective_aa.msaa_samples);
    if (raster_sample_count > 1u) {
      msaa_enabled = true;
      effective_aa.msaa_samples = raster_sample_count;
    } else {
      effective_aa.mode = rendering::AntiAliasingMode::None;
      effective_aa.msaa_samples = 1u;
      effective_aa.ssaa_scale = 1.0f;
    }
  } else if (effective_aa.mode == rendering::AntiAliasingMode::SSAA) {
    effective_ssaa_scale = effective_aa.ssaa_scale;
    if (effective_ssaa_scale > 1.0f) {
      ssaa_enabled = true;
      render_width = std::max(1, static_cast<int>(std::ceil(static_cast<float>(output_width) *
                                                           effective_ssaa_scale)));
      render_height = std::max(1, static_cast<int>(std::ceil(static_cast<float>(output_height) *
                                                            effective_ssaa_scale)));
      effective_aa.msaa_samples = 1u;
    } else {
      effective_aa.mode = rendering::AntiAliasingMode::None;
      effective_aa.msaa_samples = 1u;
      effective_aa.ssaa_scale = 1.0f;
      effective_ssaa_scale = 1.0f;
    }
  }

  setActiveRasterSampleCount(raster_sample_count);
  if ((msaa_enabled || ssaa_enabled) &&
      !ensureCameraRasterResources(render_width,
                                   render_height,
                                   active_color_format,
                                   active_depth_format,
                                   raster_sample_count)) {
    effective_aa = rendering::clampAntiAliasingSettings({});
    msaa_enabled = false;
    ssaa_enabled = false;
    effective_ssaa_scale = 1.0f;
    render_width = output_width;
    render_height = output_height;
    setActiveRasterSampleCount(1u);
  }
  if (msaa_enabled || ssaa_enabled) {
    active_rtv = camera_raster_color_.rtv;
    active_dsv = camera_raster_depth_.dsv;
    particle_dsv = camera_raster_depth_.read_only_dsv ? camera_raster_depth_.read_only_dsv
                                                      : active_dsv;
    if (ssaa_enabled) {
      particle_scene_color_srv = camera_raster_color_.srv;
      particle_scene_depth_srv = camera_raster_depth_.srv;
      particle_scene_texture = camera_raster_color_.texture;
    } else {
      particle_scene_color_srv = output_color_srv;
      particle_scene_depth_srv = output_depth_srv;
      particle_scene_texture = output_color_texture;
    }
  }
  current_frame_timing_stats_.anti_aliasing_mode = effective_aa.mode;
  current_frame_timing_stats_.anti_aliasing_msaa_samples =
      msaa_enabled ? raster_sample_count : 1u;
  current_frame_timing_stats_.anti_aliasing_ssaa_scale =
      ssaa_enabled ? effective_ssaa_scale : 1.0f;
  current_frame_timing_stats_.raster_width = static_cast<uint32_t>(std::max(render_width, 0));
  current_frame_timing_stats_.raster_height = static_cast<uint32_t>(std::max(render_height, 0));
  current_frame_timing_stats_.output_width = static_cast<uint32_t>(std::max(output_width, 0));
  current_frame_timing_stats_.output_height = static_cast<uint32_t>(std::max(output_height, 0));
  mark_stage("target setup");

  Diligent::Uint32 draw_count = 0;
  auto log_layer_diag = [&](const char* reason) {
    const auto layer_end = core::SteadyClock::now();
    const double total_ms = core::elapsedMilliseconds(layer_start, layer_end);
    if (frame_active_) {
      current_frame_timing_stats_.render_layer_count += 1u;
      current_frame_timing_stats_.render_layer_total_ms += static_cast<float>(total_ms);
      current_frame_timing_stats_.render_layer_draws += draw_count;
    }
    if (startup_layer_diag) {
      logStartupDiag("diligent_render_layer", "total", layer_start, layer_end);
    }
    if (!frame_layer_diag) {
      return;
    }
    if (total_ms < static_cast<double>(frame_layer_diag_threshold_ms)) {
      return;
    }
    const auto command_stats = getRendererCommandStats();
    const uint32_t command_draws =
        command_stats.draw + command_stats.draw_indexed +
        command_stats.draw_indirect + command_stats.draw_indexed_indirect +
        command_stats.multi_draw + command_stats.multi_draw_indexed;
    spdlog::info(
        "Diligent render layer diag: reason={} layer={} target={} total={:.3f}ms "
        "size={}x{} output={}x{} aa_mode={} msaa={} ssaa={:.2f} "
        "camera_active={} post_enabled={} taa={} draws={} "
        "inst=[submitted={} batches={} sets={} shared_batches={} drawn={} culled_batches={} "
        "draw_calls={} uploads={} bytes={} "
        "gpu_cull=[batches={} dispatch={} candidates={} indirect={}] "
        "lod=[buckets={} dispatch={} candidates={} indirect={} fallback={}] "
        "collect={:.3f} upload={:.3f}] cmd_totals=[draws={} update_buffer={} copy_texture={} dispatch={}] "
        "stages=[target={:.3f} clear={:.3f} camera={:.3f} env={:.3f} fplus={:.3f} "
        "shadow={:.3f} terrain={:.3f} collect={:.3f} opaque={:.3f} transparent={:.3f} "
        "particle_res={:.3f} particle={:.3f} line_res={:.3f} line={:.3f} post={:.3f} copy={:.3f}] "
        "frame_timing=[res_create={}:{:.3f} pipeline_create={}:{:.3f} resize={}:{:.3f}]",
        reason,
        layer,
        target,
        total_ms,
        render_width,
        render_height,
        output_width,
        output_height,
        static_cast<int>(effective_aa.mode),
        raster_sample_count,
        ssaa_enabled ? effective_ssaa_scale : 1.0f,
        camera_active_,
        post_process.enabled,
        post_process.temporal_antialiasing_enabled,
        draw_count,
        instancing_stats_.submitted_instances,
        instancing_stats_.submitted_batches,
        instancing_stats_.submitted_instance_sets,
        instancing_stats_.shared_instance_batches,
        instancing_stats_.drawn_instances,
        instancing_stats_.culled_batches,
        instancing_stats_.draw_calls,
        instancing_stats_.instance_buffer_updates,
        instancing_stats_.instance_upload_bytes,
        instancing_stats_.gpu_culling_batches,
        instancing_stats_.gpu_culling_dispatches,
        instancing_stats_.gpu_culling_candidate_instances,
        instancing_stats_.gpu_indirect_draws,
        instancing_stats_.lod_bucket_count,
        instancing_stats_.lod_culling_dispatches,
        instancing_stats_.lod_candidate_instances,
        instancing_stats_.lod_indirect_draws,
        instancing_stats_.lod_fallbacks,
        instancing_stats_.forward_state_collection_ms,
        instancing_stats_.instance_upload_ms,
        command_draws,
        command_stats.update_buffer,
        command_stats.copy_texture,
        command_stats.dispatch_compute,
        current_frame_timing_stats_.target_setup_ms,
        current_frame_timing_stats_.clear_ms,
        current_frame_timing_stats_.camera_setup_ms,
        current_frame_timing_stats_.environment_ms,
        current_frame_timing_stats_.forward_plus_ms,
        current_frame_timing_stats_.shadow_ms,
        current_frame_timing_stats_.terrain_ms,
        current_frame_timing_stats_.forward_collect_ms,
        current_frame_timing_stats_.opaque_ms,
        current_frame_timing_stats_.transparent_ms,
        current_frame_timing_stats_.particle_resources_ms,
        current_frame_timing_stats_.particle_pass_ms,
        current_frame_timing_stats_.line_resources_ms,
        current_frame_timing_stats_.line_draw_ms,
        current_frame_timing_stats_.post_process_ms,
        current_frame_timing_stats_.present_copy_ms,
        current_frame_timing_stats_.resource_creation_count,
        current_frame_timing_stats_.resource_creation_ms,
        current_frame_timing_stats_.pipeline_creation_count,
        current_frame_timing_stats_.pipeline_creation_ms,
        current_frame_timing_stats_.resize_events,
        current_frame_timing_stats_.resize_ms);
    for (const RenderLayerStageTiming& timing : stage_timings) {
      if (timing.ms >= static_cast<double>(frame_layer_diag_stage_threshold_ms)) {
        spdlog::info("Diligent render layer stage: {} {:.3f}ms", timing.name, timing.ms);
      }
    }
  };

  bool msaa_resolve_dirty = msaa_enabled;
  auto mark_msaa_dirty = [&]() {
    if (msaa_enabled) {
      msaa_resolve_dirty = true;
    }
  };
  auto resolve_msaa_if_needed = [&]() -> bool {
    if (!msaa_enabled || !msaa_resolve_dirty) {
      return true;
    }
    if (!resolveMsaaCameraResources(camera_raster_color_.texture,
                                    output_color_texture,
                                    output_dsv,
                                    active_color_format,
                                    active_depth_format,
                                    raster_sample_count,
                                    render_width,
                                    render_height)) {
      return false;
    }
    particle_scene_texture = output_color_texture;
    particle_scene_color_srv = output_color_srv;
    particle_scene_depth_srv = camera_resolved_depth_.srv ? camera_resolved_depth_.srv.RawPtr()
                                                          : output_depth_srv;
    msaa_resolve_dirty = false;
    return true;
  };
  auto finalize_camera_output = [&]() -> bool {
    if (msaa_enabled && !resolve_msaa_if_needed()) {
      return false;
    }
    if (ssaa_enabled) {
      return runSsaaDownsample(camera_raster_color_.srv,
                               camera_raster_depth_.srv,
                               output_rtv,
                               output_dsv,
                               active_color_format,
                               active_depth_format,
                               render_width,
                               render_height,
                               output_width,
                               output_height);
    }
    return true;
  };
  auto clear_output_target = [&](const float* color, bool clear_depth) {
    if (!output_rtv) {
      return;
    }
    context_->SetRenderTargets(1,
                               &output_rtv,
                               output_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->ClearRenderTarget(output_rtv,
                                color,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (clear_depth && output_dsv) {
      context_->ClearDepthStencil(output_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
  };

  auto present_active_target = [&]() {
    if (!rendering_to_default_target || !present_source_srv || !present_destination_rtv) {
      return;
    }
    const Diligent::TEXTURE_FORMAT present_format =
        swap_chain_->GetDesc().ColorBufferFormat;
    const bool native_copy_supported = [&]() {
      if (!present_source_texture || !present_destination_texture) {
        return false;
      }
      const auto& source_desc = present_source_texture->GetDesc();
      const auto& destination_desc = present_destination_texture->GetDesc();
      return source_desc.SampleCount == 1u && destination_desc.SampleCount == 1u &&
             source_desc.Width == destination_desc.Width &&
             source_desc.Height == destination_desc.Height &&
             source_desc.Format == destination_desc.Format;
    }();
    if (native_copy_supported) {
      copyTextureAfterRender(present_source_texture,
                             present_destination_texture);
      return;
    }

    if (runPresentBlit(present_source_srv,
                       present_destination_rtv,
                       output_width,
                       output_height,
                       present_format)) {
      return;
    }

    static bool warned_present_blit_failure = false;
    if (!warned_present_blit_failure) {
      warned_present_blit_failure = true;
      spdlog::error("Failed to convert the HDR scene target into the swapchain backbuffer");
    }
  };

  auto bind_active_target = [&]() {
    context_->SetRenderTargets(1, &active_rtv, active_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(render_width);
    viewport.Height = static_cast<float>(render_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context_->SetViewports(1,
                           &viewport,
                           static_cast<Diligent::Uint32>(render_width),
                           static_cast<Diligent::Uint32>(render_height));
  };

  auto clear_active_target = [&](const float* color, bool clear_depth) {
    bind_active_target();
    context_->ClearRenderTarget(active_rtv, color, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (clear_depth && active_dsv) {
      context_->ClearDepthStencil(active_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
  };

  if (!camera_active_) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    clear_active_target(black, true);
    if ((msaa_enabled || ssaa_enabled) && !finalize_camera_output()) {
      clear_output_target(black, true);
    }
    present_active_target();
    mark_stage("clear inactive camera");
    log_layer_diag("inactive_camera");
    return;
  }

  clear_active_target(clear_color_, true);
  mark_stage("clear target");

  Diligent::IPipelineState* base_forward_pipeline =
      ensureForwardPipeline(ForwardPipelineVariant::Opaque);
  if (!constants_ || !base_forward_pipeline || !shader_resources_) {
    if (!warned_no_draws_) {
      warned_no_draws_ = true;
      spdlog::error(
          "Diligent renderer cannot draw: constants={}, forward_pipeline={}, "
          "shader_resources={}",
          constants_ ? "ready" : "missing",
          base_forward_pipeline ? "ready" : "missing",
          shader_resources_ ? "ready" : "missing");
    }
    finalize_camera_output();
    present_active_target();
    mark_stage("missing draw resources");
    log_layer_diag("missing_draw_resources");
    return;
  }
  bool use_custom_shader_override = !camera_.shader_override_fragment_path.empty();
  if (use_custom_shader_override && !ensureCameraOverridePipeline(camera_)) {
    use_custom_shader_override = false;
  }
  if (use_custom_shader_override) {
    updateCameraOverrideUserConstants(camera_);
  }

  const float aspect = (render_height > 0)
                           ? static_cast<float>(render_width) / static_cast<float>(render_height)
                           : camera_.aspect;
  // Particle and shadow passes run after target selection and consume camera_.aspect.
  // Keep it aligned with this pass, including portrait and offscreen targets.
  camera_.aspect = std::isfinite(aspect) && aspect > 0.0f ? aspect : 1.0f;
  glm::mat4 projection(1.0f);
  if (camera_.perspective) {
    projection = glm::perspective(glm::radians(camera_.fov_y_degrees),
                                  camera_.aspect,
                                  camera_.near_clip,
                                  camera_.far_clip);
  } else {
    projection = glm::ortho(camera_.ortho_left,
                            camera_.ortho_right,
                            camera_.ortho_bottom,
                            camera_.ortho_top,
                            camera_.near_clip,
                            camera_.far_clip);
  }
  glm::mat4 depth_fix(1.0f);
  depth_fix[2][2] = 0.5f;
  depth_fix[3][2] = 0.5f;
  const glm::mat3 cam_basis = glm::mat3_cast(camera_.rotation);
  glm::vec3 camera_position = camera_.position;
  glm::vec3 forward = cam_basis * glm::vec3(0.0f, 0.0f, -1.0f);
  glm::vec3 up = cam_basis * glm::vec3(0.0f, 1.0f, 0.0f);
  // Camera data is gameplay-authored and can transiently become degenerate.
  // Keep the render view basis finite and orthogonal before it reaches lookAt().
  if (!isFiniteVec3(camera_position)) {
    camera_position = glm::vec3(0.0f);
  }
  if (!isFiniteVec3(forward) || glm::dot(forward, forward) <= 1.0e-8f) {
    forward = glm::vec3(0.0f, 0.0f, -1.0f);
  } else {
    forward = glm::normalize(forward);
  }
  if (!isFiniteVec3(up) || glm::dot(up, up) <= 1.0e-8f ||
      glm::dot(glm::cross(forward, up), glm::cross(forward, up)) <= 1.0e-8f) {
    const glm::vec3 reference =
        std::abs(forward.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 right = glm::cross(forward, reference);
    if (!isFiniteVec3(right) || glm::dot(right, right) <= 1.0e-8f) {
      right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
      right = glm::normalize(right);
    }
    up = glm::normalize(glm::cross(right, forward));
  } else {
    up = glm::normalize(up);
  }
  const glm::mat4 view = glm::lookAt(camera_position, camera_position + forward, up);
  const glm::vec3 cam_forward = forward;
  glm::vec3 cam_up = up;
  glm::vec3 cam_right = glm::cross(cam_forward, cam_up);
  if (!isFiniteVec3(cam_right) || glm::dot(cam_right, cam_right) <= 1.0e-8f) {
    cam_right = glm::vec3(1.0f, 0.0f, 0.0f);
  } else {
    cam_right = glm::normalize(cam_right);
  }
  cam_up = glm::normalize(glm::cross(cam_right, cam_forward));
  mark_stage("camera setup");

  ensureEnvironmentResources();
  mark_stage("environment resources");
  bind_active_target();
  float env_max_mip = 0.0f;
  if (env_prefilter_tex_) {
    const auto& desc = env_prefilter_tex_->GetDesc();
    if (desc.MipLevels > 0) {
      env_max_mip = static_cast<float>(desc.MipLevels - 1);
    }
  }

  if (env_debug_mode_ > 0 && !warned_env_debug_) {
    warned_env_debug_ = true;
  }

  renderSkybox(projection, view);
  mark_msaa_dirty();
  mark_stage("skybox");

  const glm::mat4 camera_view_proj = projection * view;
  const Diligent::Uint32 forward_plus_tile_size =
      static_cast<Diligent::Uint32>(std::max(forward_plus_tile_size_, 1));
  const auto tile_count_for_extent = [forward_plus_tile_size](int extent) {
    const uint64_t safe_extent = static_cast<uint64_t>(std::max(extent, 1));
    return (safe_extent + static_cast<uint64_t>(forward_plus_tile_size) - 1u) /
           static_cast<uint64_t>(forward_plus_tile_size);
  };
  const uint64_t forward_plus_tiles_x_unclamped = tile_count_for_extent(render_width);
  const uint64_t forward_plus_tiles_y_unclamped = tile_count_for_extent(render_height);
  static constexpr Diligent::Uint32 kMaxForwardPlusTilesPerAxis = 2048;
  const Diligent::Uint32 forward_plus_tiles_x = static_cast<Diligent::Uint32>(
      std::min<uint64_t>(forward_plus_tiles_x_unclamped, kMaxForwardPlusTilesPerAxis));
  const Diligent::Uint32 forward_plus_tiles_y = static_cast<Diligent::Uint32>(
      std::min<uint64_t>(forward_plus_tiles_y_unclamped, kMaxForwardPlusTilesPerAxis));
  const Diligent::Uint32 forward_plus_max_lights_per_tile = static_cast<Diligent::Uint32>(
      std::max(forward_plus_max_lights_per_tile_, 1));
  Diligent::Uint32 active_forward_plus_tile_size = forward_plus_tile_size;
  Diligent::Uint32 active_forward_plus_tiles_x = forward_plus_tiles_x;
  Diligent::Uint32 active_forward_plus_tiles_y = forward_plus_tiles_y;
  Diligent::Uint32 active_forward_plus_max_lights_per_tile = forward_plus_max_lights_per_tile;
  bool forward_plus_ready = false;
  bool forward_plus_overflow_risk =
      forward_plus_tiles_x != forward_plus_tiles_x_unclamped ||
      forward_plus_tiles_y != forward_plus_tiles_y_unclamped;

  auto is_local_light = [](const rendering::LightData& light) {
    return light.type != rendering::LightType::Directional &&
           light.intensity > 0.0f &&
           light.range > 0.0f;
  };
  static thread_local std::vector<ForwardPlusGpuLight> forward_plus_lights_gpu;
  static thread_local std::vector<size_t> forward_plus_light_source_index;
  forward_plus_lights_gpu.clear();
  forward_plus_light_source_index.clear();
  if (forward_plus_lights_gpu.capacity() < lights_.size()) {
    forward_plus_lights_gpu.reserve(lights_.size());
  }
  if (forward_plus_light_source_index.capacity() < lights_.size()) {
    forward_plus_light_source_index.reserve(lights_.size());
  }
  for (size_t idx = 0; idx < lights_.size(); ++idx) {
    const auto& light = lights_[idx];
    if (is_local_light(light)) {
      glm::vec4 screen_rect(1.0f, 1.0f, 0.0f, 0.0f);
      if (!projectSphereToScreenRect(view,
                                     camera_view_proj,
                                     light.position,
                                     std::max(light.range, 0.0f),
                                     std::max(camera_.near_clip, 0.001f),
                                     static_cast<float>(std::max(render_width, 1)),
                                     static_cast<float>(std::max(render_height, 1)),
                                     screen_rect)) {
        continue;
      }
      forward_plus_lights_gpu.push_back(packForwardPlusLight(light, screen_rect));
      forward_plus_light_source_index.push_back(idx);
    }
  }
  const size_t max_forward_plus_light_count =
      static_cast<size_t>(std::max(forward_plus_max_local_lights_, 1));
  if (forward_plus_lights_gpu.size() > max_forward_plus_light_count) {
    forward_plus_lights_gpu.resize(max_forward_plus_light_count);
    forward_plus_light_source_index.resize(max_forward_plus_light_count);
    forward_plus_overflow_risk = true;
  }

  auto ensure_forward_plus_buffer = [&](Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer,
                                        Diligent::RefCntAutoPtr<Diligent::IBufferView>& srv,
                                        Diligent::RefCntAutoPtr<Diligent::IBufferView>* uav,
                                        size_t& capacity,
                                        size_t required_count,
                                        Diligent::Uint32 element_stride,
                                        Diligent::BIND_FLAGS bind_flags,
                                        const char* name) {
    const bool needs_uav = (bind_flags & Diligent::BIND_UNORDERED_ACCESS) != 0;
    const size_t safe_required = std::max<size_t>(required_count, 1);
    if (buffer && srv && capacity >= safe_required && (!needs_uav || (uav && *uav))) {
      return true;
    }
    if (element_stride == 0u ||
        safe_required > std::numeric_limits<Diligent::Uint64>::max() / element_stride) {
      return false;
    }
    const size_t grown_capacity =
        capacity > 0u && capacity <= std::numeric_limits<size_t>::max() / 2u
            ? capacity * 2u
            : safe_required;
    const size_t new_capacity = std::max(safe_required, grown_capacity);
    if (new_capacity > std::numeric_limits<Diligent::Uint64>::max() / element_stride) {
      return false;
    }
    Diligent::BufferDesc desc{};
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = element_stride;
    desc.Size = static_cast<Diligent::Uint64>(new_capacity) *
                static_cast<Diligent::Uint64>(element_stride);
    Diligent::RefCntAutoPtr<Diligent::IBuffer> new_buffer;
    const auto buffer_start = core::SteadyClock::now();
    device_->CreateBuffer(desc, nullptr, &new_buffer);
    recordResourceCreation("forward_plus", name, buffer_start, core::SteadyClock::now());
    if (!new_buffer) {
      return false;
    }
    Diligent::RefCntAutoPtr<Diligent::IBufferView> new_srv;
    new_srv = new_buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
    if (!new_srv) {
      return false;
    }
    Diligent::RefCntAutoPtr<Diligent::IBufferView> new_uav;
    if (needs_uav) {
      if (!uav) {
        return false;
      }
      new_uav = new_buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
      if (!new_uav) {
        return false;
      }
    }
    buffer = std::move(new_buffer);
    srv = std::move(new_srv);
    if (uav) {
      *uav = std::move(new_uav);
    }
    capacity = new_capacity;
    return true;
  };

  static constexpr size_t kCpuForwardPlusFallbackLightCount = 64u;
  // Avoid tiled setup for small light counts while retaining the full
  // DrawConstants budget when compute culling is unavailable.
  static constexpr size_t kCpuForwardPlusDirectLightCount = 8u;
  const bool forward_plus_compute_available =
      forward_plus_compute_pso_ && forward_plus_compute_srb_ && forward_plus_compute_cb_;
  std::array<ForwardPlusGpuLight, kCpuForwardPlusFallbackLightCount> cpu_forward_plus_lights{};
  Diligent::Uint32 cpu_forward_plus_light_count = 0;
  bool cpu_forward_plus_ready = false;
  auto enable_cpu_forward_plus_fallback = [&]() {
    active_forward_plus_tiles_x = 1;
    active_forward_plus_tiles_y = 1;
    active_forward_plus_tile_size = static_cast<Diligent::Uint32>(
        std::max(std::max(render_width, render_height), 1));
    active_forward_plus_max_lights_per_tile = static_cast<Diligent::Uint32>(
        std::max<size_t>(forward_plus_lights_gpu.size(), 1));
    active_forward_plus_max_lights_per_tile = std::min(
        active_forward_plus_max_lights_per_tile,
        static_cast<Diligent::Uint32>(kCpuForwardPlusFallbackLightCount));
    cpu_forward_plus_light_count = static_cast<Diligent::Uint32>(std::min<size_t>(
        forward_plus_lights_gpu.size(), kCpuForwardPlusFallbackLightCount));
    for (Diligent::Uint32 i = 0; i < cpu_forward_plus_light_count; ++i) {
      cpu_forward_plus_lights[i] = forward_plus_lights_gpu[i];
    }
    if (forward_plus_lights_gpu.size() > static_cast<size_t>(cpu_forward_plus_light_count)) {
      forward_plus_overflow_risk = true;
    }
    cpu_forward_plus_ready = cpu_forward_plus_light_count > 0;
  };

  const bool can_use_forward_plus =
      !forward_plus_lights_gpu.empty() && render_width > 0 && render_height > 0;
  const bool prefer_cpu_forward_plus_fallback =
      can_use_forward_plus &&
      (!forward_plus_compute_available ||
       forward_plus_lights_gpu.size() <= kCpuForwardPlusDirectLightCount);

  if (prefer_cpu_forward_plus_fallback) {
    enable_cpu_forward_plus_fallback();
  }

  if (!cpu_forward_plus_ready && can_use_forward_plus && forward_plus_compute_available) {
    const size_t forward_plus_tile_count =
        static_cast<size_t>(forward_plus_tiles_x) * static_cast<size_t>(forward_plus_tiles_y);
    const size_t forward_plus_index_count =
        forward_plus_tile_count * static_cast<size_t>(forward_plus_max_lights_per_tile);
    static constexpr size_t kMaxForwardPlusIndexCount = 8u * 1024u * 1024u;

    if (forward_plus_index_count <= kMaxForwardPlusIndexCount) {
      const bool buffers_ready =
          ensure_forward_plus_buffer(forward_plus_light_buffer_,
                                     forward_plus_light_srv_,
                                     nullptr,
                                     forward_plus_light_capacity_,
                                     forward_plus_lights_gpu.size(),
                                     static_cast<Diligent::Uint32>(sizeof(ForwardPlusGpuLight)),
                                     Diligent::BIND_SHADER_RESOURCE,
                                     "Karma Forward+ Lights") &&
          ensure_forward_plus_buffer(forward_plus_tile_count_buffer_,
                                     forward_plus_tile_count_srv_,
                                     std::addressof(forward_plus_tile_count_uav_),
                                     forward_plus_tile_count_capacity_,
                                     forward_plus_tile_count,
                                     static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                     Diligent::BIND_SHADER_RESOURCE |
                                         Diligent::BIND_UNORDERED_ACCESS,
                                     "Karma Forward+ Tile Counts") &&
          ensure_forward_plus_buffer(forward_plus_tile_index_buffer_,
                                     forward_plus_tile_index_srv_,
                                     std::addressof(forward_plus_tile_index_uav_),
                                     forward_plus_tile_index_capacity_,
                                     forward_plus_index_count,
                                     static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                     Diligent::BIND_SHADER_RESOURCE |
                                         Diligent::BIND_UNORDERED_ACCESS,
                                     "Karma Forward+ Tile Indices");

      if (buffers_ready) {
        context_->UpdateBuffer(forward_plus_light_buffer_,
                               0,
                               static_cast<Diligent::Uint32>(forward_plus_lights_gpu.size() *
                                                             sizeof(ForwardPlusGpuLight)),
                               forward_plus_lights_gpu.data(),
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        ForwardPlusComputeConstants fp_constants{};
        copyMat4(fp_constants.view_proj, camera_view_proj);
        fp_constants.forward_plus_params[0] = static_cast<float>(active_forward_plus_tile_size);
        fp_constants.forward_plus_params[1] = static_cast<float>(active_forward_plus_tiles_x);
        fp_constants.forward_plus_params[2] = static_cast<float>(active_forward_plus_tiles_y);
        fp_constants.forward_plus_params[3] =
            static_cast<float>(active_forward_plus_max_lights_per_tile);
        fp_constants.screen_params[0] = static_cast<float>(render_width);
        fp_constants.screen_params[1] = static_cast<float>(render_height);
        fp_constants.screen_params[2] = static_cast<float>(forward_plus_lights_gpu.size());
        fp_constants.screen_params[3] = 0.0f;
        bool constants_ready = false;
        {
          Diligent::MapHelper<ForwardPlusComputeConstants> mapped(
              context_, forward_plus_compute_cb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
          if (auto* mapped_constants = getMappedData(mapped)) {
            *mapped_constants = fp_constants;
            constants_ready = true;
          }
        }

        if (constants_ready && forward_plus_compute_lights_var_) {
          forward_plus_compute_lights_var_->Set(forward_plus_light_srv_);
        }
        if (constants_ready && forward_plus_compute_tile_counts_var_) {
          forward_plus_compute_tile_counts_var_->Set(forward_plus_tile_count_uav_);
        }
        if (constants_ready && forward_plus_compute_tile_indices_var_) {
          forward_plus_compute_tile_indices_var_->Set(forward_plus_tile_index_uav_);
        }

        if (constants_ready) {
          context_->SetPipelineState(forward_plus_compute_pso_);
          context_->CommitShaderResources(forward_plus_compute_srb_,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          static constexpr Diligent::Uint32 kForwardPlusGroupSize = 8u;
          Diligent::DispatchComputeAttribs dispatch{};
          dispatch.ThreadGroupCountX =
              (forward_plus_tiles_x + kForwardPlusGroupSize - 1u) / kForwardPlusGroupSize;
          dispatch.ThreadGroupCountY =
              (forward_plus_tiles_y + kForwardPlusGroupSize - 1u) / kForwardPlusGroupSize;
          dispatch.ThreadGroupCountZ = 1;
          context_->DispatchCompute(dispatch);

          Diligent::StateTransitionDesc barriers[2];
          barriers[0].pResource = forward_plus_tile_count_buffer_;
          barriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
          barriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
          barriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
          barriers[1].pResource = forward_plus_tile_index_buffer_;
          barriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
          barriers[1].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
          barriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
          context_->TransitionResourceStates(2, barriers);

          forward_plus_ready = true;
        } else {
          forward_plus_overflow_risk = true;
          enable_cpu_forward_plus_fallback();
        }
      } else {
        forward_plus_overflow_risk = true;
        enable_cpu_forward_plus_fallback();
      }
    } else {
      forward_plus_overflow_risk = true;
      enable_cpu_forward_plus_fallback();
    }
  }

  forward_plus_overflow_risk =
      forward_plus_overflow_risk ||
      (forward_plus_lights_gpu.size() >
       static_cast<size_t>(active_forward_plus_max_lights_per_tile));
  forward_plus_stats_.tile_size = active_forward_plus_tile_size;
  forward_plus_stats_.max_lights_per_tile = active_forward_plus_max_lights_per_tile;
  forward_plus_stats_.tiles_x = active_forward_plus_tiles_x;
  forward_plus_stats_.tiles_y = active_forward_plus_tiles_y;
  forward_plus_stats_.local_light_count =
      static_cast<uint32_t>(std::min<size_t>(forward_plus_lights_gpu.size(),
                                             std::numeric_limits<uint32_t>::max()));
  forward_plus_stats_.active = forward_plus_ready || cpu_forward_plus_ready;
  forward_plus_stats_.cpu_fallback = cpu_forward_plus_ready;
  forward_plus_stats_.overflow_risk = forward_plus_overflow_risk;

  Diligent::IBufferView* desired_forward_plus_light_srv = nullptr;
  Diligent::IBufferView* desired_forward_plus_tile_count_srv = nullptr;
  Diligent::IBufferView* desired_forward_plus_tile_index_srv = nullptr;
  if (forward_plus_ready) {
    desired_forward_plus_light_srv = forward_plus_light_srv_;
    desired_forward_plus_tile_count_srv = forward_plus_tile_count_srv_;
    desired_forward_plus_tile_index_srv = forward_plus_tile_index_srv_;
  } else {
    const bool fallback_srvs_ready =
        ensure_forward_plus_buffer(forward_plus_light_buffer_,
                                   forward_plus_light_srv_,
                                   nullptr,
                                   forward_plus_light_capacity_,
                                   1u,
                                   static_cast<Diligent::Uint32>(sizeof(ForwardPlusGpuLight)),
                                   Diligent::BIND_SHADER_RESOURCE,
                                   "Karma Forward+ Fallback Lights") &&
        ensure_forward_plus_buffer(forward_plus_tile_count_buffer_,
                                   forward_plus_tile_count_srv_,
                                   nullptr,
                                   forward_plus_tile_count_capacity_,
                                   1u,
                                   static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                   Diligent::BIND_SHADER_RESOURCE,
                                   "Karma Forward+ Fallback Tile Counts") &&
        ensure_forward_plus_buffer(forward_plus_tile_index_buffer_,
                                   forward_plus_tile_index_srv_,
                                   nullptr,
                                   forward_plus_tile_index_capacity_,
                                   1u,
                                   static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                   Diligent::BIND_SHADER_RESOURCE,
                                   "Karma Forward+ Fallback Tile Indices");
    if (fallback_srvs_ready) {
      desired_forward_plus_light_srv = forward_plus_light_srv_;
      desired_forward_plus_tile_count_srv = forward_plus_tile_count_srv_;
      desired_forward_plus_tile_index_srv = forward_plus_tile_index_srv_;
    }
  }
  active_forward_plus_light_srv_ = desired_forward_plus_light_srv;
  active_forward_plus_tile_count_srv_ = desired_forward_plus_tile_count_srv;
  active_forward_plus_tile_index_srv_ = desired_forward_plus_tile_index_srv;
  mark_stage("forward plus setup");

  const float shadow_map_extent =
      shadow_map_tex_ ? static_cast<float>(shadow_map_tex_->GetDesc().Width)
                      : static_cast<float>(shadow_map_size_);
  const float safe_shadow_map_extent = std::max(shadow_map_extent, 1.0f);
  const float shadow_texel_size = 1.0f / safe_shadow_map_extent;
  const float point_shadow_map_extent =
      point_shadow_map_tex_ ? static_cast<float>(point_shadow_map_tex_->GetDesc().Width)
                            : static_cast<float>(point_shadow_map_size_);
  const float safe_point_shadow_map_extent = std::max(point_shadow_map_extent, 1.0f);
  const float point_shadow_texel_size = 1.0f / safe_point_shadow_map_extent;
  const float fixed_bias = std::max(shadow_bias_, 0.0f);
  const float shadow_texel_param = shadow_texel_size;
  const bool is_gl = device_->GetDeviceInfo().IsGLDevice();
  const bool camera_renders_shadows = camera_.render_shadows;
  const bool directional_light_casts_shadows = directional_light_.casts_shadows;

  ShadowLayerState shadow_state{};
  renderShadowLayer(layer,
                    aspect,
                    depth_fix,
                    camera_position,
                    cam_forward,
                    cam_up,
                    cam_right,
                    is_gl,
                    fixed_bias,
                    shadow_texel_param,
                    point_shadow_texel_size,
                    forward_plus_light_source_index,
                    shadow_state);
  mark_stage("shadow layer");

  auto& cascade_light_view_proj = shadow_state.cascade_light_view_proj;
  auto& cascade_shadow_uv_proj = shadow_state.cascade_shadow_uv_proj;
  auto& cascade_world_texel = shadow_state.cascade_world_texel;
  auto& cascade_splits = shadow_state.cascade_splits;
  auto& point_shadow_uv_proj = shadow_state.point_shadow_uv_proj;
  auto& point_shadow_local_light_indices = shadow_state.point_shadow_local_light_indices;
  const Diligent::Uint32 point_shadow_light_count = shadow_state.point_shadow_light_count;
  bool point_shadow_ready = shadow_state.point_shadow_ready;

  const glm::mat4 light_view_proj = cascade_light_view_proj[0];
  const glm::mat4 shadow_uv_proj = cascade_shadow_uv_proj[0];
  const float shadow_world_texel = cascade_world_texel[0];

  bool has_point_shadow_dsv = false;
  for (const auto& dsv : point_shadow_map_dsv_faces_) {
    if (dsv) {
      has_point_shadow_dsv = true;
      break;
    }
  }
  for (Diligent::Uint32 slot = 0; slot < point_shadow_light_count; ++slot) {
    const size_t local_light_index = point_shadow_local_light_indices[slot];
    if (local_light_index < forward_plus_lights_gpu.size()) {
      forward_plus_lights_gpu[local_light_index].spot_params[2] =
          point_shadow_slot_valid_[slot] ? static_cast<float>(slot) : -1.0f;
    }
  }
  if (cpu_forward_plus_ready) {
    const Diligent::Uint32 copy_count = static_cast<Diligent::Uint32>(std::min<size_t>(
        forward_plus_lights_gpu.size(), static_cast<size_t>(cpu_forward_plus_light_count)));
    for (Diligent::Uint32 i = 0; i < copy_count; ++i) {
      cpu_forward_plus_lights[i] = forward_plus_lights_gpu[i];
    }
  }
  if (forward_plus_ready && forward_plus_light_buffer_ && !forward_plus_lights_gpu.empty()) {
    context_->UpdateBuffer(forward_plus_light_buffer_,
                           0,
                           static_cast<Diligent::Uint32>(forward_plus_lights_gpu.size() *
                                                         sizeof(ForwardPlusGpuLight)),
                           forward_plus_lights_gpu.data(),
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }

  struct ForwardResourceBindingSnapshot {
    const DiligentBackend* backend = nullptr;
    Diligent::IBufferView* light_srv = nullptr;
    Diligent::IBufferView* tile_count_srv = nullptr;
    Diligent::IBufferView* tile_index_srv = nullptr;
    Diligent::ITextureView* shadow_srv = nullptr;
    Diligent::ITextureView* point_shadow_srv = nullptr;
    Diligent::ISampler* shadow_sampler = nullptr;
    Diligent::IShaderResourceBinding* main_srb = nullptr;
    Diligent::IShaderResourceBinding* camera_override_srb = nullptr;
  };
  static thread_local ForwardResourceBindingSnapshot binding_snapshot;
  const bool forward_resources_changed =
      binding_snapshot.backend != this ||
      binding_snapshot.light_srv != desired_forward_plus_light_srv ||
      binding_snapshot.tile_count_srv != desired_forward_plus_tile_count_srv ||
      binding_snapshot.tile_index_srv != desired_forward_plus_tile_index_srv ||
      binding_snapshot.shadow_srv != shadow_map_srv_.RawPtr() ||
      binding_snapshot.point_shadow_srv != point_shadow_map_srv_.RawPtr() ||
      binding_snapshot.shadow_sampler != shadow_sampler_.RawPtr() ||
      binding_snapshot.main_srb != shader_resources_.RawPtr() ||
      binding_snapshot.camera_override_srb != camera_override_srb_.RawPtr();
  if (forward_resources_changed) {
    auto bind_forward_plus_to_srb = [&](Diligent::IShaderResourceBinding* srb) {
      if (!srb ||
          !desired_forward_plus_light_srv ||
          !desired_forward_plus_tile_count_srv ||
          !desired_forward_plus_tile_index_srv) {
        return;
      }
      constexpr auto overwrite = Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE;
      if (auto* var =
              srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusLights")) {
        var->Set(desired_forward_plus_light_srv, overwrite);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                             "g_ForwardPlusTileLightCounts")) {
        var->Set(desired_forward_plus_tile_count_srv, overwrite);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                             "g_ForwardPlusTileLightIndices")) {
        var->Set(desired_forward_plus_tile_index_srv, overwrite);
      }
    };
    auto bind_resources_to_srb = [&](Diligent::IShaderResourceBinding* srb) {
      bind_forward_plus_to_srb(srb);
      bindShadowResourcesToSrb(srb);
    };
    bind_resources_to_srb(shader_resources_);
    bind_resources_to_srb(default_material_srb_);
    bind_resources_to_srb(opaque_double_sided_default_material_srb_);
    bind_resources_to_srb(transparent_default_material_srb_);
    bind_resources_to_srb(transparent_double_sided_default_material_srb_);
    bind_resources_to_srb(additive_default_material_srb_);
    bind_resources_to_srb(additive_double_sided_default_material_srb_);
    bind_resources_to_srb(camera_override_srb_);
    for (auto& srb : compact_default_material_srbs_) {
      bind_resources_to_srb(srb);
    }
    for (auto& entry : materials_) {
      bind_resources_to_srb(entry.second.srb);
      bind_resources_to_srb(entry.second.transparent_srb);
      bind_resources_to_srb(entry.second.transparent_double_sided_srb);
      bind_resources_to_srb(entry.second.additive_srb);
      bind_resources_to_srb(entry.second.additive_double_sided_srb);
      for (auto& srb : entry.second.layout_srbs) {
        bind_resources_to_srb(srb);
      }
      for (auto& srb : entry.second.layout_custom_srbs) {
        bind_resources_to_srb(srb);
      }
    }
    binding_snapshot = ForwardResourceBindingSnapshot{
        .backend = this,
        .light_srv = desired_forward_plus_light_srv,
        .tile_count_srv = desired_forward_plus_tile_count_srv,
        .tile_index_srv = desired_forward_plus_tile_index_srv,
        .shadow_srv = shadow_map_srv_.RawPtr(),
        .point_shadow_srv = point_shadow_map_srv_.RawPtr(),
        .shadow_sampler = shadow_sampler_.RawPtr(),
        .main_srb = shader_resources_.RawPtr(),
        .camera_override_srb = camera_override_srb_.RawPtr(),
    };
  }
  mark_stage("forward resource bindings");

  auto* rtv = active_rtv;
  auto* dsv = active_dsv;
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(render_width);
  viewport.Height = static_cast<float>(render_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(render_width),
                         static_cast<Diligent::Uint32>(render_height));

  Diligent::IPipelineState* active_forward_pipeline =
      use_custom_shader_override ? camera_override_pipeline_state_.RawPtr() : pipeline_state_.RawPtr();
  if (!active_forward_pipeline) {
    return;
  }
  context_->SetPipelineState(active_forward_pipeline);

  static bool logged_frame = false;
  if (!logged_frame) {
    logged_frame = true;
  }

  Diligent::Uint32 skipped_hidden = 0;
  Diligent::Uint32 skipped_missing_vb = 0;
  Diligent::Uint32 skipped_missing_mesh = 0;
  Diligent::Uint32 skipped_layer = 0;

  const glm::mat4 view_proj = depth_fix * projection * view;
  DrawConstants base_constants{};
  copyMat4(base_constants.instance_batch_transform, glm::mat4(1.0f));
  for (size_t slot = 0; slot < MaterialRecord::kTextureCoordSlotCount; ++slot) {
    base_constants.texcoord_row0[slot][0] = 1.0f;
    base_constants.texcoord_row0[slot][1] = 0.0f;
    base_constants.texcoord_row0[slot][2] = 0.0f;
    base_constants.texcoord_row0[slot][3] = 0.0f;
    base_constants.texcoord_row1[slot][0] = 0.0f;
    base_constants.texcoord_row1[slot][1] = 1.0f;
    base_constants.texcoord_row1[slot][2] = 0.0f;
    base_constants.texcoord_row1[slot][3] = 0.0f;
  }
  copyMat4(base_constants.mvp, view_proj);
  copyMat4(base_constants.light_view_proj, light_view_proj);
  copyMat4(base_constants.shadow_uv_proj, shadow_uv_proj);
  for (int idx = 0; idx < kShadowCascadeCount; ++idx) {
    copyMat4(base_constants.shadow_cascade_uv_proj[idx], cascade_shadow_uv_proj[idx]);
    base_constants.shadow_cascade_splits[idx] = cascade_splits[idx];
    base_constants.shadow_cascade_world_texel[idx] = cascade_world_texel[idx];
  }
  for (int idx = 0; idx < kPointShadowMatrixCount; ++idx) {
    copyMat4(base_constants.point_shadow_uv_proj[idx], point_shadow_uv_proj[idx]);
  }
  base_constants.shadow_cascade_params[0] = 0.08f;
  base_constants.shadow_cascade_params[1] = 0.0f;
  base_constants.shadow_cascade_params[2] = 0.0f;
  base_constants.shadow_cascade_params[3] = 0.0f;
  bool has_shadow_cascade_dsv = false;
  for (const auto& dsv : shadow_map_dsv_cascades_) {
    if (dsv) {
      has_shadow_cascade_dsv = true;
      break;
    }
  }
  const bool shadow_ready = camera_renders_shadows && directional_light_casts_shadows &&
                            directional_shadow_cache_valid_ &&
                            shadow_pipeline_state_ && shadow_map_srv_ &&
                            has_shadow_cascade_dsv && shadow_sampler_;
  base_constants.shadow_params[0] = shadow_ready ? 1.0f : 0.0f;
  base_constants.shadow_params[1] = fixed_bias;
  base_constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
  base_constants.shadow_params[3] = shadow_texel_param;
  const bool point_shadow_enabled =
      camera_renders_shadows &&
      point_shadow_ready && point_shadow_map_srv_ && has_point_shadow_dsv &&
      point_shadow_light_count > 0;
  base_constants.point_shadow_params[0] = point_shadow_enabled ? 1.0f : 0.0f;
  base_constants.point_shadow_params[1] = point_shadow_texel_size;
  base_constants.point_shadow_params[2] = static_cast<float>(point_shadow_map_size_);
  base_constants.point_shadow_params[3] = static_cast<float>(point_shadow_light_count);
  base_constants.local_light_params[0] = local_light_distance_damping_;
  base_constants.local_light_params[1] = local_light_range_exponent_;
  base_constants.local_light_params[2] = ao_affects_local_lights_ ? 1.0f : 0.0f;
  base_constants.local_light_params[3] = local_light_directional_shadow_lift_;
  base_constants.point_shadow_tuning[0] = point_shadow_constant_bias_;
  base_constants.point_shadow_tuning[1] = point_shadow_slope_bias_scale_;
  base_constants.point_shadow_tuning[2] = point_shadow_normal_bias_scale_;
  base_constants.point_shadow_tuning[3] = point_shadow_receiver_bias_scale_;
  base_constants.shadow_bias_params[0] = shadow_receiver_bias_scale_;
  base_constants.shadow_bias_params[1] = shadow_normal_bias_scale_;
  base_constants.shadow_bias_params[2] = shadow_world_texel;
  base_constants.shadow_bias_params[3] = 0.0f;

  glm::vec3 light_dir = directional_light_.direction;
  if (glm::length(light_dir) < 1e-4f) {
    light_dir = glm::vec3(0.3f, 1.0f, 0.2f);
  }
  light_dir = glm::normalize(light_dir);
  base_constants.light_dir[0] = light_dir.x;
  base_constants.light_dir[1] = light_dir.y;
  base_constants.light_dir[2] = light_dir.z;
  base_constants.light_dir[3] = directional_light_.mixed_bake_mask_bit < 64u
                                    ? static_cast<float>(
                                          directional_light_.mixed_bake_mask_bit)
                                    : -1.0f;
  base_constants.light_color[0] = directional_light_.color.r * directional_light_.intensity;
  base_constants.light_color[1] = directional_light_.color.g * directional_light_.intensity;
  base_constants.light_color[2] = directional_light_.color.b * directional_light_.intensity;
  base_constants.light_color[3] = 1.0f;
  base_constants.camera_pos[0] = camera_.position.x;
  base_constants.camera_pos[1] = camera_.position.y;
  base_constants.camera_pos[2] = camera_.position.z;
  base_constants.camera_pos[3] = 1.0f;
  base_constants.camera_forward[0] = cam_forward.x;
  base_constants.camera_forward[1] = cam_forward.y;
  base_constants.camera_forward[2] = cam_forward.z;
  base_constants.camera_forward[3] = 0.0f;
  base_constants.screen_params[0] = static_cast<float>(render_width);
  base_constants.screen_params[1] = static_cast<float>(render_height);
  base_constants.screen_params[2] =
      render_width > 0 ? 1.0f / static_cast<float>(render_width) : 0.0f;
  base_constants.screen_params[3] =
      render_height > 0 ? 1.0f / static_cast<float>(render_height) : 0.0f;
  base_constants.camera_clip_params[0] = std::max(camera_.near_clip, 0.001f);
  base_constants.camera_clip_params[1] =
      std::max(camera_.far_clip, base_constants.camera_clip_params[0] + 0.001f);
  base_constants.camera_clip_params[2] = camera_.perspective ? 1.0f : 0.0f;
  base_constants.camera_clip_params[3] =
      editor_view_mode_ != 0u ? static_cast<float>(editor_view_mode_)
                              : (debug_glossy_off_ ? 1.0f : 0.0f);
  if (cpu_forward_plus_ready) {
    base_constants.forward_plus_params[0] = 1.0f;
    base_constants.forward_plus_params[1] = 1.0f;
    base_constants.forward_plus_params[2] = 1.0f;
    base_constants.forward_plus_params[3] = 0.0f;
    base_constants.local_light_meta[0] = static_cast<float>(cpu_forward_plus_light_count);
    base_constants.local_light_meta[1] = static_cast<float>(cpu_forward_plus_light_count);
    base_constants.local_light_meta[2] = 0.0f;
    base_constants.local_light_meta[3] = 0.0f;
    for (Diligent::Uint32 idx = 0; idx < cpu_forward_plus_light_count; ++idx) {
      std::memcpy(base_constants.local_light_position_range[idx],
                  cpu_forward_plus_lights[idx].position_range,
                  sizeof(cpu_forward_plus_lights[idx].position_range));
      std::memcpy(base_constants.local_light_direction_type[idx],
                  cpu_forward_plus_lights[idx].direction_type,
                  sizeof(cpu_forward_plus_lights[idx].direction_type));
      std::memcpy(base_constants.local_light_color_intensity[idx],
                  cpu_forward_plus_lights[idx].color_intensity,
                  sizeof(cpu_forward_plus_lights[idx].color_intensity));
      std::memcpy(base_constants.local_light_spot_params[idx],
                  cpu_forward_plus_lights[idx].spot_params,
                  sizeof(cpu_forward_plus_lights[idx].spot_params));
    }
  } else {
    base_constants.forward_plus_params[0] = static_cast<float>(active_forward_plus_tile_size);
    base_constants.forward_plus_params[1] = static_cast<float>(active_forward_plus_tiles_x);
    base_constants.forward_plus_params[2] = static_cast<float>(active_forward_plus_tiles_y);
    base_constants.forward_plus_params[3] =
        forward_plus_ready ? static_cast<float>(active_forward_plus_max_lights_per_tile) : 0.0f;
    base_constants.local_light_meta[0] = 0.0f;
    base_constants.local_light_meta[1] = static_cast<float>(forward_plus_lights_gpu.size());
    base_constants.local_light_meta[2] = 0.0f;
    base_constants.local_light_meta[3] = 0.0f;
  }
  base_constants.local_light_meta[3] = static_cast<float>(accumulated_time_seconds_);
  base_constants.env_params[0] = environment_intensity_;
  base_constants.env_params[1] = env_max_mip;
  base_constants.env_params[2] = static_cast<float>(env_debug_mode_);
  base_constants.env_params[3] =
      post_process_settings_.tone_mapping_enabled ? -lighting_exposure_ : lighting_exposure_;

  const Diligent::Uint32 terrain_draws = renderTerrainLayer(layer,
                                                            base_constants,
                                                            view_proj,
                                                            is_gl,
                                                            rtv,
                                                            dsv,
                                                            render_width,
                                                            render_height);
  draw_count += terrain_draws;
  if (terrain_draws > 0u) {
    mark_msaa_dirty();
  }
  mark_stage("terrain pass");

  static thread_local ForwardLayerState forward_state;
  ForwardLayerStats forward_stats{};
  const auto forward_collect_start = core::SteadyClock::now();
  collectForwardLayerState(layer,
                           view_proj,
                           camera_.position,
                           cam_forward,
                           is_gl,
                           forward_state,
                           forward_stats);
  instancing_stats_.forward_state_collection_ms =
      static_cast<float>(core::elapsedMilliseconds(forward_collect_start,
                                                   core::SteadyClock::now()));
  mark_stage("collect forward state");
  skipped_hidden += forward_stats.skipped_hidden;
  skipped_missing_vb += forward_stats.skipped_missing_vb;
  skipped_missing_mesh += forward_stats.skipped_missing_mesh;
  skipped_layer += forward_stats.skipped_layer;
  instancing_stats_.culled_batches += forward_stats.instanced_culled_batches;

  const Diligent::Uint32 opaque_draws = renderOpaqueForwardLayer(forward_state,
                                                                 base_constants,
                                                                 active_forward_pipeline,
                                                                 use_custom_shader_override,
                                                                 rtv,
                                                                 dsv,
                                                                 render_width,
                                                                 render_height,
                                                                 is_gl);
  draw_count += opaque_draws;
  if (opaque_draws > 0u) {
    mark_msaa_dirty();
  }
  mark_stage("opaque pass");

  bool has_particle_work = !particle_emitter_runtime_states_.empty();
  bool has_beam_work = false;
  bool allow_distortion_particles = false;
  bool require_scene_color_copy = !forward_state.scene_reflection_draws.empty() ||
                                  !forward_state.pre_particle_scene_sample_draws.empty();
  Diligent::ITextureView* particle_scene_color_sample_srv = particle_scene_color_srv;
  particle_pass_stats_.pre_particle_scene_sample_draws += static_cast<uint32_t>(
      std::min<std::size_t>(forward_state.pre_particle_scene_sample_draws.size(),
                            static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
  for (const auto& batch : particle_batches_) {
    if (batch.layer != layer || batch.particles.empty()) {
      continue;
    }
    has_particle_work = true;
    if (batch.blend_mode == rendering::ParticleBlendMode::Distortion) {
      allow_distortion_particles = true;
      require_scene_color_copy = true;
    }
    if (batch.shading_mode == rendering::ParticleShadingMode::Shell) {
      require_scene_color_copy = true;
    }
  }
  for (const auto& submission : particle_emitter_submissions_) {
    const auto& emitter = submission.desc;
    if (emitter.layer != layer || emitter.max_particles == 0u) {
      continue;
    }
    has_particle_work = true;
    if (!emitter.visible || !emitter.enabled) {
      continue;
    }
    if (emitter.blend_mode == rendering::ParticleBlendMode::Distortion) {
      allow_distortion_particles = true;
      require_scene_color_copy = true;
    }
    if (emitter.shading_mode == rendering::ParticleShadingMode::Shell) {
      require_scene_color_copy = true;
    }
  }
  for (const auto& submission : particle_beam_submissions_) {
    const auto& beam = submission.desc;
    if (beam.layer != layer ||
        !beam.enabled ||
        !beam.visible ||
        beam.local_path_points.size() < 2u ||
        beam.start_width <= 0.0f ||
        beam.end_width <= 0.0f ||
        beam.blend_mode == rendering::ParticleBlendMode::Distortion) {
      continue;
    }
    has_beam_work = true;
    break;
  }

  if (has_particle_work) {
    ensureParticleResources();
  }
  mark_stage(has_particle_work ? "particle resources prewarm" : "particle resources skipped");
  if (has_beam_work) {
    ensureParticleBeamResources();
  }
  mark_stage(has_beam_work ? "particle beam resources ensure" : "particle beam resources skipped");
  ensureLineResources();
  mark_stage("line resources ensure");

  particle_pass_stats_.distortion_present =
      particle_pass_stats_.distortion_present || allow_distortion_particles;
  if (require_scene_color_copy && particle_scene_texture) {
    if (msaa_enabled && !resolve_msaa_if_needed()) {
      particle_scene_texture = nullptr;
    }
  }
  if (require_scene_color_copy && particle_scene_texture) {
    ensureParticleSceneCopyResources(render_width,
                                     render_height,
                                     particle_scene_texture->GetDesc().Format);
    if (particle_scene_color_copy_tex_ && particle_scene_color_copy_srv_) {
      copyTextureAfterRender(particle_scene_texture,
                             particle_scene_color_copy_tex_);
      particle_pass_stats_.scene_color_copy = true;
      particle_scene_color_sample_srv = particle_scene_color_copy_srv_;
    }
  }

  if (!forward_state.scene_reflection_draws.empty()) {
    const Diligent::Uint32 reflection_draws =
        renderTransparentForwardDraws(forward_state.scene_reflection_draws,
                                      base_constants,
                                      active_forward_pipeline,
                                      use_custom_shader_override,
                                      rtv,
                                      dsv,
                                      particle_dsv,
                                      render_width,
                                      render_height,
                                      particle_scene_color_sample_srv,
                                      particle_scene_depth_srv);
    draw_count += reflection_draws;
    if (reflection_draws > 0u) {
      mark_msaa_dirty();
    }
  }

  if (!forward_state.pre_particle_scene_sample_draws.empty()) {
    const Diligent::Uint32 pre_particle_sample_draws =
        renderTransparentForwardDraws(forward_state.pre_particle_scene_sample_draws,
                                      base_constants,
                                      active_forward_pipeline,
                                      use_custom_shader_override,
                                      rtv,
                                      dsv,
                                      particle_dsv,
                                      render_width,
                                      render_height,
                                      particle_scene_color_sample_srv,
                                      particle_scene_depth_srv);
    draw_count += pre_particle_sample_draws;
    if (pre_particle_sample_draws > 0u) {
      mark_msaa_dirty();
    }
  }

  const Diligent::Uint32 transparent_draws =
      renderTransparentForwardDraws(forward_state.transparent_draws,
                                    base_constants,
                                    active_forward_pipeline,
                                    use_custom_shader_override,
                                    rtv,
                                    dsv,
                                    particle_dsv,
                                    render_width,
                                    render_height,
                                    nullptr,
                                    nullptr);
  draw_count += transparent_draws;
  if (transparent_draws > 0u) {
    mark_msaa_dirty();
  }
  mark_stage("transparent pre-particle pass");

  if (msaa_enabled && (has_particle_work || has_beam_work)) {
    resolve_msaa_if_needed();
  }

  ParticlePassContext particle_pass{};
  particle_pass.view_proj = view_proj;
  particle_pass.camera_forward = cam_forward;
  particle_pass.camera_up = cam_up;
  particle_pass.camera_right = cam_right;
  particle_pass.active_rtv = active_rtv;
  particle_pass.active_dsv = active_dsv;
  particle_pass.particle_dsv = particle_dsv;
  particle_pass.particle_scene_color_sample_srv = particle_scene_color_sample_srv;
  particle_pass.particle_scene_depth_srv = particle_scene_depth_srv;
  particle_pass.render_width = render_width;
  particle_pass.render_height = render_height;
  particle_pass.scene_color_format =
      particle_scene_texture ? particle_scene_texture->GetDesc().Format
                             : Diligent::TEX_FORMAT_UNKNOWN;
  particle_pass.allow_distortion_particles = allow_distortion_particles;
  if (has_beam_work) {
    const uint32_t beam_draws = renderParticleBeams(layer, particle_pass);
    draw_count += beam_draws;
    if (beam_draws > 0u) {
      mark_msaa_dirty();
    }
  }
  mark_stage(has_beam_work ? "particle beam pass" : "particle beam pass skipped");
  if (has_particle_work) {
    renderParticlePasses(layer, particle_pass);
    mark_msaa_dirty();
  }
  mark_stage(has_particle_work ? "particle pass" : "particle pass skipped");

  auto draw_lines = [&](const std::vector<LineVertex>& lines,
                        Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
                        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
    if (lines.empty()) {
      return;
    }
    if (pso && line_cb_ && line_vb_) {
      if (lines.size() > line_vb_size_) {
        const size_t new_capacity =
            std::max(lines.size(), line_vb_size_ > 0 ? line_vb_size_ * 2 : static_cast<size_t>(1024));
        Diligent::BufferDesc vb_desc{};
        vb_desc.Name = "Karma Line VB";
        vb_desc.Usage = Diligent::USAGE_DYNAMIC;
        vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
        vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        vb_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(LineVertex));
        line_vb_.Release();
        const auto line_buffer_start = core::SteadyClock::now();
        device_->CreateBuffer(vb_desc, nullptr, &line_vb_);
        recordResourceCreation("line", "dynamic vertex buffer resize", line_buffer_start, core::SteadyClock::now());
        if (!line_vb_) {
          line_vb_size_ = 0;
          return;
        }
        line_vb_size_ = new_capacity;
      }
      auto* rtv = active_rtv;
      auto* dsv = active_dsv;
      context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      Diligent::Viewport viewport{};
      viewport.TopLeftX = 0.0f;
      viewport.TopLeftY = 0.0f;
      viewport.Width = static_cast<float>(render_width);
      viewport.Height = static_cast<float>(render_height);
      viewport.MinDepth = 0.0f;
      viewport.MaxDepth = 1.0f;
      context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(render_width),
                             static_cast<Diligent::Uint32>(render_height));

      {
        Diligent::MapHelper<LineVertex> vb_map(context_, line_vb_, Diligent::MAP_WRITE,
                                               Diligent::MAP_FLAG_DISCARD);
        auto* mapped_vertices = getMappedData(vb_map);
        if (mapped_vertices == nullptr) {
          return;
        }
        std::memcpy(mapped_vertices, lines.data(), lines.size() * sizeof(LineVertex));
      }

      const glm::mat4 view_proj = depth_fix * projection * view;
      LineConstants constants{};
      copyMat4(constants.view_proj, view_proj);
      {
        Diligent::MapHelper<LineConstants> cb_map(context_, line_cb_, Diligent::MAP_WRITE,
                                                  Diligent::MAP_FLAG_DISCARD);
        auto* mapped_constants = getMappedData(cb_map);
        if (mapped_constants == nullptr) {
          return;
        }
        *mapped_constants = constants;
      }

      context_->SetPipelineState(pso);
      if (srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      }
      Diligent::IBuffer* vbs[] = {line_vb_};
      Diligent::Uint64 offsets[] = {0};
      context_->SetVertexBuffers(0, 1, vbs, offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

      Diligent::DrawAttribs draw{};
      draw.NumVertices = static_cast<Diligent::Uint32>(lines.size());
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
    }
  };

  const bool require_post_particle_scene_color_copy =
      forwardDrawsRequireSceneColorCopy(forward_state.post_particle_draws);
  particle_pass_stats_.post_particle_scene_sample_draws += static_cast<uint32_t>(
      std::min<std::size_t>(forward_state.post_particle_draws.size(),
                            static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
  Diligent::ITextureView* post_particle_scene_color_sample_srv = particle_scene_color_srv;
  if (require_post_particle_scene_color_copy && particle_scene_texture) {
    if (msaa_enabled && !resolve_msaa_if_needed()) {
      particle_scene_texture = nullptr;
    }
  }
  if (require_post_particle_scene_color_copy && particle_scene_texture) {
    ensureParticleSceneCopyResources(render_width,
                                     render_height,
                                     particle_scene_texture->GetDesc().Format);
    if (particle_scene_color_copy_tex_ && particle_scene_color_copy_srv_) {
      copyTextureAfterRender(particle_scene_texture,
                             particle_scene_color_copy_tex_);
      particle_pass_stats_.post_particle_scene_color_copy = true;
      post_particle_scene_color_sample_srv = particle_scene_color_copy_srv_;
    }
  }
  const Diligent::Uint32 post_particle_draws =
      renderTransparentForwardDraws(forward_state.post_particle_draws,
                                    base_constants,
                                    active_forward_pipeline,
                                    use_custom_shader_override,
                                    rtv,
                                    dsv,
                                    particle_dsv,
                                    render_width,
                                    render_height,
                                    post_particle_scene_color_sample_srv,
                                    particle_scene_depth_srv);
  draw_count += post_particle_draws;
  if (post_particle_draws > 0u) {
    mark_msaa_dirty();
  }
  mark_stage("transparent post-particle pass");
  if (msaa_enabled) {
    const bool has_line_work =
        !line_vertices_depth_.empty() || !line_vertices_no_depth_.empty();
    draw_lines(line_vertices_depth_, line_pipeline_state_depth_, line_srb_depth_);
    draw_lines(line_vertices_no_depth_, line_pipeline_state_no_depth_, line_srb_no_depth_);
    if (has_line_work) {
      mark_msaa_dirty();
    }
    mark_stage("line draw");
    if (!resolve_msaa_if_needed()) {
      particle_scene_texture = nullptr;
      particle_scene_color_srv = nullptr;
    }
    active_rtv = output_rtv;
    active_dsv = output_dsv;
    particle_dsv = output_read_only_dsv ? output_read_only_dsv : output_dsv;
    rtv = active_rtv;
    dsv = active_dsv;
  }
  if (particle_scene_texture) {
    applyPostProcessChain(particle_scene_texture,
                          active_rtv,
                          particle_scene_depth_srv,
                          render_width,
                          render_height,
                          particle_scene_texture->GetDesc().Format,
                          target);
  }
  mark_stage("post process");
  executeFrameGraphScreenPasses(*active_frame_graph,
                                layer,
                                particle_scene_texture,
                                particle_scene_color_srv,
                                particle_scene_depth_srv,
                                active_rtv,
                                base_constants,
                                render_width,
                                render_height,
                                active_color_format,
                                target);
  mark_stage("frame graph screen passes");
  if (!msaa_enabled) {
    draw_lines(line_vertices_depth_, line_pipeline_state_depth_, line_srb_depth_);
    draw_lines(line_vertices_no_depth_, line_pipeline_state_no_depth_, line_srb_no_depth_);
    mark_stage("line draw");
  }

  finalize_camera_output();
  present_active_target();
  mark_stage("present copy");
  log_layer_diag("complete");

  if (particle_stats_log_enabled_) {
    const double frame_seconds =
        last_frame_delta_seconds_ > 0.0f ? static_cast<double>(last_frame_delta_seconds_)
                                         : 1.0 / 60.0;
    particle_stats_log_elapsed_seconds_ += frame_seconds;
    particle_stats_log_frame_count_ += 1u;
    rendering::accumulateParticleStats(particle_stats_log_totals_, particle_pass_stats_);
    if (particle_stats_log_elapsed_seconds_ >= 1.0) {
      spdlog::info("{}", rendering::formatParticleStatsReport(rendering::ParticleStatsReport{
                            .totals = particle_stats_log_totals_,
                            .frame_count = particle_stats_log_frame_count_,
                            .elapsed_seconds = particle_stats_log_elapsed_seconds_,
                        }));
      particle_stats_log_totals_ = {};
      particle_stats_log_elapsed_seconds_ = 0.0;
      particle_stats_log_frame_count_ = 0u;
    }
  }

  if (!warned_no_draws_) {
    warned_no_draws_ = true;
  }
}

}  // namespace karma::rendering::backend
