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

/// Options for loading RGBA8 image data.
struct Rgba8ImageLoadOptions {
  bool flip_y = false;
};

/// Scalar image source format for height/data maps.
enum class ScalarImageFormat : uint8_t {
  Auto = 0,
  ImageFile = 1,
  Raw16Unsigned = 2,
  R32Float = 3,
};

/// Options for loading a scalar height/data map.
struct ScalarImageLoadOptions {
  ScalarImageFormat format = ScalarImageFormat::Auto;
  uint32_t raw_width = 0u;
  uint32_t raw_height = 0u;
  bool little_endian = true;
  bool flip_y = false;
  float value_min = 0.0f;
  float value_max = 1.0f;
};

/// Loaded normalized scalar image payload.
struct ScalarImage {
  int width = 0;
  int height = 0;
  std::vector<float> values;

  /// Returns true when dimensions and sample count are consistent.
  bool valid() const {
    return width > 0 && height > 0 &&
           values.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height);
  }
};

/// Loads an image file into RGBA8 pixels when supported.
std::optional<Rgba8Image> loadRgba8Image(
    const std::filesystem::path& path,
    const Rgba8ImageLoadOptions& options = {});

/// Loads encoded image bytes into RGBA8 pixels when supported.
std::optional<Rgba8Image> loadRgba8ImageFromMemory(const std::uint8_t* data,
                                                   std::size_t size,
                                                   const Rgba8ImageLoadOptions& options = {});

/// Loads an image, RAW16, or R32 file into normalized scalar samples.
std::optional<ScalarImage> loadScalarImage(
    const std::filesystem::path& path,
    const ScalarImageLoadOptions& options = {});

}  // namespace karma::content
