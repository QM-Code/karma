#pragma once

#include "karma/scenes.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace karma::tools::scene_editor {

/// Summary for an explicit legacy render-component authoring migration.
/// Engine scene and prefab loaders remain nonmutating; editor callers opt into
/// these operations before loading or rebuilding authored content.
struct LegacyRenderMigrationReport {
  bool changed = false;
  size_t migrated_owners = 0u;
  std::vector<std::string> diagnostics;

  bool success() const { return diagnostics.empty(); }
  explicit operator bool() const { return success(); }
};

LegacyRenderMigrationReport migrateSceneLegacyRenderComponents(
    scenes::SceneDocument& document);
LegacyRenderMigrationReport migrateSceneFileLegacyRenderComponents(
    const std::filesystem::path& path,
    const std::filesystem::path& reference_root);
LegacyRenderMigrationReport migratePrefabLegacyRenderComponents(
    const std::filesystem::path& path);

/// Migrates the supplied prefab sources and recursively follows prefab-backed
/// foliage references. Each source is migrated at most once.
LegacyRenderMigrationReport migratePrefabSourceClosure(
    const std::vector<std::filesystem::path>& authored_sources,
    const std::filesystem::path& reference_root);

/// Migrates prefab instances and prefab-backed foliage referenced by a scene.
LegacyRenderMigrationReport migrateReferencedPrefabSources(
    const scenes::SceneDocument& document,
    const std::filesystem::path& content_root);

/// Runs the editor's explicit scene/prefab migration workflow, then loads the
/// now-current scene through the ordinary nonmutating scene loader.
bool loadSceneWithEditorMigration(
    const std::filesystem::path& path,
    const std::filesystem::path& content_root,
    scenes::SceneDocument& document,
    std::string& migration_status,
    std::string& diagnostic);

}  // namespace karma::tools::scene_editor
