# Animation Runtime {#karma_animation_guide}

Karma animation is split across content import, ECS components, simulation
sampling, scene transform composition, and renderer upload/submission.

For the durable architecture overview, asset-boundary explanation, showcase
commands, and retargeting notes, see `docs/ANIMATION_V2.md`.

## Imported Scene And Clip Flow

Package `gltf_scene` entries import glTF/GLB or FBX nodes, transforms, skins,
skeletons, animation clips, and mesh primitive data into a registered
`karma::assets::GltfSceneAsset` plus child assets. `instantiateGltfSceneAsset(...)`
creates ECS entities for the node tree and separate child entities for renderable
mesh primitives.

Package `animation_clip` entries import a selected animation without
instantiating its source scene. They support standalone FBX clips, optional
`clip` source-name selection, an optional display `name`, and a `humanoid` block
that registers the source skeleton and semantic rig. Both `gltf_scene` and
`animation_clip` accept `{"humanoid":{"profile":"mixamo"}}`; `rig_key`
optionally gives the registered `karma::world::HumanoidRig` a stable asset key.

Renderable primitive entities may receive:

- `karma::components::MeshComponent` for renderer mesh/material binding.
- `karma::components::DeformableMeshComponent` when the primitive has skin data,
  morph target deltas, or authored morph weights.

Imported roots receive `karma::components::AnimatorComponent` when clips are
available. The animator stores the imported clips, node entity map, and
node-to-morph-primitive map needed for runtime sampling, plus imported skeleton,
skin, and humanoid rig metadata.

Imported animation clips are plain `karma::world::AnimationClip` values. Scene
clips are resolved from the registry keys stored on `GltfSceneAsset` and copied
into `AnimatorComponent::clips`. Standalone `animation_clip` assets remain in
the registry until gameplay copies or retargets them into an animator. Neither
kind retains file handles, renderer handles, or mesh ownership, and both remain
authored in the imported node/skeleton index space until retargeted.

## Runtime Update Order

The engine-owned frame order is:

1. `karma::world::AnimationSystem` samples clips and writes local transforms
   plus morph weights.
2. `karma::world::updateWorldTransforms(...)` composes local transforms into
   final world `TransformComponent` values.
3. `karma::world::DeformationSystem` builds joint palettes, updates
   renderer-owned deformation resources, and uploads CPU-deformed meshes only
   when the CPU reference path is explicitly selected.
4. `karma::rendering::RenderSystem` submits visible meshes and deformation
   resource handles to the renderer.

The animation system is renderer-agnostic. It writes ECS data only. Mesh buffer
updates are isolated to the deformation upload stage.

## Deformation

Imported skinned and morphed meshes use GPU deformation by default. The
renderer consumes the bind mesh plus a `karma::rendering::DeformationId` that
references joint palette and morph-weight resources owned by the backend.

CPU skinning remains available through `karma::world::skinMesh(...)` and
CPU morphing through `karma::world::morphMesh(...)`. Runtime CPU mesh
uploads are selected with `karma::components::DeformationPath::CpuReference`
and are intended for tests and diagnostics.

## Morph Targets

Morph target deltas are stored on `karma::world::MeshData`. Runtime weights
live on `karma::components::DeformableMeshComponent`.

`AnimationSystem` samples glTF `weights` channels into morph components. If the
active clip has no morph track for a node, the component returns to its authored
base weights. For blended states, morph weights are blended with the same clip
weights used for transform sampling.

Morph deformation is GPU-first in the runtime path. CPU deformation remains as
a reference path for validation and diagnostics.

## Retargeting

Karma supports explicit skeleton retargeting through
`karma::world::SkeletonMap`, `SkeletonMapEntry`, `RetargetOptions`,
`validateSkeletonMap(...)`, and `retargetClip(...)`.

Retargeting converts a source clip from the source skeleton's joint/node index
space into the target skeleton's joint/node index space. The semantic layer adds
`HumanoidRig`, `HumanoidProfile`, `bindHumanoidRig(...)`,
`validateHumanoidRig(...)`, `buildHumanoidSkeletonMap(...)`, and
`retargetHumanoidClip(...)`. `HumanoidProfileKind::Mixamo` is the current
built-in profile and is the profile selected by package `humanoid` blocks.

Semantic retargeting corrects rotation, position, and scale keys relative to the
source and target rest-local transforms. It computes humanoid height from the
complete composed rest hierarchy through `humanoidRigHeight(...)`. It scales
only the translation-bearing semantic root (`Root` or, when Root is static,
`Hips`) by the target/source height ratio. Authored profiles are retained on
their rigs and work with the semantic path; explicit `SkeletonMap` values remain
available for direct joint mapping.

## Current Limits

- Mixamo is the only built-in humanoid semantic profile; other rigs need an
  authored profile or explicit skeleton map.
- Flat `MeshComponent::mesh_asset_key` loading does not instantiate scene
  animation, skinning, or morph-target data.
