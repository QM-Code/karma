# Volumetric Beam Prefabs

Karma beam visuals are authored as prefab-owned volumetric solids. A beam prefab
owns scene composition in `prefab.json` and uses `VolumetricComponent` capsule
and sphere child nodes for the persistent beam body.

Current beam prefabs use one capsule per path segment plus one sphere at each
path vertex. The volumetric material derives the white-hot core and colored
outer glow from radial falloff inside each analytic solid:

- `color` tints the outer glow.
- `emissive_color` tints the hot center.
- `density`, `scattering`, `distortion_strength`, and `noise_strength` tune the
  body, glow, screen-space warp, and shimmer.

Point spheres at path vertices recreate the old endpoint and bend flares without
particles. Lights remain normal prefab `LightComponent` entities where authored;
volumetric solids only provide the visible beam and screen-space warp.
