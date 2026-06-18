# Animation Runtime {#karma_animation_guide}

Karma animation is split across content import, ECS components, simulation
sampling, scene transform composition, and renderer upload/submission.

For the durable architecture overview, asset-boundary explanation, showcase
commands, and retargeting notes, see `docs/ANIMATION_V2.md`.

## Imported glTF Flow

`karma::scene::loadGltfScenePrefab(...)` imports glTF nodes, transforms, skins,
skeletons, animation clips, and mesh primitive data. `instantiateGltfScenePrefab(...)`
creates ECS entities for the node tree and separate child entities for renderable
mesh primitives.

Renderable primitive entities may receive:

- `karma::components::MeshComponent` for renderer mesh/material binding.
- `karma::components::DeformableMeshComponent` when the primitive has skin data,
  morph target deltas, or authored morph weights.

Imported roots receive `karma::components::AnimatorComponent` when clips are
available. The animator stores the imported clips, node entity map, and
node-to-morph-primitive map needed for runtime sampling.

Imported animation clips are plain `karma::animation::AnimationClip` values.
They are copied from `GltfScenePrefab::animations` into
`AnimatorComponent::clips`; they do not retain file handles, renderer handles,
or mesh ownership. They remain authored in the imported node/skeleton index
space until retargeted.

## Runtime Update Order

The engine-owned frame order is:

1. `karma::animation::AnimationSystem` samples clips and writes local transforms
   plus morph weights.
2. `karma::scene::updateWorldTransforms(...)` composes local transforms into
   final world `TransformComponent` values.
3. `karma::animation::DeformationSystem` builds joint palettes, updates
   renderer-owned deformation resources, and uploads CPU-deformed meshes only
   when the CPU reference path is explicitly selected.
4. `karma::renderer::RenderSystem` submits visible meshes and deformation
   resource handles to the renderer.

The animation system is renderer-agnostic. It writes ECS data only. Mesh buffer
updates are isolated to the deformation upload stage.

## Deformation

Imported skinned and morphed meshes use GPU deformation by default. The
renderer consumes the bind mesh plus a `karma::renderer::DeformationId` that
references joint palette and morph-weight resources owned by the backend.

CPU skinning remains available through `karma::animation::skinMesh(...)` and
CPU morphing through `karma::animation::morphMesh(...)`. Runtime CPU mesh
uploads are selected with `karma::components::DeformationPath::CpuReference`
and are intended for tests and diagnostics.

## Morph Targets

Morph target deltas are stored on `karma::geometry::MeshData`. Runtime weights
live on `karma::components::DeformableMeshComponent`.

`AnimationSystem` samples glTF `weights` channels into morph components. If the
active clip has no morph track for a node, the component returns to its authored
base weights. For blended states, morph weights are blended with the same clip
weights used for transform sampling.

Morph deformation is GPU-first in the runtime path. CPU deformation remains as
a reference path for validation and diagnostics.

## Retargeting

Karma supports explicit skeleton retargeting through
`karma::animation::SkeletonMap`, `SkeletonMapEntry`, `RetargetOptions`,
`validateSkeletonMap(...)`, and `retargetClip(...)`.

Retargeting converts a source clip from the source skeleton's joint/node index
space into the target skeleton's joint/node index space. The map is explicit:
Karma does not infer humanoid semantics or profile names yet.

This keeps the runtime clean for future reusable humanoid animation libraries:
clips, skeletons, node bindings, and mesh deformation are separate data
contracts, but a higher-level humanoid profile/binding layer still needs to be
authored above the current explicit maps.

## Current Limits

- Humanoid semantic retarget profiles are not implemented; retargeting uses
  explicit skeleton maps.
- `MeshComponent::mesh_key` flat mesh loading does not use the glTF scene
  animation, skinning, or morph-target path.
