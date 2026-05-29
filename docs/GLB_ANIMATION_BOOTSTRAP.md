# GLB Animation Bootstrap

This document describes the current GLB animation/skinning state after the first
node-animation and skeletal-animation passes. The implementation is intentionally
small and conservative: rendering still consumes final world `TransformComponent`
values, and skeletal deformation is CPU-side for now.

## Current Runtime Shape

Animation runs in this order during the variable render frame:

1. `AnimationSystem` samples animation clips into `LocalTransformComponent`s.
2. `scene::updateWorldTransforms(...)` composes scene hierarchy local transforms
   into world-space `TransformComponent`s.
3. `CpuSkinningSystem` deforms any `SkinnedMeshComponent` meshes from their bind
   pose using current joint world transforms.
4. particles, runtime modules, and the renderer consume world transforms and the
   latest mesh buffers.

`TransformComponent` remains the world transform used by render, physics,
lights, particles, and audio. `LocalTransformComponent` is the editable local
pose used by imported GLB node animation and hierarchy composition.

## Key Files

- `include/karma/animation/animation_clip.h`
- `src/animation/animation_clip.cpp`
- `include/karma/animation/animation_system.h`
- `src/animation/animation_system.cpp`
- `include/karma/animation/cpu_skinning_system.h`
- `src/animation/cpu_skinning_system.cpp`
- `include/karma/components/animation_player.h`
- `src/components/animation_player.cpp`
- `include/karma/components/skinned_mesh.h`
- `include/karma/components/transform.h`
- `include/karma/scene/transform_hierarchy.h`
- `src/scene/transform_hierarchy.cpp`
- `include/karma/scene/glb_scene_import.h`
- `src/scene/glb_scene_import.cpp`
- `examples/glb_animation_example.cpp`
- `tests/animation_tests.cpp`

## What Works

- GLB node/object transform animation:
  - translation, rotation, scale channels
  - linear interpolation for translation/scale
  - normalized slerp for rotation
  - looped playback
  - autoplay on imported root when clips exist, controlled by
    `GlbSceneInstantiateOptions::autoplay_animations`
- Scene hierarchy composition:
  - imported nodes get local and world transforms
  - primitive render entities are children with identity local transforms
  - renderer remains unaware of animation clips
- Basic skeletal deformation:
  - Assimp bone data is imported from skinned GLB meshes
  - vertex influences keep up to 4 weights per vertex
  - inverse bind matrices and joint node references are stored in
    `SkinnedMeshComponent`
  - `CpuSkinningSystem` computes
    `mesh_inverse_world * joint_world * inverse_bind`
  - skinned vertices/normals/tangents are written into a CPU mesh and uploaded
    through `GraphicsDevice::updateMesh(...)`

## Non-Goals / Limitations

- No GPU skinning yet.
- No shader joint matrices, bone textures, or palette buffers.
- No retargeting between different skeletons.
- No animation blending or state machine.
- No root-motion extraction.
- No morph targets.
- No explicit glTF `skin` object mapping beyond Assimp `aiMesh::mBones`.
- CPU skinning rebuilds immutable Diligent mesh buffers through `updateMesh`.
  This is fine for a first pass and poor for many characters or high-poly rigs.
- `MeshComponent.mesh_key = "model.glb"` still follows the flat mesh loader path
  and does not participate in GLB scene animation/skinning.

## Public API Surface

- `scene::loadGlbScenePrefab(...)` now returns:
  - `GlbScenePrefab::animations`
  - skinned primitive data when present
- `scene::instantiateGlbScenePrefab(...)` now:
  - creates local and world transforms for imported nodes/primitives
  - attaches `AnimationPlayerComponent` to imported root when clips exist
  - attaches `SkinnedMeshComponent` to skinned primitive entities
- Animation player helpers:
  - `setAnimationClip(player, index, reset_time)`
  - `setAnimationClip(player, name, reset_time)`
  - `playAnimation(player)`
  - `pauseAnimation(player)`
  - `stopAnimation(player)`

## Validation

Verified in this workspace:

```bash
cmake --build build --target karma_animation_tests -j2
./build/karma_animation_tests
ctest --test-dir build -R karma_animation_tests --output-on-failure
cmake --build build --target karma_glb_scene_import_example karma_glb_animation_example -j2
```

`karma_animation_tests` is headless and exits silently on success. It covers:

- keyframe clamping
- linear translation/scale interpolation
- rotation slerp normalization
- loop time wrapping
- hierarchy world transform propagation
- pause freeze behavior
- clip switching reset behavior
- direct CPU skinning math
- generated skinned GLB import with weights, joints, inverse bind matrices, and
  animation channels

`karma_glb_animation_example` is the windowed visual smoke target. It currently
generates a simple node-animated GLB in the temp directory. It is not yet a
visual skeletal test.

## Good Next Steps

1. Add a windowed skeletal visual sample with a tiny generated or checked-in GLB.
2. Validate real authored skinned GLBs from Blender, especially joint naming and
   inverse bind matrix conventions.
3. Decide whether to keep CPU skinning as a debug/fallback path or move directly
   to GPU skinning.
4. If moving to GPU skinning, add joint/weight vertex attributes, joint palette
   upload, shader deformation, and matching shadow-pass support together.
5. Preserve the current flat mesh loader behavior unless the user explicitly
   wants `MeshComponent.mesh_key` to use scene import.
6. Add compatibility tests around imported static GLB scenes to catch hierarchy
   regressions.

## Known Watch Points

- `AnimationSystem` mutates local transforms. User/game code that directly edits
  world `TransformComponent`s on imported animated nodes will be overwritten by
  hierarchy composition.
- Physics bodies are not driven by animation. Animated/skinned imports are
  visual-only for now.
- CPU skinning uploads a new mesh buffer every frame for each skinned mesh. This
  can be expensive and should be treated as a correctness path, not the final
  performance path.
- The worktree has unrelated renderer/particle/doc changes. Do not revert files
  outside the animation/skinning work without checking ownership.
