#pragma once

namespace karma::renderer {

enum class TextureFormat {
  RGBA8,
  RGB8,
  R8
};

struct TextureDesc {
  int width = 0;
  int height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  bool srgb = false;
  bool generate_mips = false;
};

}  // namespace karma::renderer
