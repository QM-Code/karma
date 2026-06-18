# Particle Runtime Notes

This directory owns particle effect parsing and CPU simulation.

Before changing behavior, read:

- `docs/NEXT_AGENT.md`
- `docs/PARTICLE_SYSTEM.md`
- `docs/EXPLOSION_STRESS_PERF.md`
- `include/karma/world/components/particle_emitter.h`

Rules for changes:

- Keep `.kpeffect` fields, `ParticleEmitterComponent`, renderer batch fields,
  and docs in sync.
- Preserve asset invalidation behavior through `content::AssetRegistry::version()`.
- Preserve restart behavior through `ParticleEffectComponent::restart_count`.
- Measure before replacing simulation architecture; current known hot spots are
  alpha/distortion sorting and CPU simulation.
- Do not move beam-authored particles onto the simulated path without visual
  validation.

Common validation targets:

```bash
cmake --build build --target particles_billboard particles_explosion_stress prefabs_gallery
```
