#include "karma/content/image/image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb_image.h"

#include <spdlog/spdlog.h>

namespace karma::content {

std::optional<Rgba8Image> loadRgba8Image(const std::filesystem::path& path) {
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* decoded = stbi_load(path.string().c_str(), &width, &height, &components, 4);
  if (decoded == nullptr || width <= 0 || height <= 0) {
    spdlog::error("Image load failed: {}", path.string());
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
