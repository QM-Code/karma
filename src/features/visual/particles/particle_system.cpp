#include "karma/visual.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <tuple>

#include "karma/assets.h"
#include "karma/core.h"
#include "karma/rendering.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"

namespace karma::visual::particles {

namespace {

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32) |
         static_cast<uint64_t>(entity.generation);
}

uint64_t hashCombine(uint64_t seed, uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
  return seed;
}

uint64_t hashFloat(uint64_t seed, float value) {
  uint32_t bits = 0u;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return hashCombine(seed, bits);
}

uint64_t hashColor(uint64_t seed, const math::Color& color) {
  seed = hashFloat(seed, color.r);
  seed = hashFloat(seed, color.g);
  seed = hashFloat(seed, color.b);
  seed = hashFloat(seed, color.a);
  return seed;
}

uint64_t hashString(uint64_t seed, const std::string& value) {
  for (const char c : value) {
    seed = hashCombine(seed, static_cast<unsigned char>(c));
  }
  return seed;
}

uint64_t hashVec3(uint64_t seed, const math::Vec3& value) {
  seed = hashFloat(seed, value.x);
  seed = hashFloat(seed, value.y);
  seed = hashFloat(seed, value.z);
  return seed;
}

uint64_t hashOptionalVec3(uint64_t seed, const std::optional<math::Vec3>& value) {
  seed = hashCombine(seed, value.has_value() ? 1ull : 0ull);
  if (value.has_value()) {
    seed = hashVec3(seed, *value);
  }
  return seed;
}

uint64_t hashOptionalFloat(uint64_t seed, const std::optional<float>& value) {
  seed = hashCombine(seed, value.has_value() ? 1ull : 0ull);
  if (value.has_value()) {
    seed = hashFloat(seed, *value);
  }
  return seed;
}

template <typename Enum>
uint64_t hashOptionalEnum(uint64_t seed, const std::optional<Enum>& value) {
  seed = hashCombine(seed, value.has_value() ? 1ull : 0ull);
  if (value.has_value()) {
    seed = hashCombine(seed, static_cast<uint64_t>(*value));
  }
  return seed;
}

uint64_t hashParticleEffectOverride(
    const components::ParticleEffectOverrideComponent* effect_override) {
  if (effect_override == nullptr || !effect_override->active) {
    return 0ull;
  }

  uint64_t seed = 0xcbf29ce484222325ull;
  seed = hashFloat(seed, effect_override->time_scale);
  seed = hashFloat(seed, effect_override->emission_scale);
  seed = hashFloat(seed, effect_override->spawn_rate_scale);
  seed = hashFloat(seed, effect_override->lifetime_scale);
  seed = hashFloat(seed, effect_override->size_scale);
  seed = hashFloat(seed, effect_override->radius_scale);
  seed = hashFloat(seed, effect_override->velocity_scale);
  seed = hashFloat(seed, effect_override->angular_velocity_scale);
  seed = hashFloat(seed, effect_override->alpha_scale);
  seed = hashCombine(seed, effect_override->start_color.has_value() ? 1ull : 0ull);
  if (effect_override->start_color.has_value()) {
    seed = hashColor(seed, *effect_override->start_color);
  }
  seed = hashCombine(seed, effect_override->end_color.has_value() ? 1ull : 0ull);
  if (effect_override->end_color.has_value()) {
    seed = hashColor(seed, *effect_override->end_color);
  }
  seed = hashCombine(seed, effect_override->texture_key.has_value() ? 1ull : 0ull);
  if (effect_override->texture_key.has_value()) {
    seed = hashString(seed, *effect_override->texture_key);
  }
  seed = hashOptionalEnum(seed, effect_override->source_shape);
  seed = hashOptionalVec3(seed, effect_override->source_box_extents);
  seed = hashOptionalVec3(seed, effect_override->source_dimensions);
  seed = hashOptionalFloat(seed, effect_override->source_radius_min);
  seed = hashOptionalFloat(seed, effect_override->source_radius_max);
  seed = hashOptionalFloat(seed, effect_override->source_inner_radius);
  seed = hashOptionalFloat(seed, effect_override->source_outer_radius);
  seed = hashOptionalFloat(seed, effect_override->source_height);
  seed = hashOptionalFloat(seed, effect_override->source_angle);
  seed = hashCombine(seed, effect_override->source_path_points.has_value() ? 1ull : 0ull);
  if (effect_override->source_path_points.has_value()) {
    seed = hashCombine(seed, effect_override->source_path_points->size());
    for (const math::Vec3& point : *effect_override->source_path_points) {
      seed = hashVec3(seed, point);
    }
  }
  seed = hashCombine(seed, effect_override->source_closed_loop.has_value() ? 1ull : 0ull);
  if (effect_override->source_closed_loop.has_value()) {
    seed = hashCombine(seed, *effect_override->source_closed_loop ? 1ull : 0ull);
  }
  seed = hashOptionalEnum(seed, effect_override->source_sampling);
  seed = hashOptionalFloat(seed, effect_override->source_jitter_radius);
  seed = hashCombine(seed, effect_override->source_mesh_asset_key.has_value() ? 1ull : 0ull);
  if (effect_override->source_mesh_asset_key.has_value()) {
    seed = hashString(seed, *effect_override->source_mesh_asset_key);
  }
  seed = hashOptionalEnum(seed, effect_override->source_distribution);
  return seed;
}

uint32_t scaleParticleCount(uint32_t count, float scale) {
  if (count == 0u || !std::isfinite(scale) || scale <= 0.0f) {
    return 0u;
  }

  const double scaled =
      std::ceil(static_cast<double>(count) * static_cast<double>(scale));
  if (scaled >= static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(scaled);
}

void applyEffectOverrideToEmitter(const components::ParticleEffectOverrideComponent& effect_override,
                                  components::ParticleEmitterComponent& emitter) {
  if (!effect_override.active) {
    return;
  }

  const float time_scale = std::max(effect_override.time_scale, 0.0f);
  const float emission_scale =
      std::isfinite(effect_override.emission_scale)
          ? std::max(effect_override.emission_scale, 0.0f)
          : 0.0f;
  const float spawn_rate_scale = std::max(effect_override.spawn_rate_scale, 0.0f);
  const float lifetime_scale = std::max(effect_override.lifetime_scale, 0.0f);
  const float size_scale = std::max(effect_override.size_scale, 0.0f);
  const float radius_scale = std::max(effect_override.radius_scale, 0.0f);
  const float velocity_scale = std::max(effect_override.velocity_scale, 0.0f);
  const float angular_velocity_scale = std::max(effect_override.angular_velocity_scale, 0.0f);
  const float alpha_scale = std::max(effect_override.alpha_scale, 0.0f);

  emitter.time_scale *= time_scale;
  emitter.max_particles = scaleParticleCount(emitter.max_particles, emission_scale);
  emitter.burst_count = scaleParticleCount(emitter.burst_count, emission_scale);
  emitter.spawn_rate *= spawn_rate_scale * emission_scale;
  emitter.particle_lifetime_min *= lifetime_scale;
  emitter.particle_lifetime_max *= lifetime_scale;
  emitter.start_size_min *= size_scale;
  emitter.start_size_max *= size_scale;
  emitter.end_size_min *= size_scale;
  emitter.end_size_max *= size_scale;
  emitter.source_box_extents.x *= radius_scale;
  emitter.source_box_extents.y *= radius_scale;
  emitter.source_box_extents.z *= radius_scale;
  emitter.source_dimensions.x *= radius_scale;
  emitter.source_dimensions.y *= radius_scale;
  emitter.source_dimensions.z *= radius_scale;
  emitter.source_radius_min *= radius_scale;
  emitter.source_radius_max *= radius_scale;
  emitter.source_inner_radius *= radius_scale;
  emitter.source_outer_radius *= radius_scale;
  emitter.source_height *= radius_scale;
  emitter.source_jitter_radius *= radius_scale;
  emitter.radial_speed_min *= velocity_scale;
  emitter.radial_speed_max *= velocity_scale;
  emitter.velocity_min.x *= velocity_scale;
  emitter.velocity_min.y *= velocity_scale;
  emitter.velocity_min.z *= velocity_scale;
  emitter.velocity_max.x *= velocity_scale;
  emitter.velocity_max.y *= velocity_scale;
  emitter.velocity_max.z *= velocity_scale;
  emitter.acceleration.x *= velocity_scale;
  emitter.acceleration.y *= velocity_scale;
  emitter.acceleration.z *= velocity_scale;
  emitter.angular_velocity_min *= angular_velocity_scale;
  emitter.angular_velocity_max *= angular_velocity_scale;

  if (effect_override.start_color.has_value()) {
    emitter.start_color = *effect_override.start_color;
  }
  if (effect_override.end_color.has_value()) {
    emitter.end_color = *effect_override.end_color;
  }
  emitter.start_color.a *= alpha_scale;
  emitter.end_color.a *= alpha_scale;

  if (effect_override.texture_key.has_value()) {
    emitter.texture_key = *effect_override.texture_key;
  }
  if (effect_override.source_shape.has_value()) {
    emitter.source_shape = *effect_override.source_shape;
  }
  if (effect_override.source_box_extents.has_value()) {
    emitter.source_box_extents = *effect_override.source_box_extents;
  }
  if (effect_override.source_dimensions.has_value()) {
    emitter.source_dimensions = *effect_override.source_dimensions;
  }
  if (effect_override.source_radius_min.has_value()) {
    emitter.source_radius_min = *effect_override.source_radius_min;
  }
  if (effect_override.source_radius_max.has_value()) {
    emitter.source_radius_max = *effect_override.source_radius_max;
  }
  if (effect_override.source_inner_radius.has_value()) {
    emitter.source_inner_radius = *effect_override.source_inner_radius;
  }
  if (effect_override.source_outer_radius.has_value()) {
    emitter.source_outer_radius = *effect_override.source_outer_radius;
  }
  if (effect_override.source_height.has_value()) {
    emitter.source_height = *effect_override.source_height;
  }
  if (effect_override.source_angle.has_value()) {
    emitter.source_angle = *effect_override.source_angle;
  }
  if (effect_override.source_path_points.has_value()) {
    emitter.source_path_points = *effect_override.source_path_points;
  }
  if (effect_override.source_closed_loop.has_value()) {
    emitter.source_closed_loop = *effect_override.source_closed_loop;
  }
  if (effect_override.source_sampling.has_value()) {
    emitter.source_sampling = *effect_override.source_sampling;
  }
  if (effect_override.source_jitter_radius.has_value()) {
    emitter.source_jitter_radius = *effect_override.source_jitter_radius;
  }
  if (effect_override.source_mesh_asset_key.has_value()) {
    emitter.source_mesh_asset_key = *effect_override.source_mesh_asset_key;
  }
  if (effect_override.source_distribution.has_value()) {
    emitter.source_distribution = *effect_override.source_distribution;
  }
}

rendering::ParticleBlendMode toRendererBlendMode(components::ParticleBlendMode mode) {
  switch (mode) {
    case components::ParticleBlendMode::Additive:
      return rendering::ParticleBlendMode::Additive;
    case components::ParticleBlendMode::Alpha:
      return rendering::ParticleBlendMode::Alpha;
    case components::ParticleBlendMode::Distortion:
      return rendering::ParticleBlendMode::Distortion;
  }
  return rendering::ParticleBlendMode::Additive;
}

rendering::ParticleAlignment toRendererAlignment(components::ParticleAlignment alignment) {
  switch (alignment) {
    case components::ParticleAlignment::Billboard:
      return rendering::ParticleAlignment::Billboard;
    case components::ParticleAlignment::Ground:
      return rendering::ParticleAlignment::Ground;
  }
  return rendering::ParticleAlignment::Billboard;
}

rendering::ParticleShadingMode toRendererShadingMode(components::ParticleShadingMode mode) {
  switch (mode) {
    case components::ParticleShadingMode::Standard:
      return rendering::ParticleShadingMode::Standard;
    case components::ParticleShadingMode::Shell:
      return rendering::ParticleShadingMode::Shell;
  }
  return rendering::ParticleShadingMode::Standard;
}

rendering::ParticleSourceShape toRendererSourceShape(components::ParticleSourceShape shape) {
  switch (shape) {
    case components::ParticleSourceShape::Box:
      return rendering::ParticleSourceShape::Box;
    case components::ParticleSourceShape::Sphere:
      return rendering::ParticleSourceShape::Sphere;
    case components::ParticleSourceShape::SphereSurface:
      return rendering::ParticleSourceShape::SphereSurface;
    case components::ParticleSourceShape::Disc:
      return rendering::ParticleSourceShape::Disc;
    case components::ParticleSourceShape::Ring:
      return rendering::ParticleSourceShape::Ring;
    case components::ParticleSourceShape::Cylinder:
      return rendering::ParticleSourceShape::Cylinder;
    case components::ParticleSourceShape::Capsule:
      return rendering::ParticleSourceShape::Capsule;
    case components::ParticleSourceShape::Cone:
      return rendering::ParticleSourceShape::Cone;
    case components::ParticleSourceShape::Line:
      return rendering::ParticleSourceShape::Line;
    case components::ParticleSourceShape::Path:
      return rendering::ParticleSourceShape::Path;
    case components::ParticleSourceShape::TrailPath:
      return rendering::ParticleSourceShape::TrailPath;
    case components::ParticleSourceShape::MeshSurface:
      return rendering::ParticleSourceShape::MeshSurface;
  }
  return rendering::ParticleSourceShape::Box;
}

rendering::ParticleSourceSamplingMode toRendererSourceSampling(
    components::ParticleSourceSamplingMode sampling) {
  switch (sampling) {
    case components::ParticleSourceSamplingMode::Random:
      return rendering::ParticleSourceSamplingMode::Random;
    case components::ParticleSourceSamplingMode::Sequential:
      return rendering::ParticleSourceSamplingMode::Sequential;
    case components::ParticleSourceSamplingMode::Vertices:
      return rendering::ParticleSourceSamplingMode::Vertices;
  }
  return rendering::ParticleSourceSamplingMode::Random;
}

rendering::ParticleSourceDistribution toRendererSourceDistribution(
    components::ParticleSourceDistribution distribution) {
  switch (distribution) {
    case components::ParticleSourceDistribution::Uniform:
      return rendering::ParticleSourceDistribution::Uniform;
    case components::ParticleSourceDistribution::Surface:
      return rendering::ParticleSourceDistribution::Surface;
    case components::ParticleSourceDistribution::Edge:
      return rendering::ParticleSourceDistribution::Edge;
  }
  return rendering::ParticleSourceDistribution::Uniform;
}

rendering::ParticleEmitterGpuDesc makeRendererEmitterDesc(
    world::Entity entity,
    uint32_t emitter_index,
    const components::ParticleEmitterComponent& emitter,
    const components::TransformComponent& transform,
    rendering::TextureId texture,
    rendering::MeshId source_mesh,
    const math::Vec3& source_mesh_bounds_center,
    float source_mesh_bounds_radius,
    uint32_t restart_count,
    bool visible,
    float dt,
    float interpolation_alpha) {
  rendering::ParticleEmitterGpuDesc desc{};
  desc.instance_id = hashCombine(entityKey(entity), static_cast<uint64_t>(emitter_index) + 1ull);
  if (desc.instance_id == 0u) {
    desc.instance_id = 1u;
  }
  desc.restart_count = restart_count;
  desc.delta_seconds = std::max(dt, 0.0f);
  desc.visible = visible;
  desc.position = transform.getInterpolatedPosition(interpolation_alpha);
  desc.rotation = transform.getInterpolatedRotation(interpolation_alpha);
  desc.scale = transform.getScale();
  desc.enabled = emitter.enabled;
  desc.playing = emitter.playing;
  desc.loop = emitter.loop;
  desc.emit_burst_on_start = emitter.emit_burst_on_start;
  desc.local_space = emitter.local_space;
  desc.layer = static_cast<rendering::LayerId>(emitter.layer);
  desc.depth_test = emitter.depth_test;
  desc.blend_mode = toRendererBlendMode(emitter.blend_mode);
  desc.alignment = toRendererAlignment(emitter.alignment);
  desc.shading_mode = toRendererShadingMode(emitter.shading_mode);
  desc.use_soft_mask = emitter.use_soft_mask;
  desc.soft_particle_distance = emitter.soft_particle_distance;
  desc.distortion_strength = emitter.distortion_strength;
  desc.fresnel_power = emitter.fresnel_power;
  desc.fresnel_strength = emitter.fresnel_strength;
  desc.refraction_strength = emitter.refraction_strength;
  desc.interior_glow = emitter.interior_glow;
  desc.texture = texture;
  desc.atlas_columns = emitter.atlas_columns;
  desc.atlas_rows = emitter.atlas_rows;
  desc.atlas_frame_count = emitter.atlas_frame_count;
  desc.atlas_frame_width = emitter.atlas_frame_width;
  desc.atlas_frame_height = emitter.atlas_frame_height;
  desc.atlas_border_x = emitter.atlas_border_x;
  desc.atlas_border_y = emitter.atlas_border_y;
  desc.atlas_spacing_x = emitter.atlas_spacing_x;
  desc.atlas_spacing_y = emitter.atlas_spacing_y;
  desc.animation_fps = emitter.animation_fps;
  desc.animate_over_lifetime = emitter.animate_over_lifetime;
  desc.random_start_frame = emitter.random_start_frame;
  desc.max_particles = emitter.max_particles;
  desc.burst_count = emitter.burst_count;
  desc.seed = emitter.seed;
  desc.time_scale = emitter.time_scale;
  desc.start_delay = emitter.start_delay;
  desc.duration = emitter.duration;
  desc.spawn_rate = emitter.spawn_rate;
  desc.particle_lifetime_min = emitter.particle_lifetime_min;
  desc.particle_lifetime_max = emitter.particle_lifetime_max;
  desc.start_size_min = emitter.start_size_min;
  desc.start_size_max = emitter.start_size_max;
  desc.end_size_min = emitter.end_size_min;
  desc.end_size_max = emitter.end_size_max;
  desc.size_curve_exponent = emitter.size_curve_exponent;
  desc.alpha_curve_exponent = emitter.alpha_curve_exponent;
  desc.initial_rotation_min = emitter.initial_rotation_min;
  desc.initial_rotation_max = emitter.initial_rotation_max;
  desc.angular_velocity_min = emitter.angular_velocity_min;
  desc.angular_velocity_max = emitter.angular_velocity_max;
  desc.source_shape = toRendererSourceShape(emitter.source_shape);
  desc.source_box_extents = emitter.source_box_extents;
  desc.source_dimensions = emitter.source_dimensions;
  desc.source_radius_min = emitter.source_radius_min;
  desc.source_radius_max = emitter.source_radius_max;
  desc.source_inner_radius = emitter.source_inner_radius;
  desc.source_outer_radius = emitter.source_outer_radius;
  desc.source_height = emitter.source_height;
  desc.source_angle = emitter.source_angle;
  desc.source_path_points = emitter.source_path_points;
  desc.source_closed_loop = emitter.source_closed_loop;
  desc.source_sampling = toRendererSourceSampling(emitter.source_sampling);
  desc.source_jitter_radius = emitter.source_jitter_radius;
  desc.source_mesh = source_mesh;
  desc.source_mesh_bounds_center = source_mesh_bounds_center;
  desc.source_mesh_bounds_radius = source_mesh_bounds_radius;
  desc.source_distribution = toRendererSourceDistribution(emitter.source_distribution);
  desc.radial_speed_min = emitter.radial_speed_min;
  desc.radial_speed_max = emitter.radial_speed_max;
  desc.velocity_min = emitter.velocity_min;
  desc.velocity_max = emitter.velocity_max;
  desc.acceleration = emitter.acceleration;
  desc.drag = emitter.drag;
  desc.orbit_axis = emitter.orbit_axis;
  desc.orbit_speed = emitter.orbit_speed;
  desc.collide_with_ground = emitter.collide_with_ground;
  desc.ground_height = emitter.ground_height;
  desc.bounce_damping = emitter.bounce_damping;
  desc.collision_friction = emitter.collision_friction;
  desc.rest_speed_threshold = emitter.rest_speed_threshold;
  desc.start_color = emitter.start_color;
  desc.end_color = emitter.end_color;
  return desc;
}

rendering::ParticleBeamGpuDesc makeRendererBeamDesc(
    world::Entity entity,
    const components::ParticleBeamComponent& beam,
    const components::TransformComponent& transform,
    rendering::TextureId texture,
    bool visible,
    float dt,
    float interpolation_alpha) {
  rendering::ParticleBeamGpuDesc desc{};
  desc.instance_id = hashCombine(entityKey(entity), 0xbea00001ull);
  if (desc.instance_id == 0u) {
    desc.instance_id = 1u;
  }
  desc.restart_count = beam.restart_count;
  desc.delta_seconds = std::max(dt, 0.0f);
  desc.visible = visible;
  desc.position = transform.getInterpolatedPosition(interpolation_alpha);
  desc.rotation = transform.getInterpolatedRotation(interpolation_alpha);
  desc.scale = transform.getScale();
  desc.enabled = beam.enabled;
  desc.layer = static_cast<rendering::LayerId>(beam.layer);
  desc.depth_test = beam.depth_test;
  desc.blend_mode = toRendererBlendMode(beam.blend_mode);
  desc.texture = texture;
  desc.local_path_points = beam.local_path_points;
  desc.start_width = beam.start_width;
  desc.end_width = beam.end_width;
  desc.start_color = beam.start_color;
  desc.end_color = beam.end_color;
  desc.edge_softness = beam.edge_softness;
  desc.uv_repeat = beam.uv_repeat;
  desc.uv_scroll_speed = beam.uv_scroll_speed;
  desc.time_scale = beam.time_scale;
  return desc;
}

}  // namespace

ParticleSystem::~ParticleSystem() {
  releaseMeshCache();
  releaseTextureCache();
}

void ParticleSystem::releaseMeshCache() {
  if (device_ != nullptr) {
    for (const auto& [key, mesh] : mesh_asset_cache_) {
      (void)mesh;
      device_->unregisterRuntimeMesh(key);
    }
  }
  mesh_asset_cache_.clear();
}

void ParticleSystem::releaseTextureCache() {
  if (device_ != nullptr) {
    for (const auto& [key, texture] : texture_asset_cache_) {
      (void)key;
      if (texture != rendering::kInvalidTexture) {
        device_->destroyTexture(texture);
      }
    }
  }
  texture_asset_cache_.clear();
}

rendering::TextureId ParticleSystem::resolveTextureAsset(const std::string& texture_key) {
  if (texture_key.empty() || assets_ == nullptr || device_ == nullptr) {
    return rendering::kInvalidTexture;
  }
  if (const auto it = texture_asset_cache_.find(texture_key);
      it != texture_asset_cache_.end()) {
    return it->second;
  }

  const assets::TextureAsset* texture = assets_->findTextureAsset(texture_key);
  if (texture == nullptr ||
      texture->desc.width <= 0 ||
      texture->desc.height <= 0) {
    return rendering::kInvalidTexture;
  }

  const assets::TextureRuntimeCapabilities capabilities{
      .bc7_unorm = device_->supportsTextureFormat(rendering::TextureFormat::BC7_RGBA_UNORM),
      .bc7_srgb = device_->supportsTextureFormat(rendering::TextureFormat::BC7_RGBA_UNORM_SRGB),
  };
  auto prepared = assets::prepareTextureUpload(*texture, capabilities);
  if (!prepared.has_value()) {
    return rendering::kInvalidTexture;
  }

  rendering::TextureId id = device_->createTexture(prepared->desc);
  if (id != rendering::kInvalidTexture) {
    const bool uploaded = device_->uploadTexture(id, prepared->upload);
    if (uploaded) {
      texture_asset_cache_[texture_key] = id;
    } else {
      device_->destroyTexture(id);
      id = rendering::kInvalidTexture;
    }
  }
  return id;
}

uint32_t ParticleSystem::syncEffectBindings(world::World& world) {
  if (assets_ == nullptr) {
    return 0u;
  }

  uint32_t binding_updates = 0u;
  const uint64_t asset_version = assets_->version();
  world.forEach<components::ParticleEffectComponent>([&](const world::Entity entity) {
    auto& effect = world.get<components::ParticleEffectComponent>(entity);
    if (!effect.auto_apply || effect.effect_key.empty()) {
      return;
    }

    const components::ParticleEffectOverrideComponent* effect_override =
        world.has<components::ParticleEffectOverrideComponent>(entity)
            ? &world.get<components::ParticleEffectOverrideComponent>(entity)
            : nullptr;
    const uint64_t override_hash = hashParticleEffectOverride(effect_override);

    const bool has_emitter = world.has<components::ParticleEmitterComponent>(entity);
    const bool needs_apply = !has_emitter ||
                             effect.applied_version != asset_version ||
                             effect.applied_override_hash != override_hash ||
                             effect.applied_restart_count != effect.restart_count ||
                             effect.applied_effect_key != effect.effect_key;
    if (!needs_apply) {
      return;
    }

    components::ParticleEmitterComponent emitter{};
    const ParticleEffectAsset* asset = assets_->findParticleEffect(effect.effect_key);
    const ParticleEmitterDesc* primary = asset != nullptr ? asset->primaryEmitter() : nullptr;
    if (primary == nullptr) {
      return;
    }
    emitter = primary->emitter;
    if (!primary->texture_key.empty()) {
      emitter.texture_key = primary->texture_key;
    }

    if (has_emitter) {
      const auto& existing = world.get<components::ParticleEmitterComponent>(entity);
      if (effect.preserve_enabled) {
        emitter.enabled = existing.enabled;
      }
      if (effect.preserve_playing) {
        emitter.playing = existing.playing;
      }
      if (effect.preserve_start_delay) {
        emitter.start_delay = existing.start_delay;
      }
    }

    if (effect_override != nullptr) {
      applyEffectOverrideToEmitter(*effect_override, emitter);
    }

    world.add(entity, std::move(emitter));
    effect.applied_version = asset_version;
    effect.applied_override_hash = override_hash;
    effect.applied_restart_count = effect.restart_count;
    effect.applied_effect_key = effect.effect_key;
    binding_updates += 1u;
  });

  return binding_updates;
}

void ParticleSystem::update(world::World& world, float dt, float interpolation_alpha) {
  if (assets_ != nullptr && assets_->meshVersion() != last_mesh_version_) {
    releaseMeshCache();
    last_mesh_version_ = assets_->meshVersion();
  }
  if (assets_ != nullptr && assets_->textureVersion() != last_texture_version_) {
    releaseTextureCache();
    last_texture_version_ = assets_->textureVersion();
  }

  rendering::ParticlePassStats frame_stats{};
  const auto sync_start = core::SteadyClock::now();
  frame_stats.effect_binding_updates = syncEffectBindings(world);
  frame_stats.sync_effect_bindings_ms =
      core::elapsedMilliseconds(sync_start, core::SteadyClock::now());

  const auto submit_start = core::SteadyClock::now();
  auto emitter_visible = [&](world::Entity entity,
                             const components::ParticleEmitterComponent& emitter) {
    bool visible = emitter.enabled;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }
    return visible;
  };

  auto resolve_source_mesh = [&](const components::ParticleEmitterComponent& emitter) {
    rendering::MeshId mesh = rendering::kInvalidMesh;
    if (device_ != nullptr &&
        assets_ != nullptr &&
        !emitter.source_mesh_asset_key.empty()) {
      const auto cache_it = mesh_asset_cache_.find(emitter.source_mesh_asset_key);
      if (cache_it != mesh_asset_cache_.end()) {
        mesh = cache_it->second;
      } else if (const world::MeshData* mesh_asset =
                     assets_->findMeshAsset(emitter.source_mesh_asset_key)) {
        mesh = device_->registerRuntimeMesh(emitter.source_mesh_asset_key, *mesh_asset);
        if (mesh != rendering::kInvalidMesh) {
          mesh_asset_cache_[emitter.source_mesh_asset_key] = mesh;
        }
      }
    }
    math::Vec3 bounds_center{};
    float bounds_radius = 0.0f;
    if (device_ != nullptr && mesh != rendering::kInvalidMesh) {
      glm::vec3 center{0.0f, 0.0f, 0.0f};
      float radius = 0.0f;
      if (device_->getMeshBounds(mesh, center, radius)) {
        bounds_center = {center.x, center.y, center.z};
        bounds_radius = radius;
      }
    }
    return std::tuple<rendering::MeshId, math::Vec3, float>{mesh, bounds_center, bounds_radius};
  };

  auto submit_emitter = [&](world::Entity entity,
                            uint32_t emitter_index,
                            const components::ParticleEmitterComponent& emitter,
                            const components::TransformComponent& transform,
                            uint32_t restart_count,
                            bool visible) {
    frame_stats.simulated_emitters += 1u;
    if (visible) {
      frame_stats.visible_emitters += 1u;
    } else {
      frame_stats.culled_emitters += 1u;
    }

    if (device_ == nullptr) {
      return;
    }

    const rendering::TextureId texture = resolveTextureAsset(emitter.texture_key);
    const auto [source_mesh, source_mesh_bounds_center, source_mesh_bounds_radius] =
        resolve_source_mesh(emitter);
    const rendering::ParticleEmitterGpuDesc desc =
        makeRendererEmitterDesc(entity,
                                emitter_index,
                                emitter,
                                transform,
                                texture,
                                source_mesh,
                                source_mesh_bounds_center,
                                source_mesh_bounds_radius,
                                restart_count,
                                visible,
                                dt,
                                interpolation_alpha);
    device_->submitParticleEmitter(desc);
    frame_stats.submitted_emitters += 1u;
  };

  if (assets_ != nullptr) {
    world.forEach<components::ParticleEffectComponent, components::TransformComponent>(
        [&](const world::Entity entity) {
      const auto& effect = world.get<components::ParticleEffectComponent>(entity);
      if (!effect.auto_apply || effect.effect_key.empty()) {
        return;
      }

      const ParticleEffectAsset* asset = assets_->findParticleEffect(effect.effect_key);
      if (asset == nullptr || asset->emitters.empty()) {
        return;
      }

      const components::ParticleEffectOverrideComponent* effect_override =
          world.has<components::ParticleEffectOverrideComponent>(entity)
              ? &world.get<components::ParticleEffectOverrideComponent>(entity)
              : nullptr;
      const components::ParticleEmitterComponent* playback_override =
          world.has<components::ParticleEmitterComponent>(entity)
              ? &world.get<components::ParticleEmitterComponent>(entity)
              : nullptr;
      const auto& transform = world.get<components::TransformComponent>(entity);
      uint32_t emitter_index = 0u;
      for (const ParticleEmitterDesc& emitter_desc : asset->emitters) {
        components::ParticleEmitterComponent emitter = emitter_desc.emitter;
        if (!emitter_desc.texture_key.empty()) {
          emitter.texture_key = emitter_desc.texture_key;
        }
        if (playback_override != nullptr) {
          if (effect.preserve_enabled) {
            emitter.enabled = playback_override->enabled;
          }
          if (effect.preserve_playing) {
            emitter.playing = playback_override->playing;
          }
          if (effect.preserve_start_delay) {
            emitter.start_delay = playback_override->start_delay;
          }
        }
        if (effect_override != nullptr) {
          applyEffectOverrideToEmitter(*effect_override, emitter);
        }
        submit_emitter(entity,
                       emitter_index,
                       emitter,
                       transform,
                       effect.restart_count,
                       emitter_visible(entity, emitter));
        emitter_index += 1u;
      }
    });
  }

  world.forEach<components::ParticleEmitterComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    if (world.has<components::ParticleEffectComponent>(entity) &&
        world.get<components::ParticleEffectComponent>(entity).auto_apply) {
      return;
    }
    const auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);

    uint32_t restart_count = 0u;
    if (world.has<components::ParticleEffectComponent>(entity)) {
      restart_count = world.get<components::ParticleEffectComponent>(entity).restart_count;
    }

    submit_emitter(entity,
                   0u,
                   emitter,
                   transform,
                   restart_count,
                   emitter_visible(entity, emitter));
  });

  world.forEach<components::ParticleBeamComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    const auto& beam = world.get<components::ParticleBeamComponent>(entity);
    bool visible = beam.enabled && beam.visible;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }
    if (beam.local_path_points.size() < 2u ||
        beam.start_width <= 0.0f ||
        beam.end_width <= 0.0f ||
        beam.blend_mode == components::ParticleBlendMode::Distortion) {
      return;
    }

    frame_stats.submitted_beams += 1u;
    frame_stats.beam_segments += static_cast<uint32_t>(std::min<std::size_t>(
        beam.local_path_points.size() - 1u,
        static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));

    if (device_ == nullptr) {
      return;
    }

    const rendering::TextureId texture = resolveTextureAsset(beam.texture_key);
    const rendering::ParticleBeamGpuDesc desc =
        makeRendererBeamDesc(entity,
                             beam,
                             world.get<components::TransformComponent>(entity),
                             texture,
                             visible,
                             dt,
                             interpolation_alpha);
    device_->submitParticleBeam(desc);
  });
  frame_stats.simulation_ms = core::elapsedMilliseconds(submit_start, core::SteadyClock::now());

  last_stats_ = frame_stats;
  if (device_ != nullptr) {
    device_->setParticleSystemStats(frame_stats);
  }
}

std::size_t ParticleSystem::liveParticleCount(world::Entity entity) const {
  (void)entity;
  return 0u;
}

}  // namespace karma::visual::particles
