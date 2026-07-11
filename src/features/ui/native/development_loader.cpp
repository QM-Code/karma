#include "features/ui/native/development_loader.h"

#include "features/ui/native/string_utils.h"

#include "content/assets/asset_source_import.h"
#include "content/assets/asset_ui_source_import.h"
#include "content/assets/ui_json_profile.h"
#include "karma/assets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <unordered_set>

namespace karma::ui::native {
namespace {

using Json = nlohmann::json;

using string_utils::lower;

std::optional<std::string> readSource(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) return std::nullopt;
  stream.seekg(0, std::ios::beg);
  std::string source(static_cast<std::size_t>(size), '\0');
  if (size > 0) stream.read(source.data(), size);
  return stream || size == 0 ? std::optional<std::string>{std::move(source)}
                             : std::nullopt;
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics,
                   std::string_view asset_key,
                   std::string code,
                   std::string message) {
  diagnostics.push_back({.severity = DiagnosticSeverity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .asset_key = std::string(asset_key)});
}

std::string developmentAssetKey(std::string_view type,
                                const std::filesystem::path& path) {
  return "ui/dev/" + std::string(type) + "-" +
         assets::hashString(path.generic_string());
}

bool rewriteReferences(Json& value,
                       const std::filesystem::path& source_directory,
                       const std::filesystem::path& root,
                       assets::AssetRegistry& staging,
                       DevelopmentGraphBuild& graph,
                       std::unordered_set<std::string>& active,
                       std::vector<Diagnostic>& diagnostics,
                       std::string_view owner_key) {
  if (value.is_array()) {
    for (Json& item : value) {
      if (!rewriteReferences(item, source_directory, root, staging, graph,
                             active, diagnostics, owner_key)) {
        return false;
      }
    }
    return true;
  }
  if (!value.is_object()) return true;

  if (value.contains("file") && value["file"].is_string()) {
    const std::filesystem::path requested = value["file"].get<std::string>();
    const auto resolved =
        resolveDevelopmentPath(requested, source_directory, root);
    if (!resolved) {
      addDiagnostic(diagnostics, owner_key, "UI_DEV_FILE_SANDBOX",
                    "development UI reference is missing or outside its root: " +
                        requested.generic_string());
      return false;
    }
    graph.watched_paths.push_back(*resolved);
    const std::string filename = lower(resolved->filename().string());
    std::string type;
    if (filename.ends_with(".kstyle.json5")) type = "theme";
    else if (filename.ends_with(".ttf") || filename.ends_with(".otf") ||
             filename.ends_with(".ttc") || filename.ends_with(".otc")) type = "font";
    else if (filename.ends_with(".svg")) type = "svg";
    else type = "texture";
    const std::string key = developmentAssetKey(type, *resolved);

    if (type == "theme") {
      if (!active.insert(resolved->generic_string()).second) {
        addDiagnostic(diagnostics, owner_key, "UI_DEV_IMPORT_CYCLE",
                      "development theme import cycle: " +
                          resolved->generic_string());
        return false;
      }
      const auto source = readSource(*resolved);
      const auto parsed = source ? assets::detail::parseJsonProfile(*source)
                                 : assets::detail::JsonProfileParseResult{};
      if (!source || !parsed || !parsed.document->value.is_object()) {
        addDiagnostic(diagnostics, owner_key, "UI_DEV_THEME_INVALID",
                      !source ? "could not read development theme: " +
                                    resolved->generic_string()
                              : parsed.error->message);
        active.erase(resolved->generic_string());
        return false;
      }
      Json theme = parsed.document->value;
      if (!rewriteReferences(theme, resolved->parent_path(), root, staging,
                             graph, active, diagnostics, key)) {
        active.erase(resolved->generic_string());
        return false;
      }
      std::string canonical;
      std::vector<assets::UiAssetDependency> dependencies;
      std::string error;
      if (!assets::detail::validateUiThemeJson(theme.dump(), canonical,
                                               dependencies, &error) ||
          !staging.registerUiThemeAsset(
              key, {.canonical_json_utf8 = std::move(canonical),
                    .dependencies = std::move(dependencies)})) {
        addDiagnostic(diagnostics, owner_key, "UI_DEV_THEME_INVALID", error);
        active.erase(resolved->generic_string());
        return false;
      }
      active.erase(resolved->generic_string());
    } else if (type == "font") {
      std::string error;
      if (!assets::detail::importFontAsset(staging, key, *resolved, &error)) {
        addDiagnostic(diagnostics, owner_key, "UI_DEV_FONT_INVALID", error);
        return false;
      }
    } else if (type == "svg") {
      std::string error;
      if (!assets::detail::importSvgAsset(staging, key, *resolved, &error)) {
        addDiagnostic(diagnostics, owner_key, "UI_DEV_SVG_INVALID", error);
        return false;
      }
    } else {
      assets::TextureImportOptions options;
      options.srgb = true;
      options.generate_mips = false;
      options.alpha_bleed = true;
      options.prefer_compressed = false;
      if (!assets::detail::importTextureAsset(staging, key, *resolved, options)) {
        addDiagnostic(diagnostics, owner_key, "UI_DEV_TEXTURE_INVALID",
                      "could not import development UI texture: " +
                          resolved->generic_string());
        return false;
      }
    }

    if (std::none_of(graph.assets.begin(), graph.assets.end(),
                     [&](const DevelopmentAssetRecord& record) {
                       return record.type == type && record.key == key;
                     })) {
      graph.assets.push_back({.type = type == "theme" ? "ui_theme" : type,
                              .key = key,
                              .source_path = *resolved});
    }
    value.erase("file");
    value["asset"] = key;
    return true;
  }

  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!rewriteReferences(iterator.value(), source_directory, root, staging,
                           graph, active, diagnostics, owner_key)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool buildDevelopmentGraph(const std::filesystem::path& document_path,
                           const std::filesystem::path& root,
                           assets::AssetRegistry& staging,
                           DevelopmentGraphBuild& graph,
                           std::vector<Diagnostic>& diagnostics) {
  graph = {};
  graph.document_path = document_path;
  graph.document_key = developmentAssetKey("document", document_path);
  graph.watched_paths.push_back(document_path);
  const auto source = readSource(document_path);
  const auto parsed = source ? assets::detail::parseJsonProfile(*source)
                             : assets::detail::JsonProfileParseResult{};
  if (!source || !parsed || !parsed.document->value.is_object()) {
    addDiagnostic(diagnostics, document_path.generic_string(),
                  "UI_DEV_DOCUMENT_INVALID",
                  !source ? "could not read development UI document"
                          : parsed.error->message);
    return false;
  }
  Json document = parsed.document->value;
  std::unordered_set<std::string> active{document_path.generic_string()};
  if (!rewriteReferences(document, document_path.parent_path(), root, staging,
                         graph, active, diagnostics, graph.document_key)) {
    return false;
  }
  std::string canonical;
  std::vector<assets::UiAssetDependency> dependencies;
  std::string error;
  if (!assets::detail::validateUiDocumentJson(document.dump(), canonical,
                                              dependencies, &error) ||
      !staging.registerUiDocumentAsset(
          graph.document_key,
          {.canonical_json_utf8 = std::move(canonical),
           .dependencies = std::move(dependencies)})) {
    addDiagnostic(diagnostics, graph.document_key, "UI_DEV_DOCUMENT_INVALID", error);
    return false;
  }
  graph.assets.push_back({.type = "ui_document",
                          .key = graph.document_key,
                          .source_path = document_path});
  std::sort(graph.watched_paths.begin(), graph.watched_paths.end());
  graph.watched_paths.erase(
      std::unique(graph.watched_paths.begin(), graph.watched_paths.end()),
      graph.watched_paths.end());
  return true;
}

bool commitDevelopmentGraph(assets::AssetRegistry& destination,
                            assets::AssetRegistry& staging,
                            const DevelopmentGraphBuild& graph) {
  for (const DevelopmentAssetRecord& record : graph.assets) {
    if (record.type == "ui_document") destination.unregisterUiDocumentAsset(record.key);
    else if (record.type == "ui_theme") destination.unregisterUiThemeAsset(record.key);
    else if (record.type == "font") destination.unregisterFontAsset(record.key);
    else if (record.type == "svg") destination.unregisterSvgAsset(record.key);
    else if (record.type == "texture") destination.unregisterTextureAsset(record.key);
    if (!destination.moveAssetFrom(staging, record.type, record.key)) return false;
  }
  return true;
}

}  // namespace karma::ui::native
