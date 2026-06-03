# Particle Public API Notes

This directory is the public particle API surface.

Keep this API small. Most gameplay code should use:

- `particles::createEffectEntity(...)`
- `particles::bindEffect(...)`
- `particles::restartEffect(...)`
- `particles::setEffectPlayback(...)`
- `ParticleLibrary` registration and lookup helpers

When adding fields:

- prefer reusable template fields in `.kpeffect`
- prefer `ParticleEffectOverrideComponent` only for common per-instance
  variation
- avoid exposing renderer implementation details in public helpers
- update `docs/PARTICLE_SYSTEM.md` and `NEXT_AGENT.md` when behavior or
  active handoff notes change

Do not add long-lived simulation state to public ECS components unless there is
a clear reason. Runtime particle state currently belongs to `ParticleSystem`.
