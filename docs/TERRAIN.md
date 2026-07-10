# Terrain Rendering

Karma terrain is authored with `components::TerrainComponent` and driven by
`visual::terrain::TerrainRuntimeModule`. The terrain plane is XZ and height displaces Y.

Sources:

- `Procedural`: deterministic height/color tiles generated from world-space noise.
- `ImageTileDirectory`: image tiles loaded from `tile_directory` using
  `{x}`/`{z}` replacements in `height_pattern` and `color_pattern`. `{y}` is an
  alias for `{z}` for terrain tools that name tile rows as Y. `{tile_x}`,
  `{tile_z}`, `{tile_y}`, `{X}`, `{Z}`, and `{Y}` are also accepted. Set
  `tile_index_base = 1` for one-based export naming.
- `SingleImage`: one fixed-size terrain tile. Set `terrain_size` for the XZ
  extent and provide `height_image` or `heatmap_image`. When `height_image` and
  `heatmap_image` are both set, `height_image` drives displacement and
  `heatmap_image` is uploaded as the visible texture. `color_image` overrides the
  visible texture when provided.

Height formats:

- `height_format = Auto` infers `.raw`/`.r16` as unsigned RAW16, `.r32` as
  float R32, and otherwise uses the built-in image decoder.
- `ImageFile` supports stb-backed scalar images such as PNG, TGA, JPG, BMP, PGM,
  PSD, and Radiance HDR. 16-bit PNG/PNM/PSD/TGA files keep 16-bit precision
  before normalization.
- `Raw16Unsigned` expects headerless unsigned 16-bit samples. Set `raw_width`,
  `raw_height`, and `raw_little_endian`.
- `R32Float` expects headerless 32-bit float samples. Set `raw_width`,
  `raw_height`, and `raw_little_endian`.
- Loaded heights are normalized to `[0, 1]`; `height_scale` and `height_offset`
  convert them to world Y. `height_value_min`/`height_value_max` remap R32 or
  already-normalized scalar inputs before renderer upload.
- EXR and TIFF are intentionally not decoded by the current built-in loader.
  The terrain schema can describe those exports, but loading them needs an
  OpenEXR/TIFF decoder dependency.

Gaea-style exports:

- Export height as `.r32`, `.raw`, or a 16-bit PNG. For `.r32`/`.raw`, set the
  raw dimensions because those files are headerless.
- Export macro color or orthophoto tiles through `color_pattern` or `color_image`.
- Export packed splat/weight maps through `control_pattern` or `control_image`.
  RGBA channels map to `material_layers[0..3]`.
- Add repeated terrain detail materials in `material_layers`. Prefer
  `material_key` entries that reference `AssetRegistry` materials, using the
  same material assets and instances that mesh slots use. Terrain consumes
  `base_color`/`albedo`, `normal`, and `roughness` texture aliases when present;
  otherwise it synthesizes a tiny albedo/roughness texture from the resolved
  material surface. The explicit `albedo_image`, `normal_image`, and
  `roughness_image` fields remain a direct texture fallback.
- The current renderer uses up to four terrain material layers. `uv_scale`
  controls texture repeats per terrain tile, and RGBA control-map channels blend
  layers `0..3`.
- Add auxiliary maps such as flow, wear, deposit, slope, and curvature in
  `data_maps`. They are loaded with the terrain tile as normalized scalar maps
  so gameplay/tools can consume them without the renderer knowing their file
  format.
- Experimental: `visual::terrain::importGaeaTerrainDirectory(...)` scans a Gaea
  build output directory and returns a normal `TerrainComponent`. It recognizes
  common height, color/albedo, splat/control, and
  flow/wear/deposit/slope/curvature output names. Because Gaea filenames come
  from authored node/output names, the import descriptor also accepts explicit
  image paths and tile patterns. Treat this as a convenience bridge over the
  existing terrain component, not a stable Gaea asset pipeline yet.
- For tiled Gaea folders, filenames such as `Height_0_0.r32` are inferred as
  `Height_{x}_{z}.r32`. Set `height_pattern`, `color_pattern`, or
  `control_pattern` directly when your build uses a different convention.

Renderer behavior:

- The runtime streams a primary-camera-centered square chunkmap.
- `SingleImage` terrain does not recenter or change size with the camera; it
  loads one tile at `origin_tile_x`/`origin_tile_z`.
- The backend receives decoded height/color payloads only.
- Diligent uses hardware tessellation when supported and CPU grid fallback when
  tessellation is unavailable or disabled by the terrain descriptor.
- `target_tessellated_edge_size` controls tessellation density in screen pixels;
  smaller values keep higher terrain LOD farther from the camera.
- Terrain renders after skybox and before opaque meshes so it writes depth for
  later scene geometry.
- Offscreen cameras render the currently resident terrain chunkmap like any other
  render layer.
- The renderer backend receives decoded height, color, control, material, and
  data-map payloads only; file loading and material-key resolution stay in
  `content`/`features`.

Physics collision:

- Add `ColliderComponent` to the same entity as `TerrainComponent` to request
  terrain collision. Existing shape collider components on a terrain entity are
  also treated as terrain-collider markers. The terrain runtime replaces that
  marker with a unified height-field `ColliderComponent` from the terrain height
  data before fixed physics steps.
- For `SingleImage`, the collider covers the fixed `terrain_size` tile. For
  tiled/procedural sources, the initial origin tile is used as the physics
  heightfield.

Build and run the heightmap sample with:

```sh
cmake --build build --target rendering_terrain
./build/examples/rendering/terrain
```
