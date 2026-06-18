\mainpage Karma Engine API

Karma is a C++20 ECS-driven 3D game engine. The generated API reference covers
the public headers in `include/karma/**`; it is meant to answer "what can game
code include and call?" rather than document private backend implementation
details.

Start with these modules:

- \ref karma_runtime for application lifecycle, input, UI, and engine wiring.
- \ref karma_world for ECS, components, systems, and scene hierarchy.
- \ref karma_rendering for renderer-facing APIs and render data contracts.
- \ref karma_rendering_guide for camera-resolved post-processing, shadows, and
  render submission boundaries.
- \ref karma_simulation for animation, physics, collision, and navigation.
- \ref karma_content for glTF/GLB import, mesh import, and prefab runtime APIs.
- \ref karma_animation_guide for glTF animation and deformation
  runtime flow.
- \ref karma_features for optional visual and UI feature modules.
- \ref karma_media for audio APIs.
- \ref karma_platform for windows, input events, and networking.
- \ref karma_core for foundational IDs, math, timing, and type IDs.

Handwritten guides live beside this generated reference:

- `docs/ENGINE_USAGE.md`
- `docs/ARCHITECTURE.md`
- `docs/ENGINE_USAGE.md#post-process-profiles`
- `docs/RENDERING_STARTUP_OPTIMIZATION.md`
- `docs/NAVIGATION.md`
- `docs/ANIMATION_V2.md`
- `docs/PARTICLE_SYSTEM.md`
- `docs/EFFECT_PREFABS.md`
- `docs/BEAM_PATHS.md`
- `docs/RIGGED_GLTF_AUTHORING.md`

## Public API Rules

Karma's public include paths use the layered `karma/<layer>/...` hierarchy.
Do not add forwarding headers for old public include paths. Backend-specific
types should stay behind public subsystem interfaces unless examples or tools
need a deliberately small access surface.

## Typical App Flow

1. Create a subclass of karma::app::GameInterface.
2. Populate the ECS in `onStart()`.
3. Register optional karma::app::RuntimeModule instances before
   karma::app::EngineApp::start() when a visual feature needs a runtime system.
4. Run karma::app::EngineApp::tick() until karma::app::EngineApp::isRunning()
   returns false.

Examples under `examples/` are treated as executable documentation for this
flow.
