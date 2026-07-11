#pragma once

#include "features/ui/native/authoring.h"
#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/runtime_dom.h"
#include "karma/ui.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace karma::ui::native {

struct HotReloadStylesheetSnapshot {
  std::string key;
  std::string source;
  std::string content_hash;
  bool available = false;
};

/// Immutable source snapshot handed from the frame thread to reload staging.
struct HotReloadSnapshot {
  DocumentHandle document{};
  std::uint64_t sequence = 0u;
  std::uint64_t epoch = 0u;
  std::string fingerprint;
  std::string asset_key;
  std::string document_source;
  std::string document_hash;
  std::vector<std::string> stylesheet_keys;
  std::vector<HotReloadStylesheetSnapshot> stylesheets;
  bool document_available = false;
  bool replace_document = false;
};

/// Parsed last-good candidate returned to the frame thread for atomic swap.
struct HotReloadStage {
  DocumentHandle document{};
  std::uint64_t sequence = 0u;
  std::uint64_t epoch = 0u;
  std::string fingerprint;
  std::string document_hash;
  std::string style_hash;
  std::vector<std::string> stylesheet_keys;
  std::unique_ptr<runtime_dom::Node> body;
  CanvasSpec canvas;
  std::vector<StyleRule> rules;
  std::vector<FontFaceDefinition> font_faces;
  std::vector<Keyframes> keyframes;
  std::vector<Diagnostic> diagnostics;
  bool replace_document = false;
  bool needs_stylesheet_resnapshot = false;
  bool valid = false;
};

/// Single-worker reload staging queue. New work for the same document replaces
/// older queued work, while completed stages remain ordered for frame-bound
/// adoption. invalidate() advances the epoch and discards all stale work.
class HotReloadWorker {
 public:
  HotReloadWorker();
  ~HotReloadWorker();

  HotReloadWorker(const HotReloadWorker&) = delete;
  HotReloadWorker& operator=(const HotReloadWorker&) = delete;

  [[nodiscard]] std::uint64_t enqueue(HotReloadSnapshot snapshot);
  [[nodiscard]] std::vector<HotReloadStage> takeCompleted();
  void waitUntilCompleted(std::uint64_t sequence);
  void invalidate();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace karma::ui::native
