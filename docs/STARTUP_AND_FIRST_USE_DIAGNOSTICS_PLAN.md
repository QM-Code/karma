# Startup And First-Use Diagnostics Plan

## Summary

Implement a diagnostics-first pass that explains boot time and the first-explosion
hitch before moving work into startup or background warm-up. Use existing
env-gated logging where possible, add focused renderer/particle resource timing,
and keep runtime behavior unchanged except for diagnostics overhead when enabled.

## Key Changes

- Extend `KARMA_ENGINE_STARTUP_DIAG=1` into a complete startup timeline:
  - Log total process-to-first-warm-frame stages already present in
    `EngineApp::start`.
  - Add missing substage timing for graphics backend construction,
    shader/render-state cache load, renderer pipeline initialization,
    environment map setup, loading splash frames, and first normal `tick`.
  - Keep logs one-line and stable:
    `Engine startup diag: stage=<name> ms=<value> total_ms=<value>`.
- Add Diligent backend startup/resource diagnostics behind
  `KARMA_ENGINE_STARTUP_DIAG=1`:
  - Log shader cache path, cache existed, load time, save time, flush state, and
    version.
  - Log pipeline/resource initialization timing by renderer area:
    frame/forward/shadow/line/particle/ui/environment where practical.
  - Add backend-private counters for GPU buffer/texture/PSO creation during
    startup and first frame; do not expose these as public API yet.
- Add focused particle first-use diagnostics behind
  `KARMA_PARTICLE_RESOURCE_DIAG=1`:
  - Log particle GPU buffer growth events with buffer kind, old capacity, new
    capacity, requested capacity, active emitter count, active particle capacity,
    and current layer.
  - Log first activation of persistent GPU particle paths: simulate, indirect
    dispatch, culling, sort, global sort/grouped fallback, and indirect draw.
  - Reuse existing async particle stats; do not add blocking GPU readback.
- Add explosion example correlation diagnostics:
  - In `karma_explosion_stress_example`, when `KARMA_EXPLOSION_STRESS_STATS=1`
    is enabled, log the first three explosion trigger windows with trigger index,
    prefab instantiate time, restart/destroy time, first submitted emitter count,
    frame time, and renderer particle stats snapshot.
  - Keep the existing 128-explosion stress behavior unchanged.
  - Add a short note to the log clarifying that Forward+ `cpu_fallback` is
    separate from particle fallback.
- Document how to measure and compare:
  - Update `docs/ENGINE_USAGE.md` with startup diagnostic env vars and sample
    commands.
  - Update `docs/EXPLOSION_STRESS_PERF.md` with the first-explosion hitch
    workflow: cold run with cache flushed, warm run with cache retained,
    particle resource diagnostics enabled, and expected indicators for shader
    cache miss vs particle buffer growth vs prefab/resource load.

## Public API / Interface Changes

- No gameplay-facing API changes in this diagnostics pass.
- Add one new environment variable:
  - `KARMA_PARTICLE_RESOURCE_DIAG=1`: logs particle GPU resource growth and
    first-use path activation.
- Do not add debug-editor UI in this pass.

## Test Plan

- Build:
  - `cmake --build build --target karma_particle_example karma_explosion_stress_example karma_prefab_gallery_example karma_example --parallel $(nproc)`
- Existing tests:
  - `ctest --test-dir build -R karma_prefab_tests --output-on-failure`
- Diagnostics smoke:
  - `timeout 8s env KARMA_ENGINE_STARTUP_DIAG=1 ./build/karma_particle_example`
  - `timeout 12s env KARMA_ENGINE_STARTUP_DIAG=1 KARMA_PARTICLE_RESOURCE_DIAG=1 KARMA_PARTICLE_STATS=1 ./build/karma_prefab_gallery_example`
  - `timeout 20s env KARMA_ENGINE_STARTUP_DIAG=1 KARMA_PARTICLE_RESOURCE_DIAG=1 KARMA_PARTICLE_STATS=1 KARMA_EXPLOSION_STRESS_STATS=1 ./build/karma_explosion_stress_example --explosions 128 --stats`
- Acceptance:
  - Logs identify startup stage timings, shader cache status, renderer warm-up
    timing, first normal tick timing, and first particle GPU resource growth.
  - First-explosion logs correlate the hitch with either shader/cache work,
    particle allocation/resizing, prefab instantiation/resource load, or first
    GPU particle path activation.
  - Normal runs without diagnostic env vars produce no new noisy logs and
    preserve current behavior.

## Assumptions

- The first implementation should measure, not optimize or move work.
- Diagnostics should cover both general engine startup and the observed
  explosion hitch.
- Vulkan/Diligent remains the primary backend to inspect.
- Any later warm-up/preallocation plan should be based on these logs.
