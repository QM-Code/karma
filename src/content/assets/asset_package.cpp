#include "karma/content/assets/asset_package.h"

#include <exception>
#include <fstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "karma/content/materials/material_loader.h"

namespace karma::content {

namespace {

using Json = nlohmann::json;

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
  if (type == "texture_rgba8") {
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
  return false;
}

void addLoaded(AssetPackageHandle& handle, std::string type, std::string key) {
  handle.assets.push_back(AssetPackageLoadedAsset{
      .type = std::move(type),
      .key = std::move(key),
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
    if (!assets.importTextureAsset(key, source_path, options)) {
      return fail(diagnostic, "failed to import texture asset: " + source_path.string());
    }
    addLoaded(handle, type, key);
    return true;
  }

  if (type == "mesh") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!assets.importMeshAsset(key, source_path)) {
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
    if (!assets.importParticleEffect(key, source_path)) {
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
    GltfSceneAsset scene = assets.importGltfSceneAsset(key, source_path);
    if (scene.scene_key.empty()) {
      return fail(diagnostic, "failed to import glTF scene: " + source_path.string());
    }
    addLoaded(handle, "gltf_scene", key);
    for (const std::string& child_key : scene.mesh_asset_keys) {
      addLoaded(handle, "mesh", child_key);
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
  const std::filesystem::path manifest_path = resolveAssetPackagePath(path);
  Json root;
  if (!readJson(manifest_path, root, diagnostic)) {
    return std::nullopt;
  }

  AssetPackageHandle handle{};
  handle.manifest_path = manifest_path;
  const std::filesystem::path base_dir = packageDirectory(manifest_path);
  for (const Json& entry : root["assets"]) {
    if (!importEntry(assets, entry, base_dir, handle, diagnostic)) {
      unloadAssetPackage(assets, handle);
      return std::nullopt;
    }
  }
  return handle;
}

bool unloadAssetPackage(AssetRegistry& assets, const AssetPackageHandle& package) {
  bool removed_any = false;
  for (auto it = package.assets.rbegin(); it != package.assets.rend(); ++it) {
    if (it->type == "texture_rgba8") {
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

}  // namespace karma::content
