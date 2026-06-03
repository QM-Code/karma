#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace karma::content {

struct Rgba8Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;

  bool valid() const {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 4u;
  }
};

std::optional<Rgba8Image> loadRgba8Image(const std::filesystem::path& path);

}  // namespace karma::content
