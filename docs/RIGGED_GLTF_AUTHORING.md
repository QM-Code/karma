# Rigged glTF/GLB Authoring

This is the expected authoring path for Karma rigged-animation assets and
validation fixtures. The runtime uses glTF-generic APIs and supports both
binary `.glb` and JSON `.gltf` sources.

## Blender Export

- Use Blender units at 1 unit = 1 meter.
- Apply object scale before export.
- Keep the armature and mesh in the same exported scene.
- Name the armature, mesh object, bones, and animation actions clearly; Karma uses
  node and joint names for diagnostics and explicit retarget maps.
- Export as `glTF 2.0` binary `.glb` for compact checked-in fixtures, or as
  `.gltf` plus external buffers/images when human-readable source assets are
  useful.
- Enable selected objects only when exporting a fixture scene.
- Enable animations and skinning.
- Disable leaf bones.
- Keep axes in Blender/glTF defaults unless the importing example explicitly
  compensates for a different convention.

## Clip Conventions

- One Blender Action should map to one named glTF animation clip.
- Keep root motion on a stable root node or hip/root joint.
- Add engine-side `AnimationEvent` markers after import for now; glTF extras are
  not consumed as event payloads yet.
- Use `STEP`, `LINEAR`, or `CUBICSPLINE` interpolation. Karma imports all three
  for transform and morph-weight tracks.
- Author shape keys as glTF morph targets on the mesh primitive. Default shape
  key values become `DeformableMeshComponent::base_morph_weights`.
- Animate shape key weights on the mesh node when a clip should drive facial,
  corrective, or other morph deformation.

## Asset Boundaries

The source `.glb` or `.gltf` is only an import container. After
`loadGltfScenePrefab(...)`, clips live as plain
`karma::animation::AnimationClip` values in `GltfScenePrefab::animations`.
After instantiation, those clips are copied into
`AnimatorComponent::clips`.

Clips are not tied to the source file bytes, renderer resources, or mesh
objects. They are still authored in the imported node/skeleton index space. To
use a clip on a different rig, retarget it into the target skeleton with an
explicit `SkeletonMap`.

## Runtime Flow

- Imported roots receive an `AnimatorComponent` when clips exist.
- Imported renderable primitives receive `DeformableMeshComponent` when their
  glTF primitive has skin data, morph target deltas, or authored morph weights.
- Animation sampling writes local node transforms and runtime morph weights.
- Animation events are mirrored to `AnimationEventBufferComponent`.
- Root-motion deltas are applied or exposed through `RootMotionComponent`.
- Scene hierarchy composition writes final world transforms after animation.
- Mesh deformation updates renderer-owned joint palette and morph-weight
  resources.
- GPU deformation is the default path for skinned or morphed primitives.

## Supported Runtime Data

- Multiple clips.
- Multiple skins and skeletons.
- Joint names, joint parent indices, joint node indices, inverse bind matrices.
- Node, joint, and morph-weight animation channel mappings.
- CPU reference deformation with shared `geometry::MeshData` joint/weight
  payloads retained.
- Renderer-facing joint indices, joint weights, and morph deltas for GPU
  deformation.
- GPU deformation through the Diligent forward, transparent, depth prepass, and
  shadow paths.
- CPU `skinMesh(...)` and `morphMesh(...)` helpers for tests and correctness
  checks.
- Morph target position, normal, and tangent deltas on imported glTF
  primitives.
- Runtime morph weights through `DeformableMeshComponent`.
- Animator state machines with clip states, 1D blend trees, transitions,
  conditions, triggers, interruption policies, events, and root-motion deltas.
- Explicit skeleton retargeting through `SkeletonMap` and `retargetClip(...)`.

## Retargeting Conventions

For animation sharing between humanoid rigs today:

- Export stable, readable bone names.
- Keep one clear root-motion source node or joint.
- Keep rest poses consistent across rigs when possible.
- Build an explicit `SkeletonMap` from source joints to target joints.
- Add rest-pose correction matrices for joints whose bind/rest orientation
  differs.
- Use `RetargetOptions::root_translation_scale` when source and target rigs use
  different character scale.

Humanoid semantic profiles are intentionally not part of the current runtime.
A future humanoid layer should sit above explicit skeleton maps instead of
replacing them.

## Current Gaps

- Humanoid semantic retarget profiles are not implemented; use explicit
  skeleton maps.
- `MeshComponent::mesh_key` flat mesh loading does not use the glTF scene
  animation, skinning, or morph-target path.

## Visual Validation Asset

Use an authored Blender `.glb` outside generated unit fixtures for the main
visual smoke test. The asset should contain:

- A reference bind pose.
- At least one locomotion clip with visible joint deformation.
- At least one transition-friendly idle clip.
- A named root-motion source node.
- A simple material that casts and receives shadows.

Generated tiny GLBs should remain in unit tests for deterministic import and
sampling coverage.

## Showcase Asset

The current interactive showcase asset lives at:

```text
examples/assets/animation_model/source/dustbound_wayfarer_merged_animations.glb
```

Run it with:

```bash
cmake --build build --target animation_gltf --parallel
./build/examples/animation/gltf
```

The example accepts another `.glb` or `.gltf` as its first command-line
argument.
