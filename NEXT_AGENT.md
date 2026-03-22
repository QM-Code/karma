# Next Agent Bootstrap

This repo is in an intentionally fast-moving state. Prefer clean architecture over backward compatibility unless the user explicitly asks otherwise.

## Start Here

There are three active technical tracks in the current worktree:

1. particle/render performance
2. collision/contact/ground-state ECS work
3. local-light / point-shadow renderer work

Read these first:

- [docs/PARTICLE_PERF_BOOTSTRAP.md](/home/irie/Documents/karma/docs/PARTICLE_PERF_BOOTSTRAP.md)
- [docs/PREFAB_GALLERY_BOOTSTRAP.md](/home/irie/Documents/karma/docs/PREFAB_GALLERY_BOOTSTRAP.md)
- [docs/COLLISION_BOOTSTRAP.md](/home/irie/Documents/karma/docs/COLLISION_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](/home/irie/Documents/karma/docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md](/home/irie/Documents/karma/docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Worktree Warning

The worktree is intentionally dirty. Do not assume unrelated modified files are safe to revert.

High-signal areas right now:

- [src/particles](/home/irie/Documents/karma/src/particles)
- [src/renderer/backends/diligent/backend_render.cpp](/home/irie/Documents/karma/src/renderer/backends/diligent/backend_render.cpp)
- [include/karma/renderer/types.h](/home/irie/Documents/karma/include/karma/renderer/types.h)
- [src/beams/beam_path_system.cpp](/home/irie/Documents/karma/src/beams/beam_path_system.cpp)
- [src/volumes/volume_sphere_system.cpp](/home/irie/Documents/karma/src/volumes/volume_sphere_system.cpp)
- [src/physics](/home/irie/Documents/karma/src/physics)
- [src/collision](/home/irie/Documents/karma/src/collision)
- [examples/collision_events_example.cpp](/home/irie/Documents/karma/examples/collision_events_example.cpp)

Local-only artifact intentionally left out of source work:

- `build-asan/`

## Build Commands

Use these first:

```bash
cmake --build build --target karma_energy_orb_example karma_prefab_gallery_example -j2
cmake --build build --target karma_collision_events_example -j2
```

Useful smoke runs in this environment:

```bash
timeout 5s ./build/karma_energy_orb_example
timeout 5s ./build/karma_collision_events_example
```

Expected headless stop here:

```text
GLFW failed to initialize
```

That is normal in this environment. The useful signal is whether startup / asset reload / system init succeeds before that line.

## Particle/Render Summary

Recent particle-side work already in the tree:

- effect hot-reload polling throttled
- particle batches moved instead of copied
- additive particle batches grouped by render state
- cheaper compaction and curve evaluation in `ParticleSystem`
- GPU-side presentation for `ParticleSystem` batches:
  - size curve
  - alpha curve
  - color interpolation
  - atlas frame selection / UV generation
- beam-authored particles intentionally remain on the baked presentation path
- prefab gallery explosion / wave composition now has its own handoff doc
- wave volume proxies now render as projected screen-bounds quads instead of near-full-screen overlays
- gallery perf logging exists behind `KARMA_PREFAB_GALLERY_STATS=1`

If continuing there, start with:

- [docs/PARTICLE_PERF_BOOTSTRAP.md](/home/irie/Documents/karma/docs/PARTICLE_PERF_BOOTSTRAP.md)
- [docs/PREFAB_GALLERY_BOOTSTRAP.md](/home/irie/Documents/karma/docs/PREFAB_GALLERY_BOOTSTRAP.md)

## Collision/Physics Summary

Recent collision-side work already in the tree:

- overlap enter/stay/exit ECS events
- solid contact enter/stay/exit ECS events with point/normal
- grounded state with enter/exit
- support entity / point / normal for player controllers
- support probe for box rigid bodies
- sample scene with driving, jumping, trigger zones, and on-screen status

If continuing there, start with:

- [docs/COLLISION_BOOTSTRAP.md](/home/irie/Documents/karma/docs/COLLISION_BOOTSTRAP.md)

## Local-Light / Shadow Summary

Recent renderer-side work already in the tree:

- Forward+ light-path fixes past the `8 -> 9` light transition
- point-shadow runtime light budgeting
- explicit `R32_FLOAT` SRVs for directional and point shadow-map arrays
- per-SRB shadow resource binding for the actual material/default draw path
- moving-point-light shadow refresh no longer chunks through the small cache threshold/budget
- reduced point-shadow seam artifacts from hard cubemap-face boundaries
- local-light probe sample now staged for gradual `1-16` shadowed-light validation with moving light markers

If continuing there, start with:

- [docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](/home/irie/Documents/karma/docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md](/home/irie/Documents/karma/docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Good Next Steps

If continuing particle work:

1. measure alpha/distortion sort cost before another architecture change
2. explore bucketed or approximate depth ordering before GPU simulation
3. only move simulation fully GPU-side if the user wants a larger renderer rewrite

If continuing collision/contact work:

1. extend grounded/support probing to sphere/capsule rigid bodies
2. validate contact event quality across more collider combinations
3. tighten docs once the API surface stabilizes

If continuing local-light/shadow work:

1. tune point-shadow quality and face update strategy
2. validate the shadow path across more scenes and hardware
3. only replace the current depth-SRV path with a custom linear-depth format if another backend shows the old failure mode

## Engineering Notes

- prefer `apply_patch` for edits
- prefer `rg` / `rg --files` for search
- do not revert unrelated dirty worktree changes
- the user values consistency and architecture more than compatibility
