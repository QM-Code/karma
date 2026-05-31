# Effect API Split Bootstrap

This file is the handoff for the effect API split pass.

It covers the runtime modularization and prefab-entry decoupling work that was
landed to move beam and analytic volume behavior out of the implicit engine
startup path.

Read this together with
[EFFECT_API_SPLIT_PLAN.md](EFFECT_API_SPLIT_PLAN.md). The plan doc describes
the target architecture. This file describes the current state of the tree.

## Scope

This pass was about the first structural split, not the full effect/plugin
architecture.

The concrete goals were:

- stop `EngineApp` from implicitly owning beam and volume-sphere behavior
- stop prefab instantiation from directly calling beam- and volume-specific
  entity creation helpers
- make the examples opt in to those features explicitly
- leave the orb/material problem for a follow-up pass

## What Landed

### 1. Runtime module seam in `EngineApp`

`EngineApp` now hosts optional runtime modules rather than hardcoding every
effect system into the app lifecycle.

Current state:

- `app::RuntimeModuleContext` and `app::RuntimeModule` exist
- `EngineApp::addRuntimeModule(...)` registers modules before or after startup
- startup attaches modules after the main engine context exists
- warmup calls `onWarmUp(...)`
- per-frame updates call `onUpdate(...)`
- shutdown calls `onDetach(...)`

Primary files:

- [`../include/karma/runtime/app/runtime_module.h`](../include/karma/runtime/app/runtime_module.h)
- [`../include/karma/runtime/app/engine_app.h`](../include/karma/runtime/app/engine_app.h)
- [`../src/runtime/app/engine_app.cpp`](../src/runtime/app/engine_app.cpp)

### 2. Beam and volume behavior moved behind opt-in modules

Beam and analytic volume behavior are no longer booted by default inside
`EngineApp`.

Current state:

- `BeamPathRuntimeModule` owns `BeamPathSystem`
- `VolumeSphereRuntimeModule` owns `VolumeSphereSystem`
- the example programs register those modules explicitly before
  `engine.start(...)`
- the public umbrella header exposes the runtime modules, not the low-level
  system headers

Primary files:

- [`../include/karma/features/visual/beams/beam_path_runtime_module.h`](../include/karma/features/visual/beams/beam_path_runtime_module.h)
- [`../src/features/visual/beams/beam_path_runtime_module.cpp`](../src/features/visual/beams/beam_path_runtime_module.cpp)
- [`../include/karma/features/visual/volumes/volume_sphere_runtime_module.h`](../include/karma/features/visual/volumes/volume_sphere_runtime_module.h)
- [`../src/features/visual/volumes/volume_sphere_runtime_module.cpp`](../src/features/visual/volumes/volume_sphere_runtime_module.cpp)
- [`../include/karma/karma.h`](../include/karma/karma.h)
- [`../examples/laser_example.cpp`](../examples/laser_example.cpp)
- [`../examples/laser_prefab_example.cpp`](../examples/laser_prefab_example.cpp)
- [`../examples/volumetric_sphere_example.cpp`](../examples/volumetric_sphere_example.cpp)
- [`../examples/volumetric_sphere_prefab_example.cpp`](../examples/volumetric_sphere_prefab_example.cpp)
- [`../examples/prefab_gallery_example.cpp`](../examples/prefab_gallery_example.cpp)

### 3. Prefab entry handler seam

Prefab instantiation no longer directly calls beam- and volume-specific helper
functions in the core runtime path.

Current state:

- a global prefab-entry handler registry exists
- `beam` and `volume_sphere` instantiation route through that registry
- the beam and volume runtime modules register and unregister their handlers on
  attach/detach
- if a prefab uses a module-backed entry type and no handler is registered,
  `instantiatePrefab(...)` fails cleanly with `std::nullopt`
- `mesh`, `particle`, and `light` still instantiate through the core switch
  path

Primary files:

- [`../include/karma/content/prefabs/prefab_entry_handler.h`](../include/karma/content/prefabs/prefab_entry_handler.h)
- [`../src/content/prefabs/prefab_entry_handler.cpp`](../src/content/prefabs/prefab_entry_handler.cpp)
- [`../src/content/prefabs/prefab_runtime.cpp`](../src/content/prefabs/prefab_runtime.cpp)
- [`../src/features/visual/beams/beam_path_runtime_module.cpp`](../src/features/visual/beams/beam_path_runtime_module.cpp)
- [`../src/features/visual/volumes/volume_sphere_runtime_module.cpp`](../src/features/visual/volumes/volume_sphere_runtime_module.cpp)

### 4. Public docs and examples now reflect the split

The public-facing docs no longer describe beam and volume behavior as implicit
engine features.

Primary files:

- [`../README.md`](../README.md)
- [`../NEXT_AGENT.md`](../NEXT_AGENT.md)
- [`../docs/ENGINE_USAGE.md`](../docs/ENGINE_USAGE.md)
- [`../docs/EFFECT_PREFABS.md`](../docs/EFFECT_PREFABS.md)
- [`../docs/BEAM_PATHS.md`](../docs/BEAM_PATHS.md)
- [`../examples/README.md`](../examples/README.md)

## Important Implementation Notes

### Runtime-module headers must stay self-contained

`BeamPathRuntimeModule` and `VolumeSphereRuntimeModule` own
`std::unique_ptr<...System>` members while only forward-declaring the concrete
system types in the public headers.

That means:

- constructors and destructors must stay out of line
- otherwise users including `karma/karma.h` can hit incomplete-type failures
  during `std::make_unique<...RuntimeModule>()`

This was already hit once during validation.

Files to watch:

- [`../include/karma/features/visual/beams/beam_path_runtime_module.h`](../include/karma/features/visual/beams/beam_path_runtime_module.h)
- [`../src/features/visual/beams/beam_path_runtime_module.cpp`](../src/features/visual/beams/beam_path_runtime_module.cpp)
- [`../include/karma/features/visual/volumes/volume_sphere_runtime_module.h`](../include/karma/features/visual/volumes/volume_sphere_runtime_module.h)
- [`../src/features/visual/volumes/volume_sphere_runtime_module.cpp`](../src/features/visual/volumes/volume_sphere_runtime_module.cpp)

### The prefab handler registry is global state

The current handler registry is a process-global static map.

That is acceptable for this pass, but it is not the cleanest final ownership
model.

Implications:

- handlers must be unregistered on module detach
- multiple engine instances in one process need careful thought
- future tests or tools that spin up and tear down engine state repeatedly may
  prefer handler ownership on `EngineApp`, `PrefabRegistry`, or another scoped
  runtime object instead of a free global map

### This is runtime decoupling, not full prefab-plugin decoupling

The parser still knows about built-in section names and entry enum values.

Examples:

- `beam`
- `volume_sphere`

So this pass decouples runtime ownership, but not yet prefab-format extension.

If the goal becomes truly arbitrary prefab section kinds, the next missing seam
is parser/entry registration.

## What Is Still Not Generic

These are the remaining architecture gaps after this pass:

- the prefab parser still hardcodes `beam` and `volume_sphere` section names
- `PrefabEntry::Type` still bakes those effect families into the core prefab
  vocabulary
- `mesh`, `particle`, and `light` still use direct core switch cases instead of
  the new handler registry
- the orb shell still depends on built-in material/shading-model branches
- `MaterialDesc` still does not provide a real public custom-shader pipeline
  path for normal material rendering

The first three are format/runtime consistency work. The orb item is the Phase
3 material/shader problem from the plan.

## Validation

Build validation used:

```bash
cmake --build build-local --target \
  karma_laser_example \
  karma_laser_prefab_example \
  karma_volumetric_sphere_example \
  karma_volumetric_sphere_prefab_example \
  karma_prefab_gallery_example \
  -j2
```

Result:

- build succeeds in the current worktree
- the examples compile cleanly through `karma/karma.h`
- interactive runtime validation is still blocked in this environment by GLFW
  initialization

Useful grep helpers:

```bash
rg -n "addRuntimeModule|RuntimeModuleContext|runtime_modules_" include/karma/app src/app
rg -n "registerPrefabEntryHandler|instantiatePrefabEntry" include/karma/prefabs src/prefabs
rg -n "BeamPathRuntimeModule|VolumeSphereRuntimeModule" examples docs
```

## Failure Modes To Check First

If a future change breaks this area, check these in order:

1. a prefab that used to instantiate now returns `std::nullopt`
2. a runtime module was not registered before `engine.start(...)`
3. a module forgot to unregister or reregister its prefab handler
4. a runtime-module public header was changed in a way that requires complete
   private system types again
5. a parser change drifted away from the runtime entry-handler expectations

## Recommended Next Steps

If continuing this track, the best next moves are:

1. add a real material/shader pipeline registration path so the orb shell stops
   depending on built-in renderer enum cases
2. decide whether `beam` and `volume_sphere` should remain core prefab syntax or
   move toward parser-registered section kinds
3. if the engine should fully standardize on one prefab instantiation path,
   consider moving `mesh`, `particle`, and `light` behind registered handlers
   too
4. if testability or multi-runtime behavior matters, move prefab-entry handler
   ownership off the current global static registry
5. only after those seams exist, revisit whether beam/volume should stay under
   `karma.h` or live in narrower optional headers only
