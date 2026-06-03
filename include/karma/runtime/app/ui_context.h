#pragma once

#include <filesystem>
#include <vector>

#include "karma/platform/window/events.h"
#include "karma/runtime/app/ui_draw_data.h"

namespace karma::input {
class InputSystem;
}

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::app {

class UIContext {
 public:
  UIContext() = default;
  ~UIContext();

  UIContext(const UIContext&) = delete;
  UIContext& operator=(const UIContext&) = delete;

  UIFrameInfo frame() const { return frame_; }

  UITextureHandle createTextureRGBA8(int w, int h, const void* pixels);
  UITexture loadTextureRGBA8FromPng(const std::filesystem::path& path);
  void updateTextureRGBA8(UITextureHandle tex, int w, int h, const void* pixels);
  void destroyTexture(UITextureHandle tex);
  void destroyOwnedTextures();
  void reset();

  UIDrawData& drawData() { return draw_data_; }

  karma::input::InputSystem& input();

 private:
  friend class EngineApp;
  UIFrameInfo frame_{};
  UIDrawData draw_data_{};
  std::vector<UITextureHandle> owned_textures_;
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
