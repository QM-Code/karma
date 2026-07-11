#pragma once

#include <cstddef>
#include <string_view>

namespace karma::ui::native {

class PresentationResources;
class TextEngine;

namespace runtime_dom {
struct DocumentInstance;
}

namespace document_layout_runtime {

/// Window-level values used while adapting computed DOM style to the retained
/// layout engine. Document canvas dimensions remain authoritative for geometry.
struct LayoutInputs {
  int logical_width = 0;
  int logical_height = 0;
  std::string_view locale;
};

struct LayoutResult {
  std::size_t laid_out_nodes = 0u;
  bool performed = false;
};

/// Measures and lays out one document, applies anchors, scrolling geometry and
/// clipping, then advances the document's layout-related work revisions.
/// Renderer/text services are borrowed and must outlive the call.
[[nodiscard]] LayoutResult layoutDocument(
    runtime_dom::DocumentInstance& document,
    const LayoutInputs& inputs,
    TextEngine& text_engine,
    PresentationResources& presentation_resources);

}  // namespace document_layout_runtime
}  // namespace karma::ui::native
