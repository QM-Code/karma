# Animation V2 Architecture

Karma animation v2 treats authored scene and animation files as import
containers, not as runtime animation owners. Import turns glTF/GLB scenes, FBX
models, and standalone FBX clips into engine data structures: renderer-agnostic
clips, skeleton and humanoid metadata, ECS node bindings, deformable mesh state,
and renderer deformation resources.

The important boundary is:

```text
authored scene or clip file
  -> assets.package.json gltf_scene or animation_clip entry
  -> GltfSceneAsset and/or registered AnimationClip, Skeleton, and HumanoidRig assets
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
      "import_lights": false,
      "humanoid": {
        "profile": "mixamo",
        "rig_key": "characters/hero/rig"
      }
    }
  ]
}
```

The Assimp-backed scene path accepts binary `.glb`, JSON `.gltf`, and FBX model
sources. JSON `.gltf` sources may use external buffers, embedded buffers, data
URIs, and sparse accessors. External image and buffer paths are resolved
relative to the source `.gltf`.

For FBX sources, pivot stacks are evaluated into ordinary node-local transforms
instead of becoming runtime helper nodes. Imported pose skeletons preserve the
complete scene-node hierarchy, including transform-bearing non-skin nodes;
`Skin` separately owns its palette joint nodes and inverse bind matrices.

Standalone animation files use an `animation_clip` entry. This registers the
selected clip without creating a model or renderable scene. `clip` optionally
selects a source animation by name; otherwise the first animation is used.
`name` optionally replaces its display name. A `humanoid` block also imports and
binds the source skeleton so the clip can be retargeted later:

```json
{
  "type": "animation_clip",
  "key": "animations/walk",
  "path": "walk.fbx",
  "name": "Walk",
  "humanoid": {
    "profile": "mixamo",
    "rig_key": "animations/walk/rig"
  }
}
```

## Imported Data

`karma::assets::GltfSceneAsset` is the package-imported scene metadata. It
contains:

- `nodes`: imported node names, local/world transforms, lights, and renderable mesh
  primitive asset keys.
- `animation_clip_keys`: registered renderer-agnostic
  `karma::world::AnimationClip` assets.
- `skeleton_keys` and `skin_keys`: registered full pose topology and separate
  skin-palette binding assets used by skinned primitives.
- `humanoid_rig_keys`: registered semantic bindings created when the package
  requests a built-in humanoid profile.
- `mesh_asset_keys`, `texture_asset_keys`, and `material_keys`: deterministic
  runtime asset keys produced during package import.

`GltfSceneImportResult` contains the ECS binding created from a registered
scene asset:

- `root_entity`: the top-level imported entity.
- `node_entities_by_index`: imported source node index to ECS entity.
- `morph_entities_by_node_index`: imported node index to renderable primitive
  entities that receive morph weights.

Those index maps are what make clips independent from the source file while
still targetable at runtime.

## Clip Ownership

`karma::world::AnimationClip` is plain engine data. A clip has a name,
duration, transform channels, morph target tracks, optional events, and optional
root-motion data. It does not hold a source-file handle, renderer handle, mesh
pointer, or file path.

Imported clips are registered in `karma::assets::AssetRegistry`. Clips owned by
a scene asset are copied into `karma::components::AnimatorComponent::clips`
during scene instantiation, along with the node and morph binding maps needed to
apply them to that ECS instance. Standalone `animation_clip` assets remain
registry values until gameplay copies or retargets them into an animator.

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
- imported humanoid rig metadata
- imported node and morph binding maps

`AnimationEventBufferComponent` is the frame-stable gameplay-facing event
buffer. `AnimationSystem` mirrors animator events into it each frame.

`RootMotionTrack` stores local transform samples for its target node. Retargeting
converts those samples into the mapped target node's rest frame before runtime
deltas are calculated. `RootMotionComponent` is the gameplay-facing root-motion buffer. In
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
2. Transform channels write local values on `TransformComponent` instances
   attached to imported node entities.
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

GPU deformation is the default runtime path for imported skinned or morphed
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

Retargeting is explicit and data-driven. The low-level path uses authored
source-joint to target-joint mappings:

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

The semantic path provides a built-in Mixamo profile through
`HumanoidProfileKind::Mixamo` and `builtinHumanoidProfile(...)`. Use
`bindHumanoidRig(...)` and `validateHumanoidRig(...)` to bind profile semantics
to an imported skeleton, then `buildHumanoidSkeletonMap(...)` or
`retargetHumanoidClip(...)` to transfer a clip. Package `humanoid` blocks perform
the binding during import and register the resulting `HumanoidRig`.

Semantic retargeting computes rotation correction from each source and target
joint's rest-local rotation. Position and scale keys are converted from the
source rest-local transform to the target rest-local transform. Humanoid height
is measured from composed rest-model joint positions along the hips-to-head up
axis and is available through `humanoidRigHeight(...)`. A dedicated semantic
`Root` remains the root-motion mapping when both rigs bind it. Translation
scaling uses that node only when it contains meaningful position motion;
otherwise it uses `Hips`. The default scale is the computed target/source
height ratio. `HumanoidRetargetOptions` can disable that derivation or supply an
explicit root scale policy.

Each `HumanoidRig` stores its authored profile, so custom profiles use the same
semantic retarget path. Explicit `SkeletonMap` values remain the lower-level
path for direct joint mapping. Clips, skeletons, semantic bindings, and mesh
deformation remain separate engine data contracts.

## Animation Showcase

The graphical showcase is:

```bash
cmake --build --preset portable --target animation_gltf --parallel 2
./build/portable/examples/animation/gltf
```

By default it loads the package:

```text
examples/assets/animation/dustbound_wayfarer/assets.package.json
```

Pass another asset-package directory or manifest as the first argument and its
scene asset key as the optional second argument:

```bash
./build/portable/examples/animation/gltf path/to/assets.package.json characters/hero
```

The ImGui panel exposes clip selection, play/pause/restart, timeline scrubbing,
crossfade duration, auto-cycle transitions, GPU versus CPU reference
deformation, root-motion mode, root-motion source node, deformation stats, root
motion deltas, and animation event output.

## Validation

Focused headless validation:

```bash
cmake --build --preset headless --target karma_animation_tests karma_rendering_tests --parallel 2
ctest --preset headless -R 'karma_animation_tests|karma_rendering_tests' --output-on-failure
```

Graphical smoke targets:

```bash
cmake --build --preset portable --target scene_gltf_import animation_gltf animation_humanoid_rpg --parallel 2
./build/portable/examples/animation/gltf
./build/portable/examples/animation/humanoid_rpg
```

The animation tests cover clip sampling, interpolation, looping, events,
animator playback, transitions, interruption policies, root motion, morph
weights, CPU reference deformation, explicit skeleton retargeting, external
`.gltf` buffers, data URIs, sparse accessors, built-in Mixamo binding, and real
FBX model/standalone-clip retargeting and deformation.

## Current Limits

- Mixamo is the only built-in humanoid semantic profile. Other naming
  conventions require an authored `HumanoidProfile` or explicit `SkeletonMap`.
- Imported cameras are not part of the glTF scene importer yet.
- Flat `MeshComponent::mesh_asset_key` remains a registered static mesh key. Use
  a package `mesh` entry for static mesh sources; that path does not instantiate
  animation, skeleton, or morph runtime data.
