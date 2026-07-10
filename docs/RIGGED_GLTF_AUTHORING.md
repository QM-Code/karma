# Rigged glTF/GLB And Mixamo FBX Authoring

This is the expected authoring path for Karma rigged-animation assets and
validation fixtures. The runtime supports binary `.glb` and JSON `.gltf`
scenes, plus Assimp-backed FBX models and standalone FBX animation clips.

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

The source scene file is only an import container. Package `gltf_scene`
entries register clips as plain `karma::world::AnimationClip` assets and
store their keys on `karma::assets::GltfSceneAsset`. After instantiation, those
clips are copied into `AnimatorComponent::clips`.

Clips are not tied to the source file bytes, renderer resources, or mesh
objects. They are still authored in the imported node/skeleton index space. To
use a clip on a different rig, retarget it into the target skeleton through
semantic humanoid bindings or an explicit `SkeletonMap`.

## Mixamo FBX Packages

Karma has a built-in `HumanoidProfileKind::Mixamo` profile. Add a `humanoid`
block to a model's `gltf_scene` entry to bind its imported skeleton and register
a semantic `HumanoidRig`. Use `animation_clip` entries for standalone FBX files;
the same block registers the clip's source rig for retargeting:

```json
{
  "version": 1,
  "assets": [
    {
      "type": "gltf_scene",
      "key": "characters/hero",
      "path": "Character.fbx",
      "humanoid": {
        "profile": "mixamo",
        "rig_key": "characters/hero/rig"
      }
    },
    {
      "type": "animation_clip",
      "key": "animations/stride",
      "path": "stride.fbx",
      "name": "Stride",
      "humanoid": {
        "profile": "mixamo",
        "rig_key": "animations/stride/rig"
      }
    }
  ]
}
```

An `animation_clip` entry may use `clip` to select a named animation when the
source contains more than one; otherwise the first clip is imported. Import
evaluates FBX pivot stacks into node-local transforms, so runtime skeletons and
animation channels do not expose Assimp pivot/helper nodes.

## Runtime Flow

- Imported roots receive an `AnimatorComponent` when clips exist.
- Imported renderable primitives receive `DeformableMeshComponent` when their
  source primitive has skin data, morph target deltas, or authored morph weights.
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
- Full pose-node names, parent indices, node indices, and rest transforms.
- Separate skin-palette joint node indices and inverse bind matrices.
- Node, joint, and morph-weight animation channel mappings.
- CPU reference deformation with shared `world::MeshData` joint/weight
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
- Built-in Mixamo semantic binding and retargeting through `HumanoidRig`,
  `bindHumanoidRig(...)`, and `retargetHumanoidClip(...)`.

## Retargeting Conventions

For animation sharing between humanoid rigs today:

- Export stable, readable bone names.
- Keep one clear root-motion source node or joint.
- Keep rest poses consistent across rigs when possible.
- For Mixamo rigs, request the built-in profile in each package entry and call
  `retargetHumanoidClip(...)` with the registered source and target rigs.
- Semantic retargeting converts rotation, position, and scale channels relative
  to each rig's rest-local transforms. By default it derives root translation
  scale from composed target/source humanoid heights.
- For custom rigs, author a `HumanoidProfile` or build an explicit `SkeletonMap`.
  Explicit maps may provide rest-pose correction matrices and
  set `RetargetOptions::root_scale_policy` to `ExplicitScale` together with
  `RetargetOptions::root_translation_scale`.

Semantic profiles sit above explicit skeleton maps; they do not replace the
lower-level mapping API.

## Current Gaps

- Mixamo is the only built-in humanoid profile. Other naming conventions need
  an authored profile or explicit skeleton map.
- Flat `MeshComponent::mesh_asset_key` loading does not instantiate scene
  animation, skinning, or morph-target data.

## Visual Validation Asset

Use authored assets outside generated unit fixtures for the main visual smoke
tests. A rigged Blender `.glb` should contain:

- A reference bind pose.
- At least one locomotion clip with visible joint deformation.
- At least one transition-friendly idle clip.
- A named root-motion source node.
- A simple material that casts and receives shadows.

Generated tiny GLBs should remain in unit tests for deterministic import and
sampling coverage. The real Mixamo FBX regression imports the checked-in
`Character.fbx` and `stride.fbx`, verifies a one-root full pose hierarchy and a
52-joint skin palette, and checks bind-pose and animated CPU-skinned bounds. See
[Humanoid RPG FBX Deformation Postmortem](HUMANOID_RPG_FBX_DEFORMATION.md).

## Showcase Asset

The current interactive showcase package lives at:

```text
examples/assets/animation/dustbound_wayfarer/assets.package.json
```

That package references
`examples/assets/animation_model/source/dustbound_wayfarer_merged_animations.glb`.

Run it with:

```bash
cmake --build --preset portable --target animation_gltf --parallel 2
./build/portable/examples/animation/gltf
```

The example accepts another asset-package directory or manifest as its first
argument and an optional scene asset key as its second:

```bash
./build/portable/examples/animation/gltf path/to/assets.package.json characters/hero
```

The standalone Mixamo FBX showcase is:

```bash
cmake --build --preset portable --target animation_humanoid_rpg --parallel 2
./build/portable/examples/animation/humanoid_rpg
```
