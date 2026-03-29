# Effect API Split Plan

## Goal

Make effect-heavy sample content such as the beam, orb, and explosion examples
feel like examples built on top of engine APIs rather than special cases wired
into the engine runtime.

The target split is:

- core engine owns reusable primitives and extension seams
- optional modules own higher-level effect behaviors
- examples own authored looks, generated textures, effect registrations, and
  prefab packages

## Current State

### Clean Enough Today

- Explosion is mostly example-layer code.
  - asset prep and effect registration live in `examples/explosion_prefab_package.cpp`
  - controller logic also lives in `examples/`
  - core engine is mainly providing prefabs, particles, lights, and rendering

- Orb prefab packaging is mostly example-layer code.
  - generated atlases and particle registrations live in
    `examples/energy_orb_prefab_package.cpp`
  - prefab authoring is data-driven

### Not Clean Today

- `EngineApp` boots `BeamPathSystem` and `VolumeSphereSystem` implicitly.
- Prefab instantiation hardcodes `beam` and `volume_sphere` entry creation.
- Beam visuals are authored inside engine code instead of an opt-in module or
  example package.
- Orb shell rendering depends on built-in shading-model branches in the
  renderer.
- Material APIs advertise shader-path fields, but the normal material pipeline
  does not actually honor arbitrary per-material shader sources yet.

## Desired Architecture

### Core Engine

Core engine should provide:

- ECS, scene, transform, physics, audio
- mesh and light rendering
- generic material creation and parameter binding
- particle library and particle simulation/render submission
- prefab loading, prefab instantiation, prefab package prepare/cleanup
- extension points for runtime effect modules
- extension points for prefab entry handlers
- extension points for custom material or shader pipelines

### Optional Engine Modules

Optional modules can provide higher-level effect behaviors built from core APIs.
These are reusable, but not implicitly enabled by the engine.

Candidates:

- beam path rendering module
- analytic volume sphere module
- future shield, trail, ribbon, or decal modules

### Examples

Examples should own:

- effect package registration
- generated or imported textures
- effect authoring files
- prefab manifests
- gameplay controllers that trigger or sequence effects
- sample-specific shader/material authoring once the material API supports it

## Execution Phases

### Phase 1: Runtime Modularization

Goal:

- stop `EngineApp` from implicitly owning beam and volume-sphere behavior

Changes:

- add a generic runtime effect module interface
- let `EngineApp` host a list of effect modules
- move beam and volume-sphere boot/update/warmup responsibilities behind that
  module interface
- register those modules explicitly where examples need them

Success criteria:

- engine startup no longer contains beam-specific or volume-sphere-specific
  member fields and update calls
- examples still work after explicitly registering the required modules

### Phase 2: Prefab Handler Modularization

Goal:

- stop prefab instantiation from hardcoding beam and volume-sphere entity
  creation logic

Changes:

- add a prefab entry handler registry
- route prefab entry instantiation through registered handlers
- keep `mesh`, `particle`, and `light` as core handlers
- move `beam` and `volume_sphere` handlers out of the core prefab runtime path
  and into optional registration

Success criteria:

- core prefab runtime does not directly call beam-specific or volume-specific
  entity creation helpers

### Phase 3: Material/Shader Split

Goal:

- stop sample looks like the orb shell from depending on built-in special-case
  shading-model branches

Changes:

- introduce a real material/shader registration API for non-core pipelines
- preserve built-in standard PBR as the default path
- keep existing built-in special models temporarily for compatibility
- migrate example-only looks toward registered material pipelines

Success criteria:

- orb-like custom materials can be defined without adding new renderer enum
  cases

### Phase 4: Cleanup and API Narrowing

Goal:

- make the public engine surface reflect the split cleanly

Changes:

- update docs to distinguish core engine primitives from optional effect modules
- trim umbrella includes that imply all effect modules are core
- document example registration flow clearly

## Immediate Implementation Scope

This pass will implement:

- Phase 1 fully
- the first structural part of Phase 2, where prefab entry creation is routed
  through registered handlers instead of direct hardcoded calls in the runtime

This pass will not fully remove:

- built-in section names like `beam` and `volume_sphere`
- built-in special-case shading models used by the orb shell

Those remain follow-up work after the new extension seams exist.
