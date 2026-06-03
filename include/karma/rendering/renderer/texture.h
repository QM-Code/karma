#pragma once

namespace karma::renderer {

/// \ingroup karma_rendering
/// CPU texture upload format.
enum class TextureFormat {
  RGBA8,
  RGB8,
  R8
};

/// \ingroup karma_rendering
/// Texture creation descriptor.
struct TextureDesc {
  int width = 0;
  int height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  bool srgb = false;
  bool generate_mips = false;
};

}  // namespace karma::renderer
