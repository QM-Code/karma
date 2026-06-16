# Terrain Rendering

Karma terrain is authored with `components::TerrainComponent` and driven by
`terrain::TerrainRuntimeModule`. The terrain plane is XZ and height displaces Y.

Sources:

- `Procedural`: deterministic height/color tiles generated from world-space noise.
- `ImageTileDirectory`: image tiles loaded from `tile_directory` using
  `{x}`/`{z}` replacements in `height_pattern` and `color_pattern`. Height images
  use the red channel normalized to `[0, 1]`; color images are RGBA orthophotos.
- `SingleImage`: one fixed-size terrain tile. Set `terrain_size` for the XZ
  extent and provide `height_image` or `heatmap_image`. When `height_image` and
  `heatmap_image` are both set, `height_image` drives displacement and
  `heatmap_image` is uploaded as the visible texture. `color_image` overrides the
  visible texture when provided.

Renderer behavior:

- The runtime streams a primary-camera-centered square chunkmap.
- `SingleImage` terrain does not recenter or change size with the camera; it
  loads one tile at `origin_tile_x`/`origin_tile_z`.
- The backend receives decoded height/color payloads only.
- Diligent uses hardware tessellation when supported and CPU grid fallback when
  tessellation is unavailable or disabled by the terrain descriptor.
- Terrain renders after skybox and before opaque meshes so it writes depth for
  later scene geometry.
- Offscreen cameras render the currently resident terrain chunkmap like any other
  render layer.

Build and run the procedural sample with:

```sh
cmake --build build --target karma_terrain_example
./build/karma_terrain_example
```
