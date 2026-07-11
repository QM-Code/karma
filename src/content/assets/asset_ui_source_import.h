#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "karma/assets.h"

namespace karma::assets::detail {

/// Nonserialized development-only source locations retained by source package
/// imports. Baked and directly registered assets intentionally have no entry.
struct UiSourceMetadataAccess {
  static void setDocument(AssetRegistry& assets,
                          std::string_view key,
                          const std::filesystem::path& path);
  static void setTheme(AssetRegistry& assets,
                       std::string_view key,
                       const std::filesystem::path& path);
  static std::optional<std::filesystem::path> document(
      const AssetRegistry& assets,
      std::string_view key);
  static std::optional<std::filesystem::path> theme(
      const AssetRegistry& assets,
      std::string_view key);
};

bool importUiDocumentAsset(AssetRegistry& assets,
                           const std::string& key,
                           const std::filesystem::path& path,
                           std::string* diagnostic);
bool importUiThemeAsset(AssetRegistry& assets,
                        const std::string& key,
                        const std::filesystem::path& path,
                        std::string* diagnostic);
bool importFontAsset(AssetRegistry& assets,
                     const std::string& key,
                     const std::filesystem::path& path,
                     std::string* diagnostic);
bool importSvgAsset(AssetRegistry& assets,
                    const std::string& key,
                    const std::filesystem::path& path,
                    std::string* diagnostic);

/// Parses and validates a UI authoring document, then emits deterministic
/// strict JSON suitable for registry and cache storage. Authoring input uses
/// Karma's deterministic JSON5 profile.
bool validateUiDocumentJson(std::string_view source,
                            std::string& canonical_json_utf8,
                            std::vector<UiAssetDependency>& dependencies,
                            std::string* diagnostic);
bool validateUiThemeJson(std::string_view source,
                         std::string& canonical_json_utf8,
                         std::vector<UiAssetDependency>& dependencies,
                         std::string* diagnostic);
bool validateFontBytes(const std::vector<uint8_t>& bytes,
                       std::string* diagnostic);
bool validateSvgSource(std::string_view source, std::string* diagnostic);

}  // namespace karma::assets::detail
