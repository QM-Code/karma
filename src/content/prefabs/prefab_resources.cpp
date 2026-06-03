#include "karma/content/prefabs/prefab_resource_context.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "karma/content/image/image.h"
#include "karma/features/visual/particles/effect_library.h"
#include "karma/rendering/renderer/device.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;

struct TextureResource {
  std::string key;
  renderer::TextureId texture = renderer::kInvalidTexture;
};

struct LoadedPrefabResources {
  std::vector<TextureResource> textures;
  std::vector<std::string> particle_effects;
};

struct ResourceManifestEntry {
  std::string key;
  std::filesystem::path path;
};

struct ResourceManifest {
  std::vector<ResourceManifestEntry> textures;
  std::vector<ResourceManifestEntry> particle_effects;
};

PrefabResourceContext g_context;
std::unordered_map<std::string, LoadedPrefabResources> g_loaded_resources;

std::filesystem::path resolvePrefabPath(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return path / "prefab.json";
  }
  if (path.extension().empty()) {
    return path / "prefab.json";
  }
  return path;
}

std::filesystem::path prefabDirectory(const std::filesystem::path& path) {
  const std::filesystem::path prefab_path = resolvePrefabPath(path);
  const std::filesystem::path parent = prefab_path.parent_path();
  if (!parent.empty()) {
    return parent;
  }
  return ".";
}

std::string cacheKey(const std::filesystem::path& directory) {
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(directory, ec);
  if (ec) {
    absolute = directory;
  }
  return absolute.lexically_normal().string();
}

bool readRequiredString(const Json& object,
                        std::string_view key,
                        std::string& out,
                        const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    spdlog::error("Prefab resources '{}' entry is missing string '{}'",
                  path.string(),
                  key);
    return false;
  }
  out = it->get<std::string>();
  return !out.empty();
}

bool parseEntryArray(const Json& manifest,
                     std::string_view key,
                     std::vector<ResourceManifestEntry>& out,
                     const std::filesystem::path& path) {
  const auto it = manifest.find(key);
  if (it == manifest.end()) {
    return true;
  }
  if (!it->is_array()) {
    spdlog::error("Prefab resources '{}' field '{}' must be an array",
                  path.string(),
                  key);
    return false;
  }

  out.reserve(out.size() + it->size());
  for (const Json& entry_json : *it) {
    if (!entry_json.is_object()) {
      spdlog::error("Prefab resources '{}' field '{}' contains a non-object entry",
                    path.string(),
                    key);
      return false;
    }

    std::string entry_key;
    std::string entry_path;
    if (!readRequiredString(entry_json, "key", entry_key, path) ||
        !readRequiredString(entry_json, "path", entry_path, path)) {
      return false;
    }
    out.push_back(ResourceManifestEntry{
        .key = std::move(entry_key),
        .path = std::filesystem::path(std::move(entry_path)),
    });
  }
  return true;
}

std::optional<ResourceManifest> loadManifest(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    spdlog::error("Failed to open prefab resources '{}'", path.string());
    return std::nullopt;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception& e) {
    spdlog::error("Failed to parse prefab resources '{}': {}", path.string(), e.what());
    return std::nullopt;
  }

  if (!json.is_object()) {
    spdlog::error("Prefab resources '{}' root JSON value must be an object", path.string());
    return std::nullopt;
  }
  const auto version_it = json.find("version");
  if (version_it == json.end() ||
      (!version_it->is_number_unsigned() && !version_it->is_number_integer()) ||
      version_it->get<int>() != 1) {
    spdlog::error("Prefab resources '{}' has unsupported or missing version", path.string());
    return std::nullopt;
  }

  ResourceManifest manifest{};
  if (!parseEntryArray(json, "textures", manifest.textures, path) ||
      !parseEntryArray(json, "particle_effects", manifest.particle_effects, path)) {
    return std::nullopt;
  }
  return manifest;
}

renderer::TextureId uploadTexture(int width, int height, const void* pixels) {
  if (g_context.create_texture_rgba8) {
    return g_context.create_texture_rgba8(width, height, pixels);
  }
  if (g_context.graphics != nullptr) {
    return g_context.graphics->createTextureRGBA8(width, height, pixels);
  }
  return renderer::kInvalidTexture;
}

void destroyTexture(renderer::TextureId texture) {
  if (texture == renderer::kInvalidTexture) {
    return;
  }
  if (g_context.destroy_texture) {
    g_context.destroy_texture(texture);
    return;
  }
  if (g_context.graphics != nullptr) {
    g_context.graphics->destroyTexture(texture);
  }
}

void cleanupLoadedResources(LoadedPrefabResources& resources) {
  if (g_context.particle_effects != nullptr) {
    for (const std::string& key : resources.particle_effects) {
      g_context.particle_effects->unregisterEffect(key);
    }
    for (const TextureResource& texture : resources.textures) {
      g_context.particle_effects->unregisterTextureAlias(texture.key);
    }
  }

  for (TextureResource& texture : resources.textures) {
    destroyTexture(texture.texture);
    texture.texture = renderer::kInvalidTexture;
  }
  resources = {};
}

bool loadResources(const std::filesystem::path& directory,
                   const ResourceManifest& manifest,
                   LoadedPrefabResources& out_resources) {
  if (!manifest.textures.empty() &&
      g_context.graphics == nullptr &&
      !g_context.create_texture_rgba8) {
    spdlog::error("Prefab resources '{}' require a graphics context", directory.string());
    return false;
  }
  if ((!manifest.textures.empty() || !manifest.particle_effects.empty()) &&
      g_context.particle_effects == nullptr) {
    spdlog::error("Prefab resources '{}' require a particle library context",
                  directory.string());
    return false;
  }

  LoadedPrefabResources loaded{};
  for (const ResourceManifestEntry& texture_entry : manifest.textures) {
    const std::filesystem::path texture_path = directory / texture_entry.path;
    std::optional<content::Rgba8Image> image = content::loadRgba8Image(texture_path);
    if (!image.has_value() || !image->valid()) {
      spdlog::error("Prefab texture '{}' failed to load", texture_path.string());
      cleanupLoadedResources(loaded);
      return false;
    }

    const renderer::TextureId texture =
        uploadTexture(image->width, image->height, image->pixels.data());
    if (texture == renderer::kInvalidTexture) {
      spdlog::error("Prefab texture '{}' failed to upload", texture_path.string());
      cleanupLoadedResources(loaded);
      return false;
    }

    g_context.particle_effects->registerTextureAlias(texture_entry.key, texture);
    loaded.textures.push_back(TextureResource{
        .key = texture_entry.key,
        .texture = texture,
    });
  }

  for (const ResourceManifestEntry& effect_entry : manifest.particle_effects) {
    const std::filesystem::path effect_path = directory / effect_entry.path;
    if (!g_context.particle_effects->registerEffectFile(effect_entry.key, effect_path)) {
      spdlog::error("Prefab particle effect '{}' failed to load", effect_path.string());
      g_context.particle_effects->unregisterEffect(effect_entry.key);
      cleanupLoadedResources(loaded);
      return false;
    }
    loaded.particle_effects.push_back(effect_entry.key);
  }

  out_resources = std::move(loaded);
  return true;
}

}  // namespace

void bindPrefabResourceContext(PrefabResourceContext context) {
  clearPrefabResourceContext();
  g_context = std::move(context);
}

void clearPrefabResourceContext() {
  for (auto& [key, resources] : g_loaded_resources) {
    (void)key;
    cleanupLoadedResources(resources);
  }
  g_loaded_resources.clear();
  g_context = {};
}

bool ensurePrefabResourcesLoaded(const std::filesystem::path& prefab_path) {
  const std::filesystem::path directory = prefabDirectory(prefab_path);
  const std::filesystem::path manifest_path = directory / "prefab.resources.json";
  std::error_code ec;
  if (!std::filesystem::exists(manifest_path, ec)) {
    return true;
  }
  if (ec) {
    spdlog::error("Failed to inspect prefab resources '{}': {}",
                  manifest_path.string(),
                  ec.message());
    return false;
  }

  const std::string key = cacheKey(directory);
  if (g_loaded_resources.find(key) != g_loaded_resources.end()) {
    return true;
  }

  std::optional<ResourceManifest> manifest = loadManifest(manifest_path);
  if (!manifest.has_value()) {
    return false;
  }

  LoadedPrefabResources resources{};
  if (!loadResources(directory, *manifest, resources)) {
    return false;
  }

  g_loaded_resources.emplace(key, std::move(resources));
  return true;
}

}  // namespace karma::prefabs
