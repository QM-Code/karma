#pragma once

#include "karma/assets.h"
#include "karma/scenes.h"

#include <filesystem>

namespace karma::scenes::detail {

std::filesystem::path resolveDocumentPath(const SceneDocument& document,
                                          const std::filesystem::path& path);

assets::AssetPackageStore& sceneAssetPackageStore(assets::AssetRegistry& registry);

const assets::GltfSceneAsset* findGltfSceneAsset(assets::AssetRegistry& registry,
                                                 const SceneAssetRef& scene_asset);

}  // namespace karma::scenes::detail
