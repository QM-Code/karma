#pragma once

#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/runtime_dom.h"
#include "karma/ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {

struct ParsedDocument {
  std::unique_ptr<runtime_dom::Node> body;
  CanvasSpec canvas;
  std::vector<std::string> stylesheet_keys;
  Value model_defaults = Value::Object{};
  std::vector<Diagnostic> diagnostics;
};

ParsedDocument parseDocumentSource(std::string_view source,
                                   std::string_view asset_key);

}  // namespace karma::ui::native
