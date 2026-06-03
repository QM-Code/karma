#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core.h>

#include "karma/runtime/app/ui_context.h"

namespace karma::rmlui {

struct RmlUiLayerConfig {
  std::string context_name = "karma";
  std::vector<std::filesystem::path> asset_roots;
  bool initialize_rmlui = true;
  bool shutdown_rmlui = true;
};

using ContextCallback = std::function<void(Rml::Context&)>;
using FrameCallback = std::function<void(Rml::Context&, app::UIContext&)>;

struct RmlUiLayerCallbacks {
  ContextCallback on_context_ready;
  FrameCallback on_frame;
  ContextCallback on_shutdown;
};

std::unique_ptr<app::UiLayer> createUiLayer(RmlUiLayerConfig config = {});
std::unique_ptr<app::UiLayer> createUiLayer(ContextCallback on_context_ready,
                                            RmlUiLayerConfig config = {});
std::unique_ptr<app::UiLayer> createUiLayer(RmlUiLayerCallbacks callbacks,
                                            RmlUiLayerConfig config = {});

}  // namespace karma::rmlui
