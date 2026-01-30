#pragma once

#include "karma/app/ui_draw_data.h"
#include "karma/platform/events.h"

namespace karma::input {
class InputSystem;
}

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::app {

class UIContext {
 public:
  UIFrameInfo frame() const { return frame_; }

  UITextureHandle createTextureRGBA8(int w, int h, const void* pixels);
  void updateTextureRGBA8(UITextureHandle tex, int w, int h, const void* pixels);
  void destroyTexture(UITextureHandle tex);

  UIDrawData& drawData() { return draw_data_; }

  karma::input::InputSystem& input();

 private:
  friend class EngineApp;
  UIFrameInfo frame_{};
  UIDrawData draw_data_{};
  input::InputSystem* input_ = nullptr;
  renderer::GraphicsDevice* device_ = nullptr;
};

class UiLayer {
 public:
  virtual ~UiLayer() = default;
  virtual void onFrame(UIContext& ctx) = 0;
  virtual void onEvent(const platform::Event& event) { (void)event; }
  virtual void onShutdown() {}
};

}  // namespace karma::app
