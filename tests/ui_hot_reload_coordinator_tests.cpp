#include "features/ui/native/development_loader.h"
#include "features/ui/native/hot_reload_coordinator.h"
#include "features/ui/native/runtime_dom.h"
#include "karma/assets.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using karma::assets::AssetRegistry;
using karma::ui::Diagnostic;
using karma::ui::DocumentHandle;
using karma::ui::native::DevelopmentGraphBuild;
using karma::ui::native::HotReloadCoordinator;
using karma::ui::native::HotReloadCoordinatorConfig;
using karma::ui::native::HotReloadStage;
using karma::ui::native::runtime_dom::DocumentInstance;

constexpr std::string_view kInitialDocument = R"JSON({
  format: 'karma.ui.document', version: 2,
  root: {type: 'body', children: [{type: 'text', props: {text: 'Before'}}]},
})JSON";

constexpr std::string_view kChangedDocument = R"JSON({
  format: 'karma.ui.document', version: 2,
  root: {type: 'body', children: [{type: 'text', props: {text: 'After'}}]},
})JSON";

class TemporaryDirectory {
 public:
  explicit TemporaryDirectory(std::string_view label) {
    static std::atomic_uint64_t sequence{0u};
    path = std::filesystem::temp_directory_path() /
           (std::string(label) + "_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "_" + std::to_string(sequence.fetch_add(1u)));
    std::filesystem::create_directories(path);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  std::filesystem::path path;
};

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.close();
  assert(output);
}

std::string assetHash(const karma::assets::UiDocumentAsset& asset) {
  return asset.content_hash.empty()
             ? karma::assets::hashString(asset.canonical_json_utf8)
             : asset.content_hash;
}

std::span<DocumentInstance* const> documents(
    std::vector<DocumentInstance*>& storage) {
  return storage;
}

std::vector<HotReloadStage> waitForCompleted(
    HotReloadCoordinator& coordinator,
    std::vector<DocumentInstance*>& open_documents) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<HotReloadStage> completed =
        coordinator.tick(0.0f, documents(open_documents));
    if (!completed.empty()) return completed;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(false && "timed out waiting for hot-reload staging");
  return {};
}

void testPollingAndFingerprintDeduplication() {
  AssetRegistry assets;
  assert(assets.registerUiDocumentAsset(
      "ui/coordinator-poll",
      {.canonical_json_utf8 = std::string(kInitialDocument)}));
  const auto* initial = assets.findUiDocumentAsset("ui/coordinator-poll");
  assert(initial != nullptr);

  DocumentInstance document;
  document.handle = DocumentHandle{.index = 0u, .generation = 1u};
  document.asset_key = "ui/coordinator-poll";
  document.source_hash = assetHash(*initial);
  std::vector<DocumentInstance*> open_documents{&document};

  HotReloadCoordinator coordinator(
      assets,
      {.enabled = true,
       .source_poll_interval = std::chrono::milliseconds(50),
       .development_files = {.enabled = false}});

  assert(assets.unregisterUiDocumentAsset("ui/coordinator-poll"));
  assert(assets.registerUiDocumentAsset(
      "ui/coordinator-poll",
      {.canonical_json_utf8 = std::string(kChangedDocument)}));

  assert(coordinator.tick(0.049f, documents(open_documents)).empty());
  assert(document.reload_request_sequence == 0u);
  std::vector<HotReloadStage> completed =
      coordinator.tick(0.002f, documents(open_documents));
  const std::uint64_t requested_sequence = document.reload_request_sequence;
  assert(requested_sequence != 0u);
  if (completed.empty()) {
    completed = waitForCompleted(coordinator, open_documents);
  }
  assert(completed.size() == 1u);
  assert(completed.front().sequence == requested_sequence);
  assert(completed.front().document == document.handle);
  assert(completed.front().valid && completed.front().replace_document);

  // The staged result has intentionally not been adopted. A later due poll of
  // the same immutable fingerprint must not enqueue another request.
  assert(coordinator.tick(0.050f, documents(open_documents)).empty());
  assert(document.reload_request_sequence == requested_sequence);
}

void testZeroIntervalCompletesInSameTick() {
  AssetRegistry assets;
  assert(assets.registerUiDocumentAsset(
      "ui/coordinator-zero",
      {.canonical_json_utf8 = std::string(kChangedDocument)}));

  DocumentInstance document;
  document.handle = DocumentHandle{.index = 3u, .generation = 7u};
  document.asset_key = "ui/coordinator-zero";
  document.source_hash = "stale-source";
  std::vector<DocumentInstance*> open_documents{&document};

  HotReloadCoordinator coordinator(
      assets,
      {.enabled = true,
       .source_poll_interval = std::chrono::milliseconds(0),
       .development_files = {.enabled = false}});
  std::vector<HotReloadStage> completed =
      coordinator.tick(0.0f, documents(open_documents));
  assert(completed.size() == 1u);
  assert(completed.front().sequence == document.reload_request_sequence);
  assert(completed.front().document == document.handle);
  assert(completed.front().valid && completed.front().replace_document);
  assert(completed.front().body != nullptr);

  // Same fingerprint remains deduplicated even though zero interval polls on
  // every tick.
  assert(coordinator.tick(0.0f, documents(open_documents)).empty());
}

void testLooseFileDebounceAndCommit() {
  TemporaryDirectory temporary("karma_ui_hot_reload_coordinator");
  const std::filesystem::path source = temporary.path / "menu.kui.json5";
  writeText(source, kInitialDocument);

  AssetRegistry assets;
  AssetRegistry staging;
  DevelopmentGraphBuild graph;
  std::vector<Diagnostic> diagnostics;
  assert(karma::ui::native::buildDevelopmentGraph(
      source, temporary.path, staging, graph, diagnostics));
  assert(diagnostics.empty());
  assert(karma::ui::native::commitDevelopmentGraph(assets, staging, graph));
  const auto* initial = assets.findUiDocumentAsset(graph.document_key);
  assert(initial != nullptr);

  DocumentInstance document;
  document.handle = DocumentHandle{.index = 1u, .generation = 1u};
  document.asset_key = graph.document_key;
  document.source_hash = assetHash(*initial);
  document.development_path = graph.document_path;
  document.development_root = temporary.path;
  document.development_dependencies = graph.watched_paths;
  std::vector<DocumentInstance*> open_documents{&document};

  HotReloadCoordinator coordinator(
      assets,
      {.enabled = true,
       .source_poll_interval = std::chrono::hours(1),
       .development_files =
           {.enabled = true,
            .roots = {temporary.path},
            .debounce = std::chrono::milliseconds(100),
            .polling_fallback = std::chrono::milliseconds(0)}});
  assert(coordinator.developmentRoots().size() == 1u);
  assert(coordinator.developmentRoots().front() ==
         std::filesystem::weakly_canonical(temporary.path));

  writeText(source, kChangedDocument);
  (void)coordinator.tick(0.0f, documents(open_documents));
  (void)coordinator.tick(0.040f, documents(open_documents));
  const auto* before_debounce = assets.findUiDocumentAsset(graph.document_key);
  assert(before_debounce != nullptr);
  assert(before_debounce->canonical_json_utf8.find("Before") !=
         std::string::npos);

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  bool committed = false;
  while (std::chrono::steady_clock::now() < deadline) {
    (void)coordinator.tick(0.030f, documents(open_documents));
    const auto* current = assets.findUiDocumentAsset(graph.document_key);
    committed = current != nullptr &&
                current->canonical_json_utf8.find("After") !=
                    std::string::npos;
    if (committed) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  assert(committed);
  assert(document.diagnostics.empty());
}

}  // namespace

int main() {
  testPollingAndFingerprintDeduplication();
  testZeroIntervalCompletesInSameTick();
  testLooseFileDebounceAndCommit();
  std::cout << "ui_hot_reload_coordinator_tests: ok\n";
  return 0;
}
