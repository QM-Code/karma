#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include "karma/features/visual/particles/effect_asset.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/material.h"
#include "karma/rendering/renderer/post_process.h"
#include "karma/rendering/renderer/texture.h"
#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/components/light.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::content {

/// CPU-side texture payload ready for renderer upload.
struct TextureAsset {
  enum class PayloadFormat : uint32_t {
    RGBA8 = 0u,
    KTX2_BASIS_UASTC = 1u,
  };

  enum class Semantic : uint32_t {
    Color = 0u,
    Linear = 1u,
    Normal = 2u,
    Data = 3u,
  };

  renderer::TextureDesc desc{};
  PayloadFormat payload_format = PayloadFormat::RGBA8;
  Semantic semantic = Semantic::Color;
  std::vector<renderer::TextureUploadSubresource> subresources;
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> fallback_rgba8;
  std::string content_hash;
};

/// Texture format choices available on the active renderer backend.
struct TextureRuntimeCapabilities {
  bool bc7_unorm = false;
  bool bc7_srgb = false;
};

/// Final texture descriptor and byte payload selected for renderer upload.
struct PreparedTextureUpload {
  renderer::TextureDesc desc{};
  renderer::TextureUploadData upload{};

  bool valid() const {
    return desc.width > 0 && desc.height > 0 && !upload.bytes.empty();
  }
};

/// Converts a cached/imported texture payload into the best backend upload.
std::optional<PreparedTextureUpload> prepareTextureUpload(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities = {});

/// Options used when importing an image file into a CPU texture asset.
struct TextureImportOptions {
  bool srgb = false;
  bool generate_mips = true;
  bool alpha_bleed = false;
  float alpha_coverage_cutoff = 0.5f;
  TextureAsset::Semantic semantic = TextureAsset::Semantic::Color;
  bool prefer_compressed = true;
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

struct GltfSceneAssetPrimitive {
  std::string name;
  std::string mesh_key;
  std::string material_key;
  uint32_t skin_index = animation::kInvalidAnimationIndex;
  std::vector<float> morph_weights;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;
};

struct GltfSceneAssetNode {
  std::string name;
  math::Vec3 local_position{};
  math::Quat local_rotation{};
  math::Vec3 local_scale{1.0f, 1.0f, 1.0f};
  math::Vec3 world_position{};
  math::Quat world_rotation{};
  math::Vec3 world_scale{1.0f, 1.0f, 1.0f};
  bool has_light = false;
  components::LightComponent light{};
  std::vector<GltfSceneAssetPrimitive> primitives;
  std::vector<uint32_t> children;
};

/// Deterministic child keys produced by a glTF scene import.
struct GltfSceneAsset {
  std::filesystem::path source_path;
  uint32_t root_node = std::numeric_limits<uint32_t>::max();
  std::vector<GltfSceneAssetNode> nodes;
  std::vector<std::string> mesh_asset_keys;
  std::vector<std::string> texture_asset_keys;
  std::vector<std::string> material_keys;
  std::vector<std::string> animation_clip_keys;
  std::vector<std::string> skeleton_keys;
  std::vector<std::string> skin_keys;
  std::string scene_key;

  bool valid() const {
    return root_node < nodes.size();
  }
};

/// \ingroup karma_content
/// Explicit registry for normalized runtime assets.
///
/// Source files enter through asset packages. Runtime systems resolve
/// components through these keys and never treat component strings as file paths.
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
  bool unregisterMeshAsset(const std::string& key);
  const geometry::MeshData* findMeshAsset(std::string_view key) const;

  bool registerTextureAsset(const std::string& key, TextureAsset texture);
  bool unregisterTextureAsset(const std::string& key);
  const TextureAsset* findTextureAsset(std::string_view key) const;
  std::vector<std::string> registerImportedMaterialTextures(
      const std::string& material_key,
      renderer::MaterialAssetDesc& material);

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
