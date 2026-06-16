# Animation Runtime Notes

This directory owns runtime animation sampling and CPU mesh deformation.

Before changing behavior, read:

- `NEXT_AGENT.md`
- `docs/RIGGED_GLB_AUTHORING.md`
- `include/karma/simulation/animation/animation_clip.h`
- `include/karma/world/components/animation_player.h`
- `include/karma/world/components/morph_target.h`
- `include/karma/world/components/skinned_mesh.h`
- `include/karma/world/scene/transform_hierarchy.h`

Rules for changes:

- Keep animation sampling independent from the renderer. The renderer should
  consume final world transforms and mesh buffers, not clips.
- `AnimationSystem` writes local transforms; `scene::updateWorldTransforms(...)`
  writes world transforms.
- `CpuSkinningSystem` is the CPU deformation upload point. It applies morph
  targets, builds joint palettes for GPU skinning, and performs CPU skinning
  fallback when needed.
- Keep tests in `tests/animation_tests.cpp` updated with any sampling, hierarchy,
  morph, or skinning behavior change.

Common validation targets:

```bash
cmake --build build --target karma_animation_tests karma_glb_scene_import_example karma_glb_animation_example -j2
./build/karma_animation_tests
ctest --test-dir build -R karma_animation_tests --output-on-failure
```
