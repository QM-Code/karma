# Local Light Probe Bootstrap

This file is the handoff for the sample-side local-light probe work.

## Purpose

The probe sample is meant to do two jobs:

1. provide a stable, readable sanity check for local lights and point-light shadows
2. scale gradually into a stress path without making the default run dangerous

It should stay a validation scene first and a stress scene second.

## Reference File

- [light_stress_example.cpp](/home/irie/Documents/karma/examples/light_stress_example.cpp)

## Current Safe-Mode Behavior

Safe mode now does all of the following:

- spawns `1-16` shadow-casting point lights via `--lights N`
- gives each point light a matching colored marker sphere so the light position is obvious
- animates the point lights and marker spheres with orbit + bob motion
- keeps the demo world visible and shadow-casting
- keeps the white receiver spheres visible but non-shadow-casting
- uses separate darker offset casters so point-light shadows read clearly

Current user-facing options:

- default run: one moving shadowed point light
- `--lights N`: scale safe mode up gradually
- `--stats`: log Forward+ stats after startup
- `--unsafe`: switch to the dense non-shadowed stress layout

Environment variables:

- `KARMA_LIGHT_PROBE_STATS=1`
- `KARMA_LIGHT_STRESS_UNSAFE=1`

## Why The Scene Looks This Way

The scene layout is intentionally opinionated.

Important sample choices:

- the world mesh casts shadows again so the buildings behave like real scene geometry
- marker spheres follow the moving lights but do not cast shadows
- receiver spheres do not cast shadows
- separate nearby darker casters create readable shadow silhouettes

That split avoids the earlier failure mode where the same white probe spheres became both the visual marker and the dominant point-shadow occluder.

## Motion

Safe-mode lights move on purpose now.

The animation exists to make these issues easy to spot:

- stale point-shadow cache updates
- moving cutoff bands
- cubemap-face seam artifacts
- directional/local shadow interaction problems

Current motion characteristics:

- orbit + bob animation
- phase offset per light
- 2x speed multiplier relative to the first animated version

## Unsafe Mode

Unsafe mode remains a separate path.

It intentionally:

- uses a dense `14x14` light layout
- disables point-light shadows
- restores the aggressive Forward+ profile

Do not casually merge the safe and unsafe layouts back together.

## Validation

Commands used during this pass:

```bash
cmake --build build --target karma_light_stress_example -j4
./build/karma_light_stress_example --help
```

Useful runtime checks:

```bash
./build/karma_light_stress_example
./build/karma_light_stress_example --lights 9
./build/karma_light_stress_example --lights 16 --stats
```

Expected result:

- moving colored marker spheres track each point light
- point-light shadows stay attached to the moving lights
- buildings, casters, and ground all receive/cast shadows in safe mode
- no obvious low-rate shadow snapping while lights move

## Recommended Next Steps

Best next moves for the sample:

1. keep safe mode visually readable even if more renderer features are tested
2. only add more scene complexity if it improves diagnosis, not spectacle
3. if the sample needs another debug path, prefer temporary targeted switches and remove them after the renderer fix lands
