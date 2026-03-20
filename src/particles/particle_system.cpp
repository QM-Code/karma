#include "karma/particles/particle_system.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "karma/components/particle_effect.h"
#include "karma/components/particle_effect_override.h"
#include "karma/components/particle_emitter.h"
#include "karma/components/transform.h"
#include "karma/components/visibility.h"
#include "karma/math/quat.h"
#include "karma/particles/effect_library.h"
#include "karma/renderer/device.h"

namespace karma::particles {

namespace {

math::Vec3 add(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 scale(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

math::Color lerpColor(const math::Color& a, const math::Color& b, float t) {
  const float s = std::clamp(t, 0.0f, 1.0f);
  return {
      a.r + (b.r - a.r) * s,
      a.g + (b.g - a.g) * s,
      a.b + (b.b - a.b) * s,
      a.a + (b.a - a.a) * s,
  };
}

float lerpFloat(float a, float b, float t) {
  return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

float applyCurveExponent(float t, float exponent) {
  const float clamped_t = std::clamp(t, 0.0f, 1.0f);
  return std::pow(clamped_t, std::max(exponent, 0.001f));
}

uint32_t nextRandom(uint32_t& state) {
  if (state == 0u) {
    state = 0xA511E9B3u;
  }
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

float random01(uint32_t& state) {
  constexpr float kInvMax = 1.0f / 4294967295.0f;
  return static_cast<float>(nextRandom(state)) * kInvMax;
}

float randomRange(uint32_t& state, float min_v, float max_v) {
  if (max_v < min_v) {
    std::swap(min_v, max_v);
  }
  return min_v + (max_v - min_v) * random01(state);
}

float lengthSquared(const math::Vec3& v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

float horizontalLengthSquared(const math::Vec3& v) {
  return v.x * v.x + v.z * v.z;
}

math::Vec3 randomVec3(uint32_t& state,
                      const math::Vec3& min_v,
                      const math::Vec3& max_v) {
  return {
      randomRange(state, min_v.x, max_v.x),
      randomRange(state, min_v.y, max_v.y),
      randomRange(state, min_v.z, max_v.z),
  };
}

math::Vec3 randomUnitVector(uint32_t& state) {
  constexpr float kTau = 6.2831853f;
  const float z = randomRange(state, -1.0f, 1.0f);
  const float radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
  const float angle = randomRange(state, 0.0f, kTau);
  return {
      radial * std::cos(angle),
      z,
      radial * std::sin(angle),
  };
}

math::Vec3 scaleVec(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
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
  seed = hashCombine(seed, effect_override->texture.has_value() ? 1ull : 0ull);
  if (effect_override->texture.has_value()) {
    seed = hashCombine(seed, static_cast<uint64_t>(*effect_override->texture));
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
  emitter.spawn_box_extents = scaleVec(emitter.spawn_box_extents, radius_scale);
  emitter.spawn_radius_min *= radius_scale;
  emitter.spawn_radius_max *= radius_scale;
  emitter.radial_speed_min *= velocity_scale;
  emitter.radial_speed_max *= velocity_scale;
  emitter.velocity_min = scaleVec(emitter.velocity_min, velocity_scale);
  emitter.velocity_max = scaleVec(emitter.velocity_max, velocity_scale);
  emitter.acceleration = scaleVec(emitter.acceleration, velocity_scale);
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

  if (effect_override.texture.has_value()) {
    emitter.texture = *effect_override.texture;
  }
}

math::Vec3 randomSpawnOffset(uint32_t& state,
                             const components::ParticleEmitterComponent& emitter) {
  if (emitter.spawn_shape == components::ParticleSpawnShape::Box) {
    return {
        randomRange(state, -emitter.spawn_box_extents.x, emitter.spawn_box_extents.x),
        randomRange(state, -emitter.spawn_box_extents.y, emitter.spawn_box_extents.y),
        randomRange(state, -emitter.spawn_box_extents.z, emitter.spawn_box_extents.z),
    };
  }

  float radius_min = std::max(emitter.spawn_radius_min, 0.0f);
  float radius_max = std::max(emitter.spawn_radius_max, radius_min);
  if (radius_max <= 0.0f) {
    return {};
  }

  float radius = radius_max;
  if (emitter.spawn_shape == components::ParticleSpawnShape::Sphere) {
    const float radius_min_cubed = radius_min * radius_min * radius_min;
    const float radius_max_cubed = radius_max * radius_max * radius_max;
    radius = std::cbrt(radius_min_cubed +
                       (radius_max_cubed - radius_min_cubed) * random01(state));
  } else {
    radius = randomRange(state, radius_min, radius_max);
  }
  return scaleVec(randomUnitVector(state), radius);
}

math::Vec3 resolveRadialVelocity(uint32_t& state,
                                 const components::ParticleEmitterComponent& emitter,
                                 const math::Vec3& local_offset) {
  if (emitter.radial_speed_min == 0.0f && emitter.radial_speed_max == 0.0f) {
    return {};
  }

  math::Vec3 direction = local_offset;
  if (lengthSquared(direction) <= 1.0e-6f) {
    direction = randomUnitVector(state);
  } else {
    const float inv_length = 1.0f / std::sqrt(lengthSquared(direction));
    direction = scaleVec(direction, inv_length);
  }

  return scaleVec(direction, randomRange(state, emitter.radial_speed_min, emitter.radial_speed_max));
}

uint32_t resolveAtlasColumns(const components::ParticleEmitterComponent& emitter) {
  return std::max(emitter.atlas_columns, 1u);
}

uint32_t resolveAtlasRows(const components::ParticleEmitterComponent& emitter) {
  return std::max(emitter.atlas_rows, 1u);
}

uint32_t resolveAtlasFrameCount(const components::ParticleEmitterComponent& emitter) {
  const uint32_t atlas_capacity = resolveAtlasColumns(emitter) * resolveAtlasRows(emitter);
  if (atlas_capacity == 0u) {
    return 1u;
  }
  if (emitter.atlas_frame_count == 0u) {
    return atlas_capacity;
  }
  return std::min(emitter.atlas_frame_count, atlas_capacity);
}

struct ParticleFrameSelection {
  uint32_t current = 0u;
  uint32_t next = 0u;
  float blend = 0.0f;
};

ParticleFrameSelection resolveParticleFrameSelection(
    const components::ParticleEmitterComponent& emitter,
    float age,
    float normalized_age,
    uint32_t frame_offset) {
  const uint32_t frame_count = resolveAtlasFrameCount(emitter);
  if (frame_count <= 1u) {
    return {};
  }

  if (emitter.animate_over_lifetime) {
    const float frame_position =
        std::clamp(normalized_age, 0.0f, 1.0f) * static_cast<float>(frame_count - 1u);
    const uint32_t current = std::min(static_cast<uint32_t>(std::floor(frame_position)),
                                      frame_count - 1u);
    const uint32_t next = std::min(current + 1u, frame_count - 1u);
    return ParticleFrameSelection{
        .current = current,
        .next = next,
        .blend = std::clamp(frame_position - static_cast<float>(current), 0.0f, 1.0f),
    };
  }

  float frame_position = 0.0f;
  if (emitter.animation_fps > 0.0f) {
    frame_position = std::max(age, 0.0f) * emitter.animation_fps;
  }
  if (emitter.random_start_frame) {
    frame_position += static_cast<float>(frame_offset);
  }

  const float wrapped_position =
      frame_count > 0u
          ? std::fmod(frame_position, static_cast<float>(frame_count))
          : 0.0f;
  const float normalized_position = wrapped_position >= 0.0f
                                        ? wrapped_position
                                        : wrapped_position + static_cast<float>(frame_count);
  const uint32_t current = static_cast<uint32_t>(std::floor(normalized_position)) % frame_count;
  const uint32_t next = (current + 1u) % frame_count;
  return ParticleFrameSelection{
      .current = current,
      .next = next,
      .blend = std::clamp(normalized_position - static_cast<float>(current), 0.0f, 1.0f),
  };
}

void computeAtlasUvRect(const components::ParticleEmitterComponent& emitter,
                        uint32_t frame_index,
                        glm::vec2& out_min,
                        glm::vec2& out_max) {
  const uint32_t columns = resolveAtlasColumns(emitter);
  const uint32_t rows = resolveAtlasRows(emitter);
  const uint32_t frame_count = resolveAtlasFrameCount(emitter);
  const uint32_t clamped_frame = std::min(frame_index, frame_count - 1u);
  const uint32_t column = clamped_frame % columns;
  const uint32_t row = clamped_frame / columns;
  if (emitter.atlas_frame_width > 0u && emitter.atlas_frame_height > 0u) {
    const uint32_t texture_width =
        columns * emitter.atlas_frame_width +
        (columns > 0u ? (columns - 1u) * emitter.atlas_spacing_x : 0u) +
        emitter.atlas_border_x * 2u;
    const uint32_t texture_height =
        rows * emitter.atlas_frame_height +
        (rows > 0u ? (rows - 1u) * emitter.atlas_spacing_y : 0u) +
        emitter.atlas_border_y * 2u;
    if (texture_width > 0u && texture_height > 0u) {
      const uint32_t frame_x =
          emitter.atlas_border_x + column * (emitter.atlas_frame_width + emitter.atlas_spacing_x);
      const uint32_t frame_y =
          emitter.atlas_border_y + row * (emitter.atlas_frame_height + emitter.atlas_spacing_y);
      out_min = glm::vec2(static_cast<float>(frame_x) / static_cast<float>(texture_width),
                          static_cast<float>(frame_y) / static_cast<float>(texture_height));
      out_max = glm::vec2(static_cast<float>(frame_x + emitter.atlas_frame_width) /
                              static_cast<float>(texture_width),
                          static_cast<float>(frame_y + emitter.atlas_frame_height) /
                              static_cast<float>(texture_height));
      return;
    }
  }

  const float inv_columns = 1.0f / static_cast<float>(columns);
  const float inv_rows = 1.0f / static_cast<float>(rows);
  out_min = glm::vec2(static_cast<float>(column) * inv_columns,
                      static_cast<float>(row) * inv_rows);
  out_max = glm::vec2(static_cast<float>(column + 1u) * inv_columns,
                      static_cast<float>(row + 1u) * inv_rows);
}

}  // namespace

void ParticleSystem::syncEffectBindings(ecs::World& world) {
  if (library_ == nullptr) {
    return;
  }

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
    }

    if (effect_override != nullptr) {
      applyEffectOverrideToEmitter(*effect_override, emitter);
    }

    world.add(entity, std::move(emitter));
    effect.applied_version = library_version;
    effect.applied_override_hash = override_hash;
    effect.applied_restart_count = effect.restart_count;
    effect.applied_effect_key = effect.effect_key;
    emitters_.erase(entityKey(entity));
  });
}

void ParticleSystem::update(ecs::World& world, float dt, float interpolation_alpha) {
  if (device_ == nullptr) {
    return;
  }

  syncEffectBindings(world);

  const float clamped_dt = std::max(dt, 0.0f);

  for (auto it = emitters_.begin(); it != emitters_.end();) {
    ecs::Entity entity{};
    entity.index = static_cast<uint32_t>(it->first >> 32);
    entity.generation = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
    const bool stale = !world.isAlive(entity) ||
                       !world.has<components::ParticleEmitterComponent>(entity) ||
                       !world.has<components::TransformComponent>(entity);
    if (stale) {
      it = emitters_.erase(it);
    } else {
      ++it;
    }
  }

  world.forEach<components::ParticleEmitterComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    const auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    const uint64_t key = entityKey(entity);
    auto& state = emitters_[key];

    if (!state.initialized) {
      const uint32_t fallback_seed =
          static_cast<uint32_t>((key >> 32) ^ (key & 0xFFFFFFFFu) ^ 0x9E3779B9u);
      state.rng_state = emitter.seed != 0u ? emitter.seed : std::max(fallback_seed, 1u);
      state.initialized = true;
    }
    state.max_particles = std::max(emitter.max_particles, 1u);
    if (state.particles.capacity() < state.max_particles) {
      state.particles.reserve(state.max_particles);
    }

    auto resolve_ground_collision = [&](Particle& particle) {
      if (!emitter.collide_with_ground || particle.position.y > emitter.ground_height) {
        return;
      }

      const float rest_speed_threshold = std::max(emitter.rest_speed_threshold, 0.0f);

      particle.position.y = emitter.ground_height;
      particle.resting_on_ground = false;

      if (particle.velocity.y < 0.0f) {
        particle.velocity.y =
            -particle.velocity.y * std::clamp(emitter.bounce_damping, 0.0f, 1.0f);
      }

      const float surface_friction =
          std::clamp(1.0f - emitter.collision_friction, 0.0f, 1.0f);
      particle.velocity.x *= surface_friction;
      particle.velocity.z *= surface_friction;

      if (std::abs(particle.velocity.y) <= rest_speed_threshold) {
        particle.velocity.y = 0.0f;
      }

      if (particle.velocity.y == 0.0f &&
          horizontalLengthSquared(particle.velocity) <=
              rest_speed_threshold * rest_speed_threshold) {
        particle.velocity.x = 0.0f;
        particle.velocity.z = 0.0f;
        particle.resting_on_ground = true;
      }
    };

    const float emitter_dt = clamped_dt * std::max(emitter.time_scale, 0.0f);

    size_t alive_count = 0;
    for (auto& particle : state.particles) {
      particle.age += emitter_dt;
      if (particle.age >= particle.lifetime) {
        continue;
      }

      if (particle.resting_on_ground && emitter.collide_with_ground) {
        const float rest_speed_threshold = std::max(emitter.rest_speed_threshold, 0.0f);
        const float slide_drag = std::clamp(
            1.0f - (emitter.drag + emitter.collision_friction) * emitter_dt, 0.0f, 1.0f);
        particle.velocity.x *= slide_drag;
        particle.velocity.z *= slide_drag;
        particle.position.x += particle.velocity.x * emitter_dt;
        particle.position.z += particle.velocity.z * emitter_dt;
        particle.position.y = emitter.ground_height;
        if (horizontalLengthSquared(particle.velocity) <=
            rest_speed_threshold * rest_speed_threshold) {
          particle.velocity.x = 0.0f;
          particle.velocity.z = 0.0f;
        } else {
          particle.resting_on_ground = false;
        }
      } else {
        particle.velocity = add(particle.velocity, scale(emitter.acceleration, emitter_dt));
        const float drag = std::clamp(1.0f - emitter.drag * emitter_dt, 0.0f, 1.0f);
        particle.velocity = scale(particle.velocity, drag);
        particle.position = add(particle.position, scale(particle.velocity, emitter_dt));
        resolve_ground_collision(particle);
      }
      particle.rotation += particle.angular_velocity * emitter_dt;
      state.particles[alive_count++] = particle;
    }
    state.particles.resize(alive_count);

    const math::Vec3 emitter_position =
        transform.getInterpolatedPosition(interpolation_alpha);
    const math::Quat emitter_rotation =
        transform.getInterpolatedRotation(interpolation_alpha);

    auto spawn_particle = [&]() {
      if (state.particles.size() >= state.max_particles) {
        return;
      }
      const math::Vec3 local_offset = randomSpawnOffset(state.rng_state, emitter);
      math::Vec3 local_velocity =
          randomVec3(state.rng_state, emitter.velocity_min, emitter.velocity_max);
      local_velocity = add(local_velocity,
                           resolveRadialVelocity(state.rng_state, emitter, local_offset));

      Particle particle{};
      particle.position = add(emitter_position, math::rotateVec(emitter_rotation, local_offset));
      particle.velocity = math::rotateVec(emitter_rotation, local_velocity);
      particle.lifetime = std::max(
          randomRange(state.rng_state, emitter.particle_lifetime_min, emitter.particle_lifetime_max),
          0.01f);
      particle.start_size = std::max(
          randomRange(state.rng_state, emitter.start_size_min, emitter.start_size_max), 0.0f);
      particle.end_size = std::max(
          randomRange(state.rng_state, emitter.end_size_min, emitter.end_size_max), 0.0f);
      particle.start_color = emitter.start_color;
      particle.end_color = emitter.end_color;
      particle.rotation = randomRange(
          state.rng_state, emitter.initial_rotation_min, emitter.initial_rotation_max);
      particle.angular_velocity = randomRange(
          state.rng_state, emitter.angular_velocity_min, emitter.angular_velocity_max);
      if (emitter.random_start_frame) {
        const uint32_t frame_count = resolveAtlasFrameCount(emitter);
        if (frame_count > 1u) {
          particle.frame_offset = nextRandom(state.rng_state) % frame_count;
        }
      }
      state.particles.push_back(particle);
    };

    if (emitter.enabled && emitter.playing) {
      if (emitter.emit_burst_on_start && emitter.burst_count > 0 && !state.burst_emitted) {
        const uint32_t live_particles =
            static_cast<uint32_t>(std::min(state.particles.size(),
                                           static_cast<size_t>(state.max_particles)));
        const uint32_t burst_to_spawn = std::min(
            emitter.burst_count,
            state.max_particles - live_particles);
        for (uint32_t i = 0; i < burst_to_spawn; ++i) {
          spawn_particle();
        }
        state.burst_emitted = true;
      }

      const bool continuous_spawn =
          emitter.loop || (emitter.duration > 0.0f && state.elapsed < emitter.duration);
      if (continuous_spawn && emitter.spawn_rate > 0.0f) {
        state.spawn_accumulator += emitter.spawn_rate * emitter_dt;
        const uint32_t spawn_count = static_cast<uint32_t>(std::floor(state.spawn_accumulator));
        state.spawn_accumulator -= static_cast<float>(spawn_count);
        const uint32_t live_particles =
            static_cast<uint32_t>(std::min(state.particles.size(),
                                           static_cast<size_t>(state.max_particles)));
        const uint32_t room_left =
            state.max_particles - live_particles;
        const uint32_t clamped_spawn_count = std::min(spawn_count, room_left);
        for (uint32_t i = 0; i < clamped_spawn_count; ++i) {
          spawn_particle();
        }
      }

      if (!emitter.loop && emitter.duration > 0.0f) {
        state.elapsed += emitter_dt;
      }
    }

    bool visible = emitter.enabled;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }
    if (!visible || state.particles.empty()) {
      return;
    }

    renderer::ParticleBatch batch{};
    batch.layer = emitter.layer;
    batch.depth_test = emitter.depth_test;
    batch.texture = emitter.texture;
    batch.blend_mode = emitter.blend_mode;
    batch.alignment = emitter.alignment;
    batch.shading_mode = emitter.shading_mode;
    batch.use_soft_mask = emitter.use_soft_mask;
    batch.soft_particle_distance = std::max(emitter.soft_particle_distance, 0.0f);
    batch.distortion_strength = std::max(emitter.distortion_strength, 0.0f);
    batch.fresnel_power = std::max(emitter.fresnel_power, 0.001f);
    batch.fresnel_strength = std::max(emitter.fresnel_strength, 0.0f);
    batch.refraction_strength = std::max(emitter.refraction_strength, 0.0f);
    batch.interior_glow = std::max(emitter.interior_glow, 0.0f);
    batch.particles.reserve(state.particles.size());

    for (const auto& particle : state.particles) {
      const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
      const float size = lerpFloat(particle.start_size,
                                   particle.end_size,
                                   applyCurveExponent(t, emitter.size_curve_exponent));
      math::Color color = lerpColor(particle.start_color, particle.end_color, t);
      color.a = lerpFloat(particle.start_color.a,
                          particle.end_color.a,
                          applyCurveExponent(t, emitter.alpha_curve_exponent));
      if (size <= 0.0f || color.a <= 0.0f) {
        continue;
      }
      glm::vec2 uv_min{0.0f, 0.0f};
      glm::vec2 uv_max{1.0f, 1.0f};
      glm::vec2 uv_min_next{0.0f, 0.0f};
      glm::vec2 uv_max_next{1.0f, 1.0f};
      const ParticleFrameSelection frame_selection =
          resolveParticleFrameSelection(emitter, particle.age, t, particle.frame_offset);
      computeAtlasUvRect(emitter, frame_selection.current, uv_min, uv_max);
      computeAtlasUvRect(emitter, frame_selection.next, uv_min_next, uv_max_next);
      batch.particles.push_back(renderer::ParticleInstance{
          .position = glm::vec3(particle.position.x, particle.position.y, particle.position.z),
          .size = size,
          .color = color,
          .rotation_radians = particle.rotation,
          .uv_min = uv_min,
          .uv_max = uv_max,
          .uv_min_next = uv_min_next,
          .uv_max_next = uv_max_next,
          .frame_blend = frame_selection.blend,
      });
    }

    if (!batch.particles.empty()) {
      device_->submitParticles(batch);
    }
  });
}

}  // namespace karma::particles
