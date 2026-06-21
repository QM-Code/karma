# Asset Pipeline Cleanup

## Scope

This cleanup pass reduced dead public surface area and split oversized content
asset implementation files without changing runtime behavior, cache schema,
package manifest schema, asset cache environment variables, texture upload
selection, or startup ordering.

The main outcome is that public runtime usage is now clearly package-backed:
source files enter through asset packages, glTF scenes are instantiated from
registered `karma::content::GltfSceneAsset` data, and old direct source-import
examples no longer appear in consumer-facing docs.

## Static Physics API Prune

Removed the dead static-body wrapper API:

- Deleted `include/karma/simulation/physics/static_body.hpp`.
- Deleted `src/simulation/physics/static_body.cpp`.
- Removed `src/simulation/physics/static_body.cpp` from `karma_simulation_physics`.
- Removed `karma::physics_backend::PhysicsStaticBodyBackend` from
  `include/karma/simulation/physics/backend.hpp`.

This does not remove collider, rigid-body, static-collider component, or static
mesh-shape paths. Static world geometry should continue to flow through
components and backend shape/collider paths.

## glTF Public API Cleanup

`karma::scene::GltfSceneInstantiateOptions` now contains only options that affect
registered scene asset instantiation:

- `create_synthetic_root`
- `autoplay_animations`

The ignored public `asset_key_prefix` field was removed. Registered
`GltfSceneAsset` data already stores deterministic mesh, material, texture,
animation, skeleton, and skin keys, so public asset instantiation does not need a
source-import key prefix.

The source importer still needs deterministic prefixes when instantiating
in-memory prefabs during internal tests and cold-import paths. That internal-only
state now lives in `GltfScenePrefabInstantiateOptions` in
`src/content/importers/gltf_scene_import_internal.h`.

## Asset Cache Split

`include/karma/content/assets/asset_cache.h` remains the public API. The cache
dispatcher, filesystem layout, environment parsing, atomic file writes, package
manifest reads/writes, index updates, and hashing stay in
`src/content/assets/asset_cache.cpp`.

Private serialization code moved into:

- `src/content/assets/asset_cache_serializers.h`
- `src/content/assets/asset_cache_mesh.cpp`
- `src/content/assets/asset_cache_texture.cpp`
- `src/content/assets/asset_cache_json.cpp`

The split preserves the v2 blob format:

- same magic and schema version
- same blob kind values
- same chunk IDs
- same JSON payload structure
- same package manifest format

`AssetCache` still owns file I/O. The new private serializer units only convert
typed assets to and from byte buffers.

## Asset Registry Split

`src/content/assets/asset_registry.cpp` now focuses on registry ownership,
version counters, key validation, and register/unregister/find/resolve methods.

Source-import responsibilities moved to:

- `src/content/assets/asset_source_import.cpp`

Texture asset helper responsibilities moved to:

- `src/content/assets/asset_texture_internal.h`
- `src/content/assets/asset_texture.cpp`

That includes texture content hashing, KTX2 encoding/transcoding helpers, BC7
runtime upload selection, RGBA8 fallback upload preparation, imported material
texture decoding, imported texture aliasing, and imported texture semantic
mapping.

## Documentation Updates

Consumer-facing docs were updated to show package-backed workflows:

- `docs/ENGINE_USAGE.md`
- `docs/ANIMATION_V2.md`
- `docs/NAVIGATION.md`
- `docs/RIGGED_GLTF_AUTHORING.md`
- `docs/api/animation.md`

Stale examples of the old public direct glTF source APIs were removed from
consumer docs. The removed examples covered direct prefab loading, direct prefab
instantiation, and one-call direct glTF source import.

Internal importer code and importer tests can still mention those names because
they are private implementation/test surfaces.

## Verification

The cleanup was verified with:

```bash
cmake --build build --target karma_content karma_simulation_physics karma_rendering_tests karma_animation_tests karma_prefab_tests karma_navmesh_tests karma_physics_tests --parallel $(nproc)
ctest --test-dir build -R 'karma_(rendering|animation|prefab|navmesh|physics)_tests' --output-on-failure
env KARMA_ASSET_CACHE=0 ctest --test-dir build -R karma_rendering_tests --output-on-failure
git diff --check
```

The build and tests passed. The stale public glTF API-name search and dead
physics API-name search returned no matches for their requested public/source
paths.

During the build, GCC 13 emitted an existing bundled fmt/spdlog warning from a
dependency include path while compiling physics. It did not fail the build and
was unrelated to the cleanup files.

## Worktree Notes

Two unrelated untracked PDFs were present before this pass and were left
untouched:

- `2504.04564v2.pdf`
- `docs/I001875980Thesis.pdf`
