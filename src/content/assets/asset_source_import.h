#pragma once

#include <filesystem>
#include <string>

#include "karma/assets.h"

namespace karma::world {
struct GltfSceneLoadOptions;
}

namespace karma::assets::detail {

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
                                    const world::GltfSceneLoadOptions& options);

}  // namespace karma::assets::detail
