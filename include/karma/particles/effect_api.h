#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "karma/components/particle_effect.h"
#include "karma/components/particle_effect_override.h"
#include "karma/components/particle_emitter.h"
#include "karma/components/transform.h"
#include "karma/components/visibility.h"
#include "karma/ecs/world.h"

namespace karma::particles {

struct ParticleEffectBindingDesc {
  std::string_view effect_key;
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  std::optional<components::ParticleEffectOverrideComponent> effect_override;
};

struct ParticleEffectEntityDesc {
  std::string_view name;
  std::string_view effect_key;
  components::TransformComponent transform{};
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  std::optional<components::ParticleEffectOverrideComponent> effect_override;
};

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
                    });
  if (desc.effect_override.has_value()) {
    world.add(entity, *desc.effect_override);
  }
  return true;
}

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
                 .effect_override = desc.effect_override,
             });
  return entity;
}

inline bool setEffectOverrides(ecs::World& world,
                               ecs::Entity entity,
                               components::ParticleEffectOverrideComponent effect_override) {
  if (!world.isAlive(entity)) {
    return false;
  }
  world.add(entity, std::move(effect_override));
  return true;
}

inline bool clearEffectOverrides(ecs::World& world, ecs::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEffectOverrideComponent>(entity)) {
    return false;
  }
  world.remove<components::ParticleEffectOverrideComponent>(entity);
  return true;
}

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

inline bool setEffectPlaying(ecs::World& world, ecs::Entity entity, bool playing) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  world.get<components::ParticleEmitterComponent>(entity).playing = playing;
  return true;
}

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
