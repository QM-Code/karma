#pragma once

#include <filesystem>
#include <string>

#include "karma/content/assets/asset_registry.h"

namespace karma::scene {
struct GltfSceneLoadOptions;
}

namespace karma::content::detail {

bool importMeshAsset(AssetRegistry& assets,
                     const std::string& key,
                     const std::filesystem::path& path);
bool importTextureAsset(AssetRegistry& assets,
                        const std::string& key,
                        const std::filesystem::path& path,
                        const TextureImportOptions& options);
bool importParticleEffect(AssetRegistry& assets,
                          const std::string& key,
                          const std::filesystem::path& path);
GltfSceneAsset importGltfSceneAsset(AssetRegistry& assets,
                                    const std::string& key,
                                    const std::filesystem::path& path,
                                    const scene::GltfSceneLoadOptions& options);

}  // namespace karma::content::detail
