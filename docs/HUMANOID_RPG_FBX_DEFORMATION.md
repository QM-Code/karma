# Humanoid RPG FBX Deformation Postmortem

Status: resolved on 2026-07-09.

The `animation_humanoid_rpg` example originally rendered the correct Mixamo
`Character.fbx` and standalone animation files with severely folded and
stretched limbs. GPU and CPU reference deformation produced the same bad pose,
which correctly localized the fault to imported hierarchy and retarget data
rather than the renderer.

## Root Cause

Assimp preserved 96 FBX pivot/helper nodes named with the
`_$AssimpFbx$_` marker. The model consequently imported as 163 nodes instead of
67. Skeleton construction only accepted a direct joint parent, so helper nodes
between Mixamo bones made 49 of the 52 joints appear to be independent skeleton
roots.

Those disconnected joints no longer shared the local transform spaces assumed
by animation sampling and semantic retargeting. Rest-pose rotation correction,
humanoid height, and root translation scaling were therefore computed from an
invalid hierarchy. App-side name remaps and scale-key removal could obscure
parts of the symptom but could not make that hierarchy correct.

## Fix

- FBX import disables `AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS`. Assimp evaluates
  the pivot stack into ordinary node-local transforms instead of exposing
  interchange-only helper nodes to the runtime.
- Pose skeletons now preserve the complete imported node hierarchy. Skin palette
  joints and inverse bind matrices remain separate `Skin` data, so non-joint
  transform nodes are never collapsed out of animation rest space.
- Assimp animation channels retain their node targets. Retargeting applies
  rotation correction in rest-local space, converts position and scale keys
  from the source rest transform to the target rest transform, and leaves the
  result in the target skeleton's node space.
- Humanoid height is measured from composed rest-model joint positions along
  the rig's hips-to-head up axis. Unless explicitly overridden, the
  translation-bearing semantic root (`Root` or `Hips`) alone uses
  `target_height / source_height`; non-root translation remains in corrected
  rest-local space.

The fix is in the importer and retargeter. No RPG-specific node remap, broad
bone-name normalization, exact-name fallback clip, or scale-stripping shim is
part of the runtime.

## Regression Coverage

`testMixamoFbxRetargetingAndDeformation` in `tests/animation_tests.cpp` imports
the checked-in model and standalone `stride.fbx` through their real asset
packages. It asserts:

- a complete model-node pose hierarchy and no `_$AssimpFbx$_` helpers;
- exactly one pose-hierarchy root with valid parents, plus a 52-joint skin
  palette;
- a valid semantic retarget with no skipped channels;
- finite 52-joint bind and animated palettes; and
- stable CPU-skinned bounds for both the bind pose and the middle of `Stride`.

Run the regression with:

```bash
cmake --build --preset headless --target karma_animation_tests --parallel 2
ctest --preset headless -R karma_animation_tests --output-on-failure
```

## Visual Validation

The graphical example was validated with all nine packaged Mixamo clips. The
character keeps a recognizable, correctly connected silhouette during playback
on both GPU and CPU reference deformation; switching paths does not change the
pose.

```bash
cmake --build --preset portable --target animation_humanoid_rpg --parallel 2
./build/portable/examples/animation/humanoid_rpg
```

Use `P` and `N` or the clip list to change clips, `Space` to pause, and `G` to
compare GPU deformation with the CPU reference path.
