# Asset Pipeline Cleanup

## Scope

This cleanup pass reduced dead public surface area and split oversized content
asset implementation files without changing runtime behavior, cache schema,
package manifest schema, asset cache environment variables, texture upload
selection, or startup ordering.

The main outcome is that public runtime usage is now clearly package-backed:
source files enter through asset packages, glTF scenes are instantiated from
registered `karma::assets::GltfSceneAsset` data, and old direct source-import
examples no longer appear in consumer-facing docs.

## Static Physics API Prune

Removed the dead static-body wrapper API:

- Deleted `include/karma/physics.h