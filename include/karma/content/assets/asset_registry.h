#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "karma/features/visual/particles/effect_asset.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/material.h"
#include "karma/rendering/renderer/post_process.h"
#include "karma/rendering/renderer/texture.h"
#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::scene {
struct GltfSceneLoadOptions;
}

namespace karma::content {

/// CPU-side texture payload ready for renderer upload.
struct TextureAsset {
  renderer::TextureDesc desc{};
  std::vector<uint8_t> bytes;
};

/// Options used when importing an image file into a CPU texture asset.
struct TextureImportOptions {
  bool srgb = false;
  bool generate_mips = true;
};

/// Runtime audio clip registration. `path` is an import source resolved at
/// registration time; runtime components reference only `key`.
struct AudioClipAsset {
  std::filesystem::path path;
  int max_instances = 5;
};

/// Runtime environment map registration. Components reference `key`; renderer
/// backends still receive the resolved source path while environment import
/// caching remains future work.
struct EnvironmentMapAsset {
  std::filesystem::path path;
};

/// Deterministic child keys produced by a glTF scene import.
struct GltfSceneAsset {
  std::filesystem::path source_path;
  std::vector<std::string> mesh_asset_keys;
  std::vector<std::string> material_keys;
  std::vector<std::string> animation_clip_keys;
  std::vector<std::string> skeleton_keys;
  std::vector<std::string> skin_keys;
  std::string scene_key;
};

/// \ingroup karma_content
/// Explicit registry for normalized runtime assets.
///
/// Source files are accepted only by import/register calls. Runtime systems
/// resolve components through these keys and never treat component strings as
/// file paths.
class AssetRegistry {
 public:
  AssetRegistry();
  ~AssetRegistry();
  AssetRegistry(AssetRegistry&&) noexcept;
  AssetRegistry& operator=(AssetRegistry&&) noexcept;
  AssetRegistry(const AssetRegistry&) = delete;
  AssetRegistry& operator=(const AssetRegistry&) = delete;

  void clear();

  static bool isValidAssetKey(std::string_view key);
  static std::string assetKeyValidationError(std::string_view key);

  bool registerMeshAsset(const std::string& key, geometry::MeshData mesh);
  bool importMeshAsset(const std::string& key, const std::filesystem::path& path);
  bool unregisterMeshAsset(const std::string& key);
  const geometry::MeshData* findMeshAsset(std::string_view key) const;

  bool registerTextureAsset(const std::string& key, TextureAsset texture);
  bool importTextureAsset(const std::string& key,
                          const std::filesystem::path& path,
                          const TextureImportOptions& options = {});
  bool unregisterTextureAsset(const std::string& key);
  const TextureAsset* findTextureAsset(std::string_view key) const;

  bool registerMaterialAsset(const std::string& key, renderer::MaterialAssetDesc material);
  bool registerMaterialAsset(const std::string& key, renderer::MaterialDesc surface);
  bool registerMaterialVariant(const std::string& key, renderer::MaterialVariantDesc material);
  bool registerMaterialVariant(
      const std::string& key,
      const std::string& base_material_key,
      std::unordered_map<std::string, renderer::MaterialParameterValue> params = {},
      std::unordered_map<std::string, std::string> textures = {});
  bool unregisterMaterial(const std::string& key);
  const renderer::MaterialAssetDesc* findMaterialAsset(std::string_view key) const;
  const renderer::MaterialVariantDesc* findMaterialVariant(std::string_view key) const;
  std::optional<renderer::ResolvedMaterialDesc> resolveMaterial(std::string_view key) const;

  bool registerPostProcessProfile(const std::string& key, renderer::PostProcessSettings profile);
  bool unregisterPostProcessProfile(const std::string& key);
  const renderer::PostProcessSettings* findPostProcessProfile(std::string_view key) const;
  const renderer::PostProcessSettings& resolvePostProcessProfile(std::string_view key) const;

  bool registerParticleEffect(const std::string& key, particles::ParticleEffectAsset effect);
  bool importParticleEffect(const std::string& key, const std::filesystem::path& path);
  bool unregisterParticleEffect(const std::string& key);
  const particles::ParticleEffectAsset* findParticleEffect(std::string_view key) const;

  bool registerAudioClip(const std::string& key, AudioClipAsset clip);
  bool unregisterAudioClip(const std::string& key);
  const AudioClipAsset* findAudioClip(std::string_view key) const;

  bool registerEnvironmentMap(const std::string& key, EnvironmentMapAsset environment);
  bool unregisterEnvironmentMap(const std::string& key);
  const EnvironmentMapAsset* findEnvironmentMap(std::string_view key) const;

  bool registerAnimationClip(const std::string& key, animation::AnimationClip clip);
  bool unregisterAnimationClip(const std::string& key);
  const animation::AnimationClip* findAnimationClip(std::string_view key) const;

  bool registerSkeleton(const std::string& key, animation::Skeleton skeleton);
  bool unregisterSkeleton(const std::string& key);
  const animation::Skeleton* findSkeleton(std::string_view key) const;

  bool registerSkin(const std::string& key, animation::Skin skin);
  bool unregisterSkin(const std::string& key);
  const animation::Skin* findSkin(std::string_view key) const;

  GltfSceneAsset importGltfSceneAsset(const std::string& key,
                                      const std::filesystem::path& path);
  GltfSceneAsset importGltfSceneAsset(const std::string& key,
                                      const std::filesystem::path& path,
                                      const scene::GltfSceneLoadOptions& options);
  bool registerGltfSceneAsset(const std::string& key, GltfSceneAsset scene);
  bool unregisterGltfSceneAsset(const std::string& key);
  const GltfSceneAsset* findGltfSceneAsset(std::string_view key) const;

  uint64_t version() const;
  uint64_t meshVersion() const;
  uint64_t textureVersion() const;

 private:
  void bumpVersion();
  void bumpMeshVersion();
  void bumpTextureVersion();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::content
