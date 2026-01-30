#pragma once

#include <cstdint>
#include <vector>

namespace karma::app {

using UITextureHandle = uint32_t;

struct UIVertex {
  float x = 0.0f;
  float y = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  uint32_t rgba = 0;
};

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

struct UIDrawData {
  std::vector<UIVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<UIDrawCmd> commands;

  void clear() {
    vertices.clear();
    indices.clear();
    commands.clear();
  }
};

struct UIFrameInfo {
  float dt = 0.0f;
  int viewport_w = 0;
  int viewport_h = 0;
  float dpi_scale = 1.0f;
};

}  // namespace karma::app
