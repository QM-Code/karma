# Particle System Analysis

This document reviews the current particle system with a focus on architecture,
prefab authoring, known issues, and optimization opportunities.

Read this with:

- `docs/PARTICLE_SYSTEM.md`
- `docs/PARTICLE_PERF_BOOTSTRAP.md`
- `docs/EXPLOSION_PREFAB.md`
- `docs/EXPLOSION_STRESS_PERF.md`
- `src/features/visual/particles/particle_system.cpp`
- `src/rendering/renderer/backends/diligent/passes/particle_draw.cpp`

## High-Level Structure

The particle system is split across four layers:

- Public API and ECS helpers:
  `include/karma/features/visual/particles/effect_api.h`,
  `include/karma/features/visual/particles/effect_library.h`, and
  `include/karma/features/visual/particles/particle_system.h`.
- ECS data:
  `include/karma/world/components/particle_effect.h`,
  `include/karma/world/components/particle_effect_override.h`, and
  `include/karma/world/components/particle_emitter.h`.
- Runtime simulation:
  `src/features/visual/particles/effect_library.cpp` and
  `src/features/visual/particles/particle_system.cpp`.
- Renderer backend:
  `src/rendering/renderer/backends/diligent/passes/particles.cpp` and
  `src/rendering/renderer/backends/diligent/passes/particle_draw.cpp`.

Authored content lives under:

- `examples/assets/particles/*.kpeffect`
- `examples/assets/prefabs/*/prefab.kprefab`
- example package code such as `examples/explosion_prefab_package.cpp`

The system flow is:

1. Game or package code registers texture aliases and `.kpeffect` files in
   `ParticleLibrary`.
2. ECS entities receive `ParticleEffectComponent` through
   `particles::createEffectEntity(...)` or prefab instantiation.
3. `ParticleSystem::syncEffectBindings(...)` resolves named effects, applies
   per-entity overrides, and writes `ParticleEmitterComponent`.
4. `ParticleSystem::update(...)` simulates CPU particles and packs
   `PackedParticleBatch` instances.
5. `GraphicsDevice::submitPackedParticles(...)` forwards batches to the
   Diligent backend.
6. `renderParticlePasses(...)` groups additive batches, sorts alpha/distortion
   particles, uploads instance data, and draws instanced quads.

## What Is Done Well

The asset/runtime split is a good foundation. `.kpeffect` files are reusable
templates, while `ParticleEffectComponent` and `ParticleEffectOverrideComponent`
let individual entities vary timing, scale, alpha, color, and texture without
duplicating authored files.

Hot reload is correctly centralized in `ParticleLibrary`. The library owns file
polling, texture alias resolution, and a monotonic version counter. Runtime
bindings compare library version, effect key, restart count, and override hash,
so file edits and gameplay changes propagate without bespoke invalidation code.

The public helper API is appropriately small. `createEffectEntity`,
`bindEffect`, `restartEffect`, and playback helpers cover the common gameplay
paths while still allowing direct `ParticleEmitterComponent` use for low-level
cases.

The runtime state is separated from ECS data. Per-entity simulation state lives
inside `ParticleSystem::emitters_`, not inside the component. That keeps ECS
components compact and makes restart/reapply behavior explicit.

The CPU/GPU presentation split is pragmatic. `ParticleSystem` uses the
`Simulated` path, where the CPU submits position, age, start/end size, start/end
color, and atlas metadata while the GPU derives size, alpha, color, and frame
UVs. Beam-authored particles remain on the `Baked` path, avoiding a risky
visual regression.

The renderer already contains useful instrumentation. `ParticlePassStats`
tracks binding sync, simulation, packing, additive grouping, alpha/distortion
sorting, draw submission, submitted particles, and invalid depth counts. This is
the right basis for measurement-driven optimization.

The prefab integration is useful. Particle entries in `.kprefab` files support
effect keys, playback state, transform, and overrides. This lets large effects
such as explosions be authored as layered ECS bundles instead of hardcoded
entity construction.

## Architecture Assessment

Overall verdict: the particle system architecture is sound for the current
engine scale. It has a clear separation between reusable assets, ECS binding,
runtime simulation state, prefab composition, and renderer submission. The
largest weaknesses are not conceptual; they are boundary pressure from renderer
state leaking into emitters, manual package lifetime management, and the lack
of benchmark-driven performance gates.

### Ownership Boundaries

The current ownership model is mostly clean:

- `ParticleLibrary` owns named effect templates, file-backed hot reload, and
  texture alias lookup.
- `ParticleEffectComponent` owns the ECS-level binding to a named effect key.
- `ParticleEffectOverrideComponent` owns small per-instance deviations from the
  shared effect.
- `ParticleEmitterComponent` owns resolved runtime emitter configuration.
- `ParticleSystem` owns live particle state and transient per-emitter simulation
  buffers.
- The Diligent backend owns GPU resources, PSOs, particle draw ordering, and
  render-target decisions.

That split is good because authored files and gameplay code do not need to know
about `EmitterState`, renderer buffers, or sorting internals.

The main boundary violation is that `ParticleEmitterComponent` now carries both
simulation behavior and renderer material state. Fields such as `spawn_rate`,
`particle_lifetime_min`, `velocity_min`, and `drag` are simulation concerns.
Fields such as `blend_mode`, `shading_mode`, `soft_particle_distance`,
`distortion_strength`, atlas layout, and texture are renderer/material
concerns. Keeping them in one component is practical today, but it makes batch
keys large and causes every effect asset to duplicate renderer state.

Recommended direction: keep the current unified emitter for now, but introduce
a `ParticleMaterialDesc` or particle render-state ID before adding many more
surface/shader fields.

### Data Flow

The data flow is easy to follow:

1. Register or hot-reload an effect template.
2. Bind an entity to an effect key.
3. Resolve the template to an emitter.
4. Apply optional overrides.
5. Reset live simulation state when binding inputs change.
6. Simulate particles.
7. Pack `ParticlePackedInstance` data.
8. Submit renderer batches.
9. Sort/group/draw in the backend.

This is the right shape for a CPU-simulated particle system. The restart path is
also explicit: `restartEffect(...)` increments `restart_count`, causing
`syncEffectBindings(...)` to reapply the template and erase runtime state.

The main risk is that the effect binding step is coarse. Any library version
change can force every bound effect to compare against the new version. That is
fine at the current scale, but if the number of effect-bound entities grows
substantially, per-effect versioning would be better than a single library-wide
version counter.

### Runtime State

Keeping live particles outside ECS is the right call. ECS components should not
contain large per-particle vectors. `ParticleSystem::emitters_` gives the system
control over reserve/reuse, cleanup, restart, and future jobification.

The current state layout is vector-of-structs:

- position
- velocity
- age/lifetime
- start/end size
- start/end color
- rotation/angular velocity
- atlas frame offset
- simple ground-rest state

That is straightforward and easy to debug. It is not optimal for cache-heavy
simulation. Before considering GPU simulation, the lower-risk architectural step
is to evaluate structure-of-arrays storage behind the same public API.

### Renderer Interface

The renderer interface is pragmatic. `PackedParticleBatch` is a compact
boundary type, and `ParticlePresentationMode::Simulated` moved color, alpha,
size, and atlas interpolation to the GPU. That was a good optimization because
it reduced CPU packing work without forcing a full GPU simulation rewrite.

The render backend still owns too much particle policy:

- additive grouping policy
- alpha and distortion sorting policy
- half-resolution alpha policy
- shader variant policy
- scene color/depth sampling policy

That is acceptable inside the Diligent backend for now, but it should not leak
back upward into gameplay or prefab code. If more knobs are needed, prefer
registered particle render-state IDs over more emitter fields.

### Extension Points

The strongest extension point is `ParticleEffectOverrideComponent`. It gives
gameplay and prefabs a safe way to vary common behavior without mutating shared
assets.

The weakest extension point is the file format. `.kpeffect` and `.kprefab`
parsers are strict and explicit, which is good for validation, but every new
field requires code edits in multiple places. Before the format grows much
more, add validation tests and consider versioned schemas.

## Architecture Concerns

### 1. Simulation Is Still Entirely Single-Threaded CPU Work

Every emitter is simulated serially in `ParticleSystem::update(...)`. Heavy
cases such as explosion stress tests combine high emitter count, high particle
count, and per-particle physics-style work. This is simple and deterministic,
but it makes the main thread the ceiling.

Likely symptoms:

- frame-time spikes during staged explosions
- poor scaling when multiple prefabs trigger together
- simulation time growing linearly with live particle count

Optimization direction:

- first measure per-effect particle counts and simulation time
- split simulation into jobs only after data ownership is cleaned up
- consider SoA storage before GPU simulation

### 2. Alpha And Distortion Sorting Are The Current Renderer Hot Path

Additive particles are grouped by render state, but alpha and distortion
particles are collected into a `SortedParticle` array and globally sorted per
depth-test mode. That preserves blending correctness, but it is O(n log n),
allocates/reuses large temporary vectors, and fragments spans when sorted
particles alternate draw-state keys.

Optimization direction:

- measure `alpha_collect_ms`, `alpha_sort_only_ms`, `alpha_span_ms`, and the
  distortion equivalents in stress scenes
- try bucketed depth sort or per-emitter approximate sort for large soft smoke
  layers
- keep exact sort as a quality mode for small/high-value translucent effects

### 3. Renderer State Keys Are Large And Float-Sensitive

`ParticleBatchGroupKey` includes many fields, including float bit patterns for
soft mask, distortion, Fresnel, atlas, and curve settings. This is precise, but
minor authored differences can prevent batching. Prefabs that duplicate almost
identical effects with tiny parameter differences will produce extra spans and
draw calls.

Optimization direction:

- normalize common particle material states into registered particle materials
- keep emitter simulation fields separate from renderer material fields
- consider quantized or ID-based batch keys for authored effects

### 4. The Particle Shader Is Monolithic

`particles.cpp` contains inline HLSL for standard particles, flipbooks, soft
particles, distortion, and shell shading. This keeps setup local, but it makes
shader changes hard to review and prevents variant-specific stripping.

Optimization direction:

- move particle shaders to asset files or generated shader modules
- split shader variants for standard, distortion, shell, and half-res composite
- avoid sampling scene color/depth for variants that do not need them

### 5. `.kpeffect` Parsing Is Simple But Rigid

The parser is line-oriented and maps every field through an explicit
`if (key == ...)` chain. That is easy to debug, but adding fields requires
parser edits, component edits, renderer-batch edits, shader constants, and docs.
Unknown fields are fatal, which is good for correctness but harsh for forward
compatibility.

Optimization direction:

- keep strict parsing by default
- add schema/version comments or a `version = 1` field before the format grows
  further
- add parser tests for representative effect files

### 6. Asset Registration Is Package-Specific And Repetitive

The explosion package builds textures, registers aliases, registers effect
files, and manually unregisters each effect and alias. That works, but it is
error-prone as packages grow.

Optimization direction:

- introduce scoped particle package registration handles
- let `ParticleLibrary` return a token that cleans up aliases/effects together
- keep package-level generated texture ownership explicit

### 7. Prefab Particle Overrides Are Useful But Narrow

Prefab particles can bind start/end color overrides and scalar override fields.
They cannot directly bind every emitter field through prefab parameters. This
is a healthy limitation for now, but it means some authoring variations still
require duplicate `.kpeffect` files.

Optimization direction:

- add only high-value prefab bindings, not arbitrary emitter mutation
- likely candidates: `spawn_rate_scale`, `size_scale`, `radius_scale`,
  `alpha_scale`, `texture`, and maybe `time_scale`
- avoid exposing low-level renderer fields through prefab params until the
  particle material abstraction is clearer

### 8. Culling Is Per-Emitter Bounds Only

The runtime computes live bounds while simulating and culls entire emitters
against the primary camera. This is a good cheap first pass, but large smoke or
debris emitters can remain visible even when most particles are off-screen.

Optimization direction:

- keep current emitter culling
- add optional coarse particle tiles or per-emitter split batches only if
  profiling shows packing/upload dominates
- avoid per-particle frustum checks in the hot path unless there is a measured
  win

### 9. Local-Space And World-Space Behavior Need Authoring Discipline

Local-space effects transform particle positions at presentation time. World-
space effects bake spawn positions and velocities using the emitter transform at
spawn time. Both are valid, but large prefabs can become visually inconsistent
if authors mix the two without intent.

Optimization direction:

- document preferred local/world-space usage per effect family
- use local-space for attached or looping object effects
- use world-space for explosion debris, smoke, and effects that should detach
  from a moving root

### 10. Ground Collision Is Built Into Generic Particles

Ground collision is convenient for debris, but the implementation is a simple
height-plane interaction inside the generic simulation loop. It is not tied to
real scene collision and adds per-particle branch cost.

Optimization direction:

- keep it for cheap debris
- do not expand it into a full collision system inside particles
- if richer collision is needed, create a separate specialized debris system

## Particle Prefab Review

Particle prefabs are currently a good fit for layered effects. The core prefab
runtime creates a root entity and one child per entry, then `PrefabSystem`
syncs child transforms. Particle entries call `particles::createEffectEntity`,
so they use the same binding path as game-authored entities.

The prefab architecture is intentionally composition-oriented:

- `[prefab]` names the bundle.
- `[param name]` declares typed authoring parameters.
- `[mesh name]`, `[particle name]`, and `[light name]` create core child
  entities.
- Optional sections such as `[beam name]` and `[volume_sphere name]` route
  through runtime-module entry handlers.
- `PrefabInstanceComponent` tracks the root and members.
- `PrefabMemberComponent` stores each member's local transform and playback
  metadata.

This is a good model for particle effects because most substantial effects are
not one emitter. They are bundles: mesh shell, core particles, smoke particles,
distortion particles, sparks/debris, rings, lights, and sometimes beams or
volumes.

What works well:

- prefab entries separate effect composition from C++ control code
- root/member naming makes layered effects easier to inspect
- `setPrefabPlayback(...)` and `restartPrefab(...)` provide coarse control over
  the whole bundle
- package callbacks cover generated textures and effect registration that a
  plain `.kprefab` cannot express

The explosion prefab is the clearest reference. The manifest layers multiple
one-shot particle children plus a point light. The package code builds or loads
atlases, registers texture aliases, registers effect files, and exposes typed
controller helpers for trigger/update/destroy.

The energy orb prefab is the clearest looping/composite reference. It uses a
mesh shell for the orb body, several local-space particle layers for core/arcs/
halo/distortion, and a point light. Its `accent` parameter drives material and
particle colors. That is the right use of prefab params: high-level art
direction changes, not low-level emitter reauthoring.

### Prefab Authoring Boundary

The clean boundary should be:

- `.kpeffect`: emitter behavior, simulation tuning, atlas metadata, blend mode,
  texture alias, spawn shape, velocity, lifetime, size, and colors.
- `.kprefab`: composition, child transforms, default playback state, high-level
  typed params, and per-instance override scales/colors.
- package C++: generated textures, external asset loading, effect registration,
  texture alias registration, staged controllers, and cleanup.

When that boundary is followed, assets remain reusable. For example, the same
`energy_orb_core` effect can be scaled and recolored in a prefab without
copying the emitter file.

When that boundary is blurred, maintenance gets harder. If `.kprefab` starts
growing raw emitter fields, it becomes a second particle format. If C++ starts
hardcoding per-layer emitter tuning, hot reload and authoring iteration get
worse.

### Playback Semantics

Particle prefab playback is currently coarse but understandable:

- one-shot staged effects usually instantiate disabled and not playing
- controllers restart specific members in timed sequences
- `setPrefabPlayback(...)` maps the root enabled state onto particle enabled
  and playing state
- `restartPrefab(...)` restarts every particle member

That is enough for the current examples. The gap is that "visible",
"enabled", and "playing" are not always the same concept. A future paused effect
may need visible particles that stop spawning, or an emitter that keeps existing
particles alive while no longer accepting new spawns. Do not overload
`setPrefabPlayback(...)` further without defining those states explicitly.

### Parameter Model

The prefab parameter model is useful because it is typed and limited. Color and
float bindings support common art-direction changes without letting arbitrary
strings rewrite the effect.

For particle entries, the current useful bindings are start/end color and the
scalar override fields. This is intentionally narrower than mesh material
bindings. That is a good constraint. Particle authoring has more ways to break
performance, so prefab params should expose only stable high-level controls.

Good particle prefab params:

- accent color
- alpha scale
- size scale
- radius scale
- spawn-rate scale for density variants
- time scale for slow/fast variants

Poor particle prefab params:

- raw max particle count
- raw atlas metadata
- raw blend mode
- raw shader/surface mode
- arbitrary velocity vectors unless there is a strong authored use case

### Package Pattern

The current package pattern is necessary for effects that cannot be represented
as data-only prefabs. The explosion package is a valid example because it needs
generated atlases, EXR fallback logic, texture alias registration, effect file
registration, controller helpers, and cleanup.

What is missing is scoped ownership. Package code manually registers and
unregisters each effect and texture alias. That is easy to get wrong when a
package grows. The next architectural improvement for packages should be a
registration handle or package scope in `ParticleLibrary` and `PrefabRegistry`.

Issues in the current prefab path:

- package registration and cleanup are manual and easy to drift
- particle prefab fields are split between `.kprefab` and `.kpeffect`, so the
  author has to know which file owns which concern
- root playback maps particles to both enabled and playing, which is simple but
  may not fit paused-but-visible effects later
- restart is all-or-nothing at the prefab level unless controllers keep child
  entity IDs and stagger restarts manually
- prefab particle overrides are scalar/color focused and do not expose texture
  alias binding directly in the parser today

Recommended prefab direction:

- keep `.kpeffect` as the emitter definition source of truth
- keep `.kprefab` as composition, transform, parameter, and playback state
- add scoped package cleanup handles before adding more package types
- document staged-controller patterns for effects that need timed child
  restarts
- add focused particle prefab validation so missing effect keys, missing texture
  aliases, and unintended high particle counts are caught before runtime

## Optimization Backlog

### P0: Measure Before Rewriting

- `KARMA_PARTICLE_STATS=1` now logs stable once-per-second renderer particle
  diagnostics after final render submission.
- Use the explosion stress and prefab gallery examples as the first benchmark
  harnesses.
- Track live emitters, live particles, submitted particles, draw calls, sort
  timings, simulation timings, and packing timings.
- Capture separate runs for additive-only, alpha-heavy smoke, and distortion.

Initial diagnostic finding:

- Default explosion stress and prefab gallery remain near `60 fps` in the local
  validation runs.
- Heavy stress (`--explosions 25 --period 2.0`) reaches about `14.8k` packed
  particles and `163` submitted batches per frame.
- The first measured bottleneck was CPU-side additive grouping, followed by
  particle simulation and packing.
- After removing the extra additive grouping copy, heavy-scene
  `additive_grouping_ms` dropped from about `1.9-2.0 ms` to about
  `0.23-0.27 ms`.
- Simulation and packing are now the main remaining particle-side costs in the
  heavy stress run.
- Exact alpha/distortion sorting is measurable but not the first issue in the
  current explosion workloads.

### P1: Sorting And Draw Submission

- Reduce additive grouping and batch/span fragmentation before replacing exact
  sorting.
- Compare exact sort against bucketed depth sort for alpha smoke after
  grouping/submission costs are under control.
- Compare exact sort against per-emitter sorted spans for distortion.
- Add thresholds where tiny sorted sets keep exact sort and large sets use
  approximation.
- Reduce span fragmentation by grouping compatible sorted ranges where quality
  impact is acceptable.

### P1: Particle Material State

- Introduce a `ParticleMaterialDesc` or registered material ID for renderer
  state currently duplicated in every emitter.
- Move texture, blend mode, soft-mask, distortion, shell, atlas layout, and
  shader variant concerns toward material-like state.
- Let emitters focus on spawn/simulation behavior.

### P1: Shader Variants

- Split standard/additive, alpha-soft, distortion, shell, and half-res
  composite shader paths.
- Remove scene color/depth bindings from variants that do not need them.
- Move inline HLSL out of C++ once the variant boundaries are stable.

### P2: CPU Simulation Layout

- Evaluate SoA storage for particle state to improve cache behavior.
- Reserve and reuse buffers per emitter aggressively; avoid avoidable per-frame
  growth and copying.
- Consider separating high-count one-shot emitters from low-count looping
  ambient emitters.

### P2: Threaded Simulation

- Only after SoA/state boundaries are clearer, split emitters into parallel
  simulation jobs.
- Keep ECS writes on the main thread; worker jobs should operate on
  `EmitterState` and immutable emitter snapshots.
- Preserve deterministic seeds per emitter where possible.

### P2: Asset And Package Hygiene

- Add a package registration handle for effect aliases and effect files.
- Add validation tooling for `.kpeffect` files and `.kprefab` particle entries.
- Add docs explaining local-space vs world-space expectations for common effect
  categories.

### P3: GPU Simulation

- Treat full GPU simulation as a later architecture change, not the next
  optimization.
- It may be justified for very high particle counts, but it will complicate
  restart, hot reload, sorting, readback-free stats, and deterministic authoring.

## Recommended Next Steps

1. Reduce particle packing cost where high-count one-shot emitters repeatedly
   rebuild similar batches.
2. Isolate simulation cost from ground-collision-heavy emitters.
3. Consider a persistent packed-batch storage path so particle submission can
   avoid repeated vector allocation/move churn.
4. Prototype bucketed alpha sorting behind a runtime flag only after grouping
   and packing are improved.
5. Introduce a particle material/state ID only after profiling confirms batching
   and state fragmentation matter.
6. Keep GPU simulation as a deliberate project, not an incremental cleanup.

## Files To Watch

- `src/features/visual/particles/particle_system.cpp`
- `src/features/visual/particles/effect_library.cpp`
- `include/karma/world/components/particle_emitter.h`
- `include/karma/world/components/particle_effect_override.h`
- `src/rendering/renderer/backends/diligent/passes/particle_draw.cpp`
- `src/rendering/renderer/backends/diligent/passes/particles.cpp`
- `src/content/prefabs/prefab_parse_particle.cpp`
- `src/content/prefabs/prefab_runtime.cpp`
- `examples/explosion_prefab_package.cpp`
- `examples/assets/particles/*.kpeffect`
- `examples/assets/prefabs/*/prefab.kprefab`
