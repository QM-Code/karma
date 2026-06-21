#include "karma/assets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "karma/assets.h"

#include "asset_texture_internal.h"
#include "material_registry_backing.h"
#include "post_process_profile_registry_backing.h"

namespace karma::assets {

namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool hasKnownSourceExtension(std::string_view key) {
  const std::filesystem::path path{std::string(key)};
  const std::string ext = lowercase(path.extension().string());
  if (ext.empty()) {
    return false;
  }
  static constexpr std::array<std::string_view, 20> kSourceExtensions{
      ".glb", ".gltf", ".obj", ".fbx", ".dae", ".png", ".jpg", ".jpeg", ".tga",
      ".bmp", ".hdr", ".exr", ".ktx", ".dds", ".mat", ".json", ".kpeffect",
      ".wav", ".ogg", ".mp3"};
  return std::find(kSourceExtensions.begin(), kSourceExtensions.end(), ext) !=
         kSourceExtensions.end();
}

bool hasDotDotSegment(std::string_view key) {
  std::size_t segment_start = 0;
  while (segment_start <= key.size()) {
    const std::size_t slash = key.find('/', segment_start);
    const std::size_t segment_end = slash == std::string_view::npos ? key.size() : slash;
    if (key.substr(segment_start, segment_end - segment_start) == "..") {
      return true;
    }
    if (slash == std::string_view::npos) {
      break;
    }
    segment_start = slash + 1;
  }
  return false;
}

template <typename T>
const T* findInMap(const std::unordered_map<std::string, T>& map,
                   std::string_view key) {
  const auto it = map.find(std::string(key));
  return it != map.end() ? &it->second : nullptr;
}

template <typename T>
void appendRaw(std::vector<uint8_t>& out, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
void appendVectorRaw(std::vector<uint8_t>& out, const std::vector<T>& values) {
  const uint64_t size = static_cast<uint64_t>(values.size());
  appendRaw(out, size);
  if (!values.empty()) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
    out.insert(out.end(), bytes, bytes + sizeof(T) * values.size());
  }
}

std::string meshContentHash(const world::MeshData& mesh) {
  std::vector<uint8_t> bytes;
  appendVectorRaw(bytes, mesh.vertices);
  appendVectorRaw(bytes, mesh.normals);
  appendVectorRaw(bytes, mesh.uvs);
  appendVectorRaw(bytes, mesh.uvs1);
  appendVectorRaw(bytes, mesh.tangents);
  appendVectorRaw(bytes, mesh.indices);
  appendVectorRaw(bytes, mesh.joint_indices);
  appendVectorRaw(bytes, mesh.joint_weights);
  appendVectorRaw(bytes, mesh.submeshes);
  return hashBytes(bytes.data(), bytes.size());
}

}  // namespace

struct AssetRegistry::Impl {
  std::unordered_map<std::string, std::shared_ptr<world::MeshData>> meshes;
  std::unordered_map<std::string, std::shared_ptr<TextureAsset>> textures;
  std::unordered_map<std::string, std::weak_ptr<world::MeshData>> mesh_payloads_by_hash;
  std::unordered_map<std::string, std::weak_ptr<TextureAsset>> texture_payloads_by_hash;
  std::unordered_map<std::string, std::string> imported_texture_keys_by_source_hash;
  std::unordered_map<std::string, visual::particles::ParticleEffectAsset> particle_effects;
  std::unordered_map<std::string, AudioClipAsset> audio_clips;
  std::unordered_map<std::string, EnvironmentMapAsset> environment_maps;
  std::unordered_map<std::string, world::AnimationClip> animation_clips;
  std::unordered_map<std::string, world::Skeleton> skeletons;
  std::unordered_map<std::string, world::Skin> skins;
  std::unordered_map<std::string, GltfSceneAsset> gltf_scenes;
  rendering::MaterialLibrary materials;
  rendering::PostProcessProfileLibrary post_process_profiles;
  uint64_t version = 0;
  uint64_t mesh_version = 0;
  uint64_t texture_version = 0;
};

AssetRegistry::AssetRegistry() : impl_(std::make_unique<Impl>()) {}

AssetRegistry::~AssetRegistry() = default;

AssetRegistry::AssetRegistry(AssetRegistry&&) noexcept = default;

AssetRegistry& AssetRegistry::operator=(AssetRegistry&&) noexcept = default;

void AssetRegistry::bumpVersion() {
  impl_->version += 1;
}

void AssetRegistry::bumpMeshVersion() {
  impl_->mesh_version += 1;
  bumpVersion();
}

void AssetRegistry::bumpTextureVersion() {
  impl_->texture_version += 1;
  bumpVersion();
}

uint64_t AssetRegistry::version() const {
  return impl_->version;
}

uint64_t AssetRegistry::meshVersion() const {
  return impl_->mesh_version;
}

uint64_t AssetRegistry::textureVersion() const {
  return impl_->texture_version;
}

bool AssetRegistry::isValidAssetKey(std::string_view key) {
  return assetKeyValidationError(key).empty();
}

std::string AssetRegistry::assetKeyValidationError(std::string_view key) {
  if (key.empty()) {
    return "asset key must not be empty";
  }
  if (key.front() == '/' || std::filesystem::path{std::string(key)}.is_absolute()) {
    return "asset key must be a logical identifier, not an absolute path";
  }
  if (key.find('\\') != std::string_view::npos) {
    return "asset key must use '/' namespace separators, not backslashes";
  }
  if (key.find(':') != std::string_view::npos) {
    return "asset key must not contain drive or URI separators";
  }
  if (hasDotDotSegment(key)) {
    return "asset key must not contain '..' path segments";
  }
  if (hasKnownSourceExtension(key)) {
    return "asset key must not be a source file path";
  }
  return {};
}

void AssetRegistry::clear() {
  impl_->meshes.clear();
  impl_->textures.clear();
  impl_->mesh_payloads_by_hash.clear();
  impl_->texture_payloads_by_hash.clear();
  impl_->imported_texture_keys_by_source_hash.clear();
  impl_->particle_effects.clear();
  impl_->audio_clips.clear();
  impl_->environment_maps.clear();
  impl_->animation_clips.clear();
  impl_->skeletons.clear();
  impl_->skins.clear();
  impl_->gltf_scenes.clear();
  impl_->materials.clear();
  impl_->post_process_profiles = rendering::PostProcessProfileLibrary{};
  bumpMeshVersion();
  bumpTextureVersion();
}

bool AssetRegistry::registerMeshAsset(const std::string& key, world::MeshData mesh) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  const std::string hash = meshContentHash(mesh);
  std::shared_ptr<world::MeshData> payload;
  if (auto existing = impl_->mesh_payloads_by_hash[hash].lock()) {
    payload = std::move(existing);
  } else {
    payload = std::make_shared<world::MeshData>(std::move(mesh));
    impl_->mesh_payloads_by_hash[hash] = payload;
  }
  impl_->meshes[key] = std::move(payload);
  bumpMeshVersion();
  return true;
}

bool AssetRegistry::unregisterMeshAsset(const std::string& key) {
  if (impl_->meshes.erase(key) == 0) {
    return false;
  }
  bumpMeshVersion();
  return true;
}

const world::MeshData* AssetRegistry::findMeshAsset(std::string_view key) const {
  const auto it = impl_->meshes.find(std::string(key));
  return it != impl_->meshes.end() && it->second ? it->second.get() : nullptr;
}

bool AssetRegistry::registerTextureAsset(const std::string& key, TextureAsset texture) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  texture.content_hash = detail::textureContentHash(texture);
  std::shared_ptr<TextureAsset> payload;
  if (auto existing = impl_->texture_payloads_by_hash[texture.content_hash].lock()) {
    payload = std::move(existing);
  } else {
    payload = std::make_shared<TextureAsset>(std::move(texture));
    impl_->texture_payloads_by_hash[payload->content_hash] = payload;
  }
  impl_->textures[key] = std::move(payload);
  bumpTextureVersion();
  return true;
}

bool AssetRegistry::unregisterTextureAsset(const std::string& key) {
  if (impl_->textures.erase(key) == 0) {
    return false;
  }
  bumpTextureVersion();
  return true;
}

const TextureAsset* AssetRegistry::findTextureAsset(std::string_view key) const {
  const auto it = impl_->textures.find(std::string(key));
  return it != impl_->textures.end() && it->second ? it->second.get() : nullptr;
}

std::vector<std::string> AssetRegistry::registerImportedMaterialTextures(
    const std::string& material_key,
    rendering::MaterialAssetDesc& material) {
  (void)material_key;
  std::vector<std::string> keys;
  if (material.imported_material == nullptr ||
      material.imported_material->textures.empty()) {
    return keys;
  }

  keys.reserve(material.imported_material->textures.size());
  for (const rendering::ImportedMaterialTexture& imported :
       material.imported_material->textures) {
    if (imported.source_key.empty()) {
      continue;
    }
    const std::string source_hash = detail::importedTextureSourceHash(imported);
    std::string texture_key;
    if (const auto existing = impl_->imported_texture_keys_by_source_hash.find(source_hash);
        existing != impl_->imported_texture_keys_by_source_hash.end()) {
      texture_key = existing->second;
    } else {
      std::optional<Rgba8Image> image = detail::decodeImportedTexture(imported);
      if (!image.has_value() || !image->valid()) {
        continue;
      }

      const std::string label = detail::sanitizeTextureKeySegment(
          imported.label.empty() ? imported.raw_name : imported.label);
      texture_key = "gltf/textures/" + source_hash + "/" + label;
      if (!AssetRegistry::isValidAssetKey(texture_key)) {
        texture_key = "gltf/textures/" + source_hash;
      }

      TextureAsset texture = detail::makeTextureAssetFromImage(
          std::move(*image),
          imported.srgb,
          true,
          detail::importedTextureSemantic(imported.semantic, imported.srgb),
          true);
      if (!registerTextureAsset(texture_key, std::move(texture))) {
        continue;
      }
      impl_->imported_texture_keys_by_source_hash[source_hash] = texture_key;
    }

    const std::string alias = detail::importedTextureAlias(imported.semantic, imported.label);
    material.textures[alias] = texture_key;
    keys.push_back(texture_key);
  }
  return keys;
}

bool AssetRegistry::registerMaterialAsset(const std::string& key,
                                          rendering::MaterialAssetDesc material) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->materials.registerMaterialAsset(key, std::move(material));
  bumpVersion();
  return true;
}

bool AssetRegistry::registerMaterialAsset(const std::string& key,
                                          rendering::MaterialDesc surface) {
  rendering::MaterialAssetDesc material{};
  material.surface = std::move(surface);
  return registerMaterialAsset(key, std::move(material));
}

bool AssetRegistry::registerMaterialVariant(const std::string& key,
                                            rendering::MaterialVariantDesc material) {
  if (!isValidAssetKey(key) || !isValidAssetKey(material.base_material_key)) {
    return false;
  }
  impl_->materials.registerMaterialVariant(key, std::move(material));
  bumpVersion();
  return true;
}

bool AssetRegistry::registerMaterialVariant(
    const std::string& key,
    const std::string& base_material_key,
    std::unordered_map<std::string, rendering::MaterialParameterValue> params,
    std::unordered_map<std::string, std::string> textures) {
  rendering::MaterialVariantDesc material{};
  material.base_material_key = base_material_key;
  material.params = std::move(params);
  material.textures = std::move(textures);
  return registerMaterialVariant(key, std::move(material));
}

bool AssetRegistry::unregisterMaterial(const std::string& key) {
  const bool removed = impl_->materials.unregisterMaterial(key);
  if (removed) {
    bumpVersion();
  }
  return removed;
}

const rendering::MaterialAssetDesc* AssetRegistry::findMaterialAsset(
    std::string_view key) const {
  return impl_->materials.findAsset(std::string(key));
}

const rendering::MaterialVariantDesc* AssetRegistry::findMaterialVariant(
    std::string_view key) const {
  return impl_->materials.findVariant(std::string(key));
}

std::optional<rendering::ResolvedMaterialDesc> AssetRegistry::resolveMaterial(
    std::string_view key) const {
  return impl_->materials.resolve(std::string(key));
}

bool AssetRegistry::registerPostProcessProfile(const std::string& key,
                                               rendering::PostProcessSettings profile) {
  const bool default_profile_key =
      key.empty() || key == rendering::kDefaultPostProcessProfileKey;
  if (!default_profile_key && !isValidAssetKey(key)) {
    return false;
  }
  impl_->post_process_profiles.registerProfile(key, profile);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterPostProcessProfile(const std::string& key) {
  const bool removed = impl_->post_process_profiles.unregisterProfile(key);
  if (removed) {
    bumpVersion();
  }
  return removed;
}

const rendering::PostProcessSettings* AssetRegistry::findPostProcessProfile(
    std::string_view key) const {
  return impl_->post_process_profiles.find(key);
}

const rendering::PostProcessSettings& AssetRegistry::resolvePostProcessProfile(
    std::string_view key) const {
  return impl_->post_process_profiles.resolve(key);
}

bool AssetRegistry::registerParticleEffect(const std::string& key,
                                           visual::particles::ParticleEffectAsset effect) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->particle_effects[key] = std::move(effect);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterParticleEffect(const std::string& key) {
  if (impl_->particle_effects.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const visual::particles::ParticleEffectAsset* AssetRegistry::findParticleEffect(
    std::string_view key) const {
  return findInMap(impl_->particle_effects, key);
}

bool AssetRegistry::registerAudioClip(const std::string& key, AudioClipAsset clip) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->audio_clips[key] = std::move(clip);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterAudioClip(const std::string& key) {
  if (impl_->audio_clips.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const AudioClipAsset* AssetRegistry::findAudioClip(std::string_view key) const {
  return findInMap(impl_->audio_clips, key);
}

bool AssetRegistry::registerEnvironmentMap(const std::string& key,
                                           EnvironmentMapAsset environment) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->environment_maps[key] = std::move(environment);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterEnvironmentMap(const std::string& key) {
  if (impl_->environment_maps.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const EnvironmentMapAsset* AssetRegistry::findEnvironmentMap(std::string_view key) const {
  return findInMap(impl_->environment_maps, key);
}

bool AssetRegistry::registerAnimationClip(const std::string& key,
                                          world::AnimationClip clip) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->animation_clips[key] = std::move(clip);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterAnimationClip(const std::string& key) {
  if (impl_->animation_clips.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const world::AnimationClip* AssetRegistry::findAnimationClip(std::string_view key) const {
  return findInMap(impl_->animation_clips, key);
}

bool AssetRegistry::registerSkeleton(const std::string& key, world::Skeleton skeleton) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->skeletons[key] = std::move(skeleton);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterSkeleton(const std::string& key) {
  if (impl_->skeletons.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const world::Skeleton* AssetRegistry::findSkeleton(std::string_view key) const {
  return findInMap(impl_->skeletons, key);
}

bool AssetRegistry::registerSkin(const std::string& key, world::Skin skin) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->skins[key] = std::move(skin);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterSkin(const std::string& key) {
  if (impl_->skins.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const world::Skin* AssetRegistry::findSkin(std::string_view key) const {
  return findInMap(impl_->skins, key);
}

bool AssetRegistry::registerGltfSceneAsset(const std::string& key, GltfSceneAsset scene) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->gltf_scenes[key] = std::move(scene);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterGltfSceneAsset(const std::string& key) {
  if (impl_->gltf_scenes.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const GltfSceneAsset* AssetRegistry::findGltfSceneAsset(std::string_view key) const {
  return findInMap(impl_->gltf_scenes, key);
}

}  // namespace karma::assets
