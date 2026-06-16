# Animation Runtime {#karma_animation_guide}

Karma animation is split across content import, ECS components, simulation
sampling, scene transform composition, and renderer upload/submission.

## Imported GLB Flow

`karma::scene::loadGlbScenePrefab(...)` imports GLB nodes, transforms, skins,
skeletons, animation clips, and mesh primitive data. `instantiateGlbScenePrefab(...)`
creates ECS entities for the node tree and separate child entities for renderable
mesh primitives.

Renderable primitive entities may receive:

- `karma::components::MeshComponent` for renderer mesh/material binding.
- `karma::components::SkinnedMeshComponent` when the primitive has skin data.
- `karma::components::MorphTargetComponent` when the primitive has morph target
  deltas or default morph weights.

Imported roots receive `karma::components::AnimatorComponent` when clips are
available. The animator stores the imported clips, node entity map, and
node-to-morph-primitive map needed for runtime sampling.

## Runtime Update Order

The engine-owned frame order is:

1. `karma::animation::AnimationSystem` samples clips and writes local transforms
   plus morph weights.
2. `karma::scene::updateWorldTransforms(...)` composes local transforms into
   final world `TransformComponent` values.
3. `karma::animation::CpuSkinningSystem` applies CPU morph deformation, builds
   skinning palettes, and uploads CPU-deformed meshes when required.
4. `karma::renderer::RenderSystem` submits visible meshes and GPU skinning
   palettes to the renderer.

The animation system is renderer-agnostic. It writes ECS data only. Mesh buffer
updates are isolated to the deformation upload stage.

## Skinning

Imported skinned meshes use GPU skinning by default when their joint palette
fits `karma::components::kMaxSkinningJointsPerDraw`. The renderer consumes the
bind or morphed-bind mesh plus the per-draw joint palette.

CPU skinning remains available through `karma::animation::skinMesh(...)` and
`karma::components::SkinningPath::Cpu`. It is intended for tests, diagnostics,
and fallback behavior when the GPU path cannot draw a mesh directly.

## Morph Targets

Morph target deltas are stored on `karma::renderer::MeshData`. Runtime weights
live on `karma::components::MorphTargetComponent`.

`AnimationSystem` samples glTF `weights` channels into morph components. If the
active clip has no morph track for a node, the component returns to its authored
base weights. For blended states, morph weights are blended with the same clip
weights used for transform sampling.

Morph deformation currently happens on the CPU before skinning. For a skinned
mesh that still uses GPU skinning, the renderer mesh is updated to the morphed
bind pose and the renderer applies the joint palette on the GPU.

## Current Limits

- Retargeting clips between different skeletons is not implemented.
- glTF sparse accessors and external `.bin` buffers are not imported by the
  explicit GLB metadata reader.
- Morph targets do not yet have a pure GPU deformation path.
- `MeshComponent::mesh_key` flat mesh loading does not use the GLB scene
  animation, skinning, or morph-target path.
