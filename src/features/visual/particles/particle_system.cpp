#include "karma/features/visual/particles/particle_system.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "karma/core/time.h"
#include "karma/features/visual/particles/effect_library.h"
#include "karma/rendering/renderer/device.h"
#include "karma/world/components/particle_effect.h"
#include "karma/world/components/particle_effect_override.h"
#include "karma/world/components/particle_emitter.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"

namespace karma::particles {

namespace {

uint64_t entityKey(ecs::Entity entity) {
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

uint64_t hashParticleEffectOverride(
    const components::ParticleEffectOverrideComponent* effect_override) {
  if (effect_override == nullptr || !effect_override->active) {
    return 0ull;
  }

  uint64_t seed = 0xcbf29ce484222325ull;
  seed = hashFloat(seed, effect_override->time_scale);
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
    for (const char c : *effect_override->texture_key) {
      seed = hashCombine(seed, static_cast<unsigned char>(c));
    }
  }
  return seed;
}

void applyEffectOverrideToEmitter(const components::ParticleEffectOverrideComponent& effect_override,
                                  components::ParticleEmitterComponent& emitter) {
  if (!effect_override.active) {
    return;
  }

  const float time_scale = std::max(effect_override.time_scale, 0.0f);
  const float spawn_rate_scale = std::max(effect_override.spawn_rate_scale, 0.0f);
  const float lifetime_scale = std::max(effect_override.lifetime_scale, 0.0f);
  const float size_scale = std::max(effect_override.size_scale, 0.0f);
  const float radius_scale = std::max(effect_override.radius_scale, 0.0f);
  const float velocity_scale = std::max(effect_override.velocity_scale, 0.0f);
  const float angular_velocity_scale = std::max(effect_override.angular_velocity_scale, 0.0f);
  const float alpha_scale = std::max(effect_override.alpha_scale, 0.0f);

  emitter.time_scale *= time_scale;
  emitter.spawn_rate *= spawn_rate_scale;
  emitter.particle_lifetime_min *= lifetime_scale;
  emitter.particle_lifetime_max *= lifetime_scale;
  emitter.start_size_min *= size_scale;
  emitter.start_size_max *= size_scale;
  emitter.end_size_min *= size_scale;
  emitter.end_size_max *= size_scale;
  emitter.spawn_box_extents.x *= radius_scale;
  emitter.spawn_box_extents.y *= radius_scale;
  emitter.spawn_box_extents.z *= radius_scale;
  emitter.spawn_radius_min *= radius_scale;
  emitter.spawn_radius_max *= radius_scale;
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
}

renderer::ParticleBlendMode toRendererBlendMode(components::ParticleBlendMode mode) {
  switch (mode) {
    case components::ParticleBlendMode::Additive:
      return renderer::ParticleBlendMode::Additive;
    case components::ParticleBlendMode::Alpha:
      return renderer::ParticleBlendMode::Alpha;
    case components::ParticleBlendMode::Distortion:
      return renderer::ParticleBlendMode::Distortion;
  }
  return renderer::ParticleBlendMode::Additive;
}

renderer::ParticleAlignment toRendererAlignment(components::ParticleAlignment alignment) {
  switch (alignment) {
    case components::ParticleAlignment::Billboard:
      return renderer::ParticleAlignment::Billboard;
    case components::ParticleAlignment::Ground:
      return renderer::ParticleAlignment::Ground;
  }
  return renderer::ParticleAlignment::Billboard;
}

renderer::ParticleShadingMode toRendererShadingMode(components::ParticleShadingMode mode) {
  switch (mode) {
    case components::ParticleShadingMode::Standard:
      return renderer::ParticleShadingMode::Standard;
    case components::ParticleShadingMode::Shell:
      return renderer::ParticleShadingMode::Shell;
  }
  return renderer::ParticleShadingMode::Standard;
}

renderer::ParticleSpawnShape toRendererSpawnShape(components::ParticleSpawnShape shape) {
  switch (shape) {
    case components::ParticleSpawnShape::Box:
      return renderer::ParticleSpawnShape::Box;
    case components::ParticleSpawnShape::Sphere:
      return renderer::ParticleSpawnShape::Sphere;
    case components::ParticleSpawnShape::SphereSurface:
      return renderer::ParticleSpawnShape::SphereSurface;
  }
  return renderer::ParticleSpawnShape::Box;
}

renderer::ParticleEmitterGpuDesc makeRendererEmitterDesc(
    ecs::Entity entity,
    const components::ParticleEmitterComponent& emitter,
    const components::TransformComponent& transform,
    renderer::TextureId texture,
    uint32_t restart_count,
    bool visible,
    float dt,
    float interpolation_alpha) {
  renderer::ParticleEmitterGpuDesc desc{};
  desc.instance_id = entityKey(entity);
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
  desc.layer = static_cast<renderer::LayerId>(emitter.layer);
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
  desc.spawn_shape = toRendererSpawnShape(emitter.spawn_shape);
  desc.spawn_box_extents = emitter.spawn_box_extents;
  desc.spawn_radius_min = emitter.spawn_radius_min;
  desc.spawn_radius_max = emitter.spawn_radius_max;
  desc.radial_speed_min = emitter.radial_speed_min;
  desc.radial_speed_max = emitter.radial_speed_max;
  desc.velocity_min = emitter.velocity_min;
  desc.velocity_max = emitter.velocity_max;
  desc.acceleration = emitter.acceleration;
  desc.drag = emitter.drag;
  desc.collide_with_ground = emitter.collide_with_ground;
  desc.ground_height = emitter.ground_height;
  desc.bounce_damping = emitter.bounce_damping;
  desc.collision_friction = emitter.collision_friction;
  desc.rest_speed_threshold = emitter.rest_speed_threshold;
  desc.start_color = emitter.start_color;
  desc.end_color = emitter.end_color;
  return desc;
}

}  // namespace

uint32_t ParticleSystem::syncEffectBindings(ecs::World& world) {
  if (library_ == nullptr) {
    return 0u;
  }

  uint32_t binding_updates = 0u;
  library_->update();
  const uint64_t library_version = library_->version();
  world.forEach<components::ParticleEffectComponent>([&](const ecs::Entity entity) {
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
                             effect.applied_version != library_version ||
                             effect.applied_override_hash != override_hash ||
                             effect.applied_restart_count != effect.restart_count ||
                             effect.applied_effect_key != effect.effect_key;
    if (!needs_apply) {
      return;
    }

    components::ParticleEmitterComponent emitter{};
    if (!library_->instantiateEmitter(effect.effect_key, emitter)) {
      return;
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
    effect.applied_version = library_version;
    effect.applied_override_hash = override_hash;
    effect.applied_restart_count = effect.restart_count;
    effect.applied_effect_key = effect.effect_key;
    binding_updates += 1u;
  });

  return binding_updates;
}

void ParticleSystem::update(ecs::World& world, float dt, float interpolation_alpha) {
  renderer::ParticlePassStats frame_stats{};
  const auto sync_start = core::SteadyClock::now();
  frame_stats.effect_binding_updates = syncEffectBindings(world);
  frame_stats.sync_effect_bindings_ms =
      core::elapsedMilliseconds(sync_start, core::SteadyClock::now());

  const auto submit_start = core::SteadyClock::now();
  world.forEach<components::ParticleEmitterComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    const auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    frame_stats.simulated_emitters += 1u;

    bool visible = emitter.enabled;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }
    if (visible) {
      frame_stats.visible_emitters += 1u;
    } else {
      frame_stats.culled_emitters += 1u;
    }

    uint32_t restart_count = 0u;
    if (world.has<components::ParticleEffectComponent>(entity)) {
      restart_count = world.get<components::ParticleEffectComponent>(entity).restart_count;
    }

    if (device_ != nullptr) {
      const renderer::TextureId texture =
          (library_ != nullptr && !emitter.texture_key.empty())
              ? library_->resolveTextureAlias(emitter.texture_key)
              : renderer::kInvalidTexture;
      const renderer::ParticleEmitterGpuDesc desc =
          makeRendererEmitterDesc(entity,
                                  emitter,
                                  transform,
                                  texture,
                                  restart_count,
                                  visible,
                                  dt,
                                  interpolation_alpha);
      device_->submitParticleEmitter(desc);
      frame_stats.submitted_emitters += 1u;
    }
  });
  frame_stats.simulation_ms = core::elapsedMilliseconds(submit_start, core::SteadyClock::now());

  if (device_ != nullptr) {
    device_->setParticleSystemStats(frame_stats);
  }
}

std::size_t ParticleSystem::liveParticleCount(ecs::Entity entity) const {
  (void)entity;
  return 0u;
}

}  // namespace karma::particles
