#pragma once

#include "karma/rendering.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace karma::ui::native {

class PresentationResources;
class TextEngine;

namespace runtime_dom {
struct DocumentInstance;
struct Node;
}  // namespace runtime_dom

namespace presentation_builder {

/// Window-level inputs needed to assemble retained document paint.
struct FrameInputs {
  int framebuffer_width = 0;
  int framebuffer_height = 0;
  float framebuffer_scale_x = 1.0f;
  float framebuffer_scale_y = 1.0f;
  std::string_view locale;
  std::size_t retained_paint_budget_bytes = 0u;
  bool graphics_available = false;
};

/// Optional bridge for diagnostics that require document ownership context.
/// Paint counters are returned in BuildResult and never reach back into System.
struct DiagnosticSink {
  using NineSliceCellLimitCallback = void (*)(
      void* context,
      runtime_dom::DocumentInstance& document,
      const runtime_dom::Node& node);

  void* context = nullptr;
  NineSliceCellLimitCallback nine_slice_cell_limit = nullptr;
};

struct BuildResult {
  std::size_t rebuilt_fragments = 0u;
  std::size_t evicted_fragments = 0u;
  /// False only when renderer resources changed during both paint attempts.
  bool generation_stable = true;
};

/// Appends all native document paint in retained order. The builder owns
/// primitive/scissor batching, glyph expansion, widget chrome, overlays,
/// retained-fragment assembly and LRU enforcement. Existing output is restored
/// exactly if a resource-generation retry cannot produce a stable slice.
[[nodiscard]] BuildResult build(
    std::span<runtime_dom::DocumentInstance* const> documents,
    const FrameInputs& frame,
    TextEngine& text_engine,
    PresentationResources& resources,
    rendering::UIDrawData& output,
    DiagnosticSink diagnostics = {});

}  // namespace presentation_builder
}  // namespace karma::ui::native
