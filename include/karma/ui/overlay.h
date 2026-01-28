#pragma once

#include "karma/platform/events.h"
#include "karma/renderer/device.h"

namespace karma::ui {

class Overlay {
 public:
  virtual ~Overlay() = default;
  virtual void onFrameBegin(int width, int height, float dt) = 0;
  virtual void onEvent(const platform::Event& event) = 0;
  virtual void onRender(renderer::GraphicsDevice& device) = 0;
  virtual void onShutdown() {}
};

}  // namespace karma::ui
