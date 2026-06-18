#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "karma/content/assets/asset_registry.h"

namespace karma::content {

/// One asset registered by an imported package.
struct AssetPackageLoadedAsset {
  std::string type;
  std::string key;
};

/// Handle returned by a successful package import.
struct AssetPackageHandle {
  std::filesystem::path manifest_path;
  std::vector<AssetPackageLoadedAsset> assets;

  bool valid() const { return !manifest_path.empty(); }
};

/// Resolves a package path. Directories resolve to `assets.package.json`.
std::filesystem::path resolveAssetPackagePath(const std::filesystem::path& path);

/// Imports an asset package all-or-nothing.
std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     std::string* diagnostic = nullptr);

/// Unregisters assets that were imported by a package handle.
bool unloadAssetPackage(AssetRegistry& assets, const AssetPackageHandle& package);

}  // namespace karma::content
