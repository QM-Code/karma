# Asset Cache V2 And Runtime Textures

## Scope

This pass replaced the partial texture-only cache with a v2 imported-asset
cache, moved examples onto package-backed assets, and made runtime texture
uploads choose a backend-supported format instead of assuming decoded RGBA8 is
always the final representation.

The cache is intentionally v2-only. There is no v1 compatibility reader and no
migration path; old cache files are ignored and rebuilt.

## Runtime Controls

- `KARMA_ASSET_CACHE_DIR=/path/to/cache` selects the cache root.
- `KARMA_ASSET_CACHE=0` disables cache reads and writes.
- `KARMA_ASSET_CACHE_FLUSH=1` clears the cache root before use.
- `KARMA_TEXTURE_BC7=1` enables BC7 runtime transcode when the backend reports
  BC7 support.

The cache version marker is `karma-asset-cache-v2`.

## Cache Layout

`AssetCache` now stores typed imported asset blobs instead of only texture
payloads. Supported blob types are:

- textures
- meshes
- material assets
- material variants
- particle effects
- glTF scene metadata
- animation clips
- skeletons
- skins

Environment maps remain path registrations only. They are written into package
manifests so packages can restore the logical key, but there is no persistent
environment blob yet.

Cache records include the v2 schema and blob kind. A mismatched schema, kind, or
malformed payload fails the read and lets the package path rebuild from source.

## Package Cache Flow

Package imports are staged in a temporary `AssetRegistry` and committed to the
live registry all-or-nothing.

On each package import:

1. The package JSON is parsed and the manifest file is hashed.
2. A package cache key is computed from the cache version, package cache content
   version, manifest path and hash, source file hashes, importer versions,
   Assimp version, libktx dependency tag, package options, and texture profile.
3. If the package manifest cache record exists, every listed blob is read into a
   staging registry.
4. If every blob restores successfully, the staged registry is committed without
   running source importers.
5. If any blob is missing, corrupt, stale, or malformed, the package cold-imports
   from source, writes every persistent blob, then writes the package manifest.

Package manifest records store the logical asset type, logical key, cache blob
key, and blob type. For `gltf_scene` records, the manifest also records the
generated child mappings for meshes, textures, materials, animations,
skeletons, and skins.

`AssetPackageLoadedAsset` carries the cache blob key used for each committed
asset so package handles know which persistent payload produced the runtime
asset. Ref-counted package acquire/release still unregisters assets only on the
final release.

## Texture Import And Upload

Imported textures can now store a KTX2 Basis UASTC payload plus an RGBA8
fallback. The content side exposes `prepareTextureUpload(...)`, which returns
the final `TextureDesc` and `TextureUploadData` for the current runtime
capabilities.

The runtime path is:

1. `RenderSystem` or the particle renderer asks `GraphicsDevice` whether BC7
   UNORM/sRGB formats are supported.
2. `prepareTextureUpload(...)` checks those capabilities and
   `KARMA_TEXTURE_BC7`.
3. If BC7 is enabled and supported, cached KTX2 Basis payloads are transcoded to
   BC7 and uploaded with block-compressed mip subresources.
4. If BC7 is disabled or unsupported, the RGBA8 fallback is uploaded.
5. If the asset is already RGBA8, its stored subresources are uploaded directly.

Diligent now reports texture format support through `supportsTextureFormat(...)`
and accepts `TextureUploadData` for RGBA8 and BC7 uploads. BC7 uploads use
block-compressed row stride and per-mip dimensions.

Texture orientation is covered by tests. Imported material textures are decoded
to match the renderer's origin once, cached upload data preserves row order, and
the warm package path restores the same bytes instead of re-decoding source
images differently.

## Public API Cleanup

The public glTF source-loading API was removed from
`karma/content/importers/gltf_scene_import.h`. Runtime code should instantiate
registered `GltfSceneAsset` data through `instantiateGltfSceneAsset(...)`.

The old public `mesh_import.h` header was removed. Source mesh import remains
internal to the package pipeline.

The direct physics `createStaticMesh(path)` backend path was removed so physics
code no longer imports mesh files directly from a path. Mesh data should arrive
through assets/components.

Internal source import helpers still exist under `src/content/importers` because
package cold-imports and importer tests need them. They are no longer the public
runtime path.

## Rendering Examples

The rendering examples now register startup asset packages through
`EngineConfig::startup_asset_packages` and instantiate registered scene assets
instead of loading GLB/GLTF source files inside `onStart`.

Packages added for rendering:

- `examples/assets/rendering/damaged_helmet`
- `examples/assets/rendering/bloom_city`
- `examples/assets/rendering/light_stress`
- `examples/assets/rendering/material_assignment`
- `examples/assets/rendering/postwar_city`

`material_assignment` imports the tank glTF scene once and instantiates it four
times. Mesh payloads and GPU mesh buffers are shared by asset key, but each tank
still creates ECS entities, render records, and material bindings. The tinted
tanks currently use material variants, so forward rendering batches split by
material even when the mesh is shared.

## Startup Integration

`EngineApp` imports configured startup packages before game `onStart`, then can
prewarm package meshes, materials, and textures through `RenderSystem`. The
prewarm handle is released during shutdown before package assets are unloaded.

The loading splash no longer has to appear for fast warm starts. The default
`EngineConfig::LoadingSplashConfig::show_after_ms` is 750 ms; set it to `0` to
show the splash immediately.

Startup diagnostics now include stage start offsets and totals, plus detailed
`game_onStart` wait-loop accounting for polling, sleeping, and splash rendering.

## Verification Coverage

Tests now cover:

- v2 cache root/env/flush behavior
- no v1 legacy fallback
- typed cache blobs for texture, mesh, and material data
- package cold import, warm restore, and missing-blob rebuild
- cache disabled path through `KARMA_ASSET_CACHE=0`
- KTX2-backed texture upload preparation
- BC7-capable and BC7-disabled upload decisions
- texture row-order preservation
- imported material texture orientation
- async package load/commit and ref-counted package store release

Useful local checks:

```bash
cmake --build build --target karma_rendering_tests --parallel $(nproc)
./build/tests/karma_rendering_tests
git diff --check
```
