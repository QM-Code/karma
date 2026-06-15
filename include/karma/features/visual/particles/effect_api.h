#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "karma/world/components/particle_effect.h"
#include "karma/world/components/particle_effect_override.h"
#include "karma/world/components/particle_emitter.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/ecs/world.h"

namespace karma::particles {

/// \ingroup karma_particles
/// Binding options for attaching a named particle effect to an entity.
struct ParticleEffectBindingDesc {
  std::string_view effect_key;
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  bool preserve_start_delay = false;
  std::optional<components::ParticleEffectOverrideComponent> effect_override;
};

/// \ingroup karma_particles
/// Creation options for a new particle effect entity.
struct ParticleEffectEntityDesc {
  std::string_view name;
  std::string_view effect_key;
  components::TransformComponent transform{};
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  bool preserve_start_delay = false;
  std::optional<components::ParticleEffectOverrideComponent> effect_override;
};

/// Binds an existing entity to a named particle effect.
inline bool bindEffect(ecs::World& world,
                       ecs::Entity entity,
                       const ParticleEffectBindingDesc& desc) {
  if (!world.isAlive(entity) || desc.effect_key.empty()) {
    return false;
  }

  if (world.has<components::ParticleEmitterComponent>(entity)) {
    auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    emitter.enabled = desc.enabled;
    emitter.playing = desc.playing;
  } else {
    world.add(entity, components::ParticleEmitterComponent{
                          .enabled = desc.enabled,
                          .playing = desc.playing,
                      });
  }
  world.add(entity, components::ParticleEffectComponent{
                        .effect_key = std::string(desc.effect_key),
                        .auto_apply = desc.auto_apply,
                        .preserve_enabled = desc.preserve_enabled,
                        .preserve_playing = desc.preserve_playing,
                        .preserve_start_delay = desc.preserve_start_delay,
                    });
  if (desc.effect_override.has_value()) {
    world.add(entity, *desc.effect_override);
  }
  return true;
}

/// Creates a transform entity and binds it to a named particle effect.
inline ecs::Entity createEffectEntity(ecs::World& world,
                                      const ParticleEffectEntityDesc& desc) {
  ecs::Entity entity = world.createEntity();
  if (!desc.name.empty()) {
    world.setName(entity, std::string(desc.name));
  }
  world.add(entity, desc.transform);
  bindEffect(world,
             entity,
             ParticleEffectBindingDesc{
                 .effect_key = desc.effect_key,
                 .enabled = desc.enabled,
                 .playing = desc.playing,
                 .auto_apply = desc.auto_apply,
                 .preserve_enabled = desc.preserve_enabled,
                 .preserve_playing = desc.preserve_playing,
                 .preserve_start_delay = desc.preserve_start_delay,
                 .effect_override = desc.effect_override,
             });
  return entity;
}

/// Adds or replaces per-instance effect overrides.
inline bool setEffectOverrides(ecs::World& world,
                               ecs::Entity entity,
                               components::ParticleEffectOverrideComponent effect_override) {
  if (!world.isAlive(entity)) {
    return false;
  }
  world.add(entity, std::move(effect_override));
  return true;
}

/// Replaces the source path for one particle-effect instance.
inline bool setEffectSourcePath(ecs::World& world,
                                ecs::Entity entity,
                                std::vector<math::Vec3> points,
                                bool closed_loop = false) {
  if (!world.isAlive(entity)) {
    return false;
  }
  components::ParticleEffectOverrideComponent effect_override =
      world.has<components::ParticleEffectOverrideComponent>(entity)
          ? world.get<components::ParticleEffectOverrideComponent>(entity)
          : components::ParticleEffectOverrideComponent{};
  effect_override.source_shape = components::ParticleSourceShape::Path;
  effect_override.source_path_points = std::move(points);
  effect_override.source_closed_loop = closed_loop;
  world.add(entity, std::move(effect_override));
  return true;
}

/// Replaces the source box extents for one particle-effect instance.
inline bool setEffectSourceBoxExtents(ecs::World& world,
                                      ecs::Entity entity,
                                      const math::Vec3& extents) {
  if (!world.isAlive(entity)) {
    return false;
  }
  components::ParticleEffectOverrideComponent effect_override =
      world.has<components::ParticleEffectOverrideComponent>(entity)
          ? world.get<components::ParticleEffectOverrideComponent>(entity)
          : components::ParticleEffectOverrideComponent{};
  effect_override.source_shape = components::ParticleSourceShape::Box;
  effect_override.source_box_extents = extents;
  world.add(entity, std::move(effect_override));
  return true;
}

/// Removes per-instance effect overrides.
inline bool clearEffectOverrides(ecs::World& world, ecs::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEffectOverrideComponent>(entity)) {
    return false;
  }
  world.remove<components::ParticleEffectOverrideComponent>(entity);
  return true;
}

/// Enables or disables an effect and matching visibility when present.
inline bool setEffectEnabled(ecs::World& world, ecs::Entity entity, bool enabled) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  world.get<components::ParticleEmitterComponent>(entity).enabled = enabled;
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = enabled;
  }
  return true;
}

/// Sets whether an emitter is actively playing.
inline bool setEffectPlaying(ecs::World& world, ecs::Entity entity, bool playing) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  world.get<components::ParticleEmitterComponent>(entity).playing = playing;
  return true;
}

/// Sets both effect enabled and playback state.
inline bool setEffectPlayback(ecs::World& world,
                              ecs::Entity entity,
                              bool enabled,
                              bool playing) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
  emitter.enabled = enabled;
  emitter.playing = playing;
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = enabled;
  }
  return true;
}

/// Restarts an effect by incrementing its restart counter.
inline bool restartEffect(ecs::World& world, ecs::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEffectComponent>(entity)) {
    return false;
  }

  if (world.has<components::ParticleEmitterComponent>(entity)) {
    auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    emitter.enabled = true;
    emitter.playing = true;
  }
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = true;
  }

  world.get<components::ParticleEffectComponent>(entity).restart_count += 1;
  return true;
}

}  // namespace karma::particles
