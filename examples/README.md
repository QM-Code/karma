# Karma examples

This folder contains small, self-contained snippets that show how to use the
Karma ECS + scene graph from game code.

- `GameInit.cpp`: builds a simple scene with a player, camera, environment, and audio.
- `GameLoop.cpp`: engine-owned loop with a game interface.
- `glb_scene_import_example.cpp`: minimal authored-scene import example using `world-with-lights.glb`.
- `laser_example.cpp`: seeded point-chain beam sample that now instantiates a prefab for the beam styling, then feeds it runtime points through the reusable ECS beam-path API. The result has a hot white core, colored glow shell, endpoint flares, optional beam lights, subtle electric shimmer, and a heat-distortion layer along the full path. See [../docs/BEAM_PATHS.md](../docs/BEAM_PATHS.md) and [../docs/EFFECT_PREFABS.md](../docs/EFFECT_PREFABS.md).
- `laser_prefab_example.cpp`: minimal prefab-only laser scene. It creates a simple world, camera, and light setup, then instantiates [../examples/assets/prefabs/beam/prefab.kprefab](../examples/assets/prefabs/beam/prefab.kprefab) directly with one call and no extra beam API setup.
- `material_override_example.cpp`: minimal runtime material override example with side-by-side GLB tints.
- `energy_orb_example.cpp`: hybrid orb sample that now goes through the prefab registry/package path. It registers the `energy_orb` package once, then instantiates the orb by prefab key, with generated orb atlas textures and dependent particle effects prepared automatically. The prefab still uses a real `shot.glb` sphere mesh for the shell plus particle-driven plasma core, electric arcs, halo glow, distortion, and a point light. See [../docs/EFFECT_PREFABS.md](../docs/EFFECT_PREFABS.md).
- `prefab_gallery_example.cpp`: prefab showcase scene. It instantiates beam, orb, and wave prefabs in a grid, with red, blue, green, and purple variants, so it acts as a direct sanity check for the reusable prefab surfaces.
- `wave_example.cpp`: transparent sphere-volume sample built on `wave.glb`, using a dedicated wave shading mode that tints and shimmers scene content seen through the sphere, keeps a glassy polished edge, and still works when the camera moves through the volume.
- `volumetric_sphere_example.cpp`: minimal analytic volume-sphere prefab scene. It instantiates [../examples/assets/prefabs/volumetric_sphere/prefab.kprefab](../examples/assets/prefabs/volumetric_sphere/prefab.kprefab) from its prefab directory, so the sample is now a direct reference for the plug-and-play volume-sphere prefab path.
- `volumetric_sphere_prefab_example.cpp`: the most minimal prefab-only volume sphere scene. It creates a simple world, camera, and sunlight setup, then instantiates the volumetric sphere prefab directory with one call and no extra runtime setup.
- `particle_example.cpp`: minimal billboard particle example with file-backed `.kpeffect` assets, live hot reload, additive sparks, alpha-sorted smoke, soft-particle depth fading, heat distortion, ground-aligned particles, simple ground-colliding debris, and a staged replayable explosion with authored fire and smoke flipbooks from EXR sequences, bright shock rings, extra debris, hotter ember particles that settle and fade, a dust ring, a scorch mark, radial spherical emission, curve-shaped smoke bloom, and a short-lived point light. See [../docs/PARTICLE_SYSTEM.md](../docs/PARTICLE_SYSTEM.md) for the ECS-facing workflow.
- `imgui_ui_demo.cpp`: ImGui draw data bridge layered over the 3D frame.
- `rmlui_ui_demo.cpp`: RmlUi draw data bridge layered over the 3D frame.
