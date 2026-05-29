# Prefab Asset Notes

This directory contains authored `.kprefab` bundles.

For particle-heavy prefabs:

- Keep `.kprefab` responsible for composition, transforms, parameters, and
  playback defaults.
- Keep `.kpeffect` responsible for emitter tuning.
- Prefer disabled/non-playing defaults for one-shot staged effects that are
  triggered by a controller.
- Add parameters only for high-value variation; avoid turning prefabs into a
  second emitter format.
- If a prefab needs generated textures or effect registration, use a
  `PrefabRegistry` package rather than direct path-only instantiation.

Reference staged effect:

- `examples/assets/prefabs/explosion/prefab.kprefab`
- `docs/EXPLOSION_PREFAB.md`
