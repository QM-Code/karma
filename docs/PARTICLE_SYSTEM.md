# Karma Particle System

This engine now has a reusable particle workflow built around five pieces:

- `particles::ParticleLibrary`: stores named effect templates and texture aliases.
- `components::ParticleEffectComponent`: binds an entity to a named effect key.
- `components::ParticleEffectOverrideComponent`: applies per-entity overrides on top of a named effect.
- `components::ParticleEmitterComponent`: runtime emitter state used by simulation/rendering.
- `particles::ParticleSystem`: engine-owned system that applies effects, simulates particles, and submits particle batches to the renderer.

Most game code should work through `ParticleLibrary` plus the ECS helpers in
`karma/features/visual/particles/effect_api.h` rather than manually constructing particle
components.

For the roadmap toward agent-generated `.kpeffect` files from images or visual
references, see [PARTICLE_EFFECT_GENERATION.md](PARTICLE_EFFECT_GENERATION.md).

## Typical Flow

1. Register texture aliases in `ParticleLibrary`.
2. Register one or more `.kpeffect` files under stable effect keys.
3. Create ECS entities bound to those effect keys.
4. Attach `ParticleEffectOverrideComponent` only when a specific instance needs to diverge from the shared template.
5. Restart or toggle those entities at runtime through the helper API.

The engine polls the effect library every frame, so file-backed effects hot
reload automatically.

## Registering Assets

```cpp
particle_effects->clear();
particle_effects->clearTextureAliases();

particle_effects->registerTextureAliases({
    {"smoke_atlas", smoke_texture},
    {"spark_atlas", spark_texture},
    {"prefabs/explosion/spark_atlas", spark_texture},
});

particle_effects->registerEffectFiles({
    {"smoke_plume", "examples/assets/particles/smoke_plume.kpeffect"},
    {"prefabs/explosion/embers",
     "examples/assets/prefabs/explosion/particles/explosion_embers.kpeffect"},
});
```

Use texture aliases inside `.kpeffect` files with `texture = alias_name`.

## Binding Effects To ECS Entities

```cpp
#include "karma/features/visual/particles/effect_api.h"

components::TransformComponent transform{};
transform.setPosition({0.0f, 1.0f, 0.0f});

ecs::Entity smoke = particles::createEffectEntity(
    *world,
    particles::ParticleEffectEntityDesc{
        .name = "Smoke Plume",
        .effect_key = "smoke_plume",
        .transform = transform,
        .playing = true,
    });
```

For an existing entity:

```cpp
particles::bindEffect(
    *world,
    entity,
    particles::ParticleEffectBindingDesc{
        .effect_key = "prefabs/explosion/embers",
        .playing = false,
    });
```

## Runtime Overrides

`ParticleEffectOverrideComponent` is the concise ECS path for per-instance
variation without duplicating effect assets.

```cpp
components::ParticleEffectOverrideComponent effect_override{};
effect_override.time_scale = 0.5f;
effect_override.radius_scale = 1.35f;
effect_override.start_color = math::Color{1.0f, 1.0f, 1.0f, 0.95f};
effect_override.end_color = math::Color{0.18f, 1.0f, 0.28f, 0.0f};

ecs::Entity orb_core = particles::createEffectEntity(
    *world,
    particles::ParticleEffectEntityDesc{
        .name = "Orb Core",
        .effect_key = "energy_orb_core",
        .transform = transform,
        .effect_override = effect_override,
    });
```

You can also attach or remove overrides on an existing entity:

```cpp
particles::setEffectOverrides(*world, entity, effect_override);
particles::clearEffectOverrides(*world, entity);
```

Supported override fields are intentionally general-purpose:

- `time_scale`
- `spawn_rate_scale`
- `lifetime_scale`
- `size_scale`
- `radius_scale`
- `velocity_scale`
- `angular_velocity_scale`
- `alpha_scale`
- optional `start_color`
- optional `end_color`
- optional `texture`

That covers common ECS authoring cases such as:

- recoloring one shared effect across multiple factions/weapons
- slowing or speeding an effect without retuning every velocity by hand
- making one instance larger or tighter than another while keeping the same asset

## Runtime Control

These helpers are intentionally small and ECS-oriented:

```cpp
particles::restartEffect(*world, explosion_entity);
particles::setEffectPlaying(*world, smoke_entity, false);
particles::setEffectEnabled(*world, smoke_entity, true);
particles::setEffectPlayback(*world, smoke_entity, true, true);
```

`restartEffect(...)` increments `ParticleEffectComponent::restart_count` and
reactivates the emitter, which is the supported way to replay one-shot effects.

## Authoring Model

`.kpeffect` files are templates. At runtime the engine:

1. Looks up the effect key in `ParticleLibrary`.
2. Resolves any texture alias into a runtime texture id.
3. Applies the resulting emitter template to the entity.
4. Applies any `ParticleEffectOverrideComponent` on top of that template.
5. Preserves `enabled` and `playing` if requested by the effect component.

That means:

- Use `ParticleEffectComponent` for named reusable effects.
- Use `ParticleEffectOverrideComponent` when one instance should diverge from the shared template.
- Use `ParticleEmitterComponent` directly only when you want to bypass the library and author emitter state entirely in code.
- Use `auto_apply = false` if you want to attach a `ParticleEffectComponent` without having the library overwrite the emitter.

Overrides are reapplied automatically whenever the effect key, restart count,
hot-reloaded template, or override payload changes.

`.kpeffect` authoring also supports higher-level particle surface controls such
as `blend_mode`, `alignment`, and `shading_mode`. `ParticleEmitterComponent`
and `.kpeffect` files now both expose `time_scale`, so you can slow or speed
simulation cleanly without manually scaling every velocity. `shading_mode =
shell` remains available for billboard impostors that need
fresnel/refraction-style shell shading, but the orb sample now pairs a real
`shot.glb` mesh shell with particle-driven core, arc, halo, and distortion
passes because a real sphere silhouette reads better than a quad impostor for
well-defined glass-like orbs.

## Recommended Pattern

For most gameplay code:

- register shared effect templates once during startup;
- create entities with `particles::createEffectEntity(...)`;
- attach overrides only when a specific instance needs a local style/timing change;
- keep entity ids around;
- move those entities by updating their `TransformComponent`;
- retrigger them with `particles::restartEffect(...)`.

The particle examples in [../examples/particle_example.cpp](../examples/particle_example.cpp)
and [../examples/energy_orb_example.cpp](../examples/energy_orb_example.cpp)
demonstrate this pattern for both staged one-shot explosions and looping
ambient effects built from layered particle passes, mesh shells, authored
`.kpeffect` assets, runtime overrides, hot reload, and file-backed effect
prefabs. For the layered prefab side, see [EFFECT_PREFABS.md](EFFECT_PREFABS.md).
