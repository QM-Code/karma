# Karma examples

This folder contains small, self-contained snippets that show how to use the
Karma ECS + scene graph from game code.

- `GameInit.cpp`: builds a simple scene with a player, camera, environment, and audio.
- `GameLoop.cpp`: engine-owned loop with a game interface.
- `glb_scene_import_example.cpp`: minimal authored-scene import example using `world-with-lights.glb`.
- `laser_example.cpp`: seeded point-chain beam sample that now goes through the reusable ECS beam-path API, with a hot white core, colored glow shell, endpoint flares, optional beam lights, subtle electric shimmer, and a heat-distortion layer along the full path. See [../docs/BEAM_PATHS.md](../docs/BEAM_PATHS.md).
- `material_override_example.cpp`: minimal runtime material override example with side-by-side GLB tints.
- `energy_orb_example.cpp`: hybrid orb sample that uses a real `shot.glb` sphere mesh for the outer shell plus particle-driven plasma core, animated electric arcs, halo glow, distortion, and a point light over the shared demo world. The sample now drives its look from one accent color plus generic `ParticleEffectOverrideComponent` ECS overrides instead of per-frame sample-specific emitter mutation.
- `particle_example.cpp`: minimal billboard particle example with file-backed `.kpeffect` assets, live hot reload, additive sparks, alpha-sorted smoke, soft-particle depth fading, heat distortion, ground-aligned particles, simple ground-colliding debris, and a staged replayable explosion with authored fire and smoke flipbooks from EXR sequences, bright shock rings, extra debris, hotter ember particles that settle and fade, a dust ring, a scorch mark, radial spherical emission, curve-shaped smoke bloom, and a short-lived point light. See [../docs/PARTICLE_SYSTEM.md](../docs/PARTICLE_SYSTEM.md) for the ECS-facing workflow.
- `imgui_ui_demo.cpp`: ImGui draw data bridge layered over the 3D frame.
- `rmlui_ui_demo.cpp`: RmlUi draw data bridge layered over the 3D frame.
