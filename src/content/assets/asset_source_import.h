#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "karma/assets.h"

namespace karma::world {
struct GltfSceneLoadOptions;
}

namespace karma::assets::detail {

struct HumanoidImportOptions {
  bool enabled = false;
  world::HumanoidProfileKind profile = world::HumanoidProfileKind::Mixamo;
  std::string rig_key;
};

struct AnimationClipImportResult {
  std::string clip_key;
  std::vector<std::string> skeleton_keys;
  std::vector<std::string> humanoid_rig_keys;
};

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
                                    const world::GltfSceneLoadOptions& options,
                                    const HumanoidImportOptions& humanoid = {});
AnimationClipImportResult importAnimationClipAsset(
    AssetRegistry& assets,
    const std::string& key,
    const std::filesystem::path& path,
    std::string_view clip_name = {},
    std::string_view display_name = {},
    const HumanoidImportOptions& humanoid = {});

}  // namespace karma::assets::detail
