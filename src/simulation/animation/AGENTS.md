# Animation Runtime Notes

This directory owns runtime animation sampling and CPU mesh deformation.

Before changing behavior, read:

- `docs/NEXT_AGENT.md`
- `docs/ANIMATION_V2.md`
- `docs/RIGGED_GLTF_AUTHORING.md`
- `include/karma/simulation/animation/animation_clip.h`
- `include/karma/world/components/animator.h`
- `include/karma/world/components/deformable_mesh.h`
- `include/karma/world/scene/transform_hierarchy.h`

Rules for changes:

- Keep animation sampling independent from the renderer. The renderer should
  consume final world transforms and mesh buffers, not clips.
- `AnimationSystem` writes local transforms; `scene::updateWorldTransforms(...)`
  writes world transforms.
- `DeformationSystem` is the deformation upload point. It updates renderer
  deformation resources for GPU skinning/morphs and performs CPU reference
  deformation when selected for diagnostics.
- Keep tests in `tests/animation_tests.cpp` updated with any sampling, hierarchy,
  morph, or skinning behavior change.

Common validation targets:

```bash
cmake --build build --target karma_animation_tests scene_gltf_import animation_gltf -j2
./build/karma_animation_tests
ctest --test-dir build -R karma_animation_tests --output-on-failure
```
