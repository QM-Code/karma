#pragma once

#include <cstdint>
#include <string>

namespace karma::renderer {

struct TextureDesc {
  enum class Usage {
    Diffuse,
    Normal,
    Specular,
    Emissive,
    RenderTarget,
    Depth
  };

  std::string asset_key;
  uint32_t width = 0;
  uint32_t height = 0;
  Usage usage = Usage::Diffuse;
  bool is_dynamic = false;
};

}  // namespace karma::renderer
