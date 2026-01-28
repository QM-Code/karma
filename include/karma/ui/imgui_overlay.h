#pragma once

#ifndef KARMA_WITH_IMGUI
#define KARMA_WITH_IMGUI 0
#endif

#if !KARMA_WITH_IMGUI
#error "ImGui backend not enabled. Build with KARMA_WITH_IMGUI=ON and provide ImGui."
#endif

#include "karma/ui/overlay.h"

#include <functional>

namespace karma::ui {

class ImGuiOverlay final : public Overlay {
 public:
  explicit ImGuiOverlay(std::function<void()> callback);

  void onFrameBegin(int width, int height, float dt) override;
  void onEvent(const platform::Event& event) override;
  void onRender(renderer::GraphicsDevice& device) override;
 void onShutdown() override;

 private:
  std::function<void()> callback_;
  renderer::GraphicsDevice* device_ = nullptr;
};

}  // namespace karma::ui
