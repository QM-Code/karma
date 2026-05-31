# Particle Runtime Notes

This directory owns particle effect parsing and CPU simulation.

Before changing behavior, read:

- `docs/PARTICLE_SYSTEM.md`
- `docs/PARTICLE_SYSTEM_ANALYSIS.md`
- `docs/PARTICLE_PERF_BOOTSTRAP.md`
- `include/karma/world/components/particle_emitter.h`

Rules for changes:

- Keep `.kpeffect` fields, `ParticleEmitterComponent`, renderer batch fields,
  and docs in sync.
- Preserve hot reload behavior through `ParticleLibrary::version()`.
- Preserve restart behavior through `ParticleEffectComponent::restart_count`.
- Measure before replacing simulation architecture; current known hot spots are
  alpha/distortion sorting and CPU simulation.
- Do not move beam-authored particles onto the simulated path without visual
  validation.

Common validation targets:

```bash
cmake --build build --target karma_particle_example karma_explosion_stress_example karma_prefab_gallery_example
```
