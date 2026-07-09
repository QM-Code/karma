#include "scene_runtime_assets.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace karma::scenes::detail {

namespace {

std::filesystem::path documentBasePath(const SceneDocument& document) {
  const std::filesystem::path parent = document.source_path.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

std::string packageCacheKey(assets::AssetRegistry* registry) {
  return std::to_string(reinterpret_cast<std::uintptr_t>(registry));
}

}  // namespace

std::filesystem::path resolveDocumentPath(const SceneDocument& document,
                                          const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }
  return (documentBasePath(document) / path).lexically_normal();
}

assets::AssetPackageStore& sceneAssetPackageStore(assets::AssetRegistry& registry) {
  static auto* stores =
      new std::unordered_map<std::string, std::unique_ptr<assets::AssetPackageStore>>();
  const std::string key = packageCacheKey(&registry);
  auto it = stores->find(key);
  if (it == stores->end()) {
    auto inserted = stores->emplace(key, std::make_unique<assets::AssetPackageStore>(registry));
    it = inserted.first;
  }
  return *it->second;
}

const assets::GltfSceneAsset* findGltfSceneAsset(assets::AssetRegistry& registry,
                                                 const SceneAssetRef& scene_asset) {
  if (const assets::GltfSceneAsset* asset =
          registry.findGltfSceneAsset(scene_asset.id)) {
    return asset;
  }
  if (!scene_asset.path.empty()) {
    return registry.findGltfSceneAsset(scene_asset.path.generic_string());
  }
  return nullptr;
}

}  // namespace karma::scenes::detail
