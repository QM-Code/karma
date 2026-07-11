#pragma once

#include "karma/assets.h"
#include "karma/scenes.h"

#include <filesystem>

namespace karma::scenes::detail {

std::filesystem::path resolveDocumentPath(const SceneDocument& document,
                                          const std::filesystem::path& path,
                                          const std::filesystem::path& reference_root = {});

assets::AssetPackageStore& sceneAssetPackageStore(assets::AssetRegistry& registry);

const assets::GltfSceneAsset* findGltfSceneAsset(assets::AssetRegistry& registry,
                                                 const SceneDocument& document,
                                                 const SceneAssetRef& scene_asset,
                                                 const std::filesystem::path& reference_root = {});

}  // namespace karma::scenes::detail
