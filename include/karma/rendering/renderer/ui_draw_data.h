#pragma once

#include <cstdint>
#include <vector>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Renderer texture handle used by UI draw commands.
using UITextureHandle = uint32_t;

/// \ingroup karma_rendering
/// One UI vertex in screen-space pixels.
struct UIVertex {
  float x = 0.0f;
  float y = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  uint32_t rgba = 0;
};

/// \ingroup karma_rendering
/// Draw command referencing a span of `UIDrawData::indices`.
struct UIDrawCmd {
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
  bool scissor_enabled = false;
  int scissor_x = 0;
  int scissor_y = 0;
  int scissor_w = 0;
  int scissor_h = 0;
  UITextureHandle texture = 0;
};

/// \ingroup karma_rendering
/// Provider-neutral UI draw list consumed by the renderer.
struct UIDrawData {
  std::vector<UIVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<UIDrawCmd> commands;
  bool premultiplied_alpha = false;

  /// Clears vertices, indices, commands, and alpha mode for a new frame.
  void clear() {
    vertices.clear();
    indices.clear();
    commands.clear();
    premultiplied_alpha = false;
  }
};

}  // namespace karma::renderer
