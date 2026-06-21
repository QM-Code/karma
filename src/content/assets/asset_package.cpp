#include "karma/assets.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <assimp/version.h>
#include <nlohmann/json.hpp>

#include "karma/assets.h"

#include "asset_source_import.h"
#include "../importers/gltf_scene_import_internal.h"

namespace karma::assets {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kPackageCacheContentVersion =
    "package-cache-v2-gltf-scene-metadata-mesh-binary";

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

std::filesystem::path packageDirectory(const std::filesystem::path& manifest_path) {
  const std::filesystem::path parent = manifest_path.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

std::filesystem::path resolveEntryPath(const std::filesystem::path& base,
                                       const std::string& value) {
  std::filesystem::path path(value);
  if (path.is_relative()) {
    path = base / path;
  }
  return path.lexically_normal();
}

bool readRequiredString(const Json& object,
                        const char* field,
                        std::string& out,
                        std::string* diagnostic) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_string() || it->get<std::string>().empty()) {
    return fail(diagnostic, std::string("asset package entry requires string field: ") + field);
  }
  out = it->get<std::string>();
  return true;
}

bool readRequiredPath(const Json& object,
                      const std::filesystem::path& base,
                      std::filesystem::path& out,
                      std::string* diagnostic) {
  std::string value;
  if (!readRequiredString(object, "path", value, diagnostic)) {
    return false;
  }
  out = resolveEntryPath(base, value);
  return true;
}

bool readJson(const std::filesystem::path& path, Json& out, std::string* diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to open asset package: " + path.string());
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    return fail(diagnostic, std::string("failed to parse asset package JSON: ") + e.what());
  }
  if (!out.is_object()) {
    return fail(diagnostic, "asset package root must be an object");
  }
  const auto version_it = out.find("version");
  if (version_it == out.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 1) {
    return fail(diagnostic, "asset package version must be integer 1");
  }
  const auto assets_it = out.find("assets");
  if (assets_it == out.end() || !assets_it->is_array()) {
    return fail(diagnostic, "asset package requires an assets array");
  }
  return true;
}

bool keyAlreadyExists(const AssetRegistry& assets,
                      const std::string& type,
                      const std::string& key) {
  if (type == "texture_rgba8" || type == "texture") {
    return assets.findTextureAsset(key) != nullptr;
  }
  if (type == "mesh") {
    return assets.findMeshAsset(key) != nullptr;
  }
  if (type == "material") {
    return assets.findMaterialAsset(key) != nullptr ||
           assets.findMaterialVariant(key) != nullptr;
  }
  if (type == "particle_effect") {
    return assets.findParticleEffect(key) != nullptr;
  }
  if (type == "environment_map") {
    return assets.findEnvironmentMap(key) != nullptr;
  }
  if (type == "gltf_scene") {
    return assets.findGltfSceneAsset(key) != nullptr;
  }
  if (type == "animation_clip") {
    return assets.findAnimationClip(key) != nullptr;
  }
  if (type == "skeleton") {
    return assets.findSkeleton(key) != nullptr;
  }
  if (type == "skin") {
    return assets.findSkin(key) != nullptr;
  }
  return false;
}

bool copyAssetTo(AssetRegistry& target,
                 const AssetRegistry& source,
                 const AssetPackageLoadedAsset& asset,
                 std::string* diagnostic) {
  if (asset.type == "texture_rgba8" || asset.type == "texture") {
    const TextureAsset* texture = source.findTextureAsset(asset.key);
    if (texture == nullptr || !target.registerTextureAsset(asset.key, *texture)) {
      return fail(diagnostic, "failed to commit texture asset: " + asset.key);
    }
    return true;
  }
  if (asset.type == "mesh") {
    const world::MeshData* mesh = source.findMeshAsset(asset.key);
    if (mesh == nullptr || !target.registerMeshAsset(asset.key, *mesh)) {
      return fail(diagnostic, "failed to commit mesh asset: " + asset.key);
    }
    return true;
  }
  if (asset.type == "material") {
    if (const rendering::MaterialAssetDesc* material = source.findMaterialAsset(asset.key)) {
      if (!target.registerMaterialAsset(asset.key, *material)) {
        return fail(diagnostic, "failed to commit material asset: " + asset.key);
      }
      return true;
    }
    if (const rendering::MaterialVariantDesc* variant = source.findMaterialVariant(asset.key)) {
      if (!target.registerMaterialVariant(asset.key, *variant)) {
        return fail(diagnostic, "failed to commit material variant: " + asset.key);
      }
      return true;
    }
    return fail(diagnostic, "missing staged material asset: " + asset.key);
  }
  if (asset.type == "particle_effect") {
    const visual::particles::ParticleEffectAsset* effect = source.findParticleEffect(asset.key);
    if (effect == nullptr || !target.registerParticleEffect(asset.key, *effect)) {
      return fail(diagnostic, "failed to commit particle effect: " + asset.key);
    }
    return true;
  }
  if (asset.type == "environment_map") {
    const EnvironmentMapAsset* environment = source.findEnvironmentMap(asset.key);
    if (environment == nullptr || !target.registerEnvironmentMap(asset.key, *environment)) {
      return fail(diagnostic, "failed to commit environment map: " + asset.key);
    }
    return true;
  }
  if (asset.type == "gltf_scene") {
    const GltfSceneAsset* scene = source.findGltfSceneAsset(asset.key);
    if (scene == nullptr || !target.registerGltfSceneAsset(asset.key, *scene)) {
      return fail(diagnostic, "failed to commit glTF scene: " + asset.key);
    }
    return true;
  }
  if (asset.type == "animation_clip") {
    const world::AnimationClip* clip = source.findAnimationClip(asset.key);
    if (clip == nullptr || !target.registerAnimationClip(asset.key, *clip)) {
      return fail(diagnostic, "failed to commit animation clip: " + asset.key);
    }
    return true;
  }
  if (asset.type == "skeleton") {
    const world::Skeleton* skeleton = source.findSkeleton(asset.key);
    if (skeleton == nullptr || !target.registerSkeleton(asset.key, *skeleton)) {
      return fail(diagnostic, "failed to commit skeleton: " + asset.key);
    }
    return true;
  }
  if (asset.type == "skin") {
    const world::Skin* skin = source.findSkin(asset.key);
    if (skin == nullptr || !target.registerSkin(asset.key, *skin)) {
      return fail(diagnostic, "failed to commit skin: " + asset.key);
    }
    return true;
  }
  return fail(diagnostic, "unsupported staged asset type: " + asset.type);
}

void addLoaded(AssetPackageHandle& handle,
               std::string type,
               std::string key,
               std::string cache_blob_key = {}) {
  for (auto& asset : handle.assets) {
    if (asset.type == type && asset.key == key) {
      if (asset.cache_blob_key.empty() && !cache_blob_key.empty()) {
        asset.cache_blob_key = std::move(cache_blob_key);
      }
      return;
    }
  }
  handle.assets.push_back(AssetPackageLoadedAsset{
      .type = std::move(type),
      .key = std::move(key),
      .cache_blob_key = std::move(cache_blob_key),
  });
}

bool importEntry(AssetRegistry& assets,
                 const Json& entry,
                 const std::filesystem::path& base_dir,
                 AssetPackageHandle& handle,
                 std::string* diagnostic) {
  if (!entry.is_object()) {
    return fail(diagnostic, "asset package entries must be objects");
  }

  std::string type;
  std::string key;
  if (!readRequiredString(entry, "type", type, diagnostic) ||
      !readRequiredString(entry, "key", key, diagnostic)) {
    return false;
  }
  if (!AssetRegistry::isValidAssetKey(key)) {
    return fail(diagnostic, "invalid asset key '" + key + "': " +
                                AssetRegistry::assetKeyValidationError(key));
  }
  if (keyAlreadyExists(assets, type, key)) {
    return fail(diagnostic, "asset package would overwrite existing key: " + key);
  }

  std::filesystem::path source_path;
  if (type == "texture_rgba8") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    TextureImportOptions options{};
    if (const auto it = entry.find("srgb"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.srgb must be a boolean");
      }
      options.srgb = it->get<bool>();
    }
    if (const auto it = entry.find("generate_mips"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.generate_mips must be a boolean");
      }
      options.generate_mips = it->get<bool>();
    }
    if (const auto it = entry.find("alpha_bleed"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.alpha_bleed must be a boolean");
      }
      options.alpha_bleed = it->get<bool>();
    }
    if (const auto it = entry.find("alpha_coverage_cutoff"); it != entry.end()) {
      if (!it->is_number()) {
        return fail(diagnostic, "texture_rgba8.alpha_coverage_cutoff must be a number");
      }
      options.alpha_coverage_cutoff = it->get<float>();
      if (options.alpha_coverage_cutoff < 0.0f || options.alpha_coverage_cutoff > 1.0f) {
        return fail(diagnostic,
                    "texture_rgba8.alpha_coverage_cutoff must be between 0 and 1");
      }
    }
    if (!detail::importTextureAsset(assets, key, source_path, options)) {
      return fail(diagnostic, "failed to import texture asset: " + source_path.string());
    }
    addLoaded(handle, type, key);
    return true;
  }

  if (type == "mesh") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!detail::importMeshAsset(assets, key, source_path)) {
      return fail(diagnostic, "failed to import mesh asset: " + source_path.string());
    }
    addLoaded(handle, type, key);
    return true;
  }

  if (type == "material") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    MaterialLoadResult result = loadMaterialFile(assets, key, source_path);
    if (!result.success) {
      return fail(diagnostic, "failed to import material asset '" + source_path.string() +
                                  "': " + result.diagnostic);
    }
    addLoaded(handle, type, key);
    return true;
  }

  if (type == "particle_effect") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!detail::importParticleEffect(assets, key, source_path)) {
      return fail(diagnostic, "failed to import particle effect: " + source_path.string());
    }
    addLoaded(handle, type, key);
    return true;
  }

  if (type == "environment_map") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!assets.registerEnvironmentMap(key, EnvironmentMapAsset{.path = source_path})) {
      return fail(diagnostic, "failed to register environment map: " + key);
    }
    addLoaded(handle, type, key);
    return true;
  }

  if (type == "gltf_scene") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    world::GltfSceneLoadOptions load_options{};
    load_options.import_meshes = entry.value("import_meshes", true);
    load_options.import_lights = entry.value("import_lights", true);
    GltfSceneAsset scene = detail::importGltfSceneAsset(assets, key, source_path, load_options);
    if (scene.scene_key.empty()) {
      return fail(diagnostic, "failed to import glTF scene: " + source_path.string());
    }
    addLoaded(handle, "gltf_scene", key);
    for (const std::string& child_key : scene.mesh_asset_keys) {
      addLoaded(handle, "mesh", child_key);
    }
    for (const std::string& child_key : scene.texture_asset_keys) {
      addLoaded(handle, "texture", child_key);
    }
    for (const std::string& child_key : scene.material_keys) {
      addLoaded(handle, "material", child_key);
    }
    for (const std::string& child_key : scene.animation_clip_keys) {
      addLoaded(handle, "animation_clip", child_key);
    }
    for (const std::string& child_key : scene.skeleton_keys) {
      addLoaded(handle, "skeleton", child_key);
    }
    for (const std::string& child_key : scene.skin_keys) {
      addLoaded(handle, "skin", child_key);
    }
    return true;
  }

  return fail(diagnostic, "unsupported asset package type: " + type);
}

std::string assimpVersionString() {
  return std::to_string(aiGetVersionMajor()) + "." +
         std::to_string(aiGetVersionMinor()) + "." +
         std::to_string(aiGetVersionRevision());
}

std::string ktxDependencyString() {
#if defined(KARMA_ENABLE_KTX2)
#if !defined(KARMA_KTX_SOFTWARE_TAG)
#define KARMA_KTX_SOFTWARE_TAG "system"
#endif
  return std::string("libktx:") + KARMA_KTX_SOFTWARE_TAG;
#else
  return "no-ktx2";
#endif
}

std::string importerVersionForType(std::string_view type) {
  if (type == "texture_rgba8" || type == "texture") {
#if defined(KARMA_ENABLE_KTX2)
    return "texture-ktx2-uastc-v2";
#else
    return "texture-rgba8-v3";
#endif
  }
  if (type == "mesh" || type == "gltf_scene") {
    return "assimp-gltf-scene-v3:" + assimpVersionString();
  }
  if (type == "material") {
    return "material-loader-v2";
  }
  if (type == "particle_effect") {
    return "particle-effect-v3";
  }
  if (type == "environment_map") {
    return "environment-path-v1";
  }
  return "unknown";
}

Json packageEntryCacheRecord(const Json& entry,
                             const std::filesystem::path& base_dir,
                             std::string* diagnostic) {
  Json record = Json::object();
  if (!entry.is_object()) {
    return record;
  }
  const std::string type = entry.value("type", std::string{});
  record["type"] = type;
  record["key"] = entry.value("key", std::string{});
  record["importer_version"] = importerVersionForType(type);
  record["entry"] = entry;
  if (const auto path_it = entry.find("path"); path_it != entry.end() && path_it->is_string()) {
    const std::filesystem::path source = resolveEntryPath(base_dir, path_it->get<std::string>());
    record["source"] = source.lexically_normal().generic_string();
    if (const auto file_hash = hashFile(source)) {
      record["source_hash"] = *file_hash;
    } else if (diagnostic != nullptr) {
      *diagnostic = "failed to hash package source: " + source.string();
    }
  }
  return record;
}

std::string packageCacheKey(const std::filesystem::path& manifest_path,
                            const Json& package_json,
                            std::string_view manifest_hash,
                            std::string* diagnostic) {
  const std::filesystem::path base_dir = packageDirectory(manifest_path);
  Json key{
      {"asset_cache_version", std::string(AssetCache::kAssetCacheVersion)},
      {"package_cache_content_version", std::string(kPackageCacheContentVersion)},
      {"manifest_path", manifest_path.lexically_normal().generic_string()},
      {"manifest_hash", std::string(manifest_hash)},
      {"assimp_version", assimpVersionString()},
      {"ktx_dependency", ktxDependencyString()},
      {"texture_profile", "ktx2_basis_uastc_zstd_rgba8_fallback"},
      {"package_options", Json::object()},
      {"assets", Json::array()},
  };
  for (const Json& entry : package_json["assets"]) {
    key["assets"].push_back(packageEntryCacheRecord(entry, base_dir, diagnostic));
  }
  return hashString(key.dump());
}

std::string packageAssetBlobKey(std::string_view package_key,
                                std::string_view type,
                                std::string_view key) {
  return hashString(Json{
      {"asset_cache_version", std::string(AssetCache::kAssetCacheVersion)},
      {"package_cache_content_version", std::string(kPackageCacheContentVersion)},
      {"package", std::string(package_key)},
      {"type", std::string(type)},
      {"key", std::string(key)},
  }.dump());
}

void assignPackageBlobKeys(AssetPackageHandle& handle, std::string_view package_key) {
  for (auto& asset : handle.assets) {
    if (asset.type == "environment_map") {
      asset.cache_blob_key.clear();
      continue;
    }
    asset.cache_blob_key = packageAssetBlobKey(package_key, asset.type, asset.key);
  }
}

std::string blobTypeForAsset(const AssetRegistry& assets,
                             const AssetPackageLoadedAsset& asset) {
  if (asset.type == "material") {
    if (assets.findMaterialVariant(asset.key) != nullptr) {
      return "material_variant";
    }
    return "material_asset";
  }
  return asset.type;
}

bool writePackageAssetBlob(AssetCache& cache,
                           const AssetRegistry& assets,
                           const AssetPackageLoadedAsset& asset,
                           std::string* diagnostic) {
  if (asset.type == "environment_map") {
    return true;
  }
  if (asset.cache_blob_key.empty()) {
    return fail(diagnostic, "missing cache blob key for package asset: " + asset.key);
  }
  if (asset.type == "texture_rgba8" || asset.type == "texture") {
    const TextureAsset* texture = assets.findTextureAsset(asset.key);
    return texture != nullptr &&
           cache.writeTexture(asset.cache_blob_key, *texture, diagnostic);
  }
  if (asset.type == "mesh") {
    const world::MeshData* mesh = assets.findMeshAsset(asset.key);
    return mesh != nullptr && cache.writeMesh(asset.cache_blob_key, *mesh, diagnostic);
  }
  if (asset.type == "material") {
    if (const rendering::MaterialVariantDesc* variant = assets.findMaterialVariant(asset.key)) {
      return cache.writeMaterialVariant(asset.cache_blob_key, *variant, diagnostic);
    }
    const rendering::MaterialAssetDesc* material = assets.findMaterialAsset(asset.key);
    return material != nullptr &&
           cache.writeMaterialAsset(asset.cache_blob_key, *material, diagnostic);
  }
  if (asset.type == "particle_effect") {
    const visual::particles::ParticleEffectAsset* effect = assets.findParticleEffect(asset.key);
    return effect != nullptr &&
           cache.writeParticleEffect(asset.cache_blob_key, *effect, diagnostic);
  }
  if (asset.type == "gltf_scene") {
    const GltfSceneAsset* scene = assets.findGltfSceneAsset(asset.key);
    return scene != nullptr && cache.writeGltfScene(asset.cache_blob_key, *scene, diagnostic);
  }
  if (asset.type == "animation_clip") {
    const world::AnimationClip* clip = assets.findAnimationClip(asset.key);
    return clip != nullptr && cache.writeAnimationClip(asset.cache_blob_key, *clip, diagnostic);
  }
  if (asset.type == "skeleton") {
    const world::Skeleton* skeleton = assets.findSkeleton(asset.key);
    return skeleton != nullptr &&
           cache.writeSkeleton(asset.cache_blob_key, *skeleton, diagnostic);
  }
  if (asset.type == "skin") {
    const world::Skin* skin = assets.findSkin(asset.key);
    return skin != nullptr && cache.writeSkin(asset.cache_blob_key, *skin, diagnostic);
  }
  return fail(diagnostic, "unsupported cache asset type: " + asset.type);
}

Json packageCacheManifest(const AssetPackageHandle& handle,
                          const AssetRegistry& assets,
                          std::string_view package_key) {
  Json root{
      {"version", 2},
      {"package_key", std::string(package_key)},
      {"manifest_path", handle.manifest_path.lexically_normal().generic_string()},
      {"assets", Json::array()},
  };
  for (const auto& asset : handle.assets) {
    Json entry{{"type", asset.type},
               {"key", asset.key},
               {"blob_key", asset.cache_blob_key},
               {"blob_type", blobTypeForAsset(assets, asset)}};
    if (asset.type == "environment_map") {
      if (const EnvironmentMapAsset* environment = assets.findEnvironmentMap(asset.key)) {
        entry["path"] = environment->path.lexically_normal().generic_string();
      }
    }
    if (asset.type == "gltf_scene") {
      if (const GltfSceneAsset* scene = assets.findGltfSceneAsset(asset.key)) {
        entry["generated"] = Json{
            {"mesh", scene->mesh_asset_keys},
            {"texture", scene->texture_asset_keys},
            {"material", scene->material_keys},
            {"animation_clip", scene->animation_clip_keys},
            {"skeleton", scene->skeleton_keys},
            {"skin", scene->skin_keys},
        };
      }
    }
    root["assets"].push_back(std::move(entry));
  }
  return root;
}

bool writePackageCache(AssetCache& cache,
                       const AssetRegistry& staging,
                       const AssetPackageHandle& handle,
                       std::string_view package_key,
                       std::string* diagnostic) {
  if (!cache.enabled()) {
    return false;
  }
  for (const auto& asset : handle.assets) {
    if (!writePackageAssetBlob(cache, staging, asset, diagnostic)) {
      return false;
    }
  }
  return cache.writePackageManifest(package_key,
                                    packageCacheManifest(handle, staging, package_key),
                                    diagnostic);
}

bool restoreCachedAsset(AssetCache& cache,
                        AssetRegistry& staging,
                        const Json& entry,
                        AssetPackageHandle& handle,
                        std::string* diagnostic) {
  if (!entry.is_object() ||
      !entry.contains("type") ||
      !entry.contains("key") ||
      !entry["type"].is_string() ||
      !entry["key"].is_string()) {
    return fail(diagnostic, "package cache asset record is malformed");
  }
  const std::string type = entry["type"].get<std::string>();
  const std::string key = entry["key"].get<std::string>();
  const std::string blob_key = entry.value("blob_key", std::string{});
  const std::string blob_type = entry.value("blob_type", type);

  if (keyAlreadyExists(staging, type, key)) {
    addLoaded(handle, type, key, blob_key);
    return true;
  }

  if (type == "environment_map") {
    const std::filesystem::path path = entry.value("path", std::string{});
    if (!staging.registerEnvironmentMap(key, EnvironmentMapAsset{.path = path})) {
      return fail(diagnostic, "failed to restore cached environment map: " + key);
    }
    addLoaded(handle, type, key);
    return true;
  }
  if (blob_key.empty()) {
    return fail(diagnostic, "package cache asset record is missing blob key: " + key);
  }
  if (blob_type == "texture" || blob_type == "texture_rgba8") {
    auto texture = cache.readTexture(blob_key, diagnostic);
    if (!texture.has_value() || !staging.registerTextureAsset(key, std::move(*texture))) {
      return fail(diagnostic, "failed to restore cached texture: " + key);
    }
  } else if (blob_type == "mesh") {
    auto mesh = cache.readMesh(blob_key, diagnostic);
    if (!mesh.has_value() || !staging.registerMeshAsset(key, std::move(*mesh))) {
      return fail(diagnostic, "failed to restore cached mesh: " + key);
    }
  } else if (blob_type == "material_asset") {
    auto material = cache.readMaterialAsset(blob_key, diagnostic);
    if (!material.has_value() || !staging.registerMaterialAsset(key, std::move(*material))) {
      return fail(diagnostic, "failed to restore cached material asset: " + key);
    }
  } else if (blob_type == "material_variant") {
    auto material = cache.readMaterialVariant(blob_key, diagnostic);
    if (!material.has_value() || !staging.registerMaterialVariant(key, std::move(*material))) {
      return fail(diagnostic, "failed to restore cached material variant: " + key);
    }
  } else if (blob_type == "particle_effect") {
    auto effect = cache.readParticleEffect(blob_key, diagnostic);
    if (!effect.has_value() || !staging.registerParticleEffect(key, std::move(*effect))) {
      return fail(diagnostic, "failed to restore cached particle effect: " + key);
    }
  } else if (blob_type == "gltf_scene") {
    auto scene = cache.readGltfScene(blob_key, diagnostic);
    if (!scene.has_value() || !staging.registerGltfSceneAsset(key, std::move(*scene))) {
      return fail(diagnostic, "failed to restore cached glTF scene: " + key);
    }
  } else if (blob_type == "animation_clip") {
    auto clip = cache.readAnimationClip(blob_key, diagnostic);
    if (!clip.has_value() || !staging.registerAnimationClip(key, std::move(*clip))) {
      return fail(diagnostic, "failed to restore cached animation clip: " + key);
    }
  } else if (blob_type == "skeleton") {
    auto skeleton = cache.readSkeleton(blob_key, diagnostic);
    if (!skeleton.has_value() || !staging.registerSkeleton(key, std::move(*skeleton))) {
      return fail(diagnostic, "failed to restore cached skeleton: " + key);
    }
  } else if (blob_type == "skin") {
    auto skin = cache.readSkin(blob_key, diagnostic);
    if (!skin.has_value() || !staging.registerSkin(key, std::move(*skin))) {
      return fail(diagnostic, "failed to restore cached skin: " + key);
    }
  } else {
    return fail(diagnostic, "unsupported cached blob type: " + blob_type);
  }

  addLoaded(handle, type, key, blob_key);
  return true;
}

std::optional<AssetPackageHandle> loadPackageFromCache(AssetCache& cache,
                                                       const std::filesystem::path& manifest_path,
                                                       std::string_view package_key,
                                                       AssetRegistry& staging,
                                                       std::string* diagnostic) {
  auto manifest = cache.readPackageManifest(package_key, diagnostic);
  if (!manifest.has_value()) {
    return std::nullopt;
  }
  if (!manifest->is_object() ||
      manifest->value("version", 0u) != 2u ||
      !manifest->contains("assets") ||
      !(*manifest)["assets"].is_array()) {
    return std::nullopt;
  }
  AssetPackageHandle handle{};
  handle.manifest_path = manifest_path;
  for (const Json& entry : (*manifest)["assets"]) {
    if (!restoreCachedAsset(cache, staging, entry, handle, diagnostic)) {
      staging.clear();
      return std::nullopt;
    }
  }
  return handle;
}

std::optional<AssetPackageHandle> commitStagedPackage(AssetRegistry& target,
                                                      const AssetRegistry& staging,
                                                      const AssetPackageHandle& staged,
                                                      std::string* diagnostic) {
  for (const auto& asset : staged.assets) {
    if (keyAlreadyExists(target, asset.type, asset.key)) {
      fail(diagnostic, "asset package would overwrite existing key: " + asset.key);
      return std::nullopt;
    }
  }

  AssetPackageHandle committed{};
  committed.manifest_path = staged.manifest_path;
  for (const auto& asset : staged.assets) {
    if (!copyAssetTo(target, staging, asset, diagnostic)) {
      unloadAssetPackage(target, committed);
      return std::nullopt;
    }
    committed.assets.push_back(asset);
  }
  return committed;
}

std::string normalizedPackageKey(const std::filesystem::path& manifest_path) {
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(manifest_path, ec);
  if (ec) {
    absolute = manifest_path;
  }
  return absolute.lexically_normal().generic_string();
}

}  // namespace

std::filesystem::path resolveAssetPackagePath(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return path / "assets.package.json";
  }
  if (path.extension().empty()) {
    return path / "assets.package.json";
  }
  return path;
}

std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     std::string* diagnostic) {
  return importAssetPackage(assets, path, AssetPackageOptions{}, diagnostic);
}

std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     const AssetPackageOptions& options,
                                                     std::string* diagnostic) {
  const std::filesystem::path manifest_path = resolveAssetPackagePath(path);
  Json root;
  if (!readJson(manifest_path, root, diagnostic)) {
    return std::nullopt;
  }
  AssetCache cache(options.cache);
  const std::string manifest_hash =
      hashFile(manifest_path).value_or(hashString(manifest_path.lexically_normal().generic_string()));
  const std::string package_cache_key =
      packageCacheKey(manifest_path,
                      root,
                      manifest_hash,
                      nullptr);

  if (cache.enabled()) {
    AssetRegistry cached_staging;
    std::string cache_diagnostic;
    if (auto cached = loadPackageFromCache(cache,
                                           manifest_path,
                                           package_cache_key,
                                           cached_staging,
                                           &cache_diagnostic)) {
      if (auto committed = commitStagedPackage(assets, cached_staging, *cached, diagnostic)) {
        return committed;
      }
      return std::nullopt;
    }
  }

  AssetRegistry staging;
  AssetPackageHandle staged{};
  staged.manifest_path = manifest_path;
  const std::filesystem::path base_dir = packageDirectory(manifest_path);
  for (const Json& entry : root["assets"]) {
    if (!importEntry(staging, entry, base_dir, staged, diagnostic)) {
      return std::nullopt;
    }
  }
  assignPackageBlobKeys(staged, package_cache_key);

  auto committed = commitStagedPackage(assets, staging, staged, diagnostic);
  if (!committed.has_value()) {
    return std::nullopt;
  }

  std::string cache_write_diagnostic;
  (void)writePackageCache(cache, staging, staged, package_cache_key, &cache_write_diagnostic);
  return committed;
}

struct AssetPackageJob::State {
  std::filesystem::path path;
  AssetPackageOptions options;
  AssetRegistry staging;
  std::optional<AssetPackageHandle> handle;
  std::string diagnostic;
  std::future<void> future;
  std::atomic_bool complete{false};
  std::atomic_bool success{false};
};

AssetPackageJob::AssetPackageJob() = default;
AssetPackageJob::~AssetPackageJob() = default;
AssetPackageJob::AssetPackageJob(std::shared_ptr<State> state) : state_(std::move(state)) {}
AssetPackageJob::AssetPackageJob(AssetPackageJob&&) noexcept = default;
AssetPackageJob& AssetPackageJob::operator=(AssetPackageJob&&) noexcept = default;

bool AssetPackageJob::valid() const {
  return static_cast<bool>(state_);
}

bool AssetPackageJob::ready() const {
  if (!state_) {
    return false;
  }
  if (state_->complete.load(std::memory_order_acquire)) {
    return true;
  }
  return state_->future.valid() &&
         state_->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void AssetPackageJob::wait() {
  if (!state_) {
    return;
  }
  if (state_->future.valid()) {
    state_->future.wait();
  }
}

bool AssetPackageJob::success() const {
  return state_ != nullptr &&
         state_->complete.load(std::memory_order_acquire) &&
         state_->success.load(std::memory_order_acquire);
}

const std::string& AssetPackageJob::diagnostic() const {
  static const std::string empty;
  return state_ != nullptr && state_->complete.load(std::memory_order_acquire)
             ? state_->diagnostic
             : empty;
}

const AssetPackageHandle* AssetPackageJob::handle() const {
  return state_ != nullptr &&
                 state_->complete.load(std::memory_order_acquire) &&
                 state_->handle.has_value()
             ? &*state_->handle
             : nullptr;
}

AssetPackageJob loadAssetPackageAsync(const std::filesystem::path& path,
                                      const AssetPackageOptions& options) {
  auto state = std::make_shared<AssetPackageJob::State>();
  state->path = path;
  state->options = options;
  state->future = std::async(std::launch::async, [state]() {
    try {
      state->handle =
          importAssetPackage(state->staging, state->path, state->options, &state->diagnostic);
      state->success.store(state->handle.has_value(), std::memory_order_release);
      if (!state->success.load(std::memory_order_acquire) && state->diagnostic.empty()) {
        state->diagnostic = "asset package import failed";
      }
    } catch (const std::exception& e) {
      state->diagnostic = e.what();
      state->success.store(false, std::memory_order_release);
    } catch (...) {
      state->diagnostic = "unknown asset package import failure";
      state->success.store(false, std::memory_order_release);
    }
    state->complete.store(true, std::memory_order_release);
  });
  return AssetPackageJob(std::move(state));
}

bool commitAssetPackageJob(AssetRegistry& assets,
                           AssetPackageJob& job,
                           AssetPackageHandle* out_handle) {
  if (!job.state_) {
    return false;
  }
  job.wait();
  if (job.state_->future.valid()) {
    job.state_->future.get();
  }
  if (!job.state_->success.load(std::memory_order_acquire) ||
      !job.state_->handle.has_value()) {
    return false;
  }

  auto committed = commitStagedPackage(assets,
                                       job.state_->staging,
                                       *job.state_->handle,
                                       &job.state_->diagnostic);
  if (!committed.has_value()) {
    return false;
  }

  if (out_handle != nullptr) {
    *out_handle = *committed;
  }
  return true;
}

bool unloadAssetPackage(AssetRegistry& assets, const AssetPackageHandle& package) {
  bool removed_any = false;
  for (auto it = package.assets.rbegin(); it != package.assets.rend(); ++it) {
    if (it->type == "texture_rgba8" || it->type == "texture") {
      removed_any = assets.unregisterTextureAsset(it->key) || removed_any;
    } else if (it->type == "mesh") {
      removed_any = assets.unregisterMeshAsset(it->key) || removed_any;
    } else if (it->type == "material") {
      removed_any = assets.unregisterMaterial(it->key) || removed_any;
    } else if (it->type == "particle_effect") {
      removed_any = assets.unregisterParticleEffect(it->key) || removed_any;
    } else if (it->type == "environment_map") {
      removed_any = assets.unregisterEnvironmentMap(it->key) || removed_any;
    } else if (it->type == "gltf_scene") {
      removed_any = assets.unregisterGltfSceneAsset(it->key) || removed_any;
    } else if (it->type == "animation_clip") {
      removed_any = assets.unregisterAnimationClip(it->key) || removed_any;
    } else if (it->type == "skeleton") {
      removed_any = assets.unregisterSkeleton(it->key) || removed_any;
    } else if (it->type == "skin") {
      removed_any = assets.unregisterSkin(it->key) || removed_any;
    }
  }
  return removed_any;
}

AssetPackageStore::AssetPackageStore(AssetRegistry& assets,
                                     AssetPackageOptions options)
    : assets_(&assets), options_(std::move(options)) {}

AssetPackageStore::~AssetPackageStore() {
  clear();
}

std::string AssetPackageStore::packageKey(const std::filesystem::path& manifest_path) const {
  return normalizedPackageKey(manifest_path);
}

std::optional<AssetPackageHandle> AssetPackageStore::acquirePackage(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  if (assets_ == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path manifest_path = resolveAssetPackagePath(path);
  const std::string key = packageKey(manifest_path);
  auto existing = records_.find(key);
  if (existing != records_.end()) {
    existing->second.ref_count += 1u;
    return existing->second.handle;
  }

  auto imported = importAssetPackage(*assets_, manifest_path, options_, diagnostic);
  if (!imported.has_value()) {
    return std::nullopt;
  }
  imported->instance_id = next_instance_id_++;
  Record record{};
  record.handle = *imported;
  record.ref_count = 1u;
  records_[key] = record;
  keys_by_instance_id_[record.handle.instance_id] = key;
  return imported;
}

bool AssetPackageStore::releasePackage(const AssetPackageHandle& package) {
  std::string key;
  if (package.instance_id != 0u) {
    const auto id_it = keys_by_instance_id_.find(package.instance_id);
    if (id_it != keys_by_instance_id_.end()) {
      key = id_it->second;
    }
  }
  if (key.empty()) {
    key = packageKey(resolveAssetPackagePath(package.manifest_path));
  }
  auto it = records_.find(key);
  if (it == records_.end()) {
    return false;
  }
  if (it->second.ref_count > 0u) {
    it->second.ref_count -= 1u;
  }
  if (it->second.ref_count == 0u) {
    if (assets_ != nullptr) {
      unloadAssetPackage(*assets_, it->second.handle);
    }
    keys_by_instance_id_.erase(it->second.handle.instance_id);
    records_.erase(it);
  }
  return true;
}

void AssetPackageStore::clear() {
  if (assets_ != nullptr) {
    for (auto& [key, record] : records_) {
      (void)key;
      unloadAssetPackage(*assets_, record.handle);
    }
  }
  records_.clear();
  keys_by_instance_id_.clear();
}

}  // namespace karma::assets
