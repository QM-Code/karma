#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "karma/assets.h"
#include "karma/assets.h"

namespace karma::demo {

inline std::filesystem::path resolveExamplePath(const std::filesystem::path& relative) {
  if (relative.is_absolute() && std::filesystem::exists(relative)) {
    return relative;
  }

  std::filesystem::path cwd = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    const std::filesystem::path candidate = cwd / relative;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (!cwd.has_parent_path()) {
      break;
    }
    cwd = cwd.parent_path();
  }

  return relative;
}

inline std::filesystem::path resolveExampleAssetPath(std::string_view name) {
  return resolveExamplePath(std::filesystem::path("examples") / "assets" /
                            std::filesystem::path{name});
}

inline std::filesystem::path resolveExampleShaderPath(std::string_view name) {
  return resolveExamplePath(std::filesystem::path("examples") / "assets" / "shaders" /
                            std::filesystem::path{name});
}

inline std::string exampleAssetKey(std::string_view category, std::string_view name) {
  std::filesystem::path logical{name};
  logical.replace_extension();
  return "examples/" + std::string(category) + "/" + logical.generic_string();
}

inline std::string importExampleMeshAsset(assets::AssetRegistry* assets, std::string_view name) {
  const std::string key = exampleAssetKey("mesh", name);
  if (assets != nullptr && assets->findMeshAsset(key) == nullptr) {
    std::filesystem::path logical{name};
    logical.replace_extension();
    std::string diagnostic;
    (void)assets::importAssetPackage(
        *assets,
        resolveExampleAssetPath(
            (std::filesystem::path("common_meshes") / logical).generic_string()),
        &diagnostic);
  }
  return key;
}

inline std::string registerExampleEnvironmentMap(assets::AssetRegistry* assets,
                                                 std::string_view name) {
  const std::filesystem::path path = resolveExampleAssetPath(name);
  const std::string key = exampleAssetKey("environment", name);
  if (assets != nullptr) {
    assets->registerEnvironmentMap(key, assets::EnvironmentMapAsset{.path = path});
  }
  return key;
}

}  // namespace karma::demo
