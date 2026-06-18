# Volumetric Solid Transparency Notes

This documents the renderer changes made to fix analytic volumetric-solid
ordering while preserving the original refractive/glowing appearance.
The shared `VolumetricSolid` material path now supports sphere and capsule
`VolumetricComponent` shapes.

## Original Problems

Analytic `VolumetricSolid` effects had three separate transparency problems:

1. They rendered in the wrong transparent pass relative to particles and beams.
2. Multiple solids sorted by their screen-proxy quad depth instead of their real
   world-space solid position.
3. Once depth sorting worked, foreground solids fully erased background solids
   because the shader wrote an opaque scene-color composite.

The visible symptoms were:

- beams could appear in front of a solid even when the beam was behind it in 3D
- overlapping volumetric solids fought for foreground order
- a rear volumetric solid became completely invisible behind a front solid
- the first alpha fix made the solid look too flat by weakening shimmer,
  distortion, glow, and the crisp boundary

## Render-Pass Routing

`WaveVolume` and `VolumetricSolid` are now deliberately split:

- `WaveVolume` stays in the pre-particle transparent scene-sampling path.
- `VolumetricSolid` renders in the post-particle transparent bucket.

That keeps the wave volume from sampling explosion particles into its background
copy, while allowing analytic solids to compose after beams and particles when
they are in front of those effects.

Primary file:

- [forward.cpp](../src/rendering/renderer/backends/diligent/passes/forward.cpp)

## Sort Key

Volumetric solids render through a camera-facing screen-space proxy quad. The
proxy quad lives at a fixed overlay depth, so sorting by proxy transform depth
caused multiple solids to tie or sort incorrectly.

Transparent sorting now uses the analytic solid center for
`VolumetricSolid` materials:

- normal transparent meshes sort by mesh bounds / transform depth
- analytic volumetric solids sort by `MaterialRecord::volume_center`

This gives stable front-to-back perception between multiple solids.

Primary file:

- [forward.cpp](../src/rendering/renderer/backends/diligent/passes/forward.cpp)

## Compositing Fix

The old volumetric-solid shader computed a dense refractive/glowing
scene-color composite, then returned `base_alpha = 1.0`. That made a single
solid look strong, but it also made a foreground solid fully replace any
background solid already in the framebuffer.

The shader now:

- computes Beer-Lambert transmittance from the actual ray path length through
  the solid
- uses path-length opacity, with a rim boost, as the transparent alpha
- preserves the old-looking refractive/glowing result by solving for the source
  color that normal alpha blending needs
- restores a camera-ray rim/shimmer contribution so the solid keeps its crisp
  boundary, glow, shimmer, and distortion

In effect, a single solid should remain visually close to the previous
appearance, while overlapping solids no longer erase each other completely.

Primary file:

- [backend_init.cpp](../src/rendering/renderer/backends/diligent/backend_init.cpp)

## Current Limitations

This is still sorted alpha transparency, not full order-independent
transparency. It handles the known volumetric-solid cases much better, but the
renderer still does not have a shared transparency-depth/composition buffer for
all transparent effects.

Future work should consider:

1. A focused visual regression scene with two overlapping volumetric solids and
   beams crossing both in front and behind.
2. A deliberate transparent-effect composition strategy for beams, particles,
   `WaveVolume`, and `VolumetricSolid`.
3. Material-aware transparent pipeline selection for depth-test/depth-write
   behavior.
4. A specialized volume/composite pass if sorted alpha becomes too fragile.

## Validation Used

The affected examples were rebuilt:

```bash
cmake --build build --target \
  prefabs_gallery \
  effects_volumetric_sphere \
  prefabs_volumetric_sphere
```

The smoke checks used:

```bash
timeout 6s ./build/examples/effects/volumetric_sphere
timeout 6s ./build/examples/prefabs/volumetric_sphere
timeout 8s env KARMA_PARTICLE_STATS=1 KARMA_PREFAB_GALLERY_STATS=1 ./build/examples/prefabs/gallery
```

The examples are interactive, so timeout exit code `124` is expected when they
stay alive until the timeout.
