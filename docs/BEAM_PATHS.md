# Beam Prefabs

Karma supports two beam-style prefab approaches:

- `ParticleBeamComponent`: continuous textured runtime ribbons submitted by
  `ParticleSystem`.
- `VolumetricComponent` capsules and spheres: analytic solid beams with radial
  falloff and screen-space distortion.

Use `ParticleBeamComponent` for fire rays, magic-missile trails, energy streaks,
and other effects that should read as one connected textured strip. Use
volumetric solids when the beam needs analytic volume, heavier refraction, or
sphere/capsule body composition.

## Particle Beams

A particle beam is authored on an entity with `TransformComponent` and
`ParticleBeamComponent`. The component stores local path points, render state,
width/color taper, texture key, UV repeat, UV scroll speed, and a restart count.

Prefab component shape:

```json
{
  "ParticleBeamComponent": {
    "enabled": true,
    "visible": true,
    "layer": 0,
    "depth_test": true,
    "blend_mode": "additive",
    "texture_key": "generated/fire_ray/beam_core",
    "local_path_points": [[0, 0, 0], [0, 0, -5]],
    "start_width": 0.35,
    "end_width": 0.18,
    "start_color": [1.0, 0.78, 0.24, 0.95],
    "end_color": [1.0, 0.18, 0.04, 0.0],
    "edge_softness": 0.15,
    "uv_repeat": 4.0,
    "uv_scroll_speed": 2.0,
    "time_scale": 1.0,
    "restart_count": 0
  }
}
```

Validation requires at least two finite path points and positive finite widths.
`blend_mode` supports textured additive and alpha ribbons.

At runtime, `ParticleSystem` submits each enabled beam to the renderer as
`rendering::ParticleBeamGpuDesc`. The Diligent backend expands every path segment
into camera-facing quads, draws beams after transparent pre-particle scene
content, then draws normal particle passes over the top.

## Generated Beams

The generator presets `fire_ray` and `magic_missile` create beam-prefab packages:

```bash
./build/karma_particle_effect_generate fire_ray.kpspec.json generated/fire_ray
KARMA_PARTICLE_STATS=1 ./build/examples/particles/generated_preview generated/fire_ray
```

The beam core lives in `prefab.json`; secondary sparks, smoke, halos, heat, and
impacts live in generated `.kpeffect` emitters.

## Volumetric Beam Prefabs

Existing volumetric beam prefabs still use one capsule per path segment plus one
sphere at each path vertex. The volumetric material derives the bright core and
colored outer glow from radial falloff inside each analytic solid:

- `color` tints the outer glow.
- `emissive_color` tints the hot center.
- `density`, `scattering`, `distortion_strength`, and `noise_strength` tune the
  body, glow, screen-space warp, and shimmer.

Lights remain normal prefab `LightComponent` entities where authored.
