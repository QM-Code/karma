# Karma Engine

Karma is a C++20 ECS-driven 3D game engine with a Diligent renderer, prefab and particle tooling, optional physics/audio/network backends, and sample applications under `examples/`.

## Start Here

If you are new to this repo, start with:

- [Usage Guide](docs/ENGINE_USAGE.md)
- [Implementation Notes](docs/ENGINE_IMPLEMENTATION.md)
- [Examples](examples/README.md)
- [Next Agent Bootstrap](NEXT_AGENT.md)

## Runtime Bootstrap

The main runtime entry point is [`src/runtime/app/engine_app.cpp`](src/runtime/app/engine_app.cpp).

Current startup flow:

- `EngineApp::start()` creates the window, graphics device, render system, prefab system, particle system, physics/collision layers, and audio layer.
- It binds the game context, attaches any registered runtime modules, applies renderer settings from `EngineConfig`, and warms one render frame so the first visible frame does less surprise work.
- `EngineApp::tick()` runs fixed-step gameplay and systems first, then per-frame updates, prefab/audio/UI prep, `beginFrame()`, particle updates, runtime-module updates, `RenderSystem::update()`, `renderLayer(0)`, UI, and `endFrame()`.

## Renderer Layout

The engine-side renderer is split between scene extraction and backend execution:

- [`src/rendering/renderer/render_system.cpp`](src/rendering/renderer/render_system.cpp): ECS-to-render extraction, mesh/material binding, camera/light/environment submission, and debug draw collection.
- [`src/rendering/renderer/backends/diligent/backend.hpp`](src/rendering/renderer/backends/diligent/backend.hpp): Diligent backend state and private pass entry points.
- [`src/rendering/renderer/backends/diligent/backend_init.cpp`](src/rendering/renderer/backends/diligent/backend_init.cpp): device/bootstrap, shader and pipeline creation, shadow resources, and core renderer setup.
- [`src/rendering/renderer/backends/diligent/backend_render.cpp`](src/rendering/renderer/backends/diligent/backend_render.cpp): frame orchestration, Forward+ setup, scene-copy orchestration, and remaining glue.
- [`src/rendering/renderer/backends/diligent/passes/`](src/rendering/renderer/backends/diligent/passes): shadow, forward, particle, environment, line, frame, and camera-override pass code.
- [`src/rendering/renderer/backends/diligent/resources/`](src/rendering/renderer/backends/diligent/resources): materials, meshes, render targets, and texture/resource helpers.

## Current Bootstrap Handoffs

Current high-signal handoff docs:

- [Renderer Refactor Bootstrap](docs/RENDERER_REFACTOR_BOOTSTRAP.md)
- [Particle Performance Bootstrap](docs/PARTICLE_PERF_BOOTSTRAP.md)
- [Effect API Split Bootstrap](docs/EFFECT_API_SPLIT_BOOTSTRAP.md)
- [Explosion Stress Bootstrap](docs/EXPLOSION_STRESS_BOOTSTRAP.md)
- [Prefab Gallery Bootstrap](docs/PREFAB_GALLERY_BOOTSTRAP.md)
- [Collision Bootstrap](docs/COLLISION_BOOTSTRAP.md)
- [Local Light Shadow Bootstrap](docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [Local Light Probe Bootstrap](docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Project Reality

This repo is no longer a minimal header-only skeleton. The current tree contains a working `src/` implementation with renderer, prefab, particle, optional effect-module, physics, collision, audio, and networking layers. Older docs that describe the repo as a tiny abstract sandbox are outdated.
