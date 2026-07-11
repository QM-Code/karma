#pragma once

#include "features/ui/native/development_path.h"
#include "karma/ui.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace karma::assets {
class AssetRegistry;
}

namespace karma::ui::native {

struct DevelopmentAssetRecord {
  std::string type;
  std::string key;
  std::filesystem::path source_path;
};

struct DevelopmentGraphBuild {
  std::string document_key;
  std::filesystem::path document_path;
  std::vector<DevelopmentAssetRecord> assets;
  std::vector<std::filesystem::path> watched_paths;
};

/// Resolves and imports a complete loose-file UI dependency graph into an
/// isolated staging registry. No live registry state changes on failure.
bool buildDevelopmentGraph(const std::filesystem::path& document_path,
                           const std::filesystem::path& root,
                           assets::AssetRegistry& staging,
                           DevelopmentGraphBuild& graph,
                           std::vector<Diagnostic>& diagnostics);

/// Replaces the synthetic live assets only after staging has succeeded.
bool commitDevelopmentGraph(assets::AssetRegistry& destination,
                            assets::AssetRegistry& staging,
                            const DevelopmentGraphBuild& graph);

}  // namespace karma::ui::native
