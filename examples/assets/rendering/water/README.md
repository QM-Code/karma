# Procedural Water Example Assets

This directory contains the example-owned material and HLSL for
`examples/rendering/water.cpp`. The renderer has no water-specific code: the
effect uses the same custom-material contract available to games.

The water mesh is a static four-vertex plane. `water_ps.hlsl` creates the
appearance from:

- opaque scene color for refraction;
- opaque scene depth for water thickness and automatic shorelines;
- domain-warped analytic swells plus two independently moving micro-normal
  samples for the perceived surface normal;
- spectral Beer-Lambert absorption and shallow/deep in-scattering;
- bottom-anchored caustics and broken shoreline breaker/backwash foam;
- energy-aware dielectric Fresnel reflection and GGX directional-light glints;
- depth-discontinuity guards that fade refraction at shore and object edges.

The example creates its seamless 256x256 micro-normal texture at startup from a
periodic Fourier spectrum, so it has no external texture dependency. Foam,
caustics, broad waves, flow and optical depth remain procedural. For authored
rivers, the procedural flow direction can be replaced with an RG flow-map
sample while keeping the depth, lighting, and shoreline portions unchanged.

The seven `material_paramsN` vectors are packed as follows:

| Vector | Components |
| --- | --- |
| `0` | shallow RGB, clarity |
| `1` | deep RGB, absorption density |
| `2` | foam RGB, foam width |
| `3` | wave scale, strength, speed, flow angle |
| `4` | refraction, roughness, Fresnel power, reflection strength |
| `5` | foam amount, caustics, depth range, fine detail |
| `6` | flow speed, sun glint, debug view, foam enabled |

Build and run with ImGui enabled:

```sh
cmake --preset portable -DKARMA_ENABLE_IMGUI=ON
cmake --build --preset portable --target rendering_water
./build/portable/examples/rendering/water
```
