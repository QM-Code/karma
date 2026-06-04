#include "karma/content/image/image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb_image.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <spdlog/spdlog.h>

namespace karma::content {

namespace {

std::string lowercaseExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return extension;
}

}  // namespace

std::optional<Rgba8Image> loadRgba8Image(const std::filesystem::path& path) {
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* decoded = stbi_load(path.string().c_str(), &width, &height, &components, 4);
  if (decoded == nullptr || width <= 0 || height <= 0) {
    if (lowercaseExtension(path) == ".exr") {
      spdlog::error(
          "Image load failed: {} (OpenEXR is not supported by the RGBA8 image loader)",
          path.string());
    } else if (const char* reason = stbi_failure_reason()) {
      spdlog::error("Image load failed: {} ({})", path.string(), reason);
    } else {
      spdlog::error("Image load failed: {}", path.string());
    }
    if (decoded != nullptr) {
      stbi_image_free(decoded);
    }
    return std::nullopt;
  }

  Rgba8Image image{};
  image.width = width;
  image.height = height;
  const std::size_t byte_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
  image.pixels.assign(decoded, decoded + byte_count);
  stbi_image_free(decoded);
  return image;
}

}  // namespace karma::content
