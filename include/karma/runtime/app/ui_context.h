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

/// \ingroup karma_runtime
/// Per-frame UI bridge passed to `UiLayer` implementations.
///
/// UI providers write normalized draw lists into `drawData()`. The context also
/// owns transient UI textures and exposes input data to provider adapters.
class UIContext {
 public:
  UIContext() = default;
  ~UIContext();

  UIContext(const UIContext&) = delete;
  UIContext& operator=(const UIContext&) = delete;

  /// Frame timing and viewport data for the current UI frame.
  UIFrameInfo frame() const { return frame_; }

  /// Creates a renderer texture from RGBA8 pixels and tracks it as UI-owned.
  UITextureHandle createTextureRGBA8(int w, int h, const void* pixels);
  /// Loads a PNG file into a UI-owned RGBA8 texture.
  UITexture loadTextureRGBA8FromPng(const std::filesystem::path& path);
  /// Updates an existing RGBA8 UI texture.
  void updateTextureRGBA8(UITextureHandle tex, int w, int h, const void* pixels);
  /// Destroys a UI texture.
  void destroyTexture(UITextureHandle tex);
  /// Destroys all textures owned by this context.
  void destroyOwnedTextures();
  /// Clears draw data for a new frame.
  void reset();

  /// Mutable draw list consumed by the renderer.
  renderer::UIDrawData& drawData() { return draw_data_; }

  /// Input system for UI provider adapters.
  karma::input::InputSystem& input();

 private:
  friend class EngineApp;
  UIFrameInfo frame_{};
  renderer::UIDrawData draw_data_{};
  std::vector<UITextureHandle> owned_textures_;
  input::InputSystem* input_ = nullptr;
  renderer::GraphicsDevice* device_ = nullptr;
};

/// \ingroup karma_runtime
/// UI provider/application layer interface.
class UiLayer {
 public:
  virtual ~UiLayer() = default;
  /// Called once per frame to populate `UIContext::drawData()`.
  virtual void onFrame(UIContext& ctx) = 0;
  /// Receives platform events before the app clears them.
  virtual void onEvent(const platform::Event& event) { (void)event; }
  /// Called during engine shutdown.
  virtual void onShutdown() {}
};

}  // namespace karma::app
