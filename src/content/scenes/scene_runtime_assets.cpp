#include "scene_runtime_assets.h"

#include <string>

namespace karma::scenes::detail {

namespace {

std::filesystem::path documentBasePath(
    const SceneDocument& document,
    const std::filesystem::path& reference_root) {
  if (!reference_root.empty()) {
    return reference_root;
  }
  if (!document.reference_root.empty()) {
    return document.reference_root;
  }
  const std::filesystem::path parent = document.source_path.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

}  // namespace

std::filesystem::path resolveDocumentPath(const SceneDocument& document,
                                          const std::filesystem::path& path,
                                          const std::filesystem::path& reference_root) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }
  return (documentBasePath(document, reference_root) / path).lexically_normal();
}

assets::AssetPackageStore& sceneAssetPackageStore(assets::AssetRegistry& registry) {
  return registry.sharedPackageStore();
}

const assets::GltfSceneAsset* findGltfSceneAsset(assets::AssetRegistry& registry,
                                                 const SceneDocument& document,
                                                 const SceneAssetRef& scene_asset,
                                                 const std::filesystem::path& reference_root) {
  if (const assets::GltfSceneAsset* asset =
          registry.findGltfSceneAsset(scene_asset.id)) {
    return asset;
  }
  if (!scene_asset.path.empty()) {
    if (const assets::GltfSceneAsset* asset =
            registry.findGltfSceneAsset(scene_asset.path.generic_string())) {
      return asset;
    }
    return registry.findGltfSceneAsset(
        resolveDocumentPath(document, scene_asset.path, reference_root)
            .generic_string());
  }
  return nullptr;
}

}  // namespace karma::scenes::detail
