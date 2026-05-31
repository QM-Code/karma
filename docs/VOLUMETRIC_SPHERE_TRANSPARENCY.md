# Volumetric Sphere Transparency Notes

This documents the renderer changes made to fix analytic volumetric-sphere
ordering while preserving the original refractive/glowing appearance.

## Original Problems

Analytic `VolumetricSphere` effects had three separate transparency problems:

1. They rendered in the wrong transparent pass relative to particles and beams.
2. Multiple spheres sorted by their screen-proxy quad depth instead of their real
   world-space sphere position.
3. Once depth sorting worked, foreground spheres fully erased background spheres
   because the shader wrote an opaque scene-color composite.

The visible symptoms were:

- beams could appear in front of a sphere even when the beam was behind it in 3D
- overlapping volumetric spheres fought for foreground order
- a rear volumetric sphere became completely invisible behind a front sphere
- the first alpha fix made the sphere look too flat by weakening shimmer,
  distortion, glow, and the crisp boundary

## Render-Pass Routing

`WaveVolume` and `VolumetricSphere` are now deliberately split:

- `WaveVolume` stays in the pre-particle transparent scene-sampling path.
- `VolumetricSphere` renders in the post-particle transparent bucket.

That keeps the wave volume from sampling explosion particles into its background
copy, while allowing analytic spheres to compose after beams and particles when
they are in front of those effects.

Primary file:

- [forward.cpp](/home/irie/Documents/karma/src/rendering/renderer/backends/diligent/passes/forward.cpp)

## Sort Key

Volumetric spheres render through a camera-facing screen-space proxy quad. The
proxy quad lives at a fixed overlay depth, so sorting by proxy transform depth
caused multiple spheres to tie or sort incorrectly.

Transparent sorting now uses the analytic sphere center for
`VolumetricSphere` materials:

- normal transparent meshes sort by mesh bounds / transform depth
- analytic volumetric spheres sort by `MaterialRecord::volume_center`

This gives stable front-to-back perception between multiple spheres.

Primary file:

- [forward.cpp](/home/irie/Documents/karma/src/rendering/renderer/backends/diligent/passes/forward.cpp)

## Compositing Fix

The old volumetric-sphere shader computed a dense refractive/glowing
scene-color composite, then returned `base_alpha = 1.0`. That made a single
sphere look strong, but it also made a foreground sphere fully replace any
background sphere already in the framebuffer.

The shader now:

- computes Beer-Lambert transmittance from the actual ray path length through
  the sphere
- uses path-length opacity, with a rim boost, as the transparent alpha
- preserves the old-looking refractive/glowing result by solving for the source
  color that normal alpha blending needs
- restores a camera-ray rim/shimmer contribution so the sphere keeps its crisp
  boundary, glow, shimmer, and distortion

In effect, a single sphere should remain visually close to the previous
appearance, while overlapping spheres no longer erase each other completely.

Primary file:

- [backend_init.cpp](/home/irie/Documents/karma/src/rendering/renderer/backends/diligent/backend_init.cpp)

## Current Limitations

This is still sorted alpha transparency, not full order-independent
transparency. It handles the known volumetric-sphere cases much better, but the
renderer still does not have a shared transparency-depth/composition buffer for
all transparent effects.

Future work should consider:

1. A focused visual regression scene with two overlapping volumetric spheres and
   beams crossing both in front and behind.
2. A deliberate transparent-effect composition strategy for beams, particles,
   `WaveVolume`, and `VolumetricSphere`.
3. Material-aware transparent pipeline selection for depth-test/depth-write
   behavior.
4. A specialized volume/composite pass if sorted alpha becomes too fragile.

## Validation Used

The affected examples were rebuilt:

```bash
cmake --build build --target \
  karma_prefab_gallery_example \
  karma_volumetric_sphere_example \
  karma_volumetric_sphere_prefab_example
```

The smoke checks used:

```bash
timeout 6s ./build/karma_volumetric_sphere_example
timeout 6s ./build/karma_volumetric_sphere_prefab_example
timeout 8s env KARMA_PARTICLE_STATS=1 KARMA_PREFAB_GALLERY_STATS=1 ./build/karma_prefab_gallery_example
```

The examples are interactive, so timeout exit code `124` is expected when they
stay alive until the timeout.
