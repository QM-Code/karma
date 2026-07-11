#include "features/ui/native/hot_reload_runtime.h"

#include "features/ui/native/diagnostics.h"
#include "features/ui/native/document_loader.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace karma::ui::native {
namespace {

HotReloadStage stageHotReload(HotReloadSnapshot snapshot) {
  HotReloadStage output;
  output.document = snapshot.document;
  output.sequence = snapshot.sequence;
  output.epoch = snapshot.epoch;
  output.fingerprint = std::move(snapshot.fingerprint);
  output.document_hash = std::move(snapshot.document_hash);
  output.replace_document = snapshot.replace_document;

  if (snapshot.replace_document && !snapshot.document_available) {
    addDiagnostic(output.diagnostics, snapshot.asset_key,
                  "UI_DOCUMENT_NOT_FOUND",
                  "UI document asset was not found during hot reload");
  } else if (snapshot.replace_document) {
    ParsedDocument parsed =
        parseDocumentSource(snapshot.document_source, snapshot.asset_key);
    output.body = std::move(parsed.body);
    output.canvas = parsed.canvas;
    output.stylesheet_keys = std::move(parsed.stylesheet_keys);
    output.diagnostics = std::move(parsed.diagnostics);
  } else {
    output.stylesheet_keys = std::move(snapshot.stylesheet_keys);
  }

  std::size_t source_order = 0u;
  std::map<std::string, std::string> theme_hashes;
  const ThemeSourceResolver resolver =
      [&](std::string_view key) -> std::optional<ThemeSource> {
    const auto source = std::find_if(
        snapshot.stylesheets.begin(), snapshot.stylesheets.end(),
        [&](const HotReloadStylesheetSnapshot& candidate) {
          return candidate.key == key;
        });
    if (source == snapshot.stylesheets.end() || !source->available) {
      return std::nullopt;
    }
    return ThemeSource{.source = source->source,
                       .content_hash = source->content_hash};
  };
  for (const std::string& key : output.stylesheet_keys) {
    ParsedTheme parsed_theme = parseThemeGraph(key, resolver, source_order);
    output.needs_stylesheet_resnapshot =
        output.needs_stylesheet_resnapshot ||
        !parsed_theme.missing_source_keys.empty();
    for (const std::string& source_key : parsed_theme.source_keys) {
      const auto source = resolver(source_key);
      if (source.has_value()) theme_hashes[source_key] = source->content_hash;
    }
    output.diagnostics.insert(
        output.diagnostics.end(),
        std::make_move_iterator(parsed_theme.diagnostics.begin()),
        std::make_move_iterator(parsed_theme.diagnostics.end()));
    const std::size_t font_order = output.font_faces.size();
    for (FontFaceDefinition& face : parsed_theme.font_faces) {
      face.source_order += font_order;
    }
    output.rules.insert(
        output.rules.end(),
        std::make_move_iterator(parsed_theme.rules.begin()),
        std::make_move_iterator(parsed_theme.rules.end()));
    output.font_faces.insert(
        output.font_faces.end(),
        std::make_move_iterator(parsed_theme.font_faces.begin()),
        std::make_move_iterator(parsed_theme.font_faces.end()));
    output.keyframes.insert(
        output.keyframes.end(),
        std::make_move_iterator(parsed_theme.keyframes.begin()),
        std::make_move_iterator(parsed_theme.keyframes.end()));
  }
  for (const auto& [key, hash] : theme_hashes) {
    output.style_hash += key + ":" + hash + ";";
  }

  output.valid = (!output.replace_document || output.body != nullptr) &&
                 std::none_of(
                     output.diagnostics.begin(), output.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error;
                     });
  return output;
}

}  // namespace

struct HotReloadWorker::State {
  State() : thread([this](std::stop_token stop) { run(stop); }) {}

  ~State() {
    thread.request_stop();
    work_ready.notify_all();
  }

  std::uint64_t enqueue(HotReloadSnapshot snapshot) {
    std::lock_guard lock(mutex);
    snapshot.sequence = next_sequence++;
    if (next_sequence == 0u) ++next_sequence;
    snapshot.epoch = epoch;
    const std::uint64_t sequence = snapshot.sequence;
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                              [&](const HotReloadSnapshot& queued) {
                                return queued.document == snapshot.document;
                              }),
               jobs.end());
    jobs.push_back(std::move(snapshot));
    work_ready.notify_one();
    return sequence;
  }

  std::vector<HotReloadStage> takeCompleted() {
    std::lock_guard lock(mutex);
    std::vector<HotReloadStage> output;
    output.reserve(completed.size());
    while (!completed.empty()) {
      output.push_back(std::move(completed.front()));
      completed.pop_front();
    }
    return output;
  }

  void waitUntilCompleted(std::uint64_t sequence) {
    std::unique_lock lock(mutex);
    completed_ready.wait(lock, [&] {
      return std::any_of(completed.begin(), completed.end(),
                         [&](const HotReloadStage& stage) {
                           return stage.sequence == sequence;
                         });
    });
  }

  void invalidate() {
    std::lock_guard lock(mutex);
    ++epoch;
    if (epoch == 0u) ++epoch;
    jobs.clear();
    completed.clear();
    completed_ready.notify_all();
  }

  void run(std::stop_token stop) {
    while (!stop.stop_requested()) {
      HotReloadSnapshot snapshot;
      {
        std::unique_lock lock(mutex);
        work_ready.wait(lock, [&] {
          return stop.stop_requested() || !jobs.empty();
        });
        if (stop.stop_requested()) return;
        snapshot = std::move(jobs.front());
        jobs.pop_front();
      }

      HotReloadStage stage;
      const DocumentHandle document = snapshot.document;
      const std::uint64_t sequence = snapshot.sequence;
      const std::uint64_t stage_epoch = snapshot.epoch;
      const std::string fingerprint = snapshot.fingerprint;
      const std::string document_hash = snapshot.document_hash;
      const std::string asset_key = snapshot.asset_key;
      const bool replace_document = snapshot.replace_document;
      try {
        stage = stageHotReload(std::move(snapshot));
      } catch (const std::exception& error) {
        stage.document = document;
        stage.sequence = sequence;
        stage.epoch = stage_epoch;
        stage.fingerprint = fingerprint;
        stage.document_hash = document_hash;
        stage.replace_document = replace_document;
        addDiagnostic(stage.diagnostics, asset_key, "UI_RELOAD_STAGING",
                      error.what());
      } catch (...) {
        stage.document = document;
        stage.sequence = sequence;
        stage.epoch = stage_epoch;
        stage.fingerprint = fingerprint;
        stage.document_hash = document_hash;
        stage.replace_document = replace_document;
        addDiagnostic(stage.diagnostics, asset_key, "UI_RELOAD_STAGING",
                      "unexpected exception while staging UI reload");
      }

      {
        std::lock_guard lock(mutex);
        if (stage.epoch == epoch) completed.push_back(std::move(stage));
      }
      completed_ready.notify_all();
    }
  }

  std::mutex mutex;
  std::condition_variable work_ready;
  std::condition_variable completed_ready;
  std::deque<HotReloadSnapshot> jobs;
  std::deque<HotReloadStage> completed;
  std::uint64_t next_sequence = 1u;
  std::uint64_t epoch = 1u;
  std::jthread thread;
};

HotReloadWorker::HotReloadWorker() : state_(std::make_unique<State>()) {}

HotReloadWorker::~HotReloadWorker() = default;

std::uint64_t HotReloadWorker::enqueue(HotReloadSnapshot snapshot) {
  return state_->enqueue(std::move(snapshot));
}

std::vector<HotReloadStage> HotReloadWorker::takeCompleted() {
  return state_->takeCompleted();
}

void HotReloadWorker::waitUntilCompleted(std::uint64_t sequence) {
  state_->waitUntilCompleted(sequence);
}

void HotReloadWorker::invalidate() { state_->invalidate(); }

}  // namespace karma::ui::native
