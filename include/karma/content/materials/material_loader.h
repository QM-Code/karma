#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "karma/rendering/renderer/material_library.h"

namespace karma::content {

/// Result metadata for loading a JSON `.mat` file.
struct MaterialLoadResult {
  bool success = false;
  std::string diagnostic;
};

/// Parses a shared material asset from a JSON `.mat` file.
std::optional<renderer::MaterialAssetDesc> loadMaterialAssetDesc(
    const std::filesystem::path& path,
    std::string* diagnostic = nullptr);

/// Parses a material instance from a JSON `.mat` file.
std::optional<renderer::MaterialInstanceDesc> loadMaterialInstanceDesc(
    const std::filesystem::path& path,
    std::string* diagnostic = nullptr);

/// Loads a JSON `.mat` file and registers it in a material library.
MaterialLoadResult loadMaterialFile(renderer::MaterialLibrary& library,
                                    const std::string& key,
                                    const std::filesystem::path& path);

}  // namespace karma::content
