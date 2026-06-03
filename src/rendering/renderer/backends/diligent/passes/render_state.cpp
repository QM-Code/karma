#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace karma::renderer_backend {

void DiligentBackend::setCamera(const renderer::CameraData& camera) {
  camera_ = camera;
}

void DiligentBackend::setCameraActive(bool active) {
  camera_active_ = active;
}

void DiligentBackend::setDirectionalLight(const renderer::DirectionalLightData& light) {
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
}

void DiligentBackend::setLights(const std::vector<renderer::LightData>& lights) {
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
  if (environment_map_ == path &&
      environment_intensity_ == intensity &&
      draw_skybox_ == draw_skybox &&
      !env_dirty_ &&
      (path.empty() || env_cubemap_srv_)) {
    return;
  }
  environment_intensity_ = intensity;
  environment_map_ = path;
  draw_skybox_ = draw_skybox;
  env_dirty_ = true;
  if (!device_) {
    return;
  }

  if (path.empty()) {
    env_cubemap_srv_ = default_env_;
    env_irradiance_srv_ = default_env_;
    env_prefilter_srv_ = default_env_;
    env_brdf_lut_srv_ = default_base_color_;
    env_dirty_ = false;
  } else {
    ensureEnvironmentResources();
  }

  auto bind_env_to_srb = [&](Diligent::IShaderResourceBinding* srb) {
    if (!srb) {
      return;
    }
    auto* irr = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex");
    auto* pre = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex");
    auto* brdf = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT");
    if (!irr) {
    }
    if (!pre) {
    }
    if (!brdf) {
    }
    if (irr) {
      irr->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_,
               Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (pre) {
      pre->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_,
               Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    if (brdf) {
      brdf->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_,
                Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  };

  bind_env_to_srb(shader_resources_);
  bind_env_to_srb(default_material_srb_);
  bind_env_to_srb(transparent_default_material_srb_);
  bind_env_to_srb(transparent_double_sided_default_material_srb_);
  bind_env_to_srb(additive_default_material_srb_);
  bind_env_to_srb(additive_double_sided_default_material_srb_);
  for (auto& entry : materials_) {
    bind_env_to_srb(entry.second.srb);
    bind_env_to_srb(entry.second.transparent_srb);
    bind_env_to_srb(entry.second.transparent_double_sided_srb);
    bind_env_to_srb(entry.second.additive_srb);
    bind_env_to_srb(entry.second.additive_double_sided_srb);
  }
}

void DiligentBackend::setVsync(bool enabled) {
  vsync_enabled_ = enabled;
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

  Diligent::SamplerDesc sampler_data{};
  sampler_data.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_data.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_data.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  Diligent::RefCntAutoPtr<Diligent::ISampler> next_sampler_data;
  device_->CreateSampler(sampler_data, &next_sampler_data);

  sampler_color_ = std::move(next_sampler_color);
  sampler_data_ = std::move(next_sampler_data);

  for (auto& entry : materials_) {
    for (auto* srb : {entry.second.srb.RawPtr(),
                      entry.second.transparent_srb.RawPtr(),
                      entry.second.transparent_double_sided_srb.RawPtr(),
                      entry.second.additive_srb.RawPtr(),
                      entry.second.additive_double_sided_srb.RawPtr()}) {
      if (!srb) {
        continue;
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    }
  }
  for (auto* srb : {default_material_srb_.RawPtr(),
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

renderer::ForwardPlusStats DiligentBackend::getForwardPlusStats() const {
  return forward_plus_stats_;
}

renderer::ParticlePassStats DiligentBackend::getParticlePassStats() const {
  return particle_pass_stats_;
}

void DiligentBackend::setParticleSystemStats(const renderer::ParticlePassStats& stats) {
  particle_pass_stats_.effect_binding_updates = stats.effect_binding_updates;
  particle_pass_stats_.simulated_emitters = stats.simulated_emitters;
  particle_pass_stats_.visible_emitters = stats.visible_emitters;
  particle_pass_stats_.culled_emitters = stats.culled_emitters;
  particle_pass_stats_.submitted_emitters = stats.submitted_emitters;
  particle_pass_stats_.simulated_particles = stats.simulated_particles;
  particle_pass_stats_.packed_particles = stats.packed_particles;
  particle_pass_stats_.culled_particles = stats.culled_particles;
  particle_pass_stats_.ground_collision_particles = stats.ground_collision_particles;
  particle_pass_stats_.sync_effect_bindings_ms = stats.sync_effect_bindings_ms;
  particle_pass_stats_.simulation_ms = stats.simulation_ms;
  particle_pass_stats_.packing_ms = stats.packing_ms;
}

void DiligentBackend::setShadowSettings(float bias,
                                        int map_size,
                                        int pcf_radius,
                                        int raster_depth_bias,
                                        float raster_slope_bias,
                                        float receiver_bias_scale,
                                        float normal_bias_scale) {
  shadow_bias_ = std::max(0.0f, bias);
  shadow_pcf_radius_ = std::clamp(pcf_radius, 0, 4);
  shadow_receiver_bias_scale_ = std::clamp(receiver_bias_scale, 0.0f, 16.0f);
  shadow_normal_bias_scale_ = std::clamp(normal_bias_scale, 0.0f, 16.0f);

  const int clamped_depth_bias = std::clamp(raster_depth_bias, -65536, 65536);
  const float clamped_slope_bias = std::clamp(raster_slope_bias, -64.0f, 64.0f);
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

  const int clamped_size = std::max(256, map_size);
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
  point_shadow_constant_bias_ = std::clamp(constant_bias, 0.0f, 0.05f);
  point_shadow_slope_bias_scale_ = std::clamp(slope_bias_scale, 0.0f, 16.0f);
  point_shadow_normal_bias_scale_ = std::clamp(normal_bias_scale, 0.0f, 16.0f);
  point_shadow_receiver_bias_scale_ = std::clamp(receiver_bias_scale, 0.0f, 8.0f);
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
  local_light_distance_damping_ = std::clamp(distance_damping, 0.0f, 4.0f);
  local_light_range_exponent_ = std::clamp(range_falloff_exponent, 0.1f, 8.0f);
  ao_affects_local_lights_ = ao_affects_local_lights;
  local_light_directional_shadow_lift_ =
      std::clamp(directional_shadow_lift_strength, 0.0f, 8.0f);
}

void DiligentBackend::setExposure(float exposure) {
  lighting_exposure_ = std::clamp(exposure, 0.01f, 32.0f);
}

}  // namespace karma::renderer_backend
