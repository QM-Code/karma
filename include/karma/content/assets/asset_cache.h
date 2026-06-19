#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "karma/content/assets/asset_registry.h"

namespace karma::content {

/// \ingroup karma_content
/// Persistent asset import cache configuration.
struct AssetCacheConfig {
  std::filesystem::path root;
  bool enabled = true;
  bool flush = false;

  static AssetCacheConfig fromEnvironment();
};

/// \ingroup karma_content
/// User-cache backed store for serialized imported asset blobs.
class AssetCache {
 public:
  static constexpr uint32_t kSchemaVersion = 2u;
  static constexpr std::string_view kAssetCacheVersion = "karma-asset-cache-v2";

  explicit AssetCache(AssetCacheConfig config = AssetCacheConfig::fromEnvironment());

  const AssetCacheConfig& config() const { return config_; }
  const std::filesystem::path& root() const { return config_.root; }
  bool enabled() const { return config_.enabled && !config_.root.empty(); }

  void flush();

  std::string makeSourceKey(const std::filesystem::path& source,
                            std::string_view importer_version,
                            const nlohmann::json& import_options,
                            std::string_view package_manifest_hash = {},
                            std::string_view dependency_version = {}) const;

  std::optional<TextureAsset> readTexture(std::string_view cache_key,
                                          std::string* diagnostic = nullptr);
  bool writeTexture(std::string_view cache_key,
                    const TextureAsset& texture,
                    std::string* diagnostic = nullptr);
  std::optional<geometry::MeshData> readMesh(std::string_view cache_key,
                                             std::string* diagnostic = nullptr);
  bool writeMesh(std::string_view cache_key,
                 const geometry::MeshData& mesh,
                 std::string* diagnostic = nullptr);
  std::optional<renderer::MaterialAssetDesc> readMaterialAsset(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeMaterialAsset(std::string_view cache_key,
                          const renderer::MaterialAssetDesc& material,
                          std::string* diagnostic = nullptr);
  std::optional<renderer::MaterialVariantDesc> readMaterialVariant(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeMaterialVariant(std::string_view cache_key,
                            const renderer::MaterialVariantDesc& material,
                            std::string* diagnostic = nullptr);
  std::optional<particles::ParticleEffectAsset> readParticleEffect(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeParticleEffect(std::string_view cache_key,
                           const particles::ParticleEffectAsset& effect,
                           std::string* diagnostic = nullptr);
  std::optional<GltfSceneAsset> readGltfScene(std::string_view cache_key,
                                              std::string* diagnostic = nullptr);
  bool writeGltfScene(std::string_view cache_key,
                      const GltfSceneAsset& scene,
                      std::string* diagnostic = nullptr);
  std::optional<animation::AnimationClip> readAnimationClip(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeAnimationClip(std::string_view cache_key,
                          const animation::AnimationClip& clip,
                          std::string* diagnostic = nullptr);
  std::optional<animation::Skeleton> readSkeleton(std::string_view cache_key,
                                                  std::string* diagnostic = nullptr);
  bool writeSkeleton(std::string_view cache_key,
                     const animation::Skeleton& skeleton,
                     std::string* diagnostic = nullptr);
  std::optional<animation::Skin> readSkin(std::string_view cache_key,
                                          std::string* diagnostic = nullptr);
  bool writeSkin(std::string_view cache_key,
                 const animation::Skin& skin,
                 std::string* diagnostic = nullptr);

  std::optional<nlohmann::json> readPackageManifest(std::string_view manifest_hash,
                                                    std::string* diagnostic = nullptr);
  bool writePackageManifest(std::string_view manifest_hash,
                            const nlohmann::json& manifest,
                            std::string* diagnostic = nullptr);

 private:
  std::filesystem::path blobPath(std::string_view cache_key) const;
  std::filesystem::path packageManifestPath(std::string_view manifest_hash) const;
  void ensureLayout();
  void touchIndex(std::string_view cache_key, std::string_view kind);

  AssetCacheConfig config_{};
};

std::string hashBytes(const std::uint8_t* data, std::size_t size);
std::string hashString(std::string_view value);
std::optional<std::string> hashFile(const std::filesystem::path& path);

}  // namespace karma::content
