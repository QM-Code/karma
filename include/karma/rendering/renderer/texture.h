#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace karma::renderer {

/// \ingroup karma_rendering
/// CPU texture upload format.
enum class TextureFormat {
  RGBA8,
  RGB8,
  R8,
  BC7_RGBA_UNORM,
  BC7_RGBA_UNORM_SRGB,
  KTX2_BASIS_UASTC
};

/// \ingroup karma_rendering
/// Texture creation descriptor.
struct TextureDesc {
  int width = 0;
  int height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  bool srgb = false;
  bool generate_mips = false;
  uint32_t mip_levels = 1u;
};

/// \ingroup karma_rendering
/// One mip/subresource inside a prepared texture upload.
struct TextureUploadSubresource {
  uint32_t mip_level = 0u;
  uint32_t array_layer = 0u;
  int width = 0;
  int height = 0;
  std::size_t offset = 0u;
  std::size_t size = 0u;
  std::size_t row_stride = 0u;
};

/// \ingroup karma_rendering
/// CPU-side texture bytes prepared for backend upload.
struct TextureUploadData {
  TextureFormat format = TextureFormat::RGBA8;
  std::vector<TextureUploadSubresource> subresources;
  std::vector<std::uint8_t> bytes;
};

}  // namespace karma::renderer
