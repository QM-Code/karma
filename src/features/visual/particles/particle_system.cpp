#include "karma/features/visual/particles/particle_system.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "karma/world/components/camera.h"
#include "karma/world/components/particle_effect.h"
#include "karma/world/components/particle_effect_override.h"
#include "karma/world/components/particle_emitter.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"
#include "karma/core/time.h"
#include "karma/features/visual/particles/effect_library.h"
#include "karma/rendering/renderer/device.h"

namespace karma::particles {

namespace {

constexpr float kMinVisibleAlphaParticleAlpha = 0.01f;

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
  if (std::abs(exponent - 1.0f) <= 1.0e-4f) {
    return clamped_t;
  }
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

float horizontalLengthSquared(const math::Vec3& v) {
  return v.x * v.x + v.z * v.z;
}

float maxComponent(const math::Vec3& v) {
  return std::max(v.x, std::max(v.y, v.z));
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

void addCount(uint32_t& total, std::size_t value) {
  const uint64_t sum = static_cast<uint64_t>(total) + static_cast<uint64_t>(value);
  total = static_cast<uint32_t>(std::min<uint64_t>(sum, std::numeric_limits<uint32_t>::max()));
}

struct ParticleCullCamera {
  bool valid = false;
  bool perspective = true;
  float fov_y_degrees = 60.0f;
  float aspect = 1.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  float ortho_left = -1.0f;
  float ortho_right = 1.0f;
  float ortho_top = 1.0f;
  float ortho_bottom = -1.0f;
  math::Vec3 position{};
  math::Vec3 forward{0.0f, 0.0f, -1.0f};
  math::Vec3 right{1.0f, 0.0f, 0.0f};
  math::Vec3 up{0.0f, 1.0f, 0.0f};
};

ParticleCullCamera resolvePrimaryCullCamera(const ecs::World& world,
                                            renderer::GraphicsDevice* device,
                                            float interpolation_alpha) {
  ParticleCullCamera camera{};
  int fb_width = 1;
  int fb_height = 1;
  if (device != nullptr) {
    device->getFramebufferSize(fb_width, fb_height);
  }
  camera.aspect =
      fb_height > 0 ? static_cast<float>(std::max(fb_width, 1)) / static_cast<float>(fb_height)
                    : 1.0f;

  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    const auto& camera_component = world.get<components::CameraComponent>(entity);
    if (!camera_component.is_primary) {
      return true;
    }

    const auto& transform = world.get<components::TransformComponent>(entity);
    const math::Quat rotation = transform.getInterpolatedRotation(interpolation_alpha);
    camera.valid = true;
    camera.perspective = camera_component.perspective;
    camera.fov_y_degrees = camera_component.fov_y_degrees;
    camera.near_clip = std::max(camera_component.near_clip, 0.001f);
    camera.far_clip = std::max(camera_component.far_clip, camera.near_clip + 0.001f);
    camera.ortho_left = camera_component.ortho_left;
    camera.ortho_right = camera_component.ortho_right;
    camera.ortho_top = camera_component.ortho_top;
    camera.ortho_bottom = camera_component.ortho_bottom;
    camera.position = transform.getInterpolatedPosition(interpolation_alpha);
    camera.forward = math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
    camera.right = math::normalize(math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}));
    camera.up = math::normalize(math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}));
    return false;
  });

  return camera;
}

bool boundsVisibleInCamera(const ParticleCullCamera& camera,
                           bool local_space,
                           const math::Vec3& emitter_position,
                           const math::Quat& emitter_rotation,
                           const math::Vec3& emitter_scale,
                           const math::Vec3& bounds_min,
                           const math::Vec3& bounds_max,
                           float max_particle_extent,
                           float emitter_uniform_scale) {
  if (!camera.valid) {
    return true;
  }

  const math::Vec3 local_center = math::scale(math::add(bounds_min, bounds_max), 0.5f);
  const math::Vec3 local_extents = math::scale(math::subtract(bounds_max, bounds_min), 0.5f);
  math::Vec3 world_center = local_center;
  float radius = 0.0f;

  if (local_space) {
    const math::Vec3 scaled_center{
        local_center.x * emitter_scale.x,
        local_center.y * emitter_scale.y,
        local_center.z * emitter_scale.z,
    };
    const math::Vec3 scaled_extents{
        std::abs(local_extents.x * emitter_scale.x),
        std::abs(local_extents.y * emitter_scale.y),
        std::abs(local_extents.z * emitter_scale.z),
    };
    world_center = math::add(emitter_position, math::rotateVec(emitter_rotation, scaled_center));
    radius = std::sqrt(lengthSquared(scaled_extents)) +
             max_particle_extent * emitter_uniform_scale;
  } else {
    radius = std::sqrt(lengthSquared(local_extents)) + max_particle_extent;
  }

  if (radius <= 0.0f) {
    return true;
  }

  const math::Vec3 to_center = math::subtract(world_center, camera.position);
  const float depth = math::dot(to_center, camera.forward);
  if (depth + radius < camera.near_clip || depth - radius > camera.far_clip) {
    return false;
  }

  const float right_distance = math::dot(to_center, camera.right);
  const float up_distance = math::dot(to_center, camera.up);
  if (camera.perspective) {
    const float clamped_depth = std::max(depth, camera.near_clip);
    const float half_height =
        std::tan(camera.fov_y_degrees * 0.5f * 3.14159265358979323846f / 180.0f) *
        clamped_depth;
    const float half_width = half_height * std::max(camera.aspect, 0.001f);
    return std::abs(right_distance) <= half_width + radius &&
           std::abs(up_distance) <= half_height + radius;
  }

  return std::abs(right_distance) <=
             std::max(std::abs(camera.ortho_left), std::abs(camera.ortho_right)) + radius &&
         std::abs(up_distance) <=
             std::max(std::abs(camera.ortho_bottom), std::abs(camera.ortho_top)) + radius;
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
  emitter.spawn_box_extents = math::scale(emitter.spawn_box_extents, radius_scale);
  emitter.spawn_radius_min *= radius_scale;
  emitter.spawn_radius_max *= radius_scale;
  emitter.radial_speed_min *= velocity_scale;
  emitter.radial_speed_max *= velocity_scale;
  emitter.velocity_min = math::scale(emitter.velocity_min, velocity_scale);
  emitter.velocity_max = math::scale(emitter.velocity_max, velocity_scale);
  emitter.acceleration = math::scale(emitter.acceleration, velocity_scale);
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
  return math::scale(randomUnitVector(state), radius);
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
    direction = math::scale(direction, inv_length);
  }

  return math::scale(direction, randomRange(state, emitter.radial_speed_min, emitter.radial_speed_max));
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
    uint32_t frame_count,
    float age,
    float normalized_age,
    uint32_t frame_offset) {
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
    addCount(binding_updates, 1u);
  });

  return binding_updates;
}

void ParticleSystem::update(ecs::World& world, float dt, float interpolation_alpha) {
  if (device_ == nullptr) {
    return;
  }

  renderer::ParticlePassStats frame_stats{};
  const auto sync_start = core::SteadyClock::now();
  frame_stats.effect_binding_updates = syncEffectBindings(world);
  frame_stats.sync_effect_bindings_ms =
      core::elapsedMilliseconds(sync_start, core::SteadyClock::now());

  const float clamped_dt = std::max(dt, 0.0f);
  const ParticleCullCamera cull_camera =
      resolvePrimaryCullCamera(world, device_, interpolation_alpha);

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
    addCount(frame_stats.simulated_emitters, 1u);
    const auto simulation_start = core::SteadyClock::now();

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
    const float emitter_dt = clamped_dt * std::max(emitter.time_scale, 0.0f);
    const bool ground_collision_enabled = !emitter.local_space && emitter.collide_with_ground;
    const float rest_speed_threshold = std::max(emitter.rest_speed_threshold, 0.0f);
    const float rest_speed_threshold_sq = rest_speed_threshold * rest_speed_threshold;
    const float surface_friction =
        std::clamp(1.0f - emitter.collision_friction, 0.0f, 1.0f);
    const float slide_drag =
        std::clamp(1.0f - (emitter.drag + emitter.collision_friction) * emitter_dt, 0.0f, 1.0f);
    const float velocity_drag = std::clamp(1.0f - emitter.drag * emitter_dt, 0.0f, 1.0f);

    auto resolve_ground_collision = [&](Particle& particle) {
      if (!ground_collision_enabled || particle.position.y > emitter.ground_height) {
        return;
      }

      addCount(frame_stats.ground_collision_particles, 1u);

      particle.position.y = emitter.ground_height;
      particle.resting_on_ground = false;

      if (particle.velocity.y < 0.0f) {
        particle.velocity.y =
            -particle.velocity.y * std::clamp(emitter.bounce_damping, 0.0f, 1.0f);
      }

      particle.velocity.x *= surface_friction;
      particle.velocity.z *= surface_friction;

      if (std::abs(particle.velocity.y) <= rest_speed_threshold) {
        particle.velocity.y = 0.0f;
      }

      if (particle.velocity.y == 0.0f &&
          horizontalLengthSquared(particle.velocity) <= rest_speed_threshold_sq) {
        particle.velocity.x = 0.0f;
        particle.velocity.z = 0.0f;
        particle.resting_on_ground = true;
      }
    };

    size_t alive_count = 0;
    bool has_live_bounds = false;
    math::Vec3 live_bounds_min{};
    math::Vec3 live_bounds_max{};
    float max_live_particle_extent = 0.0f;
    for (size_t particle_index = 0; particle_index < state.particles.size(); ++particle_index) {
      auto& particle = state.particles[particle_index];
      particle.age += emitter_dt;
      if (particle.age >= particle.lifetime) {
        continue;
      }

      if (particle.resting_on_ground && ground_collision_enabled) {
        addCount(frame_stats.ground_collision_particles, 1u);
        particle.velocity.x *= slide_drag;
        particle.velocity.z *= slide_drag;
        particle.position.x += particle.velocity.x * emitter_dt;
        particle.position.z += particle.velocity.z * emitter_dt;
        particle.position.y = emitter.ground_height;
        if (horizontalLengthSquared(particle.velocity) <= rest_speed_threshold_sq) {
          particle.velocity.x = 0.0f;
          particle.velocity.z = 0.0f;
        } else {
          particle.resting_on_ground = false;
        }
      } else {
        particle.velocity = math::add(particle.velocity, math::scale(emitter.acceleration, emitter_dt));
        particle.velocity = math::scale(particle.velocity, velocity_drag);
        particle.position = math::add(particle.position, math::scale(particle.velocity, emitter_dt));
        resolve_ground_collision(particle);
      }
      particle.rotation += particle.angular_velocity * emitter_dt;
      const float particle_extent = std::max(particle.start_size, particle.end_size);
      if (!has_live_bounds) {
        live_bounds_min = particle.position;
        live_bounds_max = particle.position;
        has_live_bounds = true;
      } else {
        live_bounds_min.x = std::min(live_bounds_min.x, particle.position.x);
        live_bounds_min.y = std::min(live_bounds_min.y, particle.position.y);
        live_bounds_min.z = std::min(live_bounds_min.z, particle.position.z);
        live_bounds_max.x = std::max(live_bounds_max.x, particle.position.x);
        live_bounds_max.y = std::max(live_bounds_max.y, particle.position.y);
        live_bounds_max.z = std::max(live_bounds_max.z, particle.position.z);
      }
      max_live_particle_extent = std::max(max_live_particle_extent, particle_extent);
      if (alive_count != particle_index) {
        state.particles[alive_count] = particle;
      }
      ++alive_count;
    }
    state.particles.resize(alive_count);
    addCount(frame_stats.simulated_particles, alive_count);

    const math::Vec3 emitter_position = transform.getInterpolatedPosition(interpolation_alpha);
    const math::Quat emitter_rotation = transform.getInterpolatedRotation(interpolation_alpha);
    const math::Vec3 emitter_scale = transform.getScale();
    const float emitter_uniform_scale = std::max(maxComponent(emitter_scale), 0.0001f);

    auto spawn_particle = [&]() {
      if (state.particles.size() >= state.max_particles) {
        return;
      }
      const math::Vec3 local_offset = randomSpawnOffset(state.rng_state, emitter);
      math::Vec3 local_velocity =
          randomVec3(state.rng_state, emitter.velocity_min, emitter.velocity_max);
      local_velocity = math::add(local_velocity,
                           resolveRadialVelocity(state.rng_state, emitter, local_offset));

      Particle particle{};
      if (emitter.local_space) {
        particle.position = local_offset;
        particle.velocity = local_velocity;
      } else {
        particle.position = math::add(emitter_position, math::rotateVec(emitter_rotation, local_offset));
        particle.velocity = math::rotateVec(emitter_rotation, local_velocity);
      }
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
      const float particle_extent = std::max(particle.start_size, particle.end_size);
      if (!has_live_bounds) {
        live_bounds_min = particle.position;
        live_bounds_max = particle.position;
        has_live_bounds = true;
      } else {
        live_bounds_min.x = std::min(live_bounds_min.x, particle.position.x);
        live_bounds_min.y = std::min(live_bounds_min.y, particle.position.y);
        live_bounds_min.z = std::min(live_bounds_min.z, particle.position.z);
        live_bounds_max.x = std::max(live_bounds_max.x, particle.position.x);
        live_bounds_max.y = std::max(live_bounds_max.y, particle.position.y);
        live_bounds_max.z = std::max(live_bounds_max.z, particle.position.z);
      }
      max_live_particle_extent = std::max(max_live_particle_extent, particle_extent);
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
      frame_stats.simulation_ms +=
          core::elapsedMilliseconds(simulation_start, core::SteadyClock::now());
      return;
    }
    addCount(frame_stats.visible_emitters, 1u);
    if (has_live_bounds &&
        !boundsVisibleInCamera(cull_camera,
                               emitter.local_space,
                               emitter_position,
                               emitter_rotation,
                               emitter_scale,
                               live_bounds_min,
                               live_bounds_max,
                               max_live_particle_extent,
                               emitter_uniform_scale)) {
      addCount(frame_stats.culled_emitters, 1u);
      addCount(frame_stats.culled_particles, state.particles.size());
      frame_stats.simulation_ms +=
          core::elapsedMilliseconds(simulation_start, core::SteadyClock::now());
      return;
    }
    frame_stats.simulation_ms +=
        core::elapsedMilliseconds(simulation_start, core::SteadyClock::now());

    const auto packing_start = core::SteadyClock::now();
    renderer::PackedParticleBatch batch{};
    batch.layer = emitter.layer;
    batch.depth_test = emitter.depth_test;
    batch.texture = emitter.texture;
    batch.blend_mode = emitter.blend_mode;
    batch.alignment = emitter.alignment;
    batch.shading_mode = emitter.shading_mode;
    batch.presentation_mode = renderer::ParticlePresentationMode::Simulated;
    // Distortion emitters currently destabilize the simulated particle path when
    // they also sample scene depth for soft fading, so clamp that combination off.
    batch.use_soft_mask =
        emitter.use_soft_mask &&
        emitter.blend_mode != renderer::ParticleBlendMode::Distortion;
    batch.soft_particle_distance = std::max(emitter.soft_particle_distance, 0.0f);
    batch.distortion_strength = std::max(emitter.distortion_strength, 0.0f);
    batch.fresnel_power = std::max(emitter.fresnel_power, 0.001f);
    batch.fresnel_strength = std::max(emitter.fresnel_strength, 0.0f);
    batch.refraction_strength = std::max(emitter.refraction_strength, 0.0f);
    batch.interior_glow = std::max(emitter.interior_glow, 0.0f);
    batch.size_curve_exponent = std::max(emitter.size_curve_exponent, 0.001f);
    batch.alpha_curve_exponent = std::max(emitter.alpha_curve_exponent, 0.001f);
    batch.atlas_columns = resolveAtlasColumns(emitter);
    batch.atlas_rows = resolveAtlasRows(emitter);
    batch.atlas_frame_count = resolveAtlasFrameCount(emitter);
    batch.animate_over_lifetime = emitter.animate_over_lifetime;
    batch.atlas_frame_width = emitter.atlas_frame_width;
    batch.atlas_frame_height = emitter.atlas_frame_height;
    batch.atlas_border_x = emitter.atlas_border_x;
    batch.atlas_border_y = emitter.atlas_border_y;
    batch.atlas_spacing_x = emitter.atlas_spacing_x;
    batch.atlas_spacing_y = emitter.atlas_spacing_y;
    batch.animation_fps = std::max(emitter.animation_fps, 0.0f);
    batch.particles.reserve(state.particles.size());

    for (const auto& particle : state.particles) {
      const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
      if ((particle.start_size <= 0.0f && particle.end_size <= 0.0f) ||
          (particle.start_color.a <= 0.0f && particle.end_color.a <= 0.0f)) {
        continue;
      }
      if (emitter.blend_mode == renderer::ParticleBlendMode::Alpha) {
        const float alpha_t = applyCurveExponent(t, emitter.alpha_curve_exponent);
        const float current_alpha =
            lerpFloat(particle.start_color.a, particle.end_color.a, alpha_t);
        if (current_alpha <= kMinVisibleAlphaParticleAlpha) {
          continue;
        }
      }
      math::Vec3 world_position = particle.position;
      float world_start_size = particle.start_size;
      float world_end_size = particle.end_size;
      if (emitter.local_space) {
        const math::Vec3 scaled_local = {
            particle.position.x * emitter_scale.x,
            particle.position.y * emitter_scale.y,
            particle.position.z * emitter_scale.z,
        };
        world_position = math::add(emitter_position, math::rotateVec(emitter_rotation, scaled_local));
        world_start_size *= emitter_uniform_scale;
        world_end_size *= emitter_uniform_scale;
      }
      auto& packed = batch.particles.emplace_back();
      packed.position_age[0] = world_position.x;
      packed.position_age[1] = world_position.y;
      packed.position_age[2] = world_position.z;
      packed.position_age[3] = t;
      packed.color_start[0] = particle.start_color.r;
      packed.color_start[1] = particle.start_color.g;
      packed.color_start[2] = particle.start_color.b;
      packed.color_start[3] = particle.start_color.a;
      packed.color_end[0] = particle.end_color.r;
      packed.color_end[1] = particle.end_color.g;
      packed.color_end[2] = particle.end_color.b;
      packed.color_end[3] = particle.end_color.a;
      packed.rotation_size[0] = std::cos(particle.rotation);
      packed.rotation_size[1] = std::sin(particle.rotation);
      packed.rotation_size[2] = world_start_size;
      packed.rotation_size[3] = world_end_size;
      packed.params[0] = 0.0f;
      packed.params[1] = static_cast<float>(particle.frame_offset);
      packed.params[2] = particle.age;
    }
    addCount(frame_stats.packed_particles, batch.particles.size());

    if (!batch.particles.empty()) {
      device_->submitPackedParticles(std::move(batch));
      addCount(frame_stats.submitted_emitters, 1u);
    }
    frame_stats.packing_ms += core::elapsedMilliseconds(packing_start, core::SteadyClock::now());
  });

  device_->setParticleSystemStats(frame_stats);
}

}  // namespace karma::particles
