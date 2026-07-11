#include "features/ui/native/hot_reload_coordinator.h"

#include "content/assets/asset_ui_source_import.h"
#include "features/ui/native/development_loader.h"
#include "features/ui/native/file_watcher.h"
#include "features/ui/native/runtime_dom.h"
#include "karma/assets.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace karma::ui::native {
namespace {

std::optional<std::string> readDevelopmentUiSource(
    const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) return std::nullopt;
  stream.seekg(0, std::ios::beg);
  std::string source(static_cast<std::size_t>(size), '\0');
  if (size > 0) stream.read(source.data(), size);
  return stream || size == 0
             ? std::optional<std::string>{std::move(source)}
             : std::nullopt;
}

}  // namespace

struct HotReloadCoordinator::Impl {
  Impl(assets::AssetRegistry& asset_registry,
       HotReloadCoordinatorConfig requested_config)
      : assets(&asset_registry), config(std::move(requested_config)) {
    if (config.development_files.enabled &&
        !config.development_files.roots.empty()) {
      file_watcher = std::make_unique<FileWatcher>(FileWatcherConfig{
          .roots = config.development_files.roots,
          .fingerprint_poll_interval =
              config.development_files.polling_fallback,
      });
    }
  }

  [[nodiscard]] const std::vector<std::filesystem::path>& developmentRoots()
      const noexcept {
    return file_watcher ? file_watcher->roots()
                        : config.development_files.roots;
  }

  void advanceLooseDevelopmentGraphs(
      float dt,
      std::span<runtime_dom::DocumentInstance* const> documents) {
    if (!config.enabled || !file_watcher) return;

    FileWatcherPollResult changes = file_watcher->drain();
    if (changes.rescan_required || !changes.changed_paths.empty()) {
      development_rescan_pending =
          development_rescan_pending || changes.rescan_required;
      for (const std::filesystem::path& path : changes.changed_paths) {
        pending_development_paths.insert(path.generic_string());
      }
      development_debounce_elapsed_seconds = 0.0;
    } else if (std::isfinite(dt) && dt > 0.0f &&
               (!pending_development_paths.empty() ||
                development_rescan_pending)) {
      development_debounce_elapsed_seconds += dt;
    }

    const double debounce_seconds = std::max(
        0.0, std::chrono::duration<double>(config.development_files.debounce)
                 .count());
    if ((pending_development_paths.empty() &&
         !development_rescan_pending) ||
        (debounce_seconds > 0.0 &&
         development_debounce_elapsed_seconds < debounce_seconds)) {
      return;
    }

    for (runtime_dom::DocumentInstance* document : documents) {
      if (document == nullptr || document->development_path.empty()) continue;
      runtime_dom::DocumentInstance& doc = *document;
      const bool affected = development_rescan_pending ||
          std::any_of(doc.development_dependencies.begin(),
                      doc.development_dependencies.end(),
                      [&](const std::filesystem::path& dependency) {
                        return pending_development_paths.contains(
                            dependency.generic_string());
                      });
      if (!affected) continue;

      assets::AssetRegistry staging;
      DevelopmentGraphBuild graph;
      std::vector<Diagnostic> diagnostics;
      if (buildDevelopmentGraph(doc.development_path, doc.development_root,
                                staging, graph, diagnostics) &&
          commitDevelopmentGraph(*assets, staging, graph)) {
        doc.development_dependencies = std::move(graph.watched_paths);
        // Successful validation replaces diagnostics even when the source is
        // byte-identical to the active last-good graph.
        doc.diagnostics = std::move(diagnostics);
        reload_poll_elapsed_seconds = std::max(
            reload_poll_elapsed_seconds,
            std::chrono::duration<double>(config.source_poll_interval).count());
      } else {
        doc.diagnostics = std::move(diagnostics);
      }
    }

    pending_development_paths.clear();
    development_rescan_pending = false;
    development_debounce_elapsed_seconds = 0.0;
  }

  void queueHotReload(runtime_dom::DocumentInstance& doc,
                      double poll_interval_seconds) {
    const assets::UiDocumentAsset* current_asset =
        assets->findUiDocumentAsset(doc.asset_key);
    HotReloadSnapshot snapshot;
    snapshot.document = doc.handle;
    snapshot.asset_key = doc.asset_key;
    if (current_asset != nullptr) {
      if (const auto source_path =
              assets::detail::UiSourceMetadataAccess::document(*assets,
                                                               doc.asset_key)) {
        if (auto source = readDevelopmentUiSource(*source_path)) {
          snapshot.document_available = true;
          snapshot.document_source = std::move(*source);
          snapshot.document_hash =
              assets::hashString(snapshot.document_source);
        }
      } else {
        snapshot.document_available = true;
        snapshot.document_source = current_asset->canonical_json_utf8;
        snapshot.document_hash =
            current_asset->content_hash.empty()
                ? assets::hashString(current_asset->canonical_json_utf8)
                : current_asset->content_hash;
      }
    }
    if (!snapshot.document_available) snapshot.document_hash = "<missing>";
    snapshot.replace_document = !snapshot.document_available ||
                                snapshot.document_hash != doc.source_hash;

    snapshot.stylesheet_keys = doc.stylesheet_keys;
    std::vector<std::string> theme_source_keys = snapshot.stylesheet_keys;
    for (const std::string& key : doc.reload_stylesheet_keys) {
      if (std::find(theme_source_keys.begin(), theme_source_keys.end(), key) ==
          theme_source_keys.end()) {
        theme_source_keys.push_back(key);
      }
    }
    if (snapshot.replace_document && current_asset != nullptr) {
      for (const assets::UiAssetDependency& dependency :
           current_asset->dependencies) {
        if (dependency.kind != assets::UiAssetDependencyKind::UiTheme ||
            std::find(theme_source_keys.begin(), theme_source_keys.end(),
                      dependency.key) != theme_source_keys.end()) {
          continue;
        }
        theme_source_keys.push_back(dependency.key);
      }
    }
    for (std::size_t index = 0u; index < theme_source_keys.size(); ++index) {
      const assets::UiThemeAsset* theme =
          assets->findUiThemeAsset(theme_source_keys[index]);
      if (theme == nullptr) continue;
      for (const assets::UiAssetDependency& dependency : theme->dependencies) {
        if (dependency.kind != assets::UiAssetDependencyKind::UiTheme ||
            std::find(theme_source_keys.begin(), theme_source_keys.end(),
                      dependency.key) != theme_source_keys.end()) {
          continue;
        }
        theme_source_keys.push_back(dependency.key);
      }
    }

    bool styles_available = true;
    std::map<std::string, std::string> current_theme_hashes;
    for (const std::string& key : theme_source_keys) {
      if (std::any_of(snapshot.stylesheets.begin(), snapshot.stylesheets.end(),
                      [&](const HotReloadStylesheetSnapshot& source) {
                        return source.key == key;
                      })) {
        continue;
      }
      HotReloadStylesheetSnapshot source;
      source.key = key;
      if (const assets::UiThemeAsset* theme = assets->findUiThemeAsset(key)) {
        if (const auto source_path =
                assets::detail::UiSourceMetadataAccess::theme(*assets, key)) {
          if (auto source_text = readDevelopmentUiSource(*source_path)) {
            source.available = true;
            source.source = std::move(*source_text);
            source.content_hash = assets::hashString(source.source);
          }
        } else {
          source.available = true;
          source.source = theme->canonical_json_utf8;
          source.content_hash =
              theme->content_hash.empty()
                  ? assets::hashString(theme->canonical_json_utf8)
                  : theme->content_hash;
        }
        if (source.available) {
          current_theme_hashes[key] = source.content_hash;
        } else {
          styles_available = false;
        }
      } else {
        styles_available = false;
      }
      snapshot.stylesheets.push_back(std::move(source));
    }

    std::string current_style_hash;
    for (const auto& [key, hash] : current_theme_hashes) {
      current_style_hash += key + ":" + hash + ";";
    }

    snapshot.fingerprint = snapshot.document_hash;
    snapshot.fingerprint += snapshot.replace_document ? "\nD\n" : "\nS\n";
    for (const HotReloadStylesheetSnapshot& source : snapshot.stylesheets) {
      snapshot.fingerprint += source.key;
      snapshot.fingerprint += ':';
      snapshot.fingerprint +=
          source.available ? source.content_hash : std::string{"<missing>"};
      snapshot.fingerprint += ';';
    }

    const bool styles_changed =
        !styles_available || current_style_hash != doc.style_hash;
    const bool reload_needed = snapshot.replace_document || styles_changed;
    const bool recover_last_good =
        !reload_needed && !doc.reload_requested_fingerprint.empty() &&
        doc.reload_requested_fingerprint != snapshot.fingerprint;
    if (!reload_needed && !recover_last_good) {
      if (doc.reload_pending &&
          doc.reload_requested_fingerprint != snapshot.fingerprint) {
        doc.reload_request_sequence = 0u;
        doc.reload_pending = false;
      }
      return;
    }
    if (doc.reload_requested_fingerprint == snapshot.fingerprint) return;

    if (recover_last_good) snapshot.replace_document = false;
    doc.reload_requested_fingerprint = snapshot.fingerprint;
    doc.reload_request_sequence = worker.enqueue(std::move(snapshot));
    doc.reload_pending = true;

    // A zero interval is the deterministic tools/tests mode. Staging still
    // happens on the worker, but this tick adopts its completed result.
    if (poll_interval_seconds <= 0.0) {
      worker.waitUntilCompleted(doc.reload_request_sequence);
    }
  }

  [[nodiscard]] std::vector<HotReloadStage> tick(
      float dt,
      std::span<runtime_dom::DocumentInstance* const> documents) {
    if (config.enabled && std::isfinite(dt) && dt > 0.0f) {
      reload_poll_elapsed_seconds += static_cast<double>(dt);
    }

    advanceLooseDevelopmentGraphs(dt, documents);

    const double poll_interval_seconds =
        std::max(0.0, std::chrono::duration<double>(config.source_poll_interval)
                          .count());
    const bool poll_hot_reload =
        config.enabled &&
        (poll_interval_seconds <= 0.0 ||
         reload_poll_elapsed_seconds >= poll_interval_seconds);
    if (poll_hot_reload && poll_interval_seconds > 0.0) {
      reload_poll_elapsed_seconds =
          std::fmod(reload_poll_elapsed_seconds, poll_interval_seconds);
    }
    if (poll_hot_reload) {
      for (runtime_dom::DocumentInstance* document : documents) {
        if (document != nullptr) {
          queueHotReload(*document, poll_interval_seconds);
        }
      }
    }
    return worker.takeCompleted();
  }

  assets::AssetRegistry* assets = nullptr;
  HotReloadCoordinatorConfig config{};
  std::unique_ptr<FileWatcher> file_watcher;
  HotReloadWorker worker{};
  double reload_poll_elapsed_seconds = 0.0;
  double development_debounce_elapsed_seconds = 0.0;
  std::unordered_set<std::string> pending_development_paths;
  bool development_rescan_pending = false;
};

HotReloadCoordinator::HotReloadCoordinator(
    assets::AssetRegistry& assets,
    HotReloadCoordinatorConfig config)
    : impl_(std::make_unique<Impl>(assets, std::move(config))) {}

HotReloadCoordinator::~HotReloadCoordinator() = default;

const std::vector<std::filesystem::path>&
HotReloadCoordinator::developmentRoots() const noexcept {
  return impl_->developmentRoots();
}

std::vector<HotReloadStage> HotReloadCoordinator::tick(
    float dt,
    std::span<runtime_dom::DocumentInstance* const> documents) {
  return impl_->tick(dt, documents);
}

void HotReloadCoordinator::invalidate() { impl_->worker.invalidate(); }

}  // namespace karma::ui::native
