# Rigged GLB Authoring

This is the expected authoring path for Karma rigged-animation validation assets.

## Blender Export

- Use Blender units at 1 unit = 1 meter.
- Apply object scale before export.
- Keep the armature and mesh in the same exported scene.
- Name the armature, mesh object, bones, and animation actions clearly; Karma uses
  node and joint names for diagnostics and fallback mapping.
- Export as `glTF 2.0` binary `.glb`.
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
  key values become `MorphTargetComponent::base_weights`.
- Animate shape key weights on the mesh node when a clip should drive facial,
  corrective, or other morph deformation.

## Runtime Flow

- Imported roots receive an `AnimatorComponent` when clips exist.
- Imported renderable primitives receive `MorphTargetComponent` when their GLB
  primitive has morph target deltas.
- Animation sampling writes local node transforms and runtime morph weights.
- Scene hierarchy composition writes final world transforms after animation.
- Mesh deformation applies morph targets before skinning.
- GPU skinning remains the default path for skinned primitives whose joint
  palette fits the renderer limit.

## Supported Runtime Data

- Multiple clips.
- Multiple skins and skeletons.
- Joint names, joint parent indices, joint node indices, inverse bind matrices.
- Node, joint, and morph-weight animation channel mappings.
- CPU skinning fallback with shared `geometry::MeshData` joint/weight payloads retained.
- Renderer-facing joint indices and weights for GPU skinning.
- GPU skinning through the Diligent forward, transparent, depth prepass, and
  shadow paths.
- CPU skinning fallback and the public `skinMesh(...)` helper for tests and
  correctness checks.
- Morph target position, normal, and tangent deltas on imported GLB primitives.
- Runtime morph weights through `MorphTargetComponent`; morph deformation is
  applied on CPU before skinning.
- Animator state machines with clip states, 1D blend trees, transitions,
  conditions, triggers, events, and root-motion deltas.

## Current Gaps

- Retargeting between skeletons is not implemented.
- glTF sparse accessors and external `.bin` buffers are not imported by the
  explicit GLB metadata reader.
- Morph deformation currently updates CPU mesh buffers before GPU skinning; a
  pure GPU morph path is not implemented yet.
- Animator transition interrupt policy is still shallow compared with the rest
  of the state machine model.

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
