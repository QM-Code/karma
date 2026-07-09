#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace karma::rendering::backend {

Diligent::ITextureView* DiligentBackend::defaultBrdfLutSrv() const {
  return default_brdf_lut_ ? default_brdf_lut_.RawPtr() : default_base_color_.RawPtr();
}

Diligent::ITextureView* DiligentBackend::brdfLutSrv() const {
  return env_brdf_lut_srv_ ? env_brdf_lut_srv_.RawPtr() : defaultBrdfLutSrv();
}

void DiligentBackend::bindEnvironmentResources() {
  auto bind_env_to_srb = [&](Diligent::IShaderResourceBinding* srb) {
    if (!srb) {
      return;
    }
    auto* irr = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex");
    auto* pre = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex");
    auto* brdf = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT");
    if (irr) {
      irr->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_,
               Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (pre) {
      pre->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_,
               Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (brdf) {
      if (Diligent::ITextureView* srv = brdfLutSrv()) {
        brdf->Set(srv, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    }
  };

  bind_env_to_srb(shader_resources_);
  bind_env_to_srb(default_material_srb_);
  bind_env_to_srb(opaque_double_sided_default_material_srb_);
  bind_env_to_srb(transparent_default_material_srb_);
  bind_env_to_srb(transparent_double_sided_default_material_srb_);
  bind_env_to_srb(additive_default_material_srb_);
  bind_env_to_srb(additive_double_sided_default_material_srb_);
  for (auto& srb : compact_default_material_srbs_) {
    bind_env_to_srb(srb);
  }
  for (auto& entry : materials_) {
    bind_env_to_srb(entry.second.srb);
    bind_env_to_srb(entry.second.transparent_srb);
    bind_env_to_srb(entry.second.transparent_double_sided_srb);
    bind_env_to_srb(entry.second.additive_srb);
    bind_env_to_srb(entry.second.additive_double_sided_srb);
    for (auto& srb : entry.second.layout_srbs) {
      bind_env_to_srb(srb);
    }
    for (auto& srb : entry.second.layout_custom_srbs) {
      bind_env_to_srb(srb);
    }
  }
}

void DiligentBackend::setCamera(const rendering::CameraData& camera) {
  camera_ = camera;
}

void DiligentBackend::setCameraActive(bool active) {
  camera_active_ = active;
}

void DiligentBackend::setDirectionalLight(const rendering::DirectionalLightData& light) {
  const rendering::DirectionalLightData previous_light = directional_light_;
  directional_light_ = light;
  if (glm::length(directional_light_.direction) < 1e-4f) {
    directional_light_.direction = glm::vec3(0.3f, -1.0f, 0.2f);
  } else {
    directional_light_.direction = glm::normalize(directional_light_.direction);
  }
  // Directional sun lights should point toward the scene (negative Y in world-up convention).
  if (directional_light_.direction.y > 0.0f) {
    directional_light_.direction = -directional_light_.direction;
  }
  if (previous_light.casts_shadows != directional_light_.casts_shadows ||
      previous_light.shadow_extent != directional_light_.shadow_extent ||
      glm::length(previous_light.direction - directional_light_.direction) > 1e-4f) {
    directional_shadow_scene_dirty_ = true;
  }
}

void DiligentBackend::setLights(const std::vector<rendering::LightData>& lights) {
  lights_ = lights;
  for (auto& light : lights_) {
    if (light.intensity < 0.0f) {
      light.intensity = 0.0f;
    }
    if (light.range < 0.0f) {
      light.range = 0.0f;
    }
    if (glm::length(light.direction) < 1e-4f) {
      light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    } else {
      light.direction = glm::normalize(light.direction);
    }
    light.inner_cone_cos = std::clamp(light.inner_cone_cos, -1.0f, 1.0f);
    light.outer_cone_cos = std::clamp(light.outer_cone_cos, -1.0f, 1.0f);
    if (light.inner_cone_cos < light.outer_cone_cos) {
      std::swap(light.inner_cone_cos, light.outer_cone_cos);
    }
  }
}

void DiligentBackend::setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                        bool draw_skybox) {
  const bool path_changed = environment_map_ != path;
  if (environment_map_ == path &&
      environment_intensity_ == intensity &&
      draw_skybox_ == draw_skybox &&
      !env_dirty_ &&
      (path.empty() || (env_cubemap_srv_ && env_irradiance_srv_ && env_prefilter_srv_ &&
                        env_brdf_lut_srv_))) {
    return;
  }
  environment_intensity_ = intensity;
  environment_map_ = path;
  draw_skybox_ = draw_skybox;
  env_dirty_ = true;
  if (path_changed) {
    env_equirect_tex_.Release();
    env_equirect_srv_.Release();
    env_cubemap_tex_.Release();
    env_cubemap_srv_.Release();
  }
  if (!device_) {
    return;
  }

  if (path.empty()) {
    env_cubemap_srv_ = default_env_;
    env_irradiance_srv_ = default_env_;
    env_prefilter_srv_ = default_env_;
    env_brdf_lut_srv_ = defaultBrdfLutSrv();
    env_dirty_ = false;
  } else {
    ensureEnvironmentResources();
  }

  bindEnvironmentResources();
}

void DiligentBackend::setClearColor(const math::Color& color) {
  clear_color_[0] = std::clamp(color.r, 0.0f, 1.0f);
  clear_color_[1] = std::clamp(color.g, 0.0f, 1.0f);
  clear_color_[2] = std::clamp(color.b, 0.0f, 1.0f);
  clear_color_[3] = std::clamp(color.a, 0.0f, 1.0f);
}

void DiligentBackend::setVsync(bool enabled) {
  vsync_enabled_ = enabled;
  present_mode_ = rendering::PresentMode::Auto;
  if (vsync_enabled_) {
    spdlog::info(
        "Diligent Vulkan present policy: vsync FIFO/FIFO_RELAXED pacing enabled");
  } else {
    spdlog::info(
        "Diligent Vulkan present policy: low-latency fallback enabled "
        "(MAILBOX -> IMMEDIATE -> FIFO)");
  }
}

void DiligentBackend::setAnisotropy(bool enabled, int level) {
  anisotropy_enabled_ = enabled;
  anisotropy_level_ = std::max(1, level);

  if (!device_) {
    return;
  }

  Diligent::SamplerDesc sampler_color{};
  sampler_color.MinFilter =
      enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MagFilter =
      enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MipFilter =
      enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MaxAnisotropy = static_cast<Diligent::Uint8>(std::clamp(anisotropy_level_, 1, 16));
  sampler_color.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  Diligent::RefCntAutoPtr<Diligent::ISampler> next_sampler_color;
  device_->CreateSampler(sampler_color, &next_sampler_color);

  Diligent::SamplerDesc sampler_color_clamp = sampler_color;
  sampler_color_clamp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler_color_clamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler_color_clamp.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
  Diligent::RefCntAutoPtr<Diligent::ISampler> next_sampler_color_clamp;
  device_->CreateSampler(sampler_color_clamp, &next_sampler_color_clamp);

  // Normal, roughness, metallic, and occlusion textures need the same
  // minification quality as color textures, especially at grazing angles.
  Diligent::SamplerDesc sampler_data = sampler_color;
  Diligent::RefCntAutoPtr<Diligent::ISampler> next_sampler_data;
  device_->CreateSampler(sampler_data, &next_sampler_data);

  sampler_color_ = std::move(next_sampler_color);
  sampler_color_clamp_ = std::move(next_sampler_color_clamp);
  sampler_data_ = std::move(next_sampler_data);

  for (auto& entry : materials_) {
    for (auto* srb : {entry.second.srb.RawPtr(),
                      entry.second.transparent_srb.RawPtr(),
                      entry.second.transparent_double_sided_srb.RawPtr(),
                      entry.second.additive_srb.RawPtr(),
                      entry.second.additive_double_sided_srb.RawPtr(),
                      entry.second.shadow_alpha_srb.RawPtr()}) {
      if (!srb) {
        continue;
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
        var->Set(sampler_color_clamp_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    }
    for (auto& srb_ref : entry.second.layout_srbs) {
      auto* srb = srb_ref.RawPtr();
      if (!srb) {
        continue;
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
        var->Set(sampler_color_clamp_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    }
    for (auto& srb_ref : entry.second.layout_custom_srbs) {
      auto* srb = srb_ref.RawPtr();
      if (!srb) {
        continue;
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
        var->Set(sampler_color_clamp_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    }
  }
  for (auto* srb : {default_material_srb_.RawPtr(),
                    opaque_double_sided_default_material_srb_.RawPtr(),
                    transparent_default_material_srb_.RawPtr(),
                    transparent_double_sided_default_material_srb_.RawPtr(),
                    additive_default_material_srb_.RawPtr(),
                    additive_double_sided_default_material_srb_.RawPtr()}) {
    if (!srb) {
      continue;
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
      var->Set(sampler_color_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
      var->Set(sampler_color_clamp_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
      var->Set(sampler_data_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
  for (auto& srb_ref : compact_default_material_srbs_) {
    auto* srb = srb_ref.RawPtr();
    if (!srb) {
      continue;
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
      var->Set(sampler_color_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
      var->Set(sampler_color_clamp_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
      var->Set(sampler_data_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
}

void DiligentBackend::setGenerateMips(bool enabled) {
  generate_mips_enabled_ = enabled;
}

void DiligentBackend::setForwardPlusSettings(int tile_size,
                                             int max_lights_per_tile,
                                             int max_local_lights) {
  forward_plus_tile_size_ = std::clamp(tile_size, 4, 64);
  forward_plus_max_lights_per_tile_ = std::clamp(max_lights_per_tile, 8, 2048);
  forward_plus_max_local_lights_ = std::clamp(max_local_lights, 1, 65536);
  forward_plus_stats_.tile_size = static_cast<uint32_t>(forward_plus_tile_size_);
  forward_plus_stats_.max_lights_per_tile =
      static_cast<uint32_t>(forward_plus_max_lights_per_tile_);
  forward_plus_stats_.max_local_lights =
      static_cast<uint32_t>(forward_plus_max_local_lights_);
}

rendering::ForwardPlusStats DiligentBackend::getForwardPlusStats() const {
  return forward_plus_stats_;
}

void DiligentBackend::setInstancingCpuTimings(float render_system_extraction_ms,
                                              float forward_state_collection_ms) {
  if (render_system_extraction_ms >= 0.0f) {
    instancing_stats_.render_system_extraction_ms = render_system_extraction_ms;
  }
  if (forward_state_collection_ms >= 0.0f) {
    instancing_stats_.forward_state_collection_ms = forward_state_collection_ms;
  }
}

rendering::InstancingStats DiligentBackend::getInstancingStats() const {
  return instancing_stats_;
}

rendering::ParticlePassStats DiligentBackend::getParticlePassStats() const {
  return particle_pass_stats_;
}

rendering::RendererCommandStats DiligentBackend::getRendererCommandStats() const {
  rendering::RendererCommandStats out{};
  if (!context_) {
    return out;
  }

  const auto& stats = context_->GetStats();
  const auto& counters = stats.CommandCounters;
  out.set_pipeline_state = counters.SetPipelineState;
  out.commit_shader_resources = counters.CommitShaderResources;
  out.set_vertex_buffers = counters.SetVertexBuffers;
  out.set_index_buffer = counters.SetIndexBuffer;
  out.set_render_targets = counters.SetRenderTargets;
  out.set_viewports = counters.SetViewports;
  out.set_scissor_rects = counters.SetScissorRects;
  out.clear_render_target = counters.ClearRenderTarget;
  out.clear_depth_stencil = counters.ClearDepthStencil;
  out.draw = counters.Draw;
  out.draw_indexed = counters.DrawIndexed;
  out.draw_indirect = counters.DrawIndirect;
  out.draw_indexed_indirect = counters.DrawIndexedIndirect;
  out.multi_draw = counters.MultiDraw;
  out.multi_draw_indexed = counters.MultiDrawIndexed;
  out.dispatch_compute = counters.DispatchCompute;
  out.dispatch_compute_indirect = counters.DispatchComputeIndirect;
  out.draw_mesh = counters.DrawMesh;
  out.draw_mesh_indirect = counters.DrawMeshIndirect;
  out.trace_rays = counters.TraceRays;
  out.trace_rays_indirect = counters.TraceRaysIndirect;
  out.update_buffer = counters.UpdateBuffer;
  out.copy_buffer = counters.CopyBuffer;
  out.map_buffer = counters.MapBuffer;
  out.update_texture = counters.UpdateTexture;
  out.copy_texture = counters.CopyTexture;
  out.map_texture_subresource = counters.MapTextureSubresource;
  out.begin_query = counters.BeginQuery;
  out.generate_mips = counters.GenerateMips;
  out.resolve_texture_subresource = counters.ResolveTextureSubresource;
  out.total_triangles = stats.GetTotalTriangleCount();
  out.total_lines = stats.GetTotalLineCount();
  out.total_points = stats.GetTotalPointCount();
  return out;
}

rendering::RendererFrameTimingStats DiligentBackend::getRendererFrameTimingStats() const {
  return last_frame_timing_stats_;
}

void DiligentBackend::setParticleSystemStats(const rendering::ParticlePassStats& stats) {
  particle_pass_stats_.effect_binding_updates = stats.effect_binding_updates;
  particle_pass_stats_.simulated_emitters = stats.simulated_emitters;
  particle_pass_stats_.visible_emitters = stats.visible_emitters;
  particle_pass_stats_.culled_emitters = stats.culled_emitters;
  particle_pass_stats_.submitted_emitters = stats.submitted_emitters;
  particle_pass_stats_.simulated_particles = stats.simulated_particles;
  particle_pass_stats_.packed_particles = stats.packed_particles;
  particle_pass_stats_.culled_particles = stats.culled_particles;
  particle_pass_stats_.ground_collision_particles = stats.ground_collision_particles;
  particle_pass_stats_.gpu_particle_capacity = stats.gpu_particle_capacity;
  particle_pass_stats_.gpu_alive_particles = stats.gpu_alive_particles;
  particle_pass_stats_.gpu_dead_particles = stats.gpu_dead_particles;
  particle_pass_stats_.gpu_spawned_particles = stats.gpu_spawned_particles;
  particle_pass_stats_.gpu_killed_particles = stats.gpu_killed_particles;
  particle_pass_stats_.gpu_compacted_particles = stats.gpu_compacted_particles;
  particle_pass_stats_.gpu_compute_dispatches = stats.gpu_compute_dispatches;
  particle_pass_stats_.gpu_indirect_draws = stats.gpu_indirect_draws;
  particle_pass_stats_.gpu_indirect_dispatches = stats.gpu_indirect_dispatches;
  particle_pass_stats_.gpu_sort_key_count = stats.gpu_sort_key_count;
  particle_pass_stats_.gpu_sort_passes = stats.gpu_sort_passes;
  particle_pass_stats_.gpu_buffer_resizes = stats.gpu_buffer_resizes;
  particle_pass_stats_.gpu_stats_readback_age = stats.gpu_stats_readback_age;
  particle_pass_stats_.gpu_allocator_live_emitters = stats.gpu_allocator_live_emitters;
  particle_pass_stats_.gpu_allocator_free_ranges = stats.gpu_allocator_free_ranges;
  particle_pass_stats_.gpu_allocator_active_capacity = stats.gpu_allocator_active_capacity;
  particle_pass_stats_.gpu_allocator_high_water_capacity =
      stats.gpu_allocator_high_water_capacity;
  particle_pass_stats_.gpu_allocator_retired_emitters =
      stats.gpu_allocator_retired_emitters;
  particle_pass_stats_.gpu_allocator_reused_slots = stats.gpu_allocator_reused_slots;
  particle_pass_stats_.gpu_allocator_allocation_failures =
      stats.gpu_allocator_allocation_failures;
  particle_pass_stats_.gpu_culled_emitters = stats.gpu_culled_emitters;
  particle_pass_stats_.gpu_culled_particles = stats.gpu_culled_particles;
  particle_pass_stats_.gpu_culling_dispatches = stats.gpu_culling_dispatches;
  particle_pass_stats_.cpu_fallback_particles = stats.cpu_fallback_particles;
  particle_pass_stats_.submitted_beams = stats.submitted_beams;
  particle_pass_stats_.beam_segments = stats.beam_segments;
  particle_pass_stats_.sync_effect_bindings_ms = stats.sync_effect_bindings_ms;
  particle_pass_stats_.simulation_ms = stats.simulation_ms;
  particle_pass_stats_.packing_ms = stats.packing_ms;
  particle_pass_stats_.gpu_sort_overflow = stats.gpu_sort_overflow;
  particle_pass_stats_.gpu_fallback_active = stats.gpu_fallback_active;
  particle_pass_stats_.gpu_global_sort_active = stats.gpu_global_sort_active;
  particle_pass_stats_.gpu_grouped_sort_fallback = stats.gpu_grouped_sort_fallback;
}

void DiligentBackend::setShadowSettings(float bias,
                                        int map_size,
                                        int pcf_radius,
                                        int raster_depth_bias,
                                        float raster_slope_bias,
                                        float receiver_bias_scale,
                                        float normal_bias_scale) {
  const rendering::ShadowSettings settings = rendering::clampShadowSettings({
      .bias = bias,
      .map_size = map_size,
      .pcf_radius = pcf_radius,
      .raster_depth_bias = raster_depth_bias,
      .raster_slope_bias = raster_slope_bias,
      .receiver_bias_scale = receiver_bias_scale,
      .normal_bias_scale = normal_bias_scale,
  });
  shadow_bias_ = settings.bias;
  shadow_pcf_radius_ = settings.pcf_radius;
  shadow_receiver_bias_scale_ = settings.receiver_bias_scale;
  shadow_normal_bias_scale_ = settings.normal_bias_scale;

  const int clamped_depth_bias = settings.raster_depth_bias;
  const float clamped_slope_bias = settings.raster_slope_bias;
  const bool raster_bias_changed = clamped_depth_bias != shadow_raster_depth_bias_ ||
                                   clamped_slope_bias != shadow_raster_slope_bias_;
  if (raster_bias_changed) {
    shadow_raster_depth_bias_ = clamped_depth_bias;
    shadow_raster_slope_bias_ = clamped_slope_bias;
    recreateShadowPipeline();
    directional_shadow_cache_valid_ = false;
    point_shadow_cache_initialized_ = false;
    point_shadow_slot_valid_.fill(false);
    point_shadow_face_dirty_.fill(1u);
  }

  const int clamped_size = settings.map_size;
  if (clamped_size != shadow_map_size_) {
    shadow_map_size_ = clamped_size;
    point_shadow_map_size_ = std::max(256, shadow_map_size_ / 2);
    recreateShadowMap();
    recreatePointShadowMap();
  }
}

void DiligentBackend::setPointShadowSettings(float constant_bias,
                                             float slope_bias_scale,
                                             float normal_bias_scale,
                                             float receiver_bias_scale) {
  const rendering::PointShadowSettings settings =
      rendering::clampPointShadowSettings({
          .constant_bias = constant_bias,
          .slope_bias_scale = slope_bias_scale,
          .normal_bias_scale = normal_bias_scale,
          .receiver_bias_scale = receiver_bias_scale,
      });
  point_shadow_constant_bias_ = settings.constant_bias;
  point_shadow_slope_bias_scale_ = settings.slope_bias_scale;
  point_shadow_normal_bias_scale_ = settings.normal_bias_scale;
  point_shadow_receiver_bias_scale_ = settings.receiver_bias_scale;
}

void DiligentBackend::setPointShadowLightLimit(int max_lights) {
  const int clamped_max_lights = std::clamp(max_lights, 1, kMaxPointShadowLights);
  if (clamped_max_lights == point_shadow_max_lights_) {
    return;
  }

  point_shadow_max_lights_ = clamped_max_lights;
  recreatePointShadowMap();
}

void DiligentBackend::setLocalLightingSettings(float distance_damping,
                                               float range_falloff_exponent,
                                               bool ao_affects_local_lights,
                                               float directional_shadow_lift_strength) {
  const rendering::LocalLightingSettings settings =
      rendering::clampLocalLightingSettings({
          .distance_damping = distance_damping,
          .range_falloff_exponent = range_falloff_exponent,
          .ao_affects_local_lights = ao_affects_local_lights,
          .directional_shadow_lift_strength = directional_shadow_lift_strength,
      });
  local_light_distance_damping_ = settings.distance_damping;
  local_light_range_exponent_ = settings.range_falloff_exponent;
  ao_affects_local_lights_ = settings.ao_affects_local_lights;
  local_light_directional_shadow_lift_ =
      settings.directional_shadow_lift_strength;
}

void DiligentBackend::setExposure(float exposure) {
  lighting_exposure_ = rendering::clampLightingExposure(exposure);
}

void DiligentBackend::applyPostProcessSettingsForPass(
    const rendering::PostProcessSettings& settings) {
  const rendering::PostProcessSettings clamped =
      rendering::clampPostProcessSettings(settings);
  if (!clamped.temporal_antialiasing_enabled ||
      !post_process_settings_.temporal_antialiasing_enabled) {
    post_process_history_valid_ = false;
  }
  post_process_settings_ = clamped;
}

}  // namespace karma::rendering::backend
