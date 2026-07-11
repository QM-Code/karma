#pragma once

#if defined(KARMA_ENABLE_IMGUI)

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

#endif  // defined(KARMA_ENABLE_IMGUI)
