#include "karma/content/assets/asset_registry.h"

#include <cstddef>
#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "karma/content/importers/gltf_scene_import.h"
#include "karma/content/importers/mesh_import.h"
#include "karma/content/image/image.h"
#include "karma/features/visual/particles/effect_asset.h"

#include "material_registry_backing.h"
#include "post_process_profile_registry_backing.h"

namespace karma::content {

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

std::string childKey(const std::string& parent,
                     std::string_view kind,
                     std::size_t index) {
  return parent + "/" + std::string(kind) + "/" + std::to_string(index);
}

geometry::MeshData combineMeshes(std::vector<geometry::MeshData> meshes) {
  geometry::MeshData combined{};
  for (geometry::MeshData& mesh : meshes) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }

    const uint32_t vertex_base = static_cast<uint32_t>(combined.vertices.size());
    const uint32_t index_base = static_cast<uint32_t>(combined.indices.size());
    combined.vertices.insert(combined.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    combined.normals.insert(combined.normals.end(), mesh.normals.begin(), mesh.normals.end());
    combined.uvs.insert(combined.uvs.end(), mesh.uvs.begin(), mesh.uvs.end());
    combined.uvs1.insert(combined.uvs1.end(), mesh.uvs1.begin(), mesh.uvs1.end());
    combined.tangents.insert(combined.tangents.end(), mesh.tangents.begin(), mesh.tangents.end());
    combined.joint_indices.insert(combined.joint_indices.end(),
                                  mesh.joint_indices.begin(),
                                  mesh.joint_indices.end());
    combined.joint_weights.insert(combined.joint_weights.end(),
                                  mesh.joint_weights.begin(),
                                  mesh.joint_weights.end());

    for (uint32_t index : mesh.indices) {
      combined.indices.push_back(vertex_base + index);
    }

    const uint32_t material_slot_base =
        static_cast<uint32_t>(combined.material_slots.size());
    combined.material_slots.insert(combined.material_slots.end(),
                                   mesh.material_slots.begin(),
                                   mesh.material_slots.end());
    if (!mesh.submeshes.empty()) {
      for (const geometry::MeshSubmesh& submesh : mesh.submeshes) {
        combined.submeshes.push_back(geometry::MeshSubmesh{
            .index_offset = index_base + submesh.index_offset,
            .index_count = submesh.index_count,
            .material_slot = material_slot_base + submesh.material_slot,
        });
      }
    } else {
      combined.submeshes.push_back(geometry::MeshSubmesh{
          .index_offset = index_base,
          .index_count = static_cast<uint32_t>(mesh.indices.size()),
          .material_slot = material_slot_base,
      });
    }
  }
  return combined;
}

template <typename T>
const T* findInMap(const std::unordered_map<std::string, T>& map,
                   std::string_view key) {
  const auto it = map.find(std::string(key));
  return it != map.end() ? &it->second : nullptr;
}

}  // namespace

struct AssetRegistry::Impl {
  std::unordered_map<std::string, geometry::MeshData> meshes;
  std::unordered_map<std::string, TextureAsset> textures;
  std::unordered_map<std::string, particles::ParticleEffectAsset> particle_effects;
  std::unordered_map<std::string, AudioClipAsset> audio_clips;
  std::unordered_map<std::string, EnvironmentMapAsset> environment_maps;
  std::unordered_map<std::string, animation::AnimationClip> animation_clips;
  std::unordered_map<std::string, animation::Skeleton> skeletons;
  std::unordered_map<std::string, animation::Skin> skins;
  std::unordered_map<std::string, GltfSceneAsset> gltf_scenes;
  renderer::MaterialLibrary materials;
  renderer::PostProcessProfileLibrary post_process_profiles;
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
  impl_->particle_effects.clear();
  impl_->audio_clips.clear();
  impl_->environment_maps.clear();
  impl_->animation_clips.clear();
  impl_->skeletons.clear();
  impl_->skins.clear();
  impl_->gltf_scenes.clear();
  impl_->materials.clear();
  impl_->post_process_profiles = renderer::PostProcessProfileLibrary{};
  bumpMeshVersion();
  bumpTextureVersion();
}

bool AssetRegistry::registerMeshAsset(const std::string& key, geometry::MeshData mesh) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->meshes[key] = std::move(mesh);
  bumpMeshVersion();
  return true;
}

bool AssetRegistry::importMeshAsset(const std::string& key, const std::filesystem::path& path) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  std::vector<geometry::MeshData> imported = importMeshes(path);
  geometry::MeshData combined = combineMeshes(std::move(imported));
  if (combined.vertices.empty() || combined.indices.empty()) {
    return false;
  }
  return registerMeshAsset(key, std::move(combined));
}

bool AssetRegistry::unregisterMeshAsset(const std::string& key) {
  if (impl_->meshes.erase(key) == 0) {
    return false;
  }
  bumpMeshVersion();
  return true;
}

const geometry::MeshData* AssetRegistry::findMeshAsset(std::string_view key) const {
  return findInMap(impl_->meshes, key);
}

bool AssetRegistry::registerTextureAsset(const std::string& key, TextureAsset texture) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->textures[key] = std::move(texture);
  bumpTextureVersion();
  return true;
}

bool AssetRegistry::importTextureAsset(const std::string& key,
                                       const std::filesystem::path& path,
                                       const TextureImportOptions& options) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  std::optional<Rgba8Image> image = loadRgba8Image(path);
  if (!image.has_value() || !image->valid()) {
    return false;
  }
  TextureAsset texture{};
  texture.desc.width = image->width;
  texture.desc.height = image->height;
  texture.desc.format = renderer::TextureFormat::RGBA8;
  texture.desc.srgb = options.srgb;
  texture.desc.generate_mips = options.generate_mips;
  texture.bytes = std::move(image->pixels);
  return registerTextureAsset(key, std::move(texture));
}

bool AssetRegistry::unregisterTextureAsset(const std::string& key) {
  if (impl_->textures.erase(key) == 0) {
    return false;
  }
  bumpTextureVersion();
  return true;
}

const TextureAsset* AssetRegistry::findTextureAsset(std::string_view key) const {
  return findInMap(impl_->textures, key);
}

bool AssetRegistry::registerMaterialAsset(const std::string& key,
                                          renderer::MaterialAssetDesc material) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->materials.registerMaterialAsset(key, std::move(material));
  bumpVersion();
  return true;
}

bool AssetRegistry::registerMaterialAsset(const std::string& key,
                                          renderer::MaterialDesc surface) {
  renderer::MaterialAssetDesc material{};
  material.surface = std::move(surface);
  return registerMaterialAsset(key, std::move(material));
}

bool AssetRegistry::registerMaterialVariant(const std::string& key,
                                            renderer::MaterialVariantDesc material) {
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
    std::unordered_map<std::string, renderer::MaterialParameterValue> params,
    std::unordered_map<std::string, std::string> textures) {
  renderer::MaterialVariantDesc material{};
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

const renderer::MaterialAssetDesc* AssetRegistry::findMaterialAsset(
    std::string_view key) const {
  return impl_->materials.findAsset(std::string(key));
}

const renderer::MaterialVariantDesc* AssetRegistry::findMaterialVariant(
    std::string_view key) const {
  return impl_->materials.findVariant(std::string(key));
}

std::optional<renderer::ResolvedMaterialDesc> AssetRegistry::resolveMaterial(
    std::string_view key) const {
  return impl_->materials.resolve(std::string(key));
}

bool AssetRegistry::registerPostProcessProfile(const std::string& key,
                                               renderer::PostProcessSettings profile) {
  if (!isValidAssetKey(key)) {
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

const renderer::PostProcessSettings* AssetRegistry::findPostProcessProfile(
    std::string_view key) const {
  return impl_->post_process_profiles.find(key);
}

const renderer::PostProcessSettings& AssetRegistry::resolvePostProcessProfile(
    std::string_view key) const {
  return impl_->post_process_profiles.resolve(key);
}

bool AssetRegistry::registerParticleEffect(const std::string& key,
                                           particles::ParticleEffectAsset effect) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->particle_effects[key] = std::move(effect);
  bumpVersion();
  return true;
}

bool AssetRegistry::importParticleEffect(const std::string& key,
                                         const std::filesystem::path& path) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  particles::ParticleEffectAsset effect{};
  if (!particles::loadParticleEffectAsset(path, effect)) {
    return false;
  }
  return registerParticleEffect(key, std::move(effect));
}

bool AssetRegistry::unregisterParticleEffect(const std::string& key) {
  if (impl_->particle_effects.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const particles::ParticleEffectAsset* AssetRegistry::findParticleEffect(
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
                                          animation::AnimationClip clip) {
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

const animation::AnimationClip* AssetRegistry::findAnimationClip(std::string_view key) const {
  return findInMap(impl_->animation_clips, key);
}

bool AssetRegistry::registerSkeleton(const std::string& key, animation::Skeleton skeleton) {
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

const animation::Skeleton* AssetRegistry::findSkeleton(std::string_view key) const {
  return findInMap(impl_->skeletons, key);
}

bool AssetRegistry::registerSkin(const std::string& key, animation::Skin skin) {
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

const animation::Skin* AssetRegistry::findSkin(std::string_view key) const {
  return findInMap(impl_->skins, key);
}

GltfSceneAsset AssetRegistry::importGltfSceneAsset(const std::string& key,
                                                   const std::filesystem::path& path) {
  return importGltfSceneAsset(key, path, scene::GltfSceneLoadOptions{});
}

GltfSceneAsset AssetRegistry::importGltfSceneAsset(
    const std::string& key,
    const std::filesystem::path& path,
    const scene::GltfSceneLoadOptions& options) {
  if (!isValidAssetKey(key)) {
    return {};
  }
  scene::GltfScenePrefab prefab = scene::loadGltfScenePrefab(path, options);

  GltfSceneAsset asset{};
  asset.source_path = prefab.source_path;
  asset.scene_key = key + "/scene";

  std::size_t mesh_index = 0;
  std::size_t material_index = 0;
  for (const scene::GltfScenePrefabNode& node : prefab.nodes) {
    for (const scene::GltfScenePrefabPrimitive& primitive : node.primitives) {
      const std::string mesh_key = childKey(key, "meshes", mesh_index++);
      if (!registerMeshAsset(mesh_key, primitive.mesh)) {
        return {};
      }
      asset.mesh_asset_keys.push_back(mesh_key);

      const std::string material_key = childKey(key, "materials", material_index++);
      renderer::MaterialAssetDesc material{};
      material.surface = primitive.material;
      if (primitive.source_material_index < prefab.imported_materials.size()) {
        material.imported_material = prefab.imported_materials[primitive.source_material_index];
      }
      if (!registerMaterialAsset(material_key, std::move(material))) {
        return {};
      }
      asset.material_keys.push_back(material_key);
    }
  }

  for (std::size_t i = 0; i < prefab.animations.size(); ++i) {
    const std::string clip_key = childKey(key, "animation_clips", i);
    if (!registerAnimationClip(clip_key, prefab.animations[i])) {
      return {};
    }
    asset.animation_clip_keys.push_back(clip_key);
  }
  for (std::size_t i = 0; i < prefab.skeletons.size(); ++i) {
    const std::string skeleton_key = childKey(key, "skeletons", i);
    if (!registerSkeleton(skeleton_key, prefab.skeletons[i])) {
      return {};
    }
    asset.skeleton_keys.push_back(skeleton_key);
  }
  for (std::size_t i = 0; i < prefab.skins.size(); ++i) {
    const std::string skin_key = childKey(key, "skins", i);
    if (!registerSkin(skin_key, prefab.skins[i])) {
      return {};
    }
    asset.skin_keys.push_back(skin_key);
  }

  if (!registerGltfSceneAsset(key, asset)) {
    return {};
  }
  return asset;
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

}  // namespace karma::content
