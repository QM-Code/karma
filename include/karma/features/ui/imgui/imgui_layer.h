#pragma once

#include <functional>
#include <memory>

#include <imgui.h>

#include "karma/runtime/app/ui_context.h"

namespace karma::imgui {

struct ImGuiLayerConfig {
  bool create_context = true;
  bool destroy_context = true;
  const char* backend_platform_name = "karma";
  const char* backend_renderer_name = "karma_ui_draw";
};

using FrameCallback = std::function<void(app::UIContext&)>;

struct ImGuiLayerCallbacks {
  FrameCallback draw;
  FrameCallback shutdown;
};

ImTextureID toTextureId(app::UITextureHandle handle);
app::UITextureHandle fromTextureId(ImTextureID id);

std::unique_ptr<app::UiLayer> createUiLayer(FrameCallback draw,
                                            ImGuiLayerConfig config = {});
std::unique_ptr<app::UiLayer> createUiLayer(ImGuiLayerCallbacks callbacks,
                                            ImGuiLayerConfig config = {});
std::unique_ptr<app::UiLayer> createUiLayer(ImGuiLayerConfig config = {});

}  // namespace karma::imgui
