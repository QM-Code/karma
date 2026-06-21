# Animation V2 Architecture

Karma animation v2 treats glTF/GLB files as import containers, not as runtime
animation owners. Import turns authored data into engine data structures:
renderer-agnostic clips, skeleton metadata, ECS node bindings, deformable mesh
state, and renderer deformation resources.

The important boundary is:

```text
glTF/GLB file
  -> assets.package.json gltf_scene entry
  -> GltfSceneAsset + registered child assets
  -> ECS entities + AnimatorComponent + DeformableMeshComponent
  -> AnimationSystem + DeformationSystem + RenderSystem
```

After import, animation playback does not need to read the source file again.

## Public Import API

Declare the source in an asset package and instantiate the registered scene
asset:

```cpp
karma::assets::AssetRegistry assets;
std::string diagnostic;
auto package =
    karma::assets::importAssetPackage(assets, "assets/character", &diagnostic);
if (!package.has_value()) {
  return;
}

const karma::assets::GltfSceneAsset* character =
    assets.findGltfSceneAsset("characters/hero");
if (character == nullptr) {
  return;
}

auto imported = karma::world::instantiateGltfSceneAsset(
    *world,
    *scene,
    assets,
    *character,
    {.create_synthetic_root = true});
```

The package manifest owns source options:

```json
{
  "version": 1,
  "assets": [
    {
      "type": "gltf_scene",
      "key": "characters/hero",
      "path": "character.gltf",
      "import_lights": false
    }
  ]
}
```

The API accepts binary `.glb` and JSON `.gltf` sources. JSON `.gltf` sources may
use external buffers, embedded buffers, data URIs, and sparse accessors.
External image and buffer paths are resolved relative to the source `.gltf`.

## Imported Data

`karma::assets::GltfSceneAsset` is the package-imported scene metadata. It
contains:

- `nodes`: glTF node names, local/world transforms, lights, and renderable mesh
  primitive asset keys.
- `animation_clip_keys`: registered renderer-agnostic
  `karma::world::AnimationClip` assets.
- `skeleton_keys` and `skin_keys`: registered joint topology and skin binding
  assets used by skinned primitives.
- `mesh_asset_keys`, `texture_asset_keys`, and `material_keys`: deterministic
  runtime asset keys produced during package import.

`GltfSceneImportResult` contains the ECS binding created from a registered
scene asset:

- `root_entity`: the top-level imported entity.
- `node_entities_by_index`: imported glTF node index to ECS entity.
- `morph_entities_by_node_index`: imported node index to renderable primitive
  entities that receive morph weights.

Those index maps are what make clips independent from the source file while
still targetable at runtime.

## Clip Ownership

`karma::world::AnimationClip` is plain engine data. A clip has a name,
duration, transform channels, morph target tracks, optional events, and optional
root-motion data. It does not hold a GLB handle, renderer handle, mesh pointer,
or file path.

Imported clips are registered in `karma::assets::AssetRegistry` and copied into
`karma::components::AnimatorComponent::clips` during scene asset instantiation.
The animator also receives the node and morph binding maps needed to apply clip
channels to the current ECS instance.

That means a clip is cleanly separated from the imported file bytes, but it is
still authored in a particular skeleton/node index space until it is retargeted.

## Runtime Components

`AnimatorComponent` is the single public animation playback surface. It owns:

- clip playback state for simple clip playback
- state machine data
- 1D blend trees
- transition runtime state and interruption policy
- event queue
- root-motion mode and accumulated root-motion data
- imported skeleton and skin metadata
- imported node and morph binding maps

`AnimationEventBufferComponent` is the frame-stable gameplay-facing event
buffer. `AnimationSystem` mirrors animator events into it each frame.

`RootMotionComponent` is the gameplay-facing root-motion buffer. In
`ExposeDelta` mode, deltas remain available until gameplay code calls
`consumeRootMotionDelta(...)`. In `ApplyToLocalTransform` mode, sampled root
motion is applied to the local transform.

`DeformableMeshComponent` is the single public deformation component for
skinned and morphed renderable primitives. It stores bind mesh data, skin
influences, joint entities, inverse bind matrices, morph weights, and the
renderer-owned `DeformationId`.

## Frame Order

Engine-owned animation update order is:

1. `AnimationSystem` samples the active clip or animator state machine.
2. Transform channels write `LocalTransformComponent` values on imported node
   entities.
3. Morph-weight channels write `DeformableMeshComponent::morph_weights` on
   renderable primitive entities.
4. Animation events and root-motion deltas are mirrored to their gameplay
   buffer components.
5. `world::updateWorldTransforms(...)` composes final world transforms.
6. `DeformationSystem` builds joint palettes and updates renderer deformation
   resources.
7. `RenderSystem` submits visible meshes and `DeformationId` handles.

The animation system is renderer-agnostic. Renderer uploads happen in the
deformation stage.

## GPU Deformation

GPU deformation is the default runtime path for skinned or morphed glTF
primitives.

The renderer owns deformation resources:

- `karma::rendering::DeformationId`
- `karma::rendering::DeformationDesc`
- `GraphicsDevice::createDeformation(...)`
- `GraphicsDevice::updateDeformation(...)`
- `GraphicsDevice::destroyDeformation(...)`

`DrawItem` references a `DeformationId`. It does not carry a per-draw vector of
joint matrices. Diligent stores joint palettes and morph weights in GPU buffers,
so the public renderer API no longer has the previous fixed 128-joint constant
buffer limit.

CPU helpers remain available for tests and diagnostics:

- `karma::world::skinMesh(...)`
- `karma::world::morphMesh(...)`

Runtime CPU deformation is selected only with
`karma::components::DeformationPath::CpuReference`.

## Retargeting

Retargeting is explicit and data-driven. Karma does not infer humanoid bone
semantics yet.

Use:

- `karma::world::Skeleton`
- `karma::world::SkeletonMap`
- `karma::world::SkeletonMapEntry`
- `karma::world::RetargetOptions`
- `karma::world::validateSkeletonMap(...)`
- `karma::world::retargetClip(...)`

The map connects source joint indices to target joint indices and can include a
rest-pose correction matrix per mapped joint. Retargeting writes a new
`AnimationClip` in the target skeleton's joint/node index space.

For now, assigning an animation from one humanoid to another requires a
SkeletonMap. A future generic humanoid layer should add:

- a named humanoid profile for common joints
- per-rig profile binding and validation
- rest-pose normalization policy
- root-motion source and scale policy
- an animation library asset format that stores clips separately from character
  model prefabs

The current architecture is ready for that layer because clips, skeletons, and
mesh deformation are already separate engine data contracts.

## Animation Showcase

The graphical showcase is:

```bash
cmake --build build --target animation_gltf --parallel
./build/examples/animation/gltf
```

By default it loads:

```text
examples/assets/animation_model/source/dustbound_wayfarer_merged_animations.glb
```

Pass another `.glb` or `.gltf` path as the first argument:

```bash
./build/examples/animation/gltf path/to/character.gltf
```

The ImGui panel exposes clip selection, play/pause/restart, timeline scrubbing,
crossfade duration, auto-cycle transitions, GPU versus CPU reference
deformation, root-motion mode, root-motion source node, deformation stats, root
motion deltas, and animation event output.

## Validation

Focused headless validation:

```bash
cmake --build build --target karma_animation_tests karma_rendering_tests --parallel
ctest --test-dir build -R 'karma_animation_tests|karma_rendering_tests' --output-on-failure
```

Graphical smoke targets:

```bash
cmake --build build --target scene_gltf_import animation_gltf --parallel
./build/examples/animation/gltf
```

The animation tests cover clip sampling, interpolation, looping, events,
animator playback, transitions, interruption policies, root motion, morph
weights, CPU reference deformation, explicit skeleton retargeting, external
`.gltf` buffers, data URIs, and sparse accessors.

## Current Limits

- Humanoid semantic profiles are not implemented.
- Retargeting requires explicit source-joint to target-joint maps.
- Imported cameras are not part of the glTF scene importer yet.
- Flat `MeshComponent::mesh_asset_key` remains a registered static mesh key. Use
  a package `mesh` entry for static mesh sources; that path does not instantiate
  animation, skeleton, or morph runtime data.
