# Diligent Pass Notes

This directory contains renderer pass implementations, including particle
resource setup and particle draw execution.

Particle-specific files:

- `particles.cpp`: particle shaders, PSOs, SRBs, fallback resources, and
  half-resolution alpha resources.
- `particle_draw.cpp`: particle grouping, sorting, upload, draw submission, and
  particle pass stats.

Particle optimization guidance:

- Use existing `ParticlePassStats` fields before changing algorithms.
- Treat additive, alpha, and distortion paths separately; they have different
  correctness and sorting requirements.
- Keep exact sorting as a fallback when testing approximate or bucketed sorting.
- Avoid growing `ParticleBatchGroupKey` further without considering a particle
  material/state ID.
- Do not casually merge `Baked` and `Simulated` presentation paths; the split is
  intentional.

Relevant docs:

- `docs/PARTICLE_SYSTEM_ANALYSIS.md`
- `docs/PARTICLE_PERF_BOOTSTRAP.md`
- `docs/EXPLOSION_STRESS_PERF.md`
