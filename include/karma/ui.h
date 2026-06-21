#pragma once

#include "karma/app.h"
#include "karma/rendering.h"



#include <functional>
#include <memory>

#include <imgui.h>


namespace karma::ui::imgui {

/// \ingroup karma_ui
/// ImGui adapter lifecycle configuration.
struct ImGuiLayerConfig {
  bool create_context = true;
  bool destroy_context = true;
  const char* backend_platform_name = "karma";
  const char* backend_renderer_name = "karma_ui_draw";
};

/// \ingroup karma_ui
/// Callback invoked during an ImGui frame.
using FrameCallback = std::function<void(app::UIContext&)>;

/// \ingroup karma_ui
/// Optional ImGui frame/shutdown callbacks.
struct ImGuiLayerCallbacks {
  FrameCallback draw;
  FrameCallback shutdown;
};

/// Converts a Karma UI texture handle to ImGui's texture id type.
ImTextureID toTextureId(app::UITextureHandle handle);
/// Converts an ImGui texture id back to a Karma UI texture handle.
app::UITextureHandle fromTextureId(ImTextureID id);

/// Creates an ImGui UI layer with a frame callback.
std::unique_ptr<app::UiLayer> createUiLayer(FrameCallback draw,
                                            ImGuiLayerConfig config = {});
/// Creates an ImGui UI layer with frame/shutdown callbacks.
std::unique_ptr<app::UiLayer> createUiLayer(ImGuiLayerCallbacks callbacks,
                                            ImGuiLayerConfig config = {});
/// Creates an ImGui UI layer for users that call ImGui externally.
std::unique_ptr<app::UiLayer> createUiLayer(ImGuiLayerConfig config = {});

}  // namespace karma::ui::imgui


#if defined(KARMA_ENABLE_RMLUI)

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core.h>


namespace karma::ui::rmlui {

/// \ingroup karma_ui
/// RmlUi adapter lifecycle and asset-root configuration.
struct RmlUiLayerConfig {
  std::string context_name = "karma";
  std::vector<std::filesystem::path> asset_roots;
  bool initialize_rmlui = true;
  bool shutdown_rmlui = true;
};

/// \ingroup karma_ui
/// Callback invoked with an RmlUi context.
using ContextCallback = std::function<void(Rml::Context&)>;
/// \ingroup karma_ui
/// Callback invoked during each RmlUi frame.
using FrameCallback = std::function<void(Rml::Context&, app::UIContext&)>;

/// \ingroup karma_ui
/// Optional RmlUi lifecycle callbacks.
struct RmlUiLayerCallbacks {
  ContextCallback on_context_ready;
  FrameCallback on_frame;
  ContextCallback on_shutdown;
};

/// Creates an RmlUi layer with default callbacks.
std::unique_ptr<app::UiLayer> createUiLayer(RmlUiLayerConfig config = {});
/// Creates an RmlUi layer with a context-ready callback.
std::unique_ptr<app::UiLayer> createUiLayer(ContextCallback on_context_ready,
                                            RmlUiLayerConfig config = {});
/// Creates an RmlUi layer with full lifecycle callbacks.
std::unique_ptr<app::UiLayer> createUiLayer(RmlUiLayerCallbacks callbacks,
                                            RmlUiLayerConfig config = {});

}  // namespace karma::ui::rmlui

#endif  // defined(KARMA_ENABLE_RMLUI)
