#include "karma/ui/imgui_overlay.h"

namespace karma::ui {

ImGuiOverlay::ImGuiOverlay(std::function<void()> callback)
    : callback_(std::move(callback)) {}

void ImGuiOverlay::onFrameBegin(int /*width*/, int /*height*/, float /*dt*/) {}

void ImGuiOverlay::onEvent(const platform::Event& event) {
  if (device_) {
    device_->handleOverlayEvent(event);
  }
}

void ImGuiOverlay::onRender(renderer::GraphicsDevice& device) {
  if (!callback_) {
    return;
  }
  if (!device_) {
    device_ = &device;
  }
  device.setOverlayCallback(callback_);
  device.renderOverlay();
}

void ImGuiOverlay::onShutdown() {
  callback_ = nullptr;
  device_ = nullptr;
}

}  // namespace karma::ui
