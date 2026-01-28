#pragma once

#include <cstdint>
#include <string>

namespace karma::renderer {

struct RenderTargetDesc {
  std::string color_texture_key;
  std::string depth_texture_key;
  uint32_t width = 0;
  uint32_t height = 0;
  bool keep_on_resize = false;
};

}  // namespace karma::renderer
