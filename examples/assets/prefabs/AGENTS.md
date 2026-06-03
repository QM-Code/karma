# Prefab Asset Notes

This directory contains authored JSON prefab bundles.

For particle-heavy prefabs:

- Keep `prefab.json` responsible for entity composition, scene hierarchy,
  component data, and playback defaults.
- Keep `.kpeffect` responsible for emitter tuning.
- Prefer enabled/playing defaults plus `ParticleEmitterComponent.start_delay`
  for direct-load one-shot staged effects.
- Save separate JSON prefabs for data variants, or instantiate and modify
  components in C++.
- If a prefab needs texture aliases or effect registration for direct path-only
  instantiation, add `prefab.resources.json` beside `prefab.json`.

Reference particle-heavy effects:

- `examples/assets/prefabs/explosion/prefab.json`
- `examples/assets/prefabs/energy_orb/prefab.json`
- `docs/EXPLOSION_PREFAB.md`
