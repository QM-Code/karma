# Prefab Runtime Notes

This directory owns JSON prefab serialization, instantiation, destruction, and
package registration.

Particle-specific guidance:

- Particle prefab nodes should serialize `ParticleEffectComponent`,
  `ParticleEmitterComponent`, and `ParticleEffectOverrideComponent` data.
- Do not duplicate full `.kpeffect` emitter authoring inside `prefab.json`.
- Keep package cleanup explicit until a scoped registration handle exists.

Relevant docs:

- `docs/EFFECT_PREFABS.md`
- `docs/PARTICLE_SYSTEM_ANALYSIS.md`
- `docs/EXPLOSION_PREFAB.md`
