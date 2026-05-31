# Prefab Runtime Notes

This directory owns prefab parsing, instantiation, playback, restart, and
package registration.

Particle-specific guidance:

- Particle entries should continue routing through `particles::createEffectEntity(...)`.
- Keep particle prefab fields aligned with `ParticleEffectBindingDesc` and
  `ParticleEffectOverrideComponent`.
- Do not duplicate full `.kpeffect` emitter authoring inside `.kprefab`.
- If adding particle parameter bindings, prefer small override-scale fields
  before exposing raw emitter fields.
- Keep package cleanup explicit until a scoped registration handle exists.

Relevant docs:

- `docs/EFFECT_PREFABS.md`
- `docs/PARTICLE_SYSTEM_ANALYSIS.md`
- `docs/EXPLOSION_PREFAB.md`
