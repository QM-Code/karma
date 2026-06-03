#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace karma::content {

/// \ingroup karma_content
/// Loaded RGBA8 image payload.
struct Rgba8Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;

  /// Returns true when dimensions and pixel count are consistent.
  bool valid() const {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 4u;
  }
};

/// Loads an image file into RGBA8 pixels when supported.
std::optional<Rgba8Image> loadRgba8Image(const std::filesystem::path& path);

}  // namespace karma::content
