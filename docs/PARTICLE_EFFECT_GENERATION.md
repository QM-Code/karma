# Particle Effect Generation Plan

This note captures the current state of `.kpeffect` authoring and what needs to
exist before an agent can reliably generate particle effect files from an image
or visual reference.

## Current State

Particle effects are reusable emitter templates stored as `.kpeffect` files.
The runtime flow is:

1. Register borrowed particle texture handles in `AssetRegistry`.
2. Register `.kpeffect` files under stable effect keys.
3. Bind ECS entities to those effect keys with `ParticleEffectComponent`.
4. Optionally apply `ParticleEffectOverrideComponent` for per-instance scale,
   color, timing, or texture changes.
5. Let `ParticleSystem` resolve the binding and submit an emitter descriptor to
   the renderer. Live particle state is renderer-owned.

The format is simple and strict:

- JSON document with `"version": 3`
- one or more entries in an `"emitters"` array
- required grouped blocks for playback, render, atlas, emission, lifetime, size,
  rotation, source, motion, collision, and color
- the ECS binding path retains and submits every emitter in the asset
- unknown fields are fatal parse errors
- texture references are aliases, not raw texture paths

Primary files:

- [particle_emitter.h](../include/karma/world/components/particle_emitter.h)
- [effect_library.cpp](../src/features/visual/particles/effect_library.cpp)
- [PARTICLE_SYSTEM.md](PARTICLE_SYSTEM.md)
- [examples/assets/particles](../examples/assets/particles)

## What Is Already Generator-Friendly

The engine already has several pieces that make generation practical:

- `.kpeffect` files are structured JSON with stable grouped blocks.
- The parser is deterministic and strict.
- Existing examples cover useful archetypes: sparks, smoke, flash, heat
  distortion, shock rings, debris, scorch marks, flipbook fire/smoke, orb core,
  orb arcs, orb halo, and orb distortion.
- Texture aliases decouple emitter files from runtime texture IDs.
- `ParticleEffectOverrideComponent` supports safe variation without duplicating
  shared emitter files.
- `prefab.json` composition is separate from emitter behavior, which is the
  right model for layered effects.

The best current generation target is a v3 `.kpeffect` JSON file with one or
more emitter layers. The next best target is a prefab directory containing
generated `.kpeffect` assets, a `prefab.json` composition file with only effect
bindings/playback defaults, and an `assets.package.json` package for
texture/effect registration.

## Current Blockers

The current system is not yet robust enough for unattended image-to-effect
generation:

- There is no standalone schema file outside the parser/tests.
- There is no standalone validator or formatter for `.kpeffect`.
- Field ranges and safe defaults live in code and examples, not in a generator
  contract.
- There are no semantic presets such as `spark_burst`, `smoke_plume`,
  `fireball`, `heat_haze`, `shock_ring`, or `orb_halo`.
- Texture/atlas creation and texture alias registration are package-specific.
- Complex effects require multiple files plus registration and sometimes C++
  package setup.
- There is no automated preview/capture loop to compare generated output against
  the source image.
- Performance constraints are implicit; a generated file can easily choose a
  particle count, blend mode, or distortion setup that looks plausible but is
  too expensive.

## Recommended Generation Architecture

Use a structured intermediate description instead of generating `.kpeffect`
directly from image analysis.

Example intermediate form:

```json
{
  "style": "smoke_plume",
  "palette": ["#5a5650", "#161412"],
  "motion": "rising",
  "density": 0.55,
  "scale": 4.0,
  "duration": 2.2,
  "loop": false,
  "softness": 0.8,
  "turbulence": 0.35
}
```

The generation pipeline should be:

1. Analyze the image or prompt into high-level visual intent.
2. Choose one or more known effect archetypes.
3. Emit a constrained intermediate JSON description.
4. Map the JSON through presets and safe field ranges into `.kpeffect` v3.
5. Validate and format the generated file.
6. Optionally generate a prefab directory when the effect needs multiple layers.
7. Run a preview scene and capture visual/performance feedback.
8. Iterate the JSON, not raw emitter fields, when the result is wrong.

## Preset Layer

Before agent generation, define a small preset library. Each preset should own
safe ranges and default field mappings for one visual archetype.

Initial presets:

1. `spark_burst`: additive, short lifetime, sphere/sphere-surface emission,
   high radial velocity, gravity, optional ground collision.
2. `smoke_plume`: alpha, longer lifetime, low upward velocity, drag, expanding
   size, dark-to-transparent color.
3. `fireball`: additive or alpha, short-to-medium lifetime, warm palette,
   outward radial motion, shrinking or expanding size depending on texture.
4. `heat_haze`: distortion, low particle count, large soft particles,
   scene-color sampling cost budget.
5. `shock_ring`: ground aligned, one or two large particles, fast expansion,
   short lifetime.
6. `ground_decal`: ground aligned alpha, one particle, long fade.
7. `orb_core`: local-space additive, sphere emission, low velocity, looping.
8. `orb_halo`: local-space additive, very low count, large soft halo.
9. `electric_arcs`: local-space additive, atlas flipbook, short lifetime,
   random frames.

The preset layer should choose performance-safe defaults first. Hand-authored
files can exceed those defaults, but generated effects should start conservative.

## Tooling Needed

Minimum tooling before unattended generation:

1. `karma_particle_effect_validate`

   Parse one or more `.kpeffect` files, report exact field/value errors, warn
   about suspicious ranges, and fail in CI.

2. `karma_particle_effect_format`

   Rewrite fields in canonical order so generated and hand-authored files are
   easy to diff.

3. A schema file

   Store field names, types, defaults, enum values, aliases, and recommended
   ranges in a machine-readable file such as JSON.

4. A preview harness

   Load a generated effect by path/key, instantiate it in a controlled scene,
   run for a fixed duration, and optionally capture screenshots and
   `KARMA_PARTICLE_STATS=1` output. The stats line includes one-frame-delayed
   persistent GPU particle state counters, indirect draw/dispatch counts, sort
   key counts, sort overflow/fallback flags, and `cpu_fallback_particles` for
   producers that still submit baked batches.

5. Package helpers

   Add scoped registration handles for texture aliases and effect files so a
   generated package can register and clean up assets safely.

## Suggested File Boundaries

Keep the existing authoring split:

- `.kpeffect`: v3 JSON emitter layers, GPU simulation tuning, renderer particle
  state, atlas metadata, texture alias, color/size/lifetime/motion.
- `prefab.json`: composition of multiple layers, transforms, effect bindings,
  playback defaults, and high-level component overrides.
- `assets.package.json`: texture asset imports, effect file registration, and
  prefab-local resource cleanup.

Do not put full emitter authoring into `prefab.json`. If generation needs a
multi-layer effect, generate multiple `.kpeffect` files and a small prefab JSON
that composes them.

## Agent Workflow Target

The eventual agent workflow should look like this:

1. User supplies an image or visual reference.
2. Agent describes the desired motion, color palette, silhouette, lifetime, and
   layering.
3. Agent chooses one or more presets.
4. Agent generates intermediate JSON.
5. Tool converts JSON to `.kpeffect` and optional prefab JSON.
6. Validator and formatter run automatically.
7. Preview scene runs and captures stats/screenshots.
8. Agent adjusts the JSON/preset parameters until the effect is close.

The important constraint is that the agent should not freely invent raw fields.
It should operate through schema, presets, validation, and preview feedback.

## First Implementation Slice

The first practical implementation should be small:

1. Document every current `.kpeffect` field with type, default, enum values, and
   recommended range.
2. Add `karma_particle_effect_validate`.
3. Add a canonical field order and formatter.
4. Add three presets: `spark_burst`, `smoke_plume`, and `heat_haze`.
5. Add a generated-effect preview example.
6. Use existing texture aliases first; defer automatic texture generation until
   the data-only pipeline is reliable.

After that, image-to-effect generation can start as a controlled preset mapping
problem instead of open-ended emitter synthesis.
