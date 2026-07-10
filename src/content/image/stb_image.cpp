#include "karma/assets.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb_image.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::assets {

namespace {

std::string lowercaseExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return extension;
}

ScalarImageFormat inferScalarFormat(const std::filesystem::path& path,
                                    ScalarImageFormat requested) {
  if (requested != ScalarImageFormat::Auto) {
    return requested;
  }
  const std::string extension = lowercaseExtension(path);
  if (extension == ".raw" || extension == ".r16" || extension == ".r16u") {
    return ScalarImageFormat::Raw16Unsigned;
  }
  if (extension == ".r32") {
    return ScalarImageFormat::R32Float;
  }
  return ScalarImageFormat::ImageFile;
}

std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    spdlog::error("Image load failed: {} (could not open file)", path.string());
    return std::nullopt;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) {
    spdlog::error("Image load failed: {} (could not determine file size)", path.string());
    return std::nullopt;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!stream && size > 0) {
    spdlog::error("Image load failed: {} (could not read file)", path.string());
    return std::nullopt;
  }
  return bytes;
}

float normalizeScalar(float value, const ScalarImageLoadOptions& options) {
  if (options.value_max <= options.value_min) {
    return value;
  }
  return std::clamp((value - options.value_min) /
                        (options.value_max - options.value_min),
                    0.0f,
                    1.0f);
}

uint16_t readU16(const uint8_t* bytes, bool little_endian) {
  if (little_endian) {
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8u);
  }
  return static_cast<uint16_t>(bytes[1]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) << 8u);
}

uint32_t readU32(const uint8_t* bytes, bool little_endian) {
  if (little_endian) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
  }
  return static_cast<uint32_t>(bytes[3]) |
         (static_cast<uint32_t>(bytes[2]) << 8u) |
         (static_cast<uint32_t>(bytes[1]) << 16u) |
         (static_cast<uint32_t>(bytes[0]) << 24u);
}

std::optional<ScalarImage> loadRaw16ScalarImage(
    const std::filesystem::path& path,
    const ScalarImageLoadOptions& options) {
  if (options.raw_width == 0u || options.raw_height == 0u) {
    spdlog::error("Image load failed: {} (RAW16 requires raw_width/raw_height)",
                  path.string());
    return std::nullopt;
  }
  auto bytes = readBinaryFile(path);
  if (!bytes) {
    return std::nullopt;
  }
  const std::size_t sample_count =
      static_cast<std::size_t>(options.raw_width) *
      static_cast<std::size_t>(options.raw_height);
  const std::size_t expected_bytes = sample_count * sizeof(uint16_t);
  if (bytes->size() != expected_bytes) {
    spdlog::error("Image load failed: {} (RAW16 size {} does not match {}x{})",
                  path.string(),
                  bytes->size(),
                  options.raw_width,
                  options.raw_height);
    return std::nullopt;
  }

  ScalarImage image{};
  image.width = static_cast<int>(options.raw_width);
  image.height = static_cast<int>(options.raw_height);
  image.values.resize(sample_count);
  for (uint32_t y = 0u; y < options.raw_height; ++y) {
    const uint32_t dst_y = options.flip_y ? options.raw_height - 1u - y : y;
    for (uint32_t x = 0u; x < options.raw_width; ++x) {
      const std::size_t src_index =
          (static_cast<std::size_t>(y) * options.raw_width + x) * sizeof(uint16_t);
      const uint16_t sample = readU16(bytes->data() + src_index, options.little_endian);
      const float normalized =
          static_cast<float>(sample) /
          static_cast<float>(std::numeric_limits<uint16_t>::max());
      image.values[static_cast<std::size_t>(dst_y) * options.raw_width + x] =
          normalizeScalar(normalized, options);
    }
  }
  return image.valid() ? std::optional<ScalarImage>{std::move(image)} : std::nullopt;
}

std::optional<ScalarImage> loadR32ScalarImage(
    const std::filesystem::path& path,
    const ScalarImageLoadOptions& options) {
  if (options.raw_width == 0u || options.raw_height == 0u) {
    spdlog::error("Image load failed: {} (R32 requires raw_width/raw_height)",
                  path.string());
    return std::nullopt;
  }
  auto bytes = readBinaryFile(path);
  if (!bytes) {
    return std::nullopt;
  }
  const std::size_t sample_count =
      static_cast<std::size_t>(options.raw_width) *
      static_cast<std::size_t>(options.raw_height);
  const std::size_t expected_bytes = sample_count * sizeof(float);
  if (bytes->size() != expected_bytes) {
    spdlog::error("Image load failed: {} (R32 size {} does not match {}x{})",
                  path.string(),
                  bytes->size(),
                  options.raw_width,
                  options.raw_height);
    return std::nullopt;
  }

  ScalarImage image{};
  image.width = static_cast<int>(options.raw_width);
  image.height = static_cast<int>(options.raw_height);
  image.values.resize(sample_count);
  for (uint32_t y = 0u; y < options.raw_height; ++y) {
    const uint32_t dst_y = options.flip_y ? options.raw_height - 1u - y : y;
    for (uint32_t x = 0u; x < options.raw_width; ++x) {
      const std::size_t src_index =
          (static_cast<std::size_t>(y) * options.raw_width + x) * sizeof(float);
      const uint32_t bits = readU32(bytes->data() + src_index, options.little_endian);
      float sample = 0.0f;
      std::memcpy(&sample, &bits, sizeof(sample));
      image.values[static_cast<std::size_t>(dst_y) * options.raw_width + x] =
          normalizeScalar(sample, options);
    }
  }
  return image.valid() ? std::optional<ScalarImage>{std::move(image)} : std::nullopt;
}

std::optional<ScalarImage> loadImageFileScalarImage(
    const std::filesystem::path& path,
    const ScalarImageLoadOptions& options) {
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load_thread(options.flip_y ? 1 : 0);

  const std::string extension = lowercaseExtension(path);
  if (extension == ".exr" || extension == ".tif" || extension == ".tiff") {
    spdlog::error(
        "Image load failed: {} ({} is not supported by the built-in scalar image loader)",
        path.string(),
        extension);
    return std::nullopt;
  }

  if (stbi_is_16_bit(path.string().c_str())) {
    stbi_us* decoded =
        stbi_load_16(path.string().c_str(), &width, &height, &components, 0);
    if (decoded == nullptr || width <= 0 || height <= 0 || components <= 0) {
      if (const char* reason = stbi_failure_reason()) {
        spdlog::error("Image load failed: {} ({})", path.string(), reason);
      } else {
        spdlog::error("Image load failed: {}", path.string());
      }
      if (decoded != nullptr) {
        stbi_image_free(decoded);
      }
      return std::nullopt;
    }
    ScalarImage image{};
    image.width = width;
    image.height = height;
    const std::size_t sample_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.values.resize(sample_count);
    for (std::size_t i = 0u; i < sample_count; ++i) {
      const float normalized =
          static_cast<float>(decoded[i * static_cast<std::size_t>(components)]) /
          static_cast<float>(std::numeric_limits<stbi_us>::max());
      image.values[i] = normalizeScalar(normalized, options);
    }
    stbi_image_free(decoded);
    return image.valid() ? std::optional<ScalarImage>{std::move(image)} : std::nullopt;
  }

  if (extension == ".hdr") {
    float* decoded =
        stbi_loadf(path.string().c_str(), &width, &height, &components, 0);
    if (decoded == nullptr || width <= 0 || height <= 0 || components <= 0) {
      if (const char* reason = stbi_failure_reason()) {
        spdlog::error("Image load failed: {} ({})", path.string(), reason);
      } else {
        spdlog::error("Image load failed: {}", path.string());
      }
      if (decoded != nullptr) {
        stbi_image_free(decoded);
      }
      return std::nullopt;
    }
    ScalarImage image{};
    image.width = width;
    image.height = height;
    const std::size_t sample_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.values.resize(sample_count);
    for (std::size_t i = 0u; i < sample_count; ++i) {
      image.values[i] =
          normalizeScalar(decoded[i * static_cast<std::size_t>(components)], options);
    }
    stbi_image_free(decoded);
    return image.valid() ? std::optional<ScalarImage>{std::move(image)} : std::nullopt;
  }

  stbi_uc* decoded =
      stbi_load(path.string().c_str(), &width, &height, &components, 0);
  if (decoded == nullptr || width <= 0 || height <= 0 || components <= 0) {
    if (const char* reason = stbi_failure_reason()) {
      spdlog::error("Image load failed: {} ({})", path.string(), reason);
    } else {
      spdlog::error("Image load failed: {}", path.string());
    }
    if (decoded != nullptr) {
      stbi_image_free(decoded);
    }
    return std::nullopt;
  }

  ScalarImage image{};
  image.width = width;
  image.height = height;
  const std::size_t sample_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  image.values.resize(sample_count);
  for (std::size_t i = 0u; i < sample_count; ++i) {
    const float normalized =
        static_cast<float>(decoded[i * static_cast<std::size_t>(components)]) / 255.0f;
    image.values[i] = normalizeScalar(normalized, options);
  }
  stbi_image_free(decoded);
  return image.valid() ? std::optional<ScalarImage>{std::move(image)} : std::nullopt;
}

}  // namespace

std::optional<Rgba8Image> loadRgba8Image(
    const std::filesystem::path& path,
    const Rgba8ImageLoadOptions& options) {
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load_thread(options.flip_y ? 1 : 0);
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

std::optional<Rgba8Image> loadRgba8ImageFromMemory(const std::uint8_t* data,
                                                   std::size_t size,
                                                   const Rgba8ImageLoadOptions& options) {
  if (data == nullptr || size == 0u ||
      size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load_thread(options.flip_y ? 1 : 0);
  stbi_uc* decoded = stbi_load_from_memory(data,
                                           static_cast<int>(size),
                                           &width,
                                           &height,
                                           &components,
                                           4);
  if (decoded == nullptr || width <= 0 || height <= 0) {
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

std::optional<ScalarImage> loadScalarImage(const std::filesystem::path& path,
                                           const ScalarImageLoadOptions& options) {
  switch (inferScalarFormat(path, options.format)) {
    case ScalarImageFormat::ImageFile:
      return loadImageFileScalarImage(path, options);
    case ScalarImageFormat::Raw16Unsigned:
      return loadRaw16ScalarImage(path, options);
    case ScalarImageFormat::R32Float:
      return loadR32ScalarImage(path, options);
    case ScalarImageFormat::Auto:
      break;
  }
  return std::nullopt;
}

}  // namespace karma::assets
