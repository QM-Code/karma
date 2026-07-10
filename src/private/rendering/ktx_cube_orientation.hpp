#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace karma::rendering::detail {

// KTX cubemaps and the runtime-rendered cubemap address opposite Y world
// directions. Bake that reflection into the KTX texels so every later sampler
// can use the engine's unmodified world direction.
inline bool normalizeKtxCubemapWorldDirections(
    std::span<unsigned char> bytes,
    std::span<const std::size_t> subresource_offsets,
    std::span<const std::uint32_t> mip_widths,
    std::span<const std::uint32_t> mip_heights,
    std::size_t bytes_per_texel) {
  constexpr std::size_t kFaceCount = 6u;
  constexpr std::size_t kPositiveYFace = 2u;
  constexpr std::size_t kNegativeYFace = 3u;

  const std::size_t mip_count = mip_widths.size();
  if (mip_count == 0u || mip_heights.size() != mip_count ||
      subresource_offsets.size() != mip_count * kFaceCount ||
      bytes_per_texel == 0u) {
    return false;
  }

  struct ByteRange {
    std::size_t begin;
    std::size_t end;
  };
  std::vector<ByteRange> ranges;
  ranges.reserve(subresource_offsets.size());

  for (std::size_t mip = 0; mip < mip_count; ++mip) {
    const std::size_t width = mip_widths[mip];
    const std::size_t height = mip_heights[mip];
    if (width == 0u || height == 0u ||
        width > std::numeric_limits<std::size_t>::max() / bytes_per_texel) {
      return false;
    }
    const std::size_t row_bytes = width * bytes_per_texel;
    if (height > std::numeric_limits<std::size_t>::max() / row_bytes) {
      return false;
    }
    const std::size_t face_bytes = row_bytes * height;

    for (std::size_t face = 0; face < kFaceCount; ++face) {
      const std::size_t offset = subresource_offsets[mip * kFaceCount + face];
      if (offset > bytes.size() || face_bytes > bytes.size() - offset) {
        return false;
      }
      const ByteRange candidate{offset, offset + face_bytes};
      for (const ByteRange range : ranges) {
        if (candidate.begin < range.end && range.begin < candidate.end) {
          return false;
        }
      }
      ranges.push_back(candidate);
    }
  }

  for (std::size_t mip = 0; mip < mip_count; ++mip) {
    const std::size_t row_bytes =
        static_cast<std::size_t>(mip_widths[mip]) * bytes_per_texel;
    const std::size_t height = mip_heights[mip];
    const std::size_t face_bytes = row_bytes * height;

    // Reflecting a cube lookup across Y flips V on every face. The faces
    // perpendicular to Y additionally exchange their positive/negative sides.
    for (std::size_t face = 0; face < kFaceCount; ++face) {
      unsigned char* const face_data =
          bytes.data() + subresource_offsets[mip * kFaceCount + face];
      for (std::size_t row = 0; row < height / 2u; ++row) {
        unsigned char* const top = face_data + row * row_bytes;
        unsigned char* const bottom = face_data + (height - row - 1u) * row_bytes;
        std::swap_ranges(top, top + row_bytes, bottom);
      }
    }

    unsigned char* const positive_y =
        bytes.data() + subresource_offsets[mip * kFaceCount + kPositiveYFace];
    unsigned char* const negative_y =
        bytes.data() + subresource_offsets[mip * kFaceCount + kNegativeYFace];
    std::swap_ranges(positive_y, positive_y + face_bytes, negative_y);
  }

  return true;
}

}  // namespace karma::rendering::detail
