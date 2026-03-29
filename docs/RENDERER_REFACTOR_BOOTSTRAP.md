# Renderer Refactor Bootstrap

This file is the handoff for the current Diligent backend monolith split.

## Goal

Continue splitting renderer code by responsibility without changing behavior.

The current direction is:

1. isolate whole responsibilities into `passes/` and `resources/`
2. keep `backend_render.cpp` as orchestration, not a giant mixed-ownership file
3. keep `backend_init.cpp` focused on bootstrap until it is ready for its own split

## What Was Already Split

The old large Diligent files were cut in this order:

### 1. Resource ownership

Old `backend_mesh.cpp` was replaced by:

- [`src/renderer/backends/diligent/resources/materials.cpp`](../src/renderer/backends/diligent/resources/materials.cpp)
- [`src/renderer/backends/diligent/resources/meshes.cpp`](../src/renderer/backends/diligent/resources/meshes.cpp)
- [`src/renderer/backends/diligent/resources/render_targets.cpp`](../src/renderer/backends/diligent/resources/render_targets.cpp)
- [`src/renderer/backends/diligent/resources/textures.cpp`](../src/renderer/backends/diligent/resources/textures.cpp)

### 2. Low-risk frame/state helpers

Moved out of `backend_render.cpp` into:

- [`src/renderer/backends/diligent/passes/frame.cpp`](../src/renderer/backends/diligent/passes/frame.cpp)
- [`src/renderer/backends/diligent/passes/camera_override.cpp`](../src/renderer/backends/diligent/passes/camera_override.cpp)
- [`src/renderer/backends/diligent/passes/render_state.cpp`](../src/renderer/backends/diligent/passes/render_state.cpp)

### 3. Setup-heavy render helpers

Moved into:

- [`src/renderer/backends/diligent/passes/environment.cpp`](../src/renderer/backends/diligent/passes/environment.cpp)
- [`src/renderer/backends/diligent/passes/line.cpp`](../src/renderer/backends/diligent/passes/line.cpp)
- [`src/renderer/backends/diligent/passes/particles.cpp`](../src/renderer/backends/diligent/passes/particles.cpp)

### 4. Heavy draw paths

Moved into:

- [`src/renderer/backends/diligent/passes/shadows.cpp`](../src/renderer/backends/diligent/passes/shadows.cpp)
- [`src/renderer/backends/diligent/passes/forward.cpp`](../src/renderer/backends/diligent/passes/forward.cpp)
- [`src/renderer/backends/diligent/passes/particle_draw.cpp`](../src/renderer/backends/diligent/passes/particle_draw.cpp)

## Current Layout

Scene extraction still starts in:

- [`src/renderer/render_system.cpp`](../src/renderer/render_system.cpp)

Backend state and private split points live in:

- [`include/karma/renderer/backends/diligent/backend.hpp`](../include/karma/renderer/backends/diligent/backend.hpp)

Current Diligent layout:

- [`src/renderer/backends/diligent/backend_init.cpp`](../src/renderer/backends/diligent/backend_init.cpp): device/bootstrap, PSO creation, inline shader code, shadow resource allocation, and static resource binding
- [`src/renderer/backends/diligent/backend_render.cpp`](../src/renderer/backends/diligent/backend_render.cpp): frame orchestration, Forward+ preparation, scene-copy flow, remaining line draw path, and present glue
- [`src/renderer/backends/diligent/passes/`](../src/renderer/backends/diligent/passes): isolated pass implementations
- [`src/renderer/backends/diligent/resources/`](../src/renderer/backends/diligent/resources): non-frame resource ownership and binding helpers

## Current Hotspots

As of this handoff, the biggest remaining renderer-side files are:

- `backend_init.cpp`: about 2236 lines
- `backend_render.cpp`: about 1151 lines
- `passes/forward.cpp`: about 1142 lines
- `passes/shadows.cpp`: about 1110 lines
- `passes/environment.cpp`: about 994 lines
- `passes/particle_draw.cpp`: about 850 lines

That means the remaining decomposition priority is still:

1. `backend_init.cpp`
2. `backend_render.cpp`
3. only then the already-isolated large pass files

## What Still Lives In backend_render.cpp

`backend_render.cpp` is much smaller now, but it still owns a few mixed responsibilities:

- active render-target selection and clear/present flow
- camera basis setup and base constant setup
- Forward+ light gathering, compute/fallback setup, and SRB rebinding
- the call into [`passes/shadows.cpp`](../src/renderer/backends/diligent/passes/shadows.cpp)
- pre-particle and post-particle scene-copy orchestration
- an inline debug line draw lambda that should move into [`passes/line.cpp`](../src/renderer/backends/diligent/passes/line.cpp)

That file is now a glue/orchestration file, but it still has enough logic to merit one more round of splitting.

## Best Next Sequence

If continuing this refactor, the lowest-risk order is:

1. move the inline line draw path from `backend_render.cpp` into `passes/line.cpp`
2. split Forward+ setup, fallback buffer management, and SRB rebinding out of `backend_render.cpp`
3. split `backend_init.cpp` into bootstrap-focused pieces:
   - device/swapchain bootstrap
   - shadow resource creation
   - pipeline creation
   - inline shader source removal
4. only after that, decide whether `passes/forward.cpp` or `passes/shadows.cpp` need another internal split

## Things To Avoid

Do not do these first:

- a broad namespace reshuffle
- a `pImpl` rewrite
- recombining pass files just to "simplify" includes
- large behavioral refactors while file ownership is still moving

The safest pattern so far has been whole-method extraction plus private helper declarations in `backend.hpp`, followed by a build.

## Validation

The split was verified with:

```bash
cmake -S . -B build-local
cmake --build build-local --target karma_light_stress_example -j2
```

That build succeeded after the current pass split. The example was not run in a windowed session during this handoff.

## Related Docs

Renderer behavior is also described in:

- [LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [LOCAL_LIGHT_PROBE_BOOTSTRAP.md](LOCAL_LIGHT_PROBE_BOOTSTRAP.md)
- [PARTICLE_PERF_BOOTSTRAP.md](PARTICLE_PERF_BOOTSTRAP.md)
