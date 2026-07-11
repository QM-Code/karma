#include "karma/assets.h"
#include "karma/scenes.h"

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
#include "asset_ui_source_import.h"
#include "material_registry_backing.h"

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
  static constexpr std::array<std::string_view, 26> kSourceExtensions{
      ".glb", ".gltf", ".obj", ".fbx", ".dae", ".png", ".jpg", ".jpeg", ".tga",
      ".bmp", ".hdr", ".exr", ".ktx", ".dds", ".mat", ".json", ".kpeffect",
      ".wav", ".ogg", ".mp3", ".json5", ".svg", ".ttf", ".otf", ".ttc", ".otc"};
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
  std::unordered_map<std::string, UiDocumentAsset> ui_documents;
  std::unordered_map<std::string, UiThemeAsset> ui_themes;
  std::unordered_map<std::string, std::filesystem::path> ui_document_source_paths;
  std::unordered_map<std::string, std::filesystem::path> ui_theme_source_paths;
  std::unordered_map<std::string, FontAsset> fonts;
  std::unordered_map<std::string, SvgAsset> svgs;
  std::unordered_map<std::string, std::weak_ptr<world::MeshData>> mesh_payloads_by_hash;
  std::unordered_map<std::string, std::weak_ptr<TextureAsset>> texture_payloads_by_hash;
  std::unordered_map<std::string, std::string> imported_texture_keys_by_source_hash;
  std::unordered_map<std::string, visual::particles::ParticleEffectAsset> particle_effects;
  std::unordered_map<std::string, AudioClipAsset> audio_clips;
  std::unordered_map<std::string, EnvironmentMapAsset> environment_maps;
  std::unordered_map<std::string, world::AnimationClip> animation_clips;
  std::unordered_map<std::string, world::Skeleton> skeletons;
  std::unordered_map<std::string, world::Skin> skins;
  std::unordered_map<std::string, world::HumanoidRig> humanoid_rigs;
  std::unordered_map<std::string, GltfSceneAsset> gltf_scenes;
  std::unordered_map<std::string, rendering::ShaderPassAssetDesc> shader_passes;
  std::unordered_map<std::string, rendering::FrameGraphDesc> frame_graphs;
  std::unordered_map<std::string, SceneAsset> scenes;
  rendering::MaterialLibrary materials;
  uint64_t version = 0;
  uint64_t mesh_version = 0;
  uint64_t texture_version = 0;
};

AssetRegistry::AssetRegistry() : impl_(std::make_unique<Impl>()) {}

AssetRegistry::~AssetRegistry() = default;

AssetPackageStore& AssetRegistry::sharedPackageStore() {
  const std::lock_guard lock(package_store_mutex_);
  if (!package_store_) {
    package_store_ = std::make_unique<AssetPackageStore>(*this);
  }
  return *package_store_;
}

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
  if (package_store_) {
    package_store_->clear();
  }
  impl_->meshes.clear();
  impl_->textures.clear();
  impl_->ui_documents.clear();
  impl_->ui_themes.clear();
  impl_->ui_document_source_paths.clear();
  impl_->ui_theme_source_paths.clear();
  impl_->fonts.clear();
  impl_->svgs.clear();
  impl_->mesh_payloads_by_hash.clear();
  impl_->texture_payloads_by_hash.clear();
  impl_->imported_texture_keys_by_source_hash.clear();
  impl_->particle_effects.clear();
  impl_->audio_clips.clear();
  impl_->environment_maps.clear();
  impl_->animation_clips.clear();
  impl_->skeletons.clear();
  impl_->skins.clear();
  impl_->humanoid_rigs.clear();
  impl_->gltf_scenes.clear();
  impl_->shader_passes.clear();
  impl_->frame_graphs.clear();
  impl_->scenes.clear();
  impl_->materials.clear();
  bumpMeshVersion();
  bumpTextureVersion();
}

bool AssetRegistry::moveAssetFrom(AssetRegistry& source,
                                  const std::string& type,
                                  const std::string& key) {
  if (!isValidAssetKey(key)) {
    return false;
  }

  if (type == "texture_rgba8" || type == "texture") {
    if (impl_->textures.find(key) != impl_->textures.end()) {
      return false;
    }
    auto node = source.impl_->textures.extract(key);
    if (node.empty() || !node.mapped()) {
      return false;
    }
    const std::shared_ptr<TextureAsset> payload = node.mapped();
    impl_->textures.insert(std::move(node));
    if (!payload->content_hash.empty()) {
      impl_->texture_payloads_by_hash[payload->content_hash] = payload;
    }
    bumpTextureVersion();
    source.bumpTextureVersion();
    return true;
  }

  if (type == "mesh") {
    if (impl_->meshes.find(key) != impl_->meshes.end()) {
      return false;
    }
    auto node = source.impl_->meshes.extract(key);
    if (node.empty() || !node.mapped()) {
      return false;
    }
    impl_->meshes.insert(std::move(node));
    bumpMeshVersion();
    source.bumpMeshVersion();
    return true;
  }

  if (type == "ui_document") {
    if (impl_->ui_documents.find(key) != impl_->ui_documents.end()) {
      return false;
    }
    auto node = source.impl_->ui_documents.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->ui_documents.insert(std::move(node));
    if (auto source_path = source.impl_->ui_document_source_paths.extract(key);
        !source_path.empty()) {
      impl_->ui_document_source_paths.insert(std::move(source_path));
    }
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "ui_theme") {
    if (impl_->ui_themes.find(key) != impl_->ui_themes.end()) {
      return false;
    }
    auto node = source.impl_->ui_themes.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->ui_themes.insert(std::move(node));
    if (auto source_path = source.impl_->ui_theme_source_paths.extract(key);
        !source_path.empty()) {
      impl_->ui_theme_source_paths.insert(std::move(source_path));
    }
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "font") {
    if (impl_->fonts.find(key) != impl_->fonts.end()) {
      return false;
    }
    auto node = source.impl_->fonts.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->fonts.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "svg") {
    if (impl_->svgs.find(key) != impl_->svgs.end()) {
      return false;
    }
    auto node = source.impl_->svgs.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->svgs.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "particle_effect") {
    if (impl_->particle_effects.find(key) != impl_->particle_effects.end()) {
      return false;
    }
    auto node = source.impl_->particle_effects.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->particle_effects.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "environment_map") {
    if (impl_->environment_maps.find(key) != impl_->environment_maps.end()) {
      return false;
    }
    auto node = source.impl_->environment_maps.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->environment_maps.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "gltf_scene") {
    if (impl_->gltf_scenes.find(key) != impl_->gltf_scenes.end()) {
      return false;
    }
    auto node = source.impl_->gltf_scenes.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->gltf_scenes.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "shader_pass") {
    if (impl_->shader_passes.find(key) != impl_->shader_passes.end()) {
      return false;
    }
    auto node = source.impl_->shader_passes.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->shader_passes.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "render_graph") {
    if (impl_->frame_graphs.find(key) != impl_->frame_graphs.end()) {
      return false;
    }
    auto node = source.impl_->frame_graphs.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->frame_graphs.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "scene") {
    if (impl_->scenes.find(key) != impl_->scenes.end()) {
      return false;
    }
    auto node = source.impl_->scenes.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->scenes.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "animation_clip") {
    if (impl_->animation_clips.find(key) != impl_->animation_clips.end()) {
      return false;
    }
    auto node = source.impl_->animation_clips.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->animation_clips.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "skeleton") {
    if (impl_->skeletons.find(key) != impl_->skeletons.end()) {
      return false;
    }
    auto node = source.impl_->skeletons.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->skeletons.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "skin") {
    if (impl_->skins.find(key) != impl_->skins.end()) {
      return false;
    }
    auto node = source.impl_->skins.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->skins.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  if (type == "humanoid_rig") {
    if (impl_->humanoid_rigs.find(key) != impl_->humanoid_rigs.end()) {
      return false;
    }
    auto node = source.impl_->humanoid_rigs.extract(key);
    if (node.empty()) {
      return false;
    }
    impl_->humanoid_rigs.insert(std::move(node));
    bumpVersion();
    source.bumpVersion();
    return true;
  }

  return false;
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

bool AssetRegistry::registerUiDocumentAsset(const std::string& key,
                                            UiDocumentAsset document) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  document.content_hash = hashString(document.canonical_json_utf8);
  impl_->ui_documents[key] = std::move(document);
  impl_->ui_document_source_paths.erase(key);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterUiDocumentAsset(const std::string& key) {
  if (impl_->ui_documents.erase(key) == 0u) {
    return false;
  }
  impl_->ui_document_source_paths.erase(key);
  bumpVersion();
  return true;
}

const UiDocumentAsset* AssetRegistry::findUiDocumentAsset(std::string_view key) const {
  return findInMap(impl_->ui_documents, key);
}

bool AssetRegistry::registerUiThemeAsset(const std::string& key,
                                         UiThemeAsset theme) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  theme.content_hash = hashString(theme.canonical_json_utf8);
  impl_->ui_themes[key] = std::move(theme);
  impl_->ui_theme_source_paths.erase(key);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterUiThemeAsset(const std::string& key) {
  if (impl_->ui_themes.erase(key) == 0u) {
    return false;
  }
  impl_->ui_theme_source_paths.erase(key);
  bumpVersion();
  return true;
}

const UiThemeAsset* AssetRegistry::findUiThemeAsset(std::string_view key) const {
  return findInMap(impl_->ui_themes, key);
}

void detail::UiSourceMetadataAccess::setDocument(
    AssetRegistry& assets,
    std::string_view key,
    const std::filesystem::path& path) {
  const std::string owned_key(key);
  if (assets.impl_->ui_documents.contains(owned_key)) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    assets.impl_->ui_document_source_paths[owned_key] =
        (error ? path : absolute).lexically_normal();
  }
}

void detail::UiSourceMetadataAccess::setTheme(
    AssetRegistry& assets,
    std::string_view key,
    const std::filesystem::path& path) {
  const std::string owned_key(key);
  if (assets.impl_->ui_themes.contains(owned_key)) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    assets.impl_->ui_theme_source_paths[owned_key] =
        (error ? path : absolute).lexically_normal();
  }
}

std::optional<std::filesystem::path>
detail::UiSourceMetadataAccess::document(const AssetRegistry& assets,
                                         std::string_view key) {
  const auto found = assets.impl_->ui_document_source_paths.find(std::string(key));
  return found == assets.impl_->ui_document_source_paths.end()
             ? std::nullopt
             : std::optional<std::filesystem::path>(found->second);
}

std::optional<std::filesystem::path>
detail::UiSourceMetadataAccess::theme(const AssetRegistry& assets,
                                      std::string_view key) {
  const auto found = assets.impl_->ui_theme_source_paths.find(std::string(key));
  return found == assets.impl_->ui_theme_source_paths.end()
             ? std::nullopt
             : std::optional<std::filesystem::path>(found->second);
}

bool AssetRegistry::registerFontAsset(const std::string& key, FontAsset font) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  font.content_hash = hashBytes(font.bytes.data(), font.bytes.size());
  impl_->fonts[key] = std::move(font);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterFontAsset(const std::string& key) {
  if (impl_->fonts.erase(key) == 0u) {
    return false;
  }
  bumpVersion();
  return true;
}

const FontAsset* AssetRegistry::findFontAsset(std::string_view key) const {
  return findInMap(impl_->fonts, key);
}

bool AssetRegistry::registerSvgAsset(const std::string& key, SvgAsset svg) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  svg.content_hash = hashString(svg.source_utf8);
  impl_->svgs[key] = std::move(svg);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterSvgAsset(const std::string& key) {
  if (impl_->svgs.erase(key) == 0u) {
    return false;
  }
  bumpVersion();
  return true;
}

const SvgAsset* AssetRegistry::findSvgAsset(std::string_view key) const {
  return findInMap(impl_->svgs, key);
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
    // The same source image may legally feed both color and data slots. Those
    // require different GPU formats and must not alias merely because their
    // encoded bytes are identical.
    const std::string source_variant =
        source_hash + (imported.srgb ? ":srgb" : ":linear");
    std::string texture_key;
    if (const auto existing =
            impl_->imported_texture_keys_by_source_hash.find(source_variant);
        existing != impl_->imported_texture_keys_by_source_hash.end()) {
      texture_key = existing->second;
    } else {
      std::optional<Rgba8Image> image = detail::decodeImportedTexture(imported);
      if (!image.has_value() || !image->valid()) {
        continue;
      }

      const std::string label = detail::sanitizeTextureKeySegment(
          imported.label.empty() ? imported.raw_name : imported.label);
      texture_key = "gltf/textures/" + source_hash +
                    (imported.srgb ? "/srgb/" : "/linear/") + label;
      if (!AssetRegistry::isValidAssetKey(texture_key)) {
        texture_key = "gltf/textures/" + source_hash +
                      (imported.srgb ? "/srgb" : "/linear");
      }

      const TextureAsset::Semantic texture_semantic =
          detail::importedTextureSemantic(imported.semantic, imported.srgb);
      const bool preserve_exact =
          texture_semantic == TextureAsset::Semantic::Normal ||
          texture_semantic == TextureAsset::Semantic::Data ||
          imported.semantic == rendering::ImportedMaterialTextureSemantic::Emissive;
      const bool prefer_compressed =
          !preserve_exact;
      TextureAsset texture = detail::makeTextureAssetFromImage(std::move(*image),
                                                               imported.srgb,
                                                               true,
                                                               texture_semantic,
                                                               prefer_compressed);
      if (!registerTextureAsset(texture_key, std::move(texture))) {
        continue;
      }
      impl_->imported_texture_keys_by_source_hash[source_variant] = texture_key;
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

bool AssetRegistry::registerShaderPass(const std::string& key,
                                       rendering::ShaderPassAssetDesc pass) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  pass.shader_pass_key = key;
  impl_->shader_passes[key] = std::move(pass);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterShaderPass(const std::string& key) {
  if (impl_->shader_passes.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const rendering::ShaderPassAssetDesc* AssetRegistry::findShaderPass(
    std::string_view key) const {
  const auto it = impl_->shader_passes.find(std::string(key));
  return it == impl_->shader_passes.end() ? nullptr : &it->second;
}

bool AssetRegistry::registerFrameGraph(const std::string& key,
                                       rendering::FrameGraphDesc graph) {
  const bool default_graph_key =
      key.empty() || key == rendering::kDefaultFrameGraphKey;
  if (!default_graph_key && !isValidAssetKey(key)) {
    return false;
  }
  const std::string resolved_key =
      default_graph_key ? std::string(rendering::kDefaultFrameGraphKey) : key;
  graph.frame_graph_key = resolved_key;
  impl_->frame_graphs[resolved_key] = std::move(graph);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterFrameGraph(const std::string& key) {
  const std::string resolved_key =
      (key.empty() || key == rendering::kDefaultFrameGraphKey)
          ? std::string(rendering::kDefaultFrameGraphKey)
          : key;
  if (impl_->frame_graphs.erase(resolved_key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const rendering::FrameGraphDesc* AssetRegistry::findFrameGraph(
    std::string_view key) const {
  const std::string resolved_key =
      (key.empty() || key == rendering::kDefaultFrameGraphKey)
          ? std::string(rendering::kDefaultFrameGraphKey)
          : std::string(key);
  const auto it = impl_->frame_graphs.find(resolved_key);
  return it == impl_->frame_graphs.end() ? nullptr : &it->second;
}

const rendering::FrameGraphDesc& AssetRegistry::resolveFrameGraph(
    std::string_view key) const {
  if (const rendering::FrameGraphDesc* graph = findFrameGraph(key)) {
    return *graph;
  }
  if (const rendering::FrameGraphDesc* graph =
          findFrameGraph(rendering::kDefaultFrameGraphKey)) {
    return *graph;
  }
  return rendering::defaultFrameGraphDesc();
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

bool AssetRegistry::registerHumanoidRig(const std::string& key, world::HumanoidRig rig) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->humanoid_rigs[key] = std::move(rig);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterHumanoidRig(const std::string& key) {
  if (impl_->humanoid_rigs.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const world::HumanoidRig* AssetRegistry::findHumanoidRig(std::string_view key) const {
  return findInMap(impl_->humanoid_rigs, key);
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

bool AssetRegistry::registerSceneAsset(const std::string& key, SceneAsset scene) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->scenes[key] = std::move(scene);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterSceneAsset(const std::string& key) {
  if (impl_->scenes.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const SceneAsset* AssetRegistry::findSceneAsset(std::string_view key) const {
  return findInMap(impl_->scenes, key);
}

}  // namespace karma::assets
