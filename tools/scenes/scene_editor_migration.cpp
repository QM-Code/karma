#include "scene_editor_migration.h"

#include "karma/assets.h"
#include "karma/prefabs.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iomanip>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace karma::tools::scene_editor {
namespace {

using Json = nlohmann::json;

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

bool readJson(const std::filesystem::path& path,
              Json& out,
              std::string& diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    diagnostic = "failed to open " + path.string();
    return false;
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    diagnostic = "failed to parse " + path.string() + ": " + e.what();
    return false;
  }
  return true;
}

bool atomicWriteJson(const std::filesystem::path& path,
                     const Json& json,
                     std::string* diagnostic) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return fail(diagnostic, "failed to create directory " +
                                  path.parent_path().string() + ": " +
                                  ec.message());
    }
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return fail(diagnostic,
                  "failed to open temporary file " + temporary.string());
    }
    stream << std::setw(2) << json << '\n';
    stream.flush();
    if (!stream) {
      std::filesystem::remove(temporary, ec);
      return fail(diagnostic,
                  "failed to write temporary file " + temporary.string());
    }
  }
#if defined(_WIN32)
  std::filesystem::remove(path, ec);
  ec.clear();
#endif
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    std::filesystem::remove(temporary);
    return fail(diagnostic,
                "failed to replace " + path.string() + ": " + ec.message());
  }
  return true;
}

std::string sourceHash(const std::filesystem::path& path) {
  return assets::hashFile(path).value_or(std::string{});
}

std::string joinPrefabDiagnostics(const prefabs::PrefabLoadResult& loaded) {
  std::string message;
  for (const std::string& entry : loaded.diagnostics) {
    if (!message.empty()) message += '\n';
    message += entry;
  }
  return message.empty() ? std::string("prefab validation failed") : message;
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
  std::string joined;
  for (const std::string& diagnostic : diagnostics) {
    if (!joined.empty()) joined += '\n';
    joined += diagnostic;
  }
  return joined;
}

bool sourceIsWritable(const std::filesystem::path& path,
                      std::string& diagnostic) {
  std::error_code error;
  const std::filesystem::file_status status =
      std::filesystem::status(path, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    diagnostic = error ? "failed to inspect source permissions: " +
                             error.message()
                       : "migration source is not a regular file";
    return false;
  }
  constexpr std::filesystem::perms writable =
      std::filesystem::perms::owner_write |
      std::filesystem::perms::group_write |
      std::filesystem::perms::others_write;
  if ((status.permissions() & writable) == std::filesystem::perms::none) {
    diagnostic = "migration source is read-only: " + path.string();
    return false;
  }
  return true;
}

bool preservePreLodBackup(const std::filesystem::path& path,
                          std::string& diagnostic) {
  const std::filesystem::path backup =
      path.string() + ".pre-lod-component.bak";
  std::error_code error;
  if (std::filesystem::exists(backup, error)) {
    if (error || !std::filesystem::is_regular_file(backup, error)) {
      diagnostic = "existing migration backup is not a readable file: " +
                   backup.string();
      return false;
    }
    return true;
  }
  if (error || !std::filesystem::copy_file(
                   path, backup, std::filesystem::copy_options::none, error)) {
    diagnostic = "failed to create migration backup '" + backup.string() +
                 "': " + (error ? error.message() : "copy failed");
    return false;
  }
  return true;
}

}  // namespace

LegacyRenderMigrationReport migrateSceneLegacyRenderComponents(
    scenes::SceneDocument& document) {
  LegacyRenderMigrationReport report{};
  scenes::SceneDocument staged = document;
  for (scenes::SceneEntity& entity : staged.entities) {
    const prefabs::LegacyRenderComponentMigrationResult migrated =
        prefabs::migrateLegacyRenderComponentsJson(entity.components);
    if (!migrated.success()) {
      for (const std::string& entry : migrated.diagnostics) {
        report.diagnostics.push_back("entity '" + entity.id + "': " + entry);
      }
      continue;
    }
    if (migrated.changed) ++report.migrated_owners;
  }
  if (!report.success()) return report;
  report.changed = report.migrated_owners != 0u;
  if (report.changed) document = std::move(staged);
  return report;
}

LegacyRenderMigrationReport migrateSceneFileLegacyRenderComponents(
    const std::filesystem::path& path,
    const std::filesystem::path& reference_root) {
  LegacyRenderMigrationReport report{};
  Json source;
  std::string diagnostic;
  if (!readJson(path, source, diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  const std::string source_hash = sourceHash(path);
  if (source_hash.empty()) {
    report.diagnostics.push_back("failed to fingerprint scene source " +
                                 path.string());
    return report;
  }
  if (!source.is_object()) {
    report.diagnostics.push_back("scene document must be a JSON object");
    return report;
  }
  const auto entities = source.find("entities");
  if (entities == source.end() || !entities->is_array()) {
    report.diagnostics.push_back("scene document is missing array 'entities'");
    return report;
  }

  Json migrated = source;
  Json& migrated_entities = migrated["entities"];
  for (size_t index = 0u; index < migrated_entities.size(); ++index) {
    Json& entity = migrated_entities[index];
    if (!entity.is_object()) {
      report.diagnostics.push_back("scene entity " + std::to_string(index) +
                                   " must be an object");
      return report;
    }
    auto components = entity.find("components");
    if (components == entity.end()) continue;
    const prefabs::LegacyRenderComponentMigrationResult result =
        prefabs::migrateLegacyRenderComponentsJson(*components);
    if (!result.success()) {
      for (const std::string& entry : result.diagnostics) {
        report.diagnostics.push_back("scene entity " +
                                     std::to_string(index) + ": " + entry);
      }
      return report;
    }
    if (result.changed) ++report.migrated_owners;
  }
  if (report.migrated_owners == 0u) return report;
  if (!sourceIsWritable(path, diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }

  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path validation_path =
      path.string() + ".scene-editor-migrate-" + std::to_string(stamp) +
      ".kscene.json";
  if (!atomicWriteJson(validation_path, migrated, &diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  const scenes::SceneLoadResult validated = scenes::loadSceneDocument(
      scenes::SceneLoadDesc{
          .path = validation_path,
          .reference_root = reference_root,
      });
  std::error_code ignored;
  std::filesystem::remove(validation_path, ignored);
  if (!validated.success()) {
    report.diagnostics = validated.diagnostics;
    return report;
  }
  if (sourceHash(path) != source_hash) {
    report.diagnostics.push_back(
        "scene source changed externally while migration was validating");
    return report;
  }
  if (!preservePreLodBackup(path, diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  if (sourceHash(path) != source_hash) {
    report.diagnostics.push_back(
        "scene source changed externally while migration backup was created");
    return report;
  }
  if (!atomicWriteJson(path, migrated, &diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  report.changed = true;
  return report;
}

LegacyRenderMigrationReport migratePrefabLegacyRenderComponents(
    const std::filesystem::path& path) {
  LegacyRenderMigrationReport report{};
  std::error_code path_error;
  const std::filesystem::path source_path =
      (std::filesystem::is_directory(path, path_error) ||
       path.extension().empty())
          ? path / "prefab.json"
          : path;
  Json source;
  std::string diagnostic;
  if (!readJson(source_path, source, diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  const std::string source_hash = sourceHash(source_path);
  if (source_hash.empty()) {
    report.diagnostics.push_back("failed to fingerprint prefab source " +
                                 source_path.string());
    return report;
  }
  Json migrated = source;
  const prefabs::LegacyRenderComponentMigrationResult migration =
      prefabs::migrateLegacyPrefabJson(migrated);
  if (!migration.success()) {
    report.diagnostics = migration.diagnostics;
    return report;
  }
  if (!migration.changed) return report;
  if (!sourceIsWritable(source_path, diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }

  const auto source_nodes = source.find("nodes");
  const auto migrated_nodes = migrated.find("nodes");
  if (source_nodes != source.end() && source_nodes->is_array() &&
      migrated_nodes != migrated.end() && migrated_nodes->is_array()) {
    const size_t count = std::min(source_nodes->size(), migrated_nodes->size());
    for (size_t index = 0u; index < count; ++index) {
      if ((*source_nodes)[index].value("components", Json::object()) !=
          (*migrated_nodes)[index].value("components", Json::object())) {
        ++report.migrated_owners;
      }
    }
  }

  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path validation_path =
      source_path.string() + ".scene-editor-migrate-" +
      std::to_string(stamp) + ".json";
  if (!atomicWriteJson(validation_path, migrated, &diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  const prefabs::PrefabLoadResult validated =
      prefabs::loadPrefabDocument(validation_path);
  std::error_code ignored;
  std::filesystem::remove(validation_path, ignored);
  if (!validated.success()) {
    report.diagnostics.push_back(joinPrefabDiagnostics(validated));
    return report;
  }
  if (sourceHash(source_path) != source_hash) {
    report.diagnostics.push_back(
        "prefab source changed externally while migration was validating");
    return report;
  }
  if (!preservePreLodBackup(source_path, diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  if (sourceHash(source_path) != source_hash) {
    report.diagnostics.push_back(
        "prefab source changed externally while migration backup was created");
    return report;
  }
  if (!atomicWriteJson(source_path, migrated, &diagnostic)) {
    report.diagnostics.push_back(std::move(diagnostic));
    return report;
  }
  report.changed = true;
  return report;
}

LegacyRenderMigrationReport migratePrefabSourceClosure(
    const std::vector<std::filesystem::path>& authored_sources,
    const std::filesystem::path& reference_root) {
  LegacyRenderMigrationReport combined{};
  std::vector<std::filesystem::path> pending;
  std::unordered_set<std::string> queued;
  const auto add_source = [&](const std::filesystem::path& authored,
                              const std::filesystem::path& base) {
    if (authored.empty()) return;
    std::filesystem::path resolved =
        (authored.is_absolute() ? authored : base / authored)
            .lexically_normal();
    std::error_code path_error;
    if (std::filesystem::is_directory(resolved, path_error) ||
        resolved.extension().empty()) {
      resolved /= "prefab.json";
    }
    if (queued.insert(resolved.generic_string()).second) {
      pending.push_back(resolved);
    }
  };
  for (const std::filesystem::path& source : authored_sources) {
    add_source(source, reference_root);
  }
  for (size_t source_index = 0u; source_index < pending.size();
       ++source_index) {
    const std::filesystem::path source = pending[source_index];
    const LegacyRenderMigrationReport migrated =
        migratePrefabLegacyRenderComponents(source);
    if (!migrated.success()) {
      for (const std::string& diagnostic : migrated.diagnostics) {
        combined.diagnostics.push_back(source.generic_string() + ": " +
                                       diagnostic);
      }
      continue;
    }
    combined.changed = combined.changed || migrated.changed;
    combined.migrated_owners += migrated.migrated_owners;

    const prefabs::PrefabLoadResult loaded =
        prefabs::loadPrefabDocument(source);
    if (!loaded.success() || !loaded.document.has_value()) {
      for (const std::string& diagnostic : loaded.diagnostics) {
        combined.diagnostics.push_back(source.generic_string() + ": " +
                                       diagnostic);
      }
      if (loaded.diagnostics.empty()) {
        combined.diagnostics.push_back(
            source.generic_string() + ": prefab validation failed");
      }
      continue;
    }
    const std::filesystem::path prefab_directory =
        loaded.source_path.parent_path();
    for (const prefabs::PrefabNode& node : loaded.document->nodes) {
      if (!node.components.is_object()) continue;
      const auto foliage = node.components.find("FoliageComponent");
      if (foliage == node.components.end() || !foliage->is_object()) {
        continue;
      }
      const auto nested_prefab = foliage->find("prefab_path");
      if (nested_prefab != foliage->end() && nested_prefab->is_string()) {
        add_source(nested_prefab->get<std::string>(), prefab_directory);
      }
    }
  }
  return combined;
}

LegacyRenderMigrationReport migrateReferencedPrefabSources(
    const scenes::SceneDocument& document,
    const std::filesystem::path& content_root) {
  std::vector<std::filesystem::path> sources;
  sources.reserve(document.prefab_instances.size() + document.entities.size());
  for (const scenes::ScenePrefabInstance& prefab : document.prefab_instances) {
    sources.push_back(prefab.prefab_path);
  }
  for (const scenes::SceneEntity& entity : document.entities) {
    if (!entity.components.is_object()) continue;
    const auto foliage = entity.components.find("FoliageComponent");
    if (foliage == entity.components.end() || !foliage->is_object()) continue;
    const auto prefab_path = foliage->find("prefab_path");
    if (prefab_path != foliage->end() && prefab_path->is_string()) {
      sources.emplace_back(prefab_path->get<std::string>());
    }
  }
  return migratePrefabSourceClosure(sources, content_root);
}

bool loadSceneWithEditorMigration(
    const std::filesystem::path& path,
    const std::filesystem::path& content_root,
    scenes::SceneDocument& document,
    std::string& migration_status,
    std::string& diagnostic) {
  migration_status.clear();
  diagnostic.clear();
  const LegacyRenderMigrationReport scene_migration =
      migrateSceneFileLegacyRenderComponents(path, content_root);
  if (!scene_migration.success()) {
    diagnostic = joinDiagnostics(scene_migration.diagnostics);
    return false;
  }
  const scenes::SceneLoadResult loaded = scenes::loadSceneDocument(
      scenes::SceneLoadDesc{.path = path, .reference_root = content_root});
  if (!loaded.success() || !loaded.document.has_value()) {
    diagnostic = joinDiagnostics(loaded.diagnostics);
    return false;
  }
  const LegacyRenderMigrationReport prefab_migration =
      migrateReferencedPrefabSources(*loaded.document, content_root);
  if (!prefab_migration.success()) {
    diagnostic = joinDiagnostics(prefab_migration.diagnostics);
    return false;
  }
  const size_t migrated_count = scene_migration.migrated_owners +
                                prefab_migration.migrated_owners;
  if (scene_migration.changed || prefab_migration.changed) {
    migration_status =
        "Automatically migrated " + std::to_string(migrated_count) +
        " render owner(s); pre-LOD backups were preserved";
  }
  document = *loaded.document;
  return true;
}

}  // namespace karma::tools::scene_editor
