# Particle Asset Notes

This directory contains authored `.kpeffect` templates.

Authoring rules:

- Use stable texture aliases; prefab resource sidecars, packages, or example
  code must register them before the effect is instantiated.
- Keep emitter behavior in `.kpeffect`; keep composition and positioning in
  JSON prefabs.
- Use `local_space = true` for attached looping effects and `local_space =
  false` for detached smoke, debris, and explosions.
- Keep high particle counts intentional and documented. Stress-test assets are
  not necessarily gameplay-safe defaults.
- For flipbooks with gutters or borders, keep `atlas_frame_width`,
  `atlas_frame_height`, `atlas_border`, and `atlas_spacing` accurate.

After adding or changing an effect, build at least the relevant example target.
For broad validation:

```bash
cmake --build build --target karma_particle_example karma_prefab_gallery_example
```
