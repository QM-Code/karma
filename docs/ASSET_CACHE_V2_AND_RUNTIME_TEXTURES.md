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

## Prepared Upload Cache

Runtime texture warm-up can now cache the final backend-ready upload payload as
`TextureAsset::PayloadFormat::PreparedUpload`. This is a generated derivative
of an imported texture asset, keyed by the source texture and runtime format
capabilities through `preparedTextureUploadCacheKey(...)`.

On a prepared-cache miss, `RenderSystem` prepares or transcodes the imported
texture, uploads it, and writes the prepared blob for the next run. On a hit,
the renderer skips KTX2 transcode work and uploads the cached prepared bytes.
Set `KARMA_RENDER_TEXTURE_PREPARED_CACHE=0` to disable this layer.

Prepared RGBA8 blobs may contain only mip 0 while retaining
`TextureDesc::generate_mips`. The backend then allocates and generates the full
mip chain after upload. The prepared-cache key revision is v2 and includes the
generated-mip flag, so older blobs that accidentally cleared this intent are
not reused.

Prepared upload blobs are larger than compressed source blobs. In the
world-bake benchmark, adding 91 prepared blobs grew the compact test cache from
about 300 MB to about 749 MB, while reducing best warm startup from 3906.64 ms
to 1657.84 ms.

Prepared upload blobs are written without touching `index.json` so first
population does not repeatedly parse and rewrite a very large cache index. That
no-index write path and generated-mip round trip are covered by the rendering
tests.

## Public API Cleanup

The public glTF source-loading API was removed from
`karma/assets.h`.
