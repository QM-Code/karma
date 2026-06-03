# Animation Runtime Notes

This directory owns runtime animation sampling and CPU skinning.

Before changing behavior, read:

- `NEXT_AGENT.md`
- `docs/RIGGED_GLB_AUTHORING.md`
- `include/karma/simulation/animation/animation_clip.h`
- `include/karma/world/components/animation_player.h`
- `include/karma/world/components/skinned_mesh.h`
- `include/karma/world/scene/transform_hierarchy.h`

Rules for changes:

- Keep animation sampling independent from the renderer. The renderer should
  consume final world transforms and mesh buffers, not clips.
- `AnimationSystem` writes local transforms; `scene::updateWorldTransforms(...)`
  writes world transforms.
- `CpuSkinningSystem` is a first-pass correctness path. Do not optimize it by
  changing renderer vertex formats unless you are intentionally implementing GPU
  skinning across forward and shadow passes.
- Keep tests in `tests/animation_tests.cpp` updated with any sampling, hierarchy,
  or skinning behavior change.

Common validation targets:

```bash
cmake --build build --target karma_animation_tests karma_glb_scene_import_example karma_glb_animation_example -j2
./build/karma_animation_tests
ctest --test-dir build -R karma_animation_tests --output-on-failure
```
