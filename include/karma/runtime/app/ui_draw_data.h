#pragma once

#include "karma/rendering/renderer/ui_draw_data.h"

namespace karma::app {

/// \ingroup karma_runtime
/// Renderer texture handle used by UI draw commands.
using UITextureHandle = renderer::UITextureHandle;

/// \ingroup karma_runtime
/// UI texture descriptor returned by texture creation/loading helpers.
struct UITexture {
  UITextureHandle handle = 0;
  int width = 0;
  int height = 0;

  explicit operator bool() const { return handle != 0 && width > 0 && height > 0; }
};

/// \ingroup karma_runtime
/// Timing and viewport data supplied to UI layers.
struct UIFrameInfo {
  float dt = 0.0f;
  int viewport_w = 0;
  int viewport_h = 0;
  float dpi_scale = 1.0f;
};

}  // namespace karma::app
