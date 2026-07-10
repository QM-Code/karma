# Animation Refactor Status

Status: stopping point reached on 2026-07-09.

## Completed

- FBX pivot helpers are evaluated by Assimp instead of entering the runtime
  hierarchy. The invalid helper-stack normalization shim was removed.
- Pose skeletons now preserve the complete imported scene-node hierarchy.
  `Skin` independently owns palette joint nodes and inverse bind matrices, so
  transform-bearing non-skin nodes are no longer collapsed out of rest space.
- Animation channels use an invalid node sentinel by default, resolve node
  targets before joint metadata, and reject out-of-range runtime targets.
- Semantic humanoid rigs retain their authored `HumanoidProfile`. Validation
  rejects missing requirements, duplicate/reused bindings, invalid indices,
  cyclic or disconnected hierarchies, undeclared semantics, and empty rigs.
- Retargeting converts position and scale keys relative to source/target rest
  transforms, validates explicit maps, keeps copied unmapped channels in source
  space, and clears source-space root motion on failure.
- Root translation scaling is isolated from root-motion mapping. A dedicated
  `Root` is used when it carries meaningful translation; otherwise locomotion
  scaling uses `Hips`. Height is derived from the current composed hierarchy via
  `humanoidRigHeight(...)`, not cached mutable state.
- Standalone `animation_clip` package entries import FBX clips, full pose
  skeletons, and semantic source rigs. Named clip selection now fails when the
  requested name is absent instead of silently choosing another clip.
- The asset cache is schema v3. Skeleton rest transforms, authored profiles,
  humanoid rigs, scene rig keys, and clip targeting metadata round-trip through
  cache blobs. Cache reads reject structurally invalid humanoid rigs.
- Package cache keys hash primary source contents and declared external
  `dependencies`. `AssetPackageHandle::restored_from_cache` makes cold versus
  warm behavior observable and testable.
- `animation_humanoid_rpg` uses the production semantic path for all nine
  packaged Mixamo clips. The app-side bind-pose, no-scale, name-remap, and
  scale-stripping diagnostic variants were removed.
- The resolved FBX deformation issue is documented in
  `HUMANOID_RPG_FBX_DEFORMATION.md` and removed from active known issues.

## Regression Coverage

`karma_animation_tests` now covers:

- explicit-map validation, rest-relative position/scale conversion, root-motion
  failure behavior, root-frame correction, and isolated root scaling;
- custom profiles, profile alias precedence, disconnected/invalid rigs, and
  copied unmapped channels;
- safe joint-only and out-of-range channel targeting;
- real checked-in `Character.fbx` plus standalone `stride.fbx` import;
- absence of Assimp FBX helper nodes, a one-root full pose hierarchy, and the
  52-joint skin palette;
- finite bind and animated palettes plus stable CPU-skinned bounds; and
- direct cache serialization plus asserted cold and warm package paths.

## Follow-Up Work

These are enhancements, not known regressions in the checked Mixamo path:

- Add production rigs from other exporters and substantially different rest
  poses to the corpus before adding more built-in semantic profiles.
- Define package-level authored profile files if custom profiles need to be
  selected from JSON; custom profiles are currently supplied through the C++
  API, while package `humanoid.profile` supports `mixamo`.
- Automate external glTF/FBX dependency discovery. Package authors currently
  declare external buffers and images explicitly in `dependencies`.
- Add GPU deformation image/bounds validation to CI for each graphical backend;
  current deterministic deformation assertions use the CPU reference path,
  with the GPU path covered by the interactive visual smoke test.
- Expand root-motion import/extraction fixtures. Runtime and retarget contracts
  are covered, but the checked standalone FBX clips currently animate Hips
  channels rather than authored `RootMotionTrack` payloads.

## Verification

```bash
cmake --build --preset headless --parallel 2
ctest --preset headless --output-on-failure

cmake --build --preset portable \
  --target animation_humanoid_rpg --parallel 2
./build/portable/examples/animation/humanoid_rpg
```

After integration with the latest upstream scene and rendering work, the full
headless suite passed 19/19 tests in 64.27 seconds. The portable humanoid target
also built successfully. The example was visually checked with semantic Stride
playback on the GPU path and against CPU reference deformation.

## GitHub Integration

On 2026-07-09, the local checkpoint was rebased onto `origin/main` at
`a4e0ab1`. The upstream scene bake/runtime implementation was retained while
the asset cache v3, full pose hierarchy, humanoid import, and FBX texture-path
changes were merged into the shared content pipeline. The combined package
cache content version is v8 so neither branch's stale cache entries are reused.

The integrated state passed the verification commands above before push.
