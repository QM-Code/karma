#include "explosion_prefab_package.h"

#include "demo_asset_paths.h"
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/karma.h"
#include "karma/world/components/visibility.h"

namespace karma::demo {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr int kAtlasFrameSize = 64;
constexpr int kAtlasFrameCount = 4;
constexpr int kFlipbookColumns = 5;
constexpr int kFlipbookRows = 5;
constexpr int kFlipbookFrameSize = 400;
constexpr int kFlipbookBorder = 4;
constexpr int kFlipbookSpacing = 4;
constexpr int kFastCoreFlipbookFrameSize = 128;
constexpr int kFastCoreFlipbookBorder = 2;
constexpr int kFastCoreFlipbookSpacing = 2;
constexpr int kFastSmokeFlipbookFrameSize = 96;
constexpr int kFastSmokeFlipbookBorder = 2;
constexpr int kFastSmokeFlipbookSpacing = 2;
constexpr float kLightPulseDuration = 0.64f;
constexpr std::uint32_t kAtlasCacheMagic = 0x5441584bu;  // "KXAT" little-endian.
constexpr std::uint32_t kAtlasCacheVersion = 1u;

struct ExplosionPackageState {
  renderer::TextureId spark_texture = renderer::kInvalidTexture;
  renderer::TextureId glow_texture = renderer::kInvalidTexture;
  renderer::TextureId smoke_texture = renderer::kInvalidTexture;
  renderer::TextureId heat_texture = renderer::kInvalidTexture;
  renderer::TextureId dust_ring_texture = renderer::kInvalidTexture;
  renderer::TextureId shock_ring_texture = renderer::kInvalidTexture;
  renderer::TextureId scorch_texture = renderer::kInvalidTexture;
  renderer::TextureId debris_texture = renderer::kInvalidTexture;
  renderer::TextureId explosion_flipbook_texture = renderer::kInvalidTexture;
  renderer::TextureId explosion_smoke_flipbook_texture = renderer::kInvalidTexture;
  ExplosionFlipbookTextureSource core_flipbook_source =
      ExplosionFlipbookTextureSource::Unknown;
  ExplosionFlipbookTextureSource smoke_flipbook_source =
      ExplosionFlipbookTextureSource::Unknown;
};

ExplosionPrefabPackageDebugInfo g_explosion_package_debug_info{};

float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float smoothStep01(float value) {
  const float t = saturate(value);
  return t * t * (3.0f - 2.0f * t);
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0f));
}

std::string_view flipbookSourceName(ExplosionFlipbookTextureSource source) {
  switch (source) {
    case ExplosionFlipbookTextureSource::ExrSequence:
      return "exr_sequence";
    case ExplosionFlipbookTextureSource::ProceduralAtlas:
      return "procedural_atlas";
    case ExplosionFlipbookTextureSource::Unknown:
    default:
      return "unknown";
  }
}

std::string trim(std::string_view text) {
  size_t start = 0u;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }
  size_t end = text.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(text[end - 1u])) != 0) {
    --end;
  }
  return std::string(text.substr(start, end - start));
}

std::string lowercase(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

class ScopedStartupTimer {
 public:
  explicit ScopedStartupTimer(std::string label)
      : label_(std::move(label)), start_(core::SteadyClock::now()) {}

  ~ScopedStartupTimer() {
    spdlog::info("{} took {:.2f} ms", label_, core::elapsedMillisecondsSince(start_));
  }

 private:
  std::string label_;
  core::SteadyClock::time_point start_;
};

void destroyTextureIfValid(renderer::GraphicsDevice* graphics, renderer::TextureId& texture) {
  if (graphics == nullptr || texture == renderer::kInvalidTexture) {
    return;
  }
  graphics->destroyTexture(texture);
  texture = renderer::kInvalidTexture;
}

struct FloatImage {
  int width = 0;
  int height = 0;
  bool has_meaningful_alpha = false;
  std::vector<float> pixels;
};

struct SequenceAtlasBuildConfig {
  std::filesystem::path sequence_dir;
  size_t first_frame_index = 0u;
  size_t last_frame_index = 0u;
  int atlas_columns = 1;
  int atlas_rows = 1;
  int frame_width = 0;
  int frame_height = 0;
  int atlas_border = 0;
  int atlas_spacing = 0;
  float alpha_signal_bias = 0.0f;
  float alpha_signal_scale = 2.0f;
  float alpha_signal_power = 1.0f;
  float alpha_output_scale = 1.2f;
  float source_alpha_floor = 0.35f;
  bool use_luminance_for_alpha = false;
  bool multiply_source_alpha = false;
};

struct CpuAtlasRGBA8 {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;

  bool valid() const {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<size_t>(width) *
                                static_cast<size_t>(height) * 4u;
  }
};

int flipbookAtlasWidth(int frame_size = kFlipbookFrameSize,
                       int border = kFlipbookBorder,
                       int spacing = kFlipbookSpacing) {
  return kFlipbookColumns * frame_size +
         (kFlipbookColumns - 1) * spacing + border * 2;
}

int flipbookAtlasHeight(int frame_size = kFlipbookFrameSize,
                        int border = kFlipbookBorder,
                        int spacing = kFlipbookSpacing) {
  return kFlipbookRows * frame_size +
         (kFlipbookRows - 1) * spacing + border * 2;
}

float halfToFloat(std::uint16_t value) {
  const std::uint32_t sign = (static_cast<std::uint32_t>(value & 0x8000u)) << 16u;
  const std::uint32_t exponent_bits = (value >> 10u) & 0x1Fu;
  const std::uint32_t mantissa_bits = value & 0x03FFu;

  std::uint32_t float_bits = 0u;
  if (exponent_bits == 0u) {
    if (mantissa_bits == 0u) {
      float_bits = sign;
    } else {
      int exponent = -14;
      std::uint32_t mantissa = mantissa_bits;
      while ((mantissa & 0x0400u) == 0u) {
        mantissa <<= 1u;
        --exponent;
      }
      mantissa &= 0x03FFu;
      float_bits =
          sign | (static_cast<std::uint32_t>(exponent + 127) << 23u) | (mantissa << 13u);
    }
  } else if (exponent_bits == 0x1Fu) {
    float_bits = sign | 0x7F800000u | (mantissa_bits << 13u);
  } else {
    float_bits = sign | ((exponent_bits + 112u) << 23u) | (mantissa_bits << 13u);
  }

  float out = 0.0f;
  std::memcpy(&out, &float_bits, sizeof(out));
  return out;
}

FloatImage loadUncompressedExrRGBA32F(const std::filesystem::path& path) {
  struct ChannelDef {
    std::string name;
    std::uint32_t pixel_type = 0u;
    std::uint32_t x_sampling = 1u;
    std::uint32_t y_sampling = 1u;
  };

  auto read_u32 = [](const std::vector<std::uint8_t>& bytes, size_t offset) -> std::uint32_t {
    std::uint32_t value = 0u;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
  };

  auto read_u64 = [](const std::vector<std::uint8_t>& bytes, size_t offset) -> std::uint64_t {
    std::uint64_t value = 0u;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
  };

  auto read_i32 = [](const std::vector<std::uint8_t>& bytes, size_t offset) -> std::int32_t {
    std::int32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
  };

  auto read_f32 = [](const std::vector<std::uint8_t>& bytes, size_t offset) -> float {
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
  };

  FloatImage image{};
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec || file_size < 16u) {
    return image;
  }

  std::vector<std::uint8_t> bytes(static_cast<size_t>(file_size));
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return image;
  }
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return image;
  }

  constexpr std::uint32_t kOpenExrMagic = 20000630u;
  if (read_u32(bytes, 0u) != kOpenExrMagic) {
    return image;
  }

  const std::uint32_t version = read_u32(bytes, 4u);
  constexpr std::uint32_t kTiledBit = 1u << 9u;
  constexpr std::uint32_t kDeepDataBit = 1u << 11u;
  constexpr std::uint32_t kMultiPartBit = 1u << 12u;
  if ((version & (kTiledBit | kDeepDataBit | kMultiPartBit)) != 0u) {
    return image;
  }

  std::vector<ChannelDef> channels;
  std::int32_t min_x = 0;
  std::int32_t min_y = 0;
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  int compression = -1;
  int line_order = 0;

  size_t cursor = 8u;
  auto read_c_string = [&](std::string& out) -> bool {
    size_t end = cursor;
    while (end < bytes.size() && bytes[end] != 0u) {
      ++end;
    }
    if (end >= bytes.size()) {
      return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data() + cursor), end - cursor);
    cursor = end + 1u;
    return true;
  };

  while (cursor < bytes.size()) {
    std::string name;
    if (!read_c_string(name)) {
      return FloatImage{};
    }
    if (name.empty()) {
      break;
    }

    std::string type;
    if (!read_c_string(type) || cursor + 4u > bytes.size()) {
      return FloatImage{};
    }
    const std::uint32_t value_size = read_u32(bytes, cursor);
    cursor += 4u;
    if (cursor + static_cast<size_t>(value_size) > bytes.size()) {
      return FloatImage{};
    }

    const size_t value_offset = cursor;
    if (name == "compression" && type == "compression" && value_size == 1u) {
      compression = static_cast<int>(bytes[value_offset]);
    } else if (name == "lineOrder" && type == "lineOrder" && value_size == 1u) {
      line_order = static_cast<int>(bytes[value_offset]);
    } else if ((name == "dataWindow" || name == "displayWindow") && type == "box2i" &&
               value_size == 16u && max_x < min_x) {
      min_x = read_i32(bytes, value_offset + 0u);
      min_y = read_i32(bytes, value_offset + 4u);
      max_x = read_i32(bytes, value_offset + 8u);
      max_y = read_i32(bytes, value_offset + 12u);
    } else if (name == "channels" && type == "chlist") {
      size_t channel_cursor = value_offset;
      const size_t channel_end = value_offset + static_cast<size_t>(value_size);
      while (channel_cursor < channel_end) {
        size_t end = channel_cursor;
        while (end < channel_end && bytes[end] != 0u) {
          ++end;
        }
        if (end >= channel_end) {
          return FloatImage{};
        }
        if (end == channel_cursor) {
          break;
        }

        ChannelDef channel{};
        channel.name.assign(reinterpret_cast<const char*>(bytes.data() + channel_cursor),
                            end - channel_cursor);
        channel_cursor = end + 1u;
        if (channel_cursor + 16u > channel_end) {
          return FloatImage{};
        }
        channel.pixel_type = read_u32(bytes, channel_cursor + 0u);
        channel.x_sampling = read_u32(bytes, channel_cursor + 8u);
        channel.y_sampling = read_u32(bytes, channel_cursor + 12u);
        channel_cursor += 16u;
        channels.push_back(std::move(channel));
      }
    }

    cursor += static_cast<size_t>(value_size);
  }

  if (compression != 0 || line_order != 0 || channels.empty() || max_x < min_x || max_y < min_y) {
    return image;
  }

  const int width = max_x - min_x + 1;
  const int height = max_y - min_y + 1;
  if (width <= 0 || height <= 0) {
    return image;
  }

  const size_t offset_table_offset = cursor;
  const size_t scanline_blocks = static_cast<size_t>(height);
  const size_t offset_table_size = scanline_blocks * sizeof(std::uint64_t);
  if (offset_table_offset + offset_table_size > bytes.size()) {
    return image;
  }

  std::sort(channels.begin(), channels.end(), [](const ChannelDef& lhs, const ChannelDef& rhs) {
    return lhs.name < rhs.name;
  });

  size_t expected_scanline_bytes = 0u;
  for (const ChannelDef& channel : channels) {
    if (channel.x_sampling != 1u || channel.y_sampling != 1u) {
      return image;
    }

    size_t bytes_per_sample = 0u;
    switch (channel.pixel_type) {
      case 0u:
      case 2u:
        bytes_per_sample = 4u;
        break;
      case 1u:
        bytes_per_sample = 2u;
        break;
      default:
        return image;
    }
    expected_scanline_bytes += static_cast<size_t>(width) * bytes_per_sample;
  }

  image.width = width;
  image.height = height;
  image.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0.0f);
  for (size_t pixel = 0u; pixel < static_cast<size_t>(width) * static_cast<size_t>(height); ++pixel) {
    image.pixels[pixel * 4u + 3u] = 1.0f;
  }

  auto channel_index_for_name = [](const std::string& name) -> int {
    if (name == "R") {
      return 0;
    }
    if (name == "G") {
      return 1;
    }
    if (name == "B") {
      return 2;
    }
    if (name == "A") {
      return 3;
    }
    return -1;
  };

  for (size_t block = 0u; block < scanline_blocks; ++block) {
    const std::uint64_t chunk_offset = read_u64(bytes, offset_table_offset + block * 8u);
    if (chunk_offset + 8u > bytes.size()) {
      return FloatImage{};
    }

    const std::int32_t scanline_y = read_i32(bytes, static_cast<size_t>(chunk_offset) + 0u);
    const std::uint32_t data_size = read_u32(bytes, static_cast<size_t>(chunk_offset) + 4u);
    if (data_size != expected_scanline_bytes ||
        chunk_offset + 8u + static_cast<std::uint64_t>(data_size) > bytes.size()) {
      return FloatImage{};
    }

    const int row = scanline_y - min_y;
    if (row < 0 || row >= height) {
      return FloatImage{};
    }

    size_t data_cursor = static_cast<size_t>(chunk_offset) + 8u;
    for (const ChannelDef& channel : channels) {
      const int rgba_channel = channel_index_for_name(channel.name);
      for (int x = 0; x < width; ++x) {
        float sample = 0.0f;
        switch (channel.pixel_type) {
          case 0u:
            sample = static_cast<float>(read_u32(bytes, data_cursor));
            data_cursor += 4u;
            break;
          case 1u: {
            std::uint16_t half = 0u;
            std::memcpy(&half, bytes.data() + data_cursor, sizeof(half));
            sample = halfToFloat(half);
            data_cursor += 2u;
            break;
          }
          case 2u:
            sample = read_f32(bytes, data_cursor);
            data_cursor += 4u;
            break;
          default:
            return FloatImage{};
        }

        if (rgba_channel >= 0) {
          const size_t pixel_index =
              (static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
          image.pixels[pixel_index + static_cast<size_t>(rgba_channel)] = sample;
        }
      }
    }
  }

  float alpha_min = std::numeric_limits<float>::max();
  float alpha_max = std::numeric_limits<float>::lowest();
  for (size_t index = 3u; index < image.pixels.size(); index += 4u) {
    alpha_min = std::min(alpha_min, image.pixels[index]);
    alpha_max = std::max(alpha_max, image.pixels[index]);
  }
  image.has_meaningful_alpha = (alpha_max - alpha_min) > 1.0e-4f && alpha_max > 1.0e-3f;
  return image;
}

float toneMapHdr(float value) {
  const float clamped = std::max(value, 0.0f);
  const float mapped = 1.0f - std::exp(-clamped * 1.45f);
  return std::pow(std::clamp(mapped, 0.0f, 1.0f), 1.0f / 2.2f);
}

FloatImage loadImageRGBA32F(const std::filesystem::path& path) {
  if (path.extension() == ".exr") {
    FloatImage image = loadUncompressedExrRGBA32F(path);
    if (image.width > 0 && image.height > 0) {
      return image;
    }
  }

  FloatImage image{};
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load(0);
  float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &components, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    if (pixels != nullptr) {
      stbi_image_free(pixels);
    }
    return image;
  }

  image.width = width;
  image.height = height;
  image.pixels.assign(
      pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
  stbi_image_free(pixels);

  float alpha_min = std::numeric_limits<float>::max();
  float alpha_max = std::numeric_limits<float>::lowest();
  for (size_t index = 3u; index < image.pixels.size(); index += 4u) {
    alpha_min = std::min(alpha_min, image.pixels[index]);
    alpha_max = std::max(alpha_max, image.pixels[index]);
  }
  image.has_meaningful_alpha = (alpha_max - alpha_min) > 1.0e-4f && alpha_max > 1.0e-3f;
  return image;
}

std::array<float, 4> sampleImageBilinear(const FloatImage& image, float u, float v) {
  if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }

  const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
  const float y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, image.width - 1);
  const int y1 = std::min(y0 + 1, image.height - 1);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);

  auto fetch = [&](int px, int py) {
    const size_t index =
        (static_cast<size_t>(py) * static_cast<size_t>(image.width) + static_cast<size_t>(px)) * 4u;
    return std::array<float, 4>{
        image.pixels[index + 0u],
        image.pixels[index + 1u],
        image.pixels[index + 2u],
        image.pixels[index + 3u],
    };
  };

  const auto c00 = fetch(x0, y0);
  const auto c10 = fetch(x1, y0);
  const auto c01 = fetch(x0, y1);
  const auto c11 = fetch(x1, y1);

  std::array<float, 4> out{};
  for (int channel = 0; channel < 4; ++channel) {
    const float top = c00[channel] + (c10[channel] - c00[channel]) * tx;
    const float bottom = c01[channel] + (c11[channel] - c01[channel]) * tx;
    out[static_cast<size_t>(channel)] = top + (bottom - top) * ty;
  }
  return out;
}

void writePackedExplosionFrame(std::vector<std::uint8_t>& atlas_pixels,
                               int atlas_width,
                               int atlas_height,
                               int dst_x,
                               int dst_y,
                               int dst_width,
                               int dst_height,
                               const FloatImage& source,
                               const SequenceAtlasBuildConfig& config) {
  auto write_pixel = [&](int x, int y, const std::array<std::uint8_t, 4>& rgba) {
    if (x < 0 || y < 0 || x >= atlas_width || y >= atlas_height) {
      return;
    }
    const size_t index =
        (static_cast<size_t>(y) * static_cast<size_t>(atlas_width) + static_cast<size_t>(x)) * 4u;
    atlas_pixels[index + 0u] = rgba[0];
    atlas_pixels[index + 1u] = rgba[1];
    atlas_pixels[index + 2u] = rgba[2];
    atlas_pixels[index + 3u] = rgba[3];
  };

  std::vector<std::array<std::uint8_t, 4>> frame_pixels(
      static_cast<size_t>(dst_width) * static_cast<size_t>(dst_height));
  for (int y = 0; y < dst_height; ++y) {
    for (int x = 0; x < dst_width; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dst_width);
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dst_height);
      const auto sample = sampleImageBilinear(source, u, v);
      const float mapped_r = toneMapHdr(sample[0]);
      const float mapped_g = toneMapHdr(sample[1]);
      const float mapped_b = toneMapHdr(sample[2]);
      const float hdr_peak = std::max(sample[0], std::max(sample[1], sample[2]));
      const float hdr_luminance =
          std::max(0.0f, sample[0] * 0.2126f + sample[1] * 0.7152f + sample[2] * 0.0722f);
      const float alpha_signal =
          std::max((config.use_luminance_for_alpha ? hdr_luminance : hdr_peak) -
                       config.alpha_signal_bias,
                   0.0f);
      float derived_alpha =
          1.0f - std::exp(-alpha_signal * std::max(config.alpha_signal_scale, 0.0f));
      derived_alpha = std::pow(
          saturate(derived_alpha * std::max(config.alpha_output_scale, 0.0f)),
          std::max(config.alpha_signal_power, 0.001f));
      float alpha = derived_alpha;
      if (source.has_meaningful_alpha) {
        const float source_alpha = saturate(sample[3]);
        if (config.multiply_source_alpha) {
          alpha = source_alpha * derived_alpha;
        } else {
          alpha = std::max(derived_alpha * std::max(config.source_alpha_floor, 0.0f), source_alpha);
        }
      }

      const auto rgba = std::array<std::uint8_t, 4>{
          toByte(mapped_r),
          toByte(mapped_g),
          toByte(mapped_b),
          toByte(alpha),
      };
      frame_pixels[static_cast<size_t>(y) * static_cast<size_t>(dst_width) +
                   static_cast<size_t>(x)] = rgba;
      write_pixel(dst_x + x, dst_y + y, rgba);
    }
  }

  const auto sample_frame_pixel = [&](int x, int y) -> const std::array<std::uint8_t, 4>& {
    const int clamped_x = std::clamp(x, 0, dst_width - 1);
    const int clamped_y = std::clamp(y, 0, dst_height - 1);
    return frame_pixels[static_cast<size_t>(clamped_y) * static_cast<size_t>(dst_width) +
                        static_cast<size_t>(clamped_x)];
  };

  for (int y = 0; y < dst_height; ++y) {
    const auto& left = sample_frame_pixel(0, y);
    const auto& right = sample_frame_pixel(dst_width - 1, y);
    for (int bleed = 1; bleed <= 2; ++bleed) {
      write_pixel(dst_x - bleed, dst_y + y, left);
      write_pixel(dst_x + dst_width - 1 + bleed, dst_y + y, right);
    }
  }
  for (int x = 0; x < dst_width; ++x) {
    const auto& top = sample_frame_pixel(x, 0);
    const auto& bottom = sample_frame_pixel(x, dst_height - 1);
    for (int bleed = 1; bleed <= 2; ++bleed) {
      write_pixel(dst_x + x, dst_y - bleed, top);
      write_pixel(dst_x + x, dst_y + dst_height - 1 + bleed, bottom);
    }
  }
}

int sequenceAtlasWidth(const SequenceAtlasBuildConfig& config) {
  return config.atlas_columns * config.frame_width +
         (config.atlas_columns - 1) * config.atlas_spacing + config.atlas_border * 2;
}

int sequenceAtlasHeight(const SequenceAtlasBuildConfig& config) {
  return config.atlas_rows * config.frame_height +
         (config.atlas_rows - 1) * config.atlas_spacing + config.atlas_border * 2;
}

std::string normalizedPathString(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec) {
    return canonical.string();
  }
  const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
  if (!ec) {
    return absolute.lexically_normal().string();
  }
  return path.lexically_normal().string();
}

std::vector<std::filesystem::path> listExrFrames(const std::filesystem::path& sequence_dir,
                                                 std::int64_t* newest_write_tick = nullptr) {
  if (newest_write_tick != nullptr) {
    *newest_write_tick = 0;
  }

  std::vector<std::filesystem::path> source_frames;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(sequence_dir, ec)) {
    if (ec) {
      break;
    }
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || entry_ec) {
      continue;
    }
    if (lowercase(entry.path().extension().string()) != ".exr") {
      continue;
    }
    source_frames.push_back(entry.path());
    if (newest_write_tick != nullptr) {
      const auto write_time = std::filesystem::last_write_time(entry.path(), entry_ec);
      if (!entry_ec) {
        const auto tick =
            static_cast<std::int64_t>(write_time.time_since_epoch().count());
        *newest_write_tick = std::max(*newest_write_tick, tick);
      }
    }
  }
  std::sort(source_frames.begin(), source_frames.end());
  return source_frames;
}

std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char c : text) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string buildAtlasCacheKey(const SequenceAtlasBuildConfig& config,
                               size_t source_frame_count,
                               std::int64_t newest_write_tick) {
  std::ostringstream key;
  key << "sequence_dir=" << normalizedPathString(config.sequence_dir)
      << "\nsource_frame_count=" << source_frame_count
      << "\nnewest_write_tick=" << newest_write_tick
      << "\nfirst_frame_index=" << config.first_frame_index
      << "\nlast_frame_index=" << config.last_frame_index
      << "\natlas_columns=" << config.atlas_columns
      << "\natlas_rows=" << config.atlas_rows
      << "\nframe_width=" << config.frame_width
      << "\nframe_height=" << config.frame_height
      << "\natlas_border=" << config.atlas_border
      << "\natlas_spacing=" << config.atlas_spacing
      << "\nalpha_signal_bias=" << config.alpha_signal_bias
      << "\nalpha_signal_scale=" << config.alpha_signal_scale
      << "\nalpha_signal_power=" << config.alpha_signal_power
      << "\nalpha_output_scale=" << config.alpha_output_scale
      << "\nsource_alpha_floor=" << config.source_alpha_floor
      << "\nuse_luminance_for_alpha=" << config.use_luminance_for_alpha
      << "\nmultiply_source_alpha=" << config.multiply_source_alpha;
  return key.str();
}

std::filesystem::path generatedCacheDir() {
  return resolveExamplePath("cache") / "generated";
}

std::filesystem::path atlasCachePath(std::string_view label, std::uint64_t key_hash) {
  return generatedCacheDir() /
         ("explosion_flipbook_" + std::string(label) + "_" +
          std::to_string(key_hash) + ".rgba8");
}

bool readExact(std::istream& input, void* data, size_t bytes) {
  input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(bytes));
  return input.good() || input.gcount() == static_cast<std::streamsize>(bytes);
}

template <typename T>
bool readValue(std::istream& input, T& value) {
  return readExact(input, &value, sizeof(value));
}

template <typename T>
bool writeValue(std::ostream& output, const T& value) {
  output.write(reinterpret_cast<const char*>(&value), sizeof(value));
  return output.good();
}

bool readCachedAtlas(std::string_view label,
                     const SequenceAtlasBuildConfig& config,
                     CpuAtlasRGBA8& out_atlas) {
  std::int64_t newest_write_tick = 0;
  const auto source_frames = listExrFrames(config.sequence_dir, &newest_write_tick);
  if (source_frames.empty()) {
    return false;
  }

  const std::string key =
      buildAtlasCacheKey(config, source_frames.size(), newest_write_tick);
  const std::uint64_t key_hash = fnv1a64(key);
  const std::filesystem::path path = atlasCachePath(label, key_hash);

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }

  std::uint32_t magic = 0u;
  std::uint32_t version = 0u;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::uint64_t cached_key_hash = 0u;
  std::uint64_t pixel_bytes = 0u;
  std::uint32_t key_size = 0u;
  if (!readValue(input, magic) || !readValue(input, version) ||
      !readValue(input, width) || !readValue(input, height) ||
      !readValue(input, cached_key_hash) || !readValue(input, pixel_bytes) ||
      !readValue(input, key_size)) {
    return false;
  }

  const int expected_width = sequenceAtlasWidth(config);
  const int expected_height = sequenceAtlasHeight(config);
  const std::uint64_t expected_bytes =
      static_cast<std::uint64_t>(expected_width) *
      static_cast<std::uint64_t>(expected_height) * 4ull;
  if (magic != kAtlasCacheMagic || version != kAtlasCacheVersion ||
      width != static_cast<std::uint32_t>(expected_width) ||
      height != static_cast<std::uint32_t>(expected_height) ||
      cached_key_hash != key_hash || pixel_bytes != expected_bytes ||
      key_size != key.size() || key_size > 64u * 1024u) {
    return false;
  }

  std::string cached_key(key_size, '\0');
  if (!cached_key.empty() && !readExact(input, cached_key.data(), cached_key.size())) {
    return false;
  }
  if (cached_key != key) {
    return false;
  }

  CpuAtlasRGBA8 atlas{};
  atlas.width = static_cast<int>(width);
  atlas.height = static_cast<int>(height);
  atlas.pixels.resize(static_cast<size_t>(pixel_bytes));
  if (!atlas.pixels.empty() && !readExact(input, atlas.pixels.data(), atlas.pixels.size())) {
    return false;
  }
  if (!atlas.valid()) {
    return false;
  }

  out_atlas = std::move(atlas);
  spdlog::info("Explosion prefab package loaded cached {} EXR atlas: {}",
               label,
               path.string());
  return true;
}

bool writeCachedAtlas(std::string_view label,
                      const SequenceAtlasBuildConfig& config,
                      const CpuAtlasRGBA8& atlas) {
  if (!atlas.valid()) {
    return false;
  }

  std::int64_t newest_write_tick = 0;
  const auto source_frames = listExrFrames(config.sequence_dir, &newest_write_tick);
  if (source_frames.empty()) {
    return false;
  }

  const std::string key =
      buildAtlasCacheKey(config, source_frames.size(), newest_write_tick);
  const std::uint64_t key_hash = fnv1a64(key);
  const std::filesystem::path path = atlasCachePath(label, key_hash);

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    spdlog::warn("Failed to create explosion atlas cache directory '{}': {}",
                 path.parent_path().string(),
                 ec.message());
    return false;
  }

  const std::filesystem::path tmp_path = path.string() + ".tmp";
  std::ofstream output(tmp_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  const std::uint32_t width = static_cast<std::uint32_t>(atlas.width);
  const std::uint32_t height = static_cast<std::uint32_t>(atlas.height);
  const std::uint64_t pixel_bytes = static_cast<std::uint64_t>(atlas.pixels.size());
  const std::uint32_t key_size = static_cast<std::uint32_t>(key.size());
  if (!writeValue(output, kAtlasCacheMagic) ||
      !writeValue(output, kAtlasCacheVersion) ||
      !writeValue(output, width) || !writeValue(output, height) ||
      !writeValue(output, key_hash) || !writeValue(output, pixel_bytes) ||
      !writeValue(output, key_size)) {
    return false;
  }
  output.write(key.data(), static_cast<std::streamsize>(key.size()));
  output.write(reinterpret_cast<const char*>(atlas.pixels.data()),
               static_cast<std::streamsize>(atlas.pixels.size()));
  output.close();
  if (!output.good()) {
    std::filesystem::remove(tmp_path, ec);
    return false;
  }

  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp_path, path, ec);
  }
  if (ec) {
    spdlog::warn("Failed to write explosion atlas cache '{}': {}",
                 path.string(),
                 ec.message());
    std::filesystem::remove(tmp_path, ec);
    return false;
  }

  spdlog::info("Explosion prefab package wrote cached {} EXR atlas: {}",
               label,
               path.string());
  return true;
}

CpuAtlasRGBA8 buildSequenceAtlasPixels(const SequenceAtlasBuildConfig& config) {
  if (config.atlas_columns <= 0 || config.atlas_rows <= 0 ||
      config.frame_width <= 0 || config.frame_height <= 0) {
    return {};
  }
  const int atlas_frames = config.atlas_columns * config.atlas_rows;
  const int atlas_width = sequenceAtlasWidth(config);
  const int atlas_height = sequenceAtlasHeight(config);
  const std::vector<std::filesystem::path> source_frames = listExrFrames(config.sequence_dir);
  if (source_frames.empty()) {
    return {};
  }

  const size_t first_index = std::min(config.first_frame_index, source_frames.size() - 1u);
  const size_t last_index =
      std::max(first_index, std::min(config.last_frame_index, source_frames.size() - 1u));
  CpuAtlasRGBA8 atlas{};
  atlas.width = atlas_width;
  atlas.height = atlas_height;
  atlas.pixels.assign(
      static_cast<size_t>(atlas_width) * static_cast<size_t>(atlas_height) * 4u, 0u);

  for (int atlas_frame = 0; atlas_frame < atlas_frames; ++atlas_frame) {
    const float t = atlas_frames > 1
                        ? static_cast<float>(atlas_frame) / static_cast<float>(atlas_frames - 1)
                        : 0.0f;
    const size_t source_index =
        first_index + static_cast<size_t>(std::lround(
                          static_cast<float>(last_index - first_index) * t));
    const FloatImage source = loadImageRGBA32F(source_frames[source_index]);
    if (source.width <= 0 || source.height <= 0) {
      return {};
    }
    const int column = atlas_frame % config.atlas_columns;
    const int row = atlas_frame / config.atlas_columns;
    const int dst_x = config.atlas_border + column * (config.frame_width + config.atlas_spacing);
    const int dst_y = config.atlas_border + row * (config.frame_height + config.atlas_spacing);
    writePackedExplosionFrame(atlas.pixels,
                              atlas_width,
                              atlas_height,
                              dst_x,
                              dst_y,
                              config.frame_width,
                              config.frame_height,
                              source,
                              config);
  }

  return atlas;
}

renderer::TextureId uploadAtlasRGBA8(renderer::GraphicsDevice& graphics,
                                     const CpuAtlasRGBA8& atlas) {
  if (!atlas.valid()) {
    return renderer::kInvalidTexture;
  }
  return graphics.createTextureRGBA8(atlas.width, atlas.height, atlas.pixels.data());
}

SequenceAtlasBuildConfig explosionFireSequenceConfig(
    const std::filesystem::path& sequence_dir) {
  return SequenceAtlasBuildConfig{
      .sequence_dir = sequence_dir,
      .first_frame_index = 4u,
      .last_frame_index = 54u,
      .atlas_columns = kFlipbookColumns,
      .atlas_rows = kFlipbookRows,
      .frame_width = kFlipbookFrameSize,
      .frame_height = kFlipbookFrameSize,
      .atlas_border = kFlipbookBorder,
      .atlas_spacing = kFlipbookSpacing,
      .alpha_signal_bias = 0.032f,
      .alpha_signal_scale = 4.0f,
      .alpha_signal_power = 0.90f,
      .alpha_output_scale = 1.35f,
      .source_alpha_floor = 0.0f,
      .use_luminance_for_alpha = false,
      .multiply_source_alpha = false,
  };
}

SequenceAtlasBuildConfig explosionSmokeSequenceConfig(
    const std::filesystem::path& sequence_dir) {
  return SequenceAtlasBuildConfig{
      .sequence_dir = sequence_dir,
      .first_frame_index = 10u,
      .last_frame_index = 92u,
      .atlas_columns = kFlipbookColumns,
      .atlas_rows = kFlipbookRows,
      .frame_width = kFlipbookFrameSize,
      .frame_height = kFlipbookFrameSize,
      .atlas_border = kFlipbookBorder,
      .atlas_spacing = kFlipbookSpacing,
      .alpha_signal_bias = 0.185f,
      .alpha_signal_scale = 8.0f,
      .alpha_signal_power = 1.10f,
      .alpha_output_scale = 1.15f,
      .source_alpha_floor = 0.0f,
      .use_luminance_for_alpha = true,
      .multiply_source_alpha = false,
  };
}

std::vector<std::uint8_t> buildExplosionCoreFlipbookAtlas(int frame_size = kFlipbookFrameSize,
                                                          int border = kFlipbookBorder,
                                                          int spacing = kFlipbookSpacing);
std::vector<std::uint8_t> buildExplosionSmokeFlipbookAtlas(int frame_size = kFlipbookFrameSize,
                                                           int border = kFlipbookBorder,
                                                           int spacing = kFlipbookSpacing);

renderer::TextureId buildCachedExrSequenceAtlas(renderer::GraphicsDevice& graphics,
                                                std::string_view label,
                                                const SequenceAtlasBuildConfig& config,
                                                bool rebuild_cache) {
  if (!rebuild_cache) {
    CpuAtlasRGBA8 cached_atlas{};
    {
      ScopedStartupTimer timer("Explosion prefab package " + std::string(label) +
                               " EXR cache lookup");
      if (readCachedAtlas(label, config, cached_atlas)) {
        return uploadAtlasRGBA8(graphics, cached_atlas);
      }
    }
  } else {
    spdlog::info("Explosion prefab package {} EXR cache rebuild requested", label);
  }

  CpuAtlasRGBA8 atlas{};
  {
    ScopedStartupTimer timer("Explosion prefab package " + std::string(label) +
                             " EXR atlas build");
    atlas = buildSequenceAtlasPixels(config);
  }
  if (!atlas.valid()) {
    return renderer::kInvalidTexture;
  }

  writeCachedAtlas(label, config, atlas);
  return uploadAtlasRGBA8(graphics, atlas);
}

renderer::TextureId buildProceduralExplosionCoreFlipbook(renderer::GraphicsDevice& graphics,
                                                        int frame_size,
                                                        int border,
                                                        int spacing) {
  ScopedStartupTimer timer("Explosion prefab package procedural fire atlas generation");
  const auto atlas = buildExplosionCoreFlipbookAtlas(frame_size, border, spacing);
  return graphics.createTextureRGBA8(flipbookAtlasWidth(frame_size, border, spacing),
                                     flipbookAtlasHeight(frame_size, border, spacing),
                                     atlas.data());
}

renderer::TextureId buildProceduralExplosionSmokeFlipbook(renderer::GraphicsDevice& graphics,
                                                         int frame_size,
                                                         int border,
                                                         int spacing) {
  ScopedStartupTimer timer("Explosion prefab package procedural smoke atlas generation");
  const auto atlas = buildExplosionSmokeFlipbookAtlas(frame_size, border, spacing);
  return graphics.createTextureRGBA8(flipbookAtlasWidth(frame_size, border, spacing),
                                     flipbookAtlasHeight(frame_size, border, spacing),
                                     atlas.data());
}

std::vector<std::uint8_t> buildSparkAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float plume = saturate(
            1.0f - (std::abs(px) * (2.7f + t * 1.1f) +
                    std::max(0.0f, 0.15f - py) * (1.1f + t * 0.5f) +
                    std::max(0.0f, py) * (0.7f + (1.0f - t) * 0.9f)));
        const float core =
            saturate(1.0f - std::sqrt(px * px * 3.1f + (py - 0.05f) * (py - 0.05f) * (1.2f + t)));
        const float flicker =
            0.85f + 0.15f * std::sin((px + t * 0.6f) * 10.0f) * std::cos((py - t * 0.8f) * 7.0f);
        const float alpha = saturate(std::max(core * core, plume * 0.8f) * flicker);
        const float heat = saturate(core * 1.15f + plume * 0.3f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(1.0f);
        pixels[dst_index + 1u] = toByte(0.22f + 0.68f * heat);
        pixels[dst_index + 2u] = toByte(0.02f + 0.30f * core);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildGlowAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 0 ? static_cast<float>(frame) / static_cast<float>(frame_count)
                                    : 0.0f;
    const float ring_radius = 0.28f + 0.08f * std::sin(t * kTau);
    const float core_radius = 0.58f - 0.05f * std::cos(t * kTau);
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        const float core = saturate(1.0f - radius / std::max(core_radius, 0.001f));
        const float ring = saturate(1.0f - std::abs(radius - ring_radius) * 7.0f);
        const float alpha = saturate(core * 0.75f + ring * 0.45f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(0.10f + 0.18f * ring);
        pixels[dst_index + 1u] = toByte(0.45f + 0.38f * core);
        pixels[dst_index + 2u] = toByte(0.85f + 0.15f * (core + ring) * 0.5f);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildSmokeAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float swirl =
            0.22f * std::sin((px * 2.1f + py * 1.3f + t * 0.7f) * kTau) +
            0.18f * std::cos((py * 1.7f - px * 1.1f - t * 0.4f) * kTau);
        const float stretch_y = 0.88f - t * 0.22f;
        const float radius =
            std::sqrt(px * px * (1.15f + t * 0.25f) +
                      py * py / std::max(stretch_y * stretch_y, 0.05f));
        const float body = saturate(1.0f - radius + swirl * 0.22f);
        const float rim = saturate(1.0f - std::abs(radius - (0.48f + t * 0.10f)) * 3.4f);
        const float alpha = saturate(body * 0.85f + rim * 0.22f);
        const float density = saturate(0.38f + body * 0.45f + rim * 0.10f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(0.46f + density * 0.18f);
        pixels[dst_index + 1u] = toByte(0.48f + density * 0.20f);
        pixels[dst_index + 2u] = toByte(0.52f + density * 0.24f);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildHeatAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        const float angle = std::atan2(py, px);
        const float shimmer =
            0.5f + 0.5f * std::sin(angle * 3.5f + radius * 9.0f - t * kTau * 1.2f);
        const float swirl =
            0.5f + 0.5f * std::cos(angle * 2.0f - radius * 7.0f + t * kTau * 0.85f);
        const float ring = saturate(1.0f - std::abs(radius - (0.36f + t * 0.14f)) * 4.6f);
        const float core = saturate(1.0f - radius * (1.55f - t * 0.25f));
        const float alpha = saturate((ring * 0.75f + core * 0.45f) * (1.0f - radius * 0.45f));

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(shimmer);
        pixels[dst_index + 1u] = toByte(swirl);
        pixels[dst_index + 2u] = toByte(0.12f + alpha * 0.35f);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildDustRingAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    const float ring_radius = 0.14f + t * 0.56f;
    const float ring_width = 0.20f - t * 0.08f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        const float ring =
            saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width, 0.02f));
        const float breakup =
            0.65f + 0.35f * std::sin((px * 7.0f + py * 6.0f + t * 2.4f) * kPi) *
                         std::cos((px * 5.0f - py * 4.0f - t * 1.3f) * kPi);
        const float dust = saturate(ring * breakup);
        const float alpha = saturate(dust * (1.0f - t * 0.28f));

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(0.58f + dust * 0.18f);
        pixels[dst_index + 1u] = toByte(0.48f + dust * 0.15f);
        pixels[dst_index + 2u] = toByte(0.34f + dust * 0.10f);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildShockRingAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    const float ring_radius = 0.06f + t * 0.82f;
    const float ring_width = 0.075f + (1.0f - t) * 0.045f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        const float ring =
            saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width, 0.015f));
        const float halo =
            saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width * 2.8f, 0.03f));
        const float core =
            saturate(1.0f - radius / std::max(ring_radius + ring_width * 0.8f, 0.01f));
        const float breakup =
            0.84f + 0.16f * std::sin((px * 7.0f + py * 5.0f + t * 2.2f) * kPi) *
                         std::cos((px * 4.0f - py * 6.0f - t * 1.2f) * kPi);
        const float glow = saturate((ring * 1.25f + halo * 0.42f + core * 0.10f) * breakup);
        const float alpha = saturate((ring * 1.20f + halo * 0.35f) * (1.02f - t * 0.10f));

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(1.0f);
        pixels[dst_index + 1u] = toByte(0.58f + glow * 0.34f);
        pixels[dst_index + 2u] = toByte(0.14f + glow * 0.12f);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildScorchAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px * 1.12f + py * py * 0.92f);
        const float char_core = saturate(1.0f - radius * (1.35f + t * 0.08f));
        const float soot =
            saturate(0.52f - std::abs(radius - (0.42f + t * 0.05f)) * 1.8f +
                     0.14f * std::sin((px * 5.4f + py * 4.1f + t * 1.8f) * kPi));
        const float alpha = saturate(char_core * 0.62f + soot * 0.38f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        const float darkness = 0.05f + 0.11f * alpha;
        pixels[dst_index + 0u] = toByte(darkness);
        pixels[dst_index + 1u] = toByte(darkness * 0.92f);
        pixels[dst_index + 2u] = toByte(darkness * 0.80f);
        pixels[dst_index + 3u] = toByte(alpha * (0.82f - t * 0.12f));
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildDebrisAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    const float skew = -0.28f + 0.18f * static_cast<float>(frame);
    const float notch = 0.18f + 0.06f * static_cast<float>(frame % 2);
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float qx = px + py * skew;
        const float radius =
            std::max(std::abs(qx) * (1.0f + t * 0.2f), std::abs(py) * (0.82f + t * 0.1f));
        const float silhouette = saturate(1.0f - (radius - 0.42f) * 6.2f);
        const float chipped = saturate((qx + py) * 2.2f + notch);
        const float crack =
            0.5f + 0.5f * std::sin((qx * 8.5f - py * 6.2f + t * 1.6f) * kPi);
        const float alpha = saturate(silhouette * (0.72f + 0.28f * crack) * chipped);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        const float tone = 0.22f + 0.18f * crack + 0.06f * (1.0f - t);
        pixels[dst_index + 0u] = toByte(tone * 0.95f);
        pixels[dst_index + 1u] = toByte(tone * 0.82f);
        pixels[dst_index + 2u] = toByte(tone * 0.66f);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

void writeAtlasPixel(std::vector<std::uint8_t>& pixels,
                     int atlas_width,
                     int atlas_height,
                     int x,
                     int y,
                     const std::array<std::uint8_t, 4>& rgba) {
  if (x < 0 || y < 0 || x >= atlas_width || y >= atlas_height) {
    return;
  }
  const size_t index =
      (static_cast<size_t>(y) * static_cast<size_t>(atlas_width) + static_cast<size_t>(x)) * 4u;
  pixels[index + 0u] = rgba[0];
  pixels[index + 1u] = rgba[1];
  pixels[index + 2u] = rgba[2];
  pixels[index + 3u] = rgba[3];
}

std::vector<std::uint8_t> buildExplosionCoreFlipbookAtlas(int frame_size,
                                                          int border,
                                                          int spacing) {
  const int atlas_width = flipbookAtlasWidth(frame_size, border, spacing);
  const int atlas_height = flipbookAtlasHeight(frame_size, border, spacing);
  std::vector<std::uint8_t> pixels(
      static_cast<size_t>(atlas_width) * static_cast<size_t>(atlas_height) * 4u,
      0u);

  for (int frame = 0; frame < kFlipbookColumns * kFlipbookRows; ++frame) {
    const float t = static_cast<float>(frame) /
                    static_cast<float>(std::max(kFlipbookColumns * kFlipbookRows - 1, 1));
    const int column = frame % kFlipbookColumns;
    const int row = frame / kFlipbookColumns;
    const int frame_x = border + column * (frame_size + spacing);
    const int frame_y = border + row * (frame_size + spacing);

    std::vector<std::array<std::uint8_t, 4>> frame_pixels(
        static_cast<size_t>(frame_size) * static_cast<size_t>(frame_size));

    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float stretch_y = 0.82f + t * 0.42f;
        const float radius =
            std::sqrt(px * px * (1.08f + t * 0.55f) +
                      (py + 0.10f - t * 0.18f) * (py + 0.10f - t * 0.18f) /
                          std::max(stretch_y * stretch_y, 0.05f));
        const float plume =
            saturate(1.0f - radius * (1.35f + t * 0.50f));
        const float core =
            saturate(1.0f - std::sqrt(px * px * (3.7f + t * 0.8f) +
                                      (py - 0.02f) * (py - 0.02f) * (1.6f + t)));
        const float rim =
            saturate(1.0f - std::abs(radius - (0.36f + t * 0.22f)) * (4.0f - t * 0.7f));
        const float flicker =
            0.80f + 0.20f * std::sin((px * 7.5f + py * 4.0f + t * 7.2f) * kPi) *
                        std::cos((py * 5.2f - px * 3.4f - t * 4.3f) * kPi);
        const float alpha = saturate((core * 0.88f + plume * 0.62f + rim * 0.35f) *
                                     flicker * (1.0f - t * 0.34f));
        const float heat = saturate(core * 1.15f + plume * 0.72f + rim * 0.38f);
        const float ember = saturate(rim * 0.55f + plume * (1.0f - t) * 0.35f);
        const auto rgba = std::array<std::uint8_t, 4>{
            toByte(0.95f + heat * 0.05f),
            toByte(0.18f + heat * 0.62f),
            toByte(0.02f + ember * 0.20f),
            toByte(alpha),
        };
        frame_pixels[static_cast<size_t>(y) * static_cast<size_t>(frame_size) +
                     static_cast<size_t>(x)] = rgba;
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x + x, frame_y + y, rgba);
      }
    }

    const auto sample_frame_pixel = [&](int x, int y) -> const std::array<std::uint8_t, 4>& {
      const int clamped_x = std::clamp(x, 0, frame_size - 1);
      const int clamped_y = std::clamp(y, 0, frame_size - 1);
      return frame_pixels[static_cast<size_t>(clamped_y) * static_cast<size_t>(frame_size) +
                          static_cast<size_t>(clamped_x)];
    };

    for (int y = 0; y < frame_size; ++y) {
      const auto& left = sample_frame_pixel(0, y);
      const auto& right = sample_frame_pixel(frame_size - 1, y);
      for (int bleed = 1; bleed <= 2; ++bleed) {
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x - bleed, frame_y + y, left);
        writeAtlasPixel(
            pixels, atlas_width, atlas_height, frame_x + frame_size - 1 + bleed, frame_y + y, right);
      }
    }
    for (int x = 0; x < frame_size; ++x) {
      const auto& top = sample_frame_pixel(x, 0);
      const auto& bottom = sample_frame_pixel(x, frame_size - 1);
      for (int bleed = 1; bleed <= 2; ++bleed) {
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x + x, frame_y - bleed, top);
        writeAtlasPixel(
            pixels, atlas_width, atlas_height, frame_x + x, frame_y + frame_size - 1 + bleed, bottom);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildExplosionSmokeFlipbookAtlas(int frame_size,
                                                           int border,
                                                           int spacing) {
  const int atlas_width = flipbookAtlasWidth(frame_size, border, spacing);
  const int atlas_height = flipbookAtlasHeight(frame_size, border, spacing);
  std::vector<std::uint8_t> pixels(
      static_cast<size_t>(atlas_width) * static_cast<size_t>(atlas_height) * 4u,
      0u);

  for (int frame = 0; frame < kFlipbookColumns * kFlipbookRows; ++frame) {
    const float t = static_cast<float>(frame) /
                    static_cast<float>(std::max(kFlipbookColumns * kFlipbookRows - 1, 1));
    const int column = frame % kFlipbookColumns;
    const int row = frame / kFlipbookColumns;
    const int frame_x = border + column * (frame_size + spacing);
    const int frame_y = border + row * (frame_size + spacing);

    std::vector<std::array<std::uint8_t, 4>> frame_pixels(
        static_cast<size_t>(frame_size) * static_cast<size_t>(frame_size));

    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float swirl =
            0.22f * std::sin((px * 2.1f + py * 1.3f + t * 0.7f) * kTau) +
            0.18f * std::cos((py * 1.7f - px * 1.1f - t * 0.4f) * kTau);
        const float stretch_y = 0.90f - t * 0.18f;
        const float radius =
            std::sqrt(px * px * (1.10f + t * 0.32f) +
                      py * py / std::max(stretch_y * stretch_y, 0.05f));
        const float body = saturate(1.0f - radius + swirl * 0.26f);
        const float rim = saturate(1.0f - std::abs(radius - (0.44f + t * 0.18f)) * 3.0f);
        const float wisps =
            0.5f + 0.5f * std::sin((px * 4.8f - py * 6.4f + t * 3.6f) * kPi) *
                       std::cos((px * 7.6f + py * 3.3f - t * 2.2f) * kPi);
        const float alpha = saturate((body * 0.76f + rim * 0.28f) * (0.56f + wisps * 0.26f));
        const float density = saturate(0.24f + body * 0.52f + rim * 0.18f);
        const auto rgba = std::array<std::uint8_t, 4>{
            toByte(0.46f * density),
            toByte(0.46f * density),
            toByte(0.46f * density),
            toByte(alpha),
        };
        frame_pixels[static_cast<size_t>(y) * static_cast<size_t>(frame_size) +
                     static_cast<size_t>(x)] = rgba;
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x + x, frame_y + y, rgba);
      }
    }

    const auto sample_frame_pixel = [&](int x, int y) -> const std::array<std::uint8_t, 4>& {
      const int clamped_x = std::clamp(x, 0, frame_size - 1);
      const int clamped_y = std::clamp(y, 0, frame_size - 1);
      return frame_pixels[static_cast<size_t>(clamped_y) * static_cast<size_t>(frame_size) +
                          static_cast<size_t>(clamped_x)];
    };

    for (int y = 0; y < frame_size; ++y) {
      const auto& left = sample_frame_pixel(0, y);
      const auto& right = sample_frame_pixel(frame_size - 1, y);
      for (int bleed = 1; bleed <= 2; ++bleed) {
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x - bleed, frame_y + y, left);
        writeAtlasPixel(
            pixels, atlas_width, atlas_height, frame_x + frame_size - 1 + bleed, frame_y + y, right);
      }
    }
    for (int x = 0; x < frame_size; ++x) {
      const auto& top = sample_frame_pixel(x, 0);
      const auto& bottom = sample_frame_pixel(x, frame_size - 1);
      for (int bleed = 1; bleed <= 2; ++bleed) {
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x + x, frame_y - bleed, top);
        writeAtlasPixel(
            pixels, atlas_width, atlas_height, frame_x + x, frame_y + frame_size - 1 + bleed, bottom);
      }
    }
  }

  return pixels;
}

bool prepareExplosionPackage(const prefabs::PrefabPackageContext& context,
                             const std::shared_ptr<ExplosionPackageState>& state) {
  if (context.graphics == nullptr || context.particle_effects == nullptr) {
    return false;
  }

  ScopedStartupTimer package_timer("Explosion prefab package prepare");
  const ExplosionFlipbookSourceMode flipbook_source_mode =
      parseExplosionFlipbookSourceMode();
  const bool rebuild_flipbook_cache = explosionFlipbookRebuildRequested();
  const bool use_fast_flipbook_effects =
      flipbook_source_mode == ExplosionFlipbookSourceMode::Fast;
  const int core_fallback_frame_size =
      use_fast_flipbook_effects ? kFastCoreFlipbookFrameSize : kFlipbookFrameSize;
  const int core_fallback_border =
      use_fast_flipbook_effects ? kFastCoreFlipbookBorder : kFlipbookBorder;
  const int core_fallback_spacing =
      use_fast_flipbook_effects ? kFastCoreFlipbookSpacing : kFlipbookSpacing;
  const int smoke_fallback_frame_size =
      use_fast_flipbook_effects ? kFastSmokeFlipbookFrameSize : kFlipbookFrameSize;
  const int smoke_fallback_border =
      use_fast_flipbook_effects ? kFastSmokeFlipbookBorder : kFlipbookBorder;
  const int smoke_fallback_spacing =
      use_fast_flipbook_effects ? kFastSmokeFlipbookSpacing : kFlipbookSpacing;
  spdlog::info("Explosion prefab package flipbook mode={} rebuild_cache={}",
               explosionFlipbookSourceModeName(flipbook_source_mode),
               rebuild_flipbook_cache);

  {
    ScopedStartupTimer timer("Explosion prefab package small procedural atlas generation");
    if (state->spark_texture == renderer::kInvalidTexture) {
      const auto atlas = buildSparkAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->spark_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->glow_texture == renderer::kInvalidTexture) {
      const auto atlas = buildGlowAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->glow_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->smoke_texture == renderer::kInvalidTexture) {
      const auto atlas = buildSmokeAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->smoke_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->heat_texture == renderer::kInvalidTexture) {
      const auto atlas = buildHeatAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->heat_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->dust_ring_texture == renderer::kInvalidTexture) {
      const auto atlas = buildDustRingAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->dust_ring_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->shock_ring_texture == renderer::kInvalidTexture) {
      const auto atlas = buildShockRingAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->shock_ring_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->scorch_texture == renderer::kInvalidTexture) {
      const auto atlas = buildScorchAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->scorch_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
    if (state->debris_texture == renderer::kInvalidTexture) {
      const auto atlas = buildDebrisAtlas(kAtlasFrameSize, kAtlasFrameCount);
      state->debris_texture = context.graphics->createTextureRGBA8(
          kAtlasFrameSize * kAtlasFrameCount, kAtlasFrameSize, atlas.data());
    }
  }
  if (state->explosion_flipbook_texture == renderer::kInvalidTexture) {
    if (flipbook_source_mode != ExplosionFlipbookSourceMode::Fast) {
      state->explosion_flipbook_texture =
          buildExplosionPackageFireExrFlipbook(*context.graphics, rebuild_flipbook_cache);
      if (state->explosion_flipbook_texture != renderer::kInvalidTexture) {
        state->core_flipbook_source = ExplosionFlipbookTextureSource::ExrSequence;
        spdlog::info(
            "Explosion prefab package loaded Explosion00 fire atlas from EXR source");
      }
    }
    if (state->explosion_flipbook_texture == renderer::kInvalidTexture) {
      if (flipbook_source_mode != ExplosionFlipbookSourceMode::Fast) {
        spdlog::warn(
            "Explosion prefab package EXR fire atlas unavailable; falling back to procedural atlas");
      }
      state->explosion_flipbook_texture =
          buildProceduralExplosionCoreFlipbook(*context.graphics,
                                               core_fallback_frame_size,
                                               core_fallback_border,
                                               core_fallback_spacing);
      if (state->explosion_flipbook_texture != renderer::kInvalidTexture) {
        state->core_flipbook_source = ExplosionFlipbookTextureSource::ProceduralAtlas;
      }
    }
    if (state->explosion_flipbook_texture == renderer::kInvalidTexture) {
      spdlog::error("Explosion prefab package failed to create fire flipbook atlas");
      return false;
    }
  }
  if (state->explosion_smoke_flipbook_texture == renderer::kInvalidTexture) {
    if (flipbook_source_mode != ExplosionFlipbookSourceMode::Fast) {
      state->explosion_smoke_flipbook_texture =
          buildExplosionPackageSmokeExrFlipbook(*context.graphics, rebuild_flipbook_cache);
      if (state->explosion_smoke_flipbook_texture != renderer::kInvalidTexture) {
        state->smoke_flipbook_source = ExplosionFlipbookTextureSource::ExrSequence;
        spdlog::info(
            "Explosion prefab package loaded Explosion01 smoke atlas from EXR source");
      }
    }
    if (state->explosion_smoke_flipbook_texture == renderer::kInvalidTexture) {
      if (flipbook_source_mode != ExplosionFlipbookSourceMode::Fast) {
        spdlog::warn(
            "Explosion prefab package EXR smoke atlas unavailable; falling back to procedural smoke atlas");
      }
      state->explosion_smoke_flipbook_texture =
          buildProceduralExplosionSmokeFlipbook(*context.graphics,
                                               smoke_fallback_frame_size,
                                               smoke_fallback_border,
                                               smoke_fallback_spacing);
      if (state->explosion_smoke_flipbook_texture != renderer::kInvalidTexture) {
        state->smoke_flipbook_source = ExplosionFlipbookTextureSource::ProceduralAtlas;
      }
    }
    if (state->explosion_smoke_flipbook_texture == renderer::kInvalidTexture) {
      spdlog::error("Explosion prefab package failed to create smoke flipbook atlas");
      return false;
    }
  }

  g_explosion_package_debug_info = ExplosionPrefabPackageDebugInfo{
      .core_flipbook_source = state->core_flipbook_source,
      .smoke_flipbook_source = state->smoke_flipbook_source,
  };
  spdlog::info("Explosion prefab package flipbooks: core={} smoke={}",
               flipbookSourceName(state->core_flipbook_source),
               flipbookSourceName(state->smoke_flipbook_source));

  context.particle_effects->registerTextureAliases({
      {kExplosionTextureSpark, state->spark_texture},
      {kExplosionTextureGlow, state->glow_texture},
      {kExplosionTextureSmoke, state->smoke_texture},
      {kExplosionTextureHeat, state->heat_texture},
      {kExplosionTextureDustRing, state->dust_ring_texture},
      {kExplosionTextureShockRing, state->shock_ring_texture},
      {kExplosionTextureScorch, state->scorch_texture},
      {kExplosionTextureDebris, state->debris_texture},
      {kExplosionTextureCoreFlipbook, state->explosion_flipbook_texture},
      {kExplosionTextureSmokeFlipbook, state->explosion_smoke_flipbook_texture},
  });

  const std::filesystem::path core_flipbook_effect_path =
      use_fast_flipbook_effects
          ? explosionPackageAssetPath("particles/explosion_core_flipbook_fast.kpeffect")
          : explosionPackageAssetPath("particles/explosion_core_flipbook.kpeffect");
  const std::filesystem::path smoke_flipbook_effect_path =
      use_fast_flipbook_effects
          ? explosionPackageAssetPath("particles/explosion_smoke_flipbook_fast.kpeffect")
          : explosionPackageAssetPath("particles/explosion_smoke_flipbook.kpeffect");

  return context.particle_effects->registerEffectFiles({
      {kExplosionEffectFlash,
       explosionPackageAssetPath("particles/explosion_flash.kpeffect")},
      {kExplosionEffectFireball,
       explosionPackageAssetPath("particles/explosion_fireball.kpeffect")},
      {kExplosionEffectSmoke,
       explosionPackageAssetPath("particles/explosion_smoke.kpeffect")},
      {kExplosionEffectHeat,
       explosionPackageAssetPath("particles/explosion_heat.kpeffect")},
      {kExplosionEffectShockRing,
       explosionPackageAssetPath("particles/explosion_shock_ring.kpeffect")},
      {kExplosionEffectDustRing,
       explosionPackageAssetPath("particles/explosion_dust_ring.kpeffect")},
      {kExplosionEffectScorch,
       explosionPackageAssetPath("particles/explosion_scorch.kpeffect")},
      {kExplosionEffectDebris,
       explosionPackageAssetPath("particles/explosion_debris.kpeffect")},
      {kExplosionEffectEmbers,
       explosionPackageAssetPath("particles/explosion_embers.kpeffect")},
      {kExplosionEffectCoreFlipbook, core_flipbook_effect_path},
      {kExplosionEffectSmokeFlipbook, smoke_flipbook_effect_path},
  });
}

void cleanupExplosionPackage(const prefabs::PrefabPackageContext& context,
                             const std::shared_ptr<ExplosionPackageState>& state) {
  if (context.particle_effects != nullptr) {
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectFlash));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectFireball));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectSmoke));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectHeat));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectShockRing));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectDustRing));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectScorch));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectDebris));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectEmbers));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectCoreFlipbook));
    context.particle_effects->unregisterEffect(std::string(kExplosionEffectSmokeFlipbook));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureSpark));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureGlow));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureSmoke));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureHeat));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureDustRing));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureShockRing));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureScorch));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureDebris));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureCoreFlipbook));
    context.particle_effects->unregisterTextureAlias(std::string(kExplosionTextureSmokeFlipbook));
  }

  destroyTextureIfValid(context.graphics, state->spark_texture);
  destroyTextureIfValid(context.graphics, state->glow_texture);
  destroyTextureIfValid(context.graphics, state->smoke_texture);
  destroyTextureIfValid(context.graphics, state->heat_texture);
  destroyTextureIfValid(context.graphics, state->dust_ring_texture);
  destroyTextureIfValid(context.graphics, state->shock_ring_texture);
  destroyTextureIfValid(context.graphics, state->scorch_texture);
  destroyTextureIfValid(context.graphics, state->debris_texture);
  destroyTextureIfValid(context.graphics, state->explosion_flipbook_texture);
  destroyTextureIfValid(context.graphics, state->explosion_smoke_flipbook_texture);
}

void queueRestart(ExplosionPrefabController& controller,
                  ecs::Entity entity,
                  float delay_seconds,
                  float time_seconds) {
  if (!entity.isValid()) {
    return;
  }
  controller.scheduled_restarts.push_back(
      ExplosionPrefabController::ScheduledRestart{
          .entity = entity,
          .trigger_time = time_seconds + std::max(delay_seconds, 0.0f),
      });
}

void serviceScheduledRestarts(ecs::World& world,
                              ExplosionPrefabController& controller,
                              float time_seconds) {
  for (auto it = controller.scheduled_restarts.begin();
       it != controller.scheduled_restarts.end();) {
    if (time_seconds < it->trigger_time) {
      ++it;
      continue;
    }
    particles::restartEffect(world, it->entity);
    it = controller.scheduled_restarts.erase(it);
  }
}

void updateExplosionLight(ecs::World& world,
                          ExplosionPrefabController& controller,
                          float time_seconds) {
  if (!world.isAlive(controller.light) || !world.has<components::LightComponent>(controller.light)) {
    return;
  }

  auto& light = world.get<components::LightComponent>(controller.light);
  components::VisibilityComponent* visibility =
      world.has<components::VisibilityComponent>(controller.light)
          ? &world.get<components::VisibilityComponent>(controller.light)
          : nullptr;
  light.color = controller.light_peak_color;

  if (!controller.light_active) {
    light.intensity = 0.0f;
    light.range = controller.light_off_range;
    if (visibility != nullptr) {
      visibility->visible = false;
    }
    return;
  }

  const float duration =
      std::max(controller.light_end_time - controller.light_start_time, 0.001f);
  float t = saturate((time_seconds - controller.light_start_time) / duration);
  if (time_seconds >= controller.light_end_time) {
    controller.light_active = false;
    light.intensity = 0.0f;
    light.range = controller.light_off_range;
    if (visibility != nullptr) {
      visibility->visible = false;
    }
    return;
  }

  if (visibility != nullptr) {
    visibility->visible = true;
  }
  const float fade = 1.0f - smoothStep01(t);
  light.intensity = controller.light_peak_intensity * std::pow(fade, 2.0f);
  light.range = std::max(controller.light_off_range,
                         controller.light_peak_range *
                             (0.35f + 0.65f * std::pow(fade, 0.50f)));
}

}  // namespace

std::string_view explosionFlipbookSourceModeName(ExplosionFlipbookSourceMode mode) {
  switch (mode) {
    case ExplosionFlipbookSourceMode::Fast:
      return "fast";
    case ExplosionFlipbookSourceMode::Exr:
      return "exr";
    case ExplosionFlipbookSourceMode::Auto:
      return "auto";
    default:
      return "fast";
  }
}

ExplosionFlipbookSourceMode parseExplosionFlipbookSourceMode() {
  const char* value = std::getenv("KARMA_EXPLOSION_FLIPBOOK_SOURCE");
  if (value == nullptr || value[0] == '\0') {
    return ExplosionFlipbookSourceMode::Fast;
  }

  const std::string normalized = lowercase(trim(value));
  if (normalized == "fast") {
    return ExplosionFlipbookSourceMode::Fast;
  }
  if (normalized == "exr") {
    return ExplosionFlipbookSourceMode::Exr;
  }
  if (normalized == "auto") {
    return ExplosionFlipbookSourceMode::Auto;
  }

  spdlog::warn(
      "Unknown KARMA_EXPLOSION_FLIPBOOK_SOURCE='{}'; expected fast, exr, or auto. Using fast.",
      value);
  return ExplosionFlipbookSourceMode::Fast;
}

bool explosionFlipbookRebuildRequested() {
  const char* value = std::getenv("KARMA_EXPLOSION_FLIPBOOK_REBUILD");
  if (value == nullptr) {
    return false;
  }
  const std::string normalized = lowercase(trim(value));
  return normalized == "1" || normalized == "true" || normalized == "yes" ||
         normalized == "on";
}

renderer::TextureId buildExplosionPackageFireExrFlipbook(
    renderer::GraphicsDevice& graphics,
    bool rebuild_cache) {
  return buildCachedExrSequenceAtlas(
      graphics,
      "fire",
      explosionFireSequenceConfig(
          explosionPackageAssetPath("source/Explosion00-sequence-exr")),
      rebuild_cache);
}

renderer::TextureId buildExplosionPackageSmokeExrFlipbook(
    renderer::GraphicsDevice& graphics,
    bool rebuild_cache) {
  return buildCachedExrSequenceAtlas(
      graphics,
      "smoke",
      explosionSmokeSequenceConfig(
          explosionPackageAssetPath("source/Explosion01-light-nofire-sequence-exr")),
      rebuild_cache);
}

bool registerExplosionPrefabPackage(prefabs::PrefabRegistry& registry) {
  const auto state = std::make_shared<ExplosionPackageState>();
  return registry.registerPrefab(
      std::string(kExplosionPrefabKey),
      prefabs::RegisteredPrefabDesc{
          .prefab_path = explosionPackageAssetPath({}),
          .prepare =
              [state](const prefabs::PrefabPackageContext& context) {
                return prepareExplosionPackage(context, state);
              },
          .cleanup =
              [state](const prefabs::PrefabPackageContext& context) {
                cleanupExplosionPackage(context, state);
              },
      });
}

std::optional<ExplosionPrefabController> instantiateExplosionPrefabController(
    ecs::World& world,
    prefabs::PrefabRegistry& registry,
    const prefabs::PrefabInstantiateDesc& desc) {
  const auto instance = registry.instantiate(world, kExplosionPrefabKey, desc);
  if (!instance.has_value()) {
    return std::nullopt;
  }

  ExplosionPrefabController controller{};
  controller.instance = *instance;
  controller.flash = instance->find("flash");
  controller.fireball = instance->find("fireball");
  controller.heat = instance->find("heat");
  controller.core_flipbook = instance->find("core_flipbook");
  controller.smoke_flipbook = instance->find("smoke_flipbook");
  controller.embers = instance->find("embers");
  controller.shock_ring = instance->find("shock_ring");
  controller.debris = instance->find("debris");
  controller.dust_ring = instance->find("dust_ring");
  controller.smoke = instance->find("smoke");
  controller.scorch = instance->find("scorch");
  controller.light = instance->find("glow");

  if (world.isAlive(controller.light) && world.has<components::LightComponent>(controller.light)) {
    auto& light = world.get<components::LightComponent>(controller.light);
    controller.light_peak_color = light.color;
    controller.light_peak_intensity = light.intensity;
    controller.light_peak_range = light.range;
    light.intensity = 0.0f;
    light.range = controller.light_off_range;
    if (world.has<components::VisibilityComponent>(controller.light)) {
      world.get<components::VisibilityComponent>(controller.light).visible = false;
    }
  }

  return controller;
}

void triggerExplosionPrefab(ecs::World& world,
                            ExplosionPrefabController& controller,
                            float time_seconds) {
  controller.scheduled_restarts.clear();
  controller.light_active = true;
  controller.light_start_time = time_seconds;
  controller.light_end_time = time_seconds + kLightPulseDuration;

  queueRestart(controller, controller.flash, 0.00f, time_seconds);
  queueRestart(controller, controller.core_flipbook, 0.01f, time_seconds);
  queueRestart(controller, controller.fireball, 0.03f, time_seconds);
  queueRestart(controller, controller.heat, 0.04f, time_seconds);
  queueRestart(controller, controller.shock_ring, 0.04f, time_seconds);
  queueRestart(controller, controller.embers, 0.05f, time_seconds);
  queueRestart(controller, controller.debris, 0.05f, time_seconds);
  queueRestart(controller, controller.dust_ring, 0.05f, time_seconds);
  queueRestart(controller, controller.smoke_flipbook, 0.22f, time_seconds);
  queueRestart(controller, controller.smoke, 0.24f, time_seconds);
  queueRestart(controller, controller.scorch, 0.12f, time_seconds);

  serviceScheduledRestarts(world, controller, time_seconds);
  updateExplosionLight(world, controller, time_seconds);
}

void updateExplosionPrefab(ecs::World& world,
                           ExplosionPrefabController& controller,
                           float time_seconds) {
  serviceScheduledRestarts(world, controller, time_seconds);
  updateExplosionLight(world, controller, time_seconds);
}

bool destroyExplosionPrefabController(ecs::World& world,
                                      ExplosionPrefabController& controller) {
  controller.scheduled_restarts.clear();
  controller.light_active = false;

  const bool destroyed =
      controller.instance.valid() && prefabs::destroyPrefab(world, controller.instance.root);

  controller.instance = {};
  controller.flash = {};
  controller.fireball = {};
  controller.heat = {};
  controller.core_flipbook = {};
  controller.smoke_flipbook = {};
  controller.embers = {};
  controller.shock_ring = {};
  controller.debris = {};
  controller.dust_ring = {};
  controller.smoke = {};
  controller.scorch = {};
  controller.light = {};
  controller.light_peak_color = {};
  controller.light_peak_intensity = 0.0f;
  controller.light_peak_range = 0.0f;
  controller.light_start_time = 0.0f;
  controller.light_end_time = 0.0f;

  return destroyed;
}

ExplosionPrefabPackageDebugInfo getExplosionPrefabPackageDebugInfo() {
  return g_explosion_package_debug_info;
}

std::string_view explosionFlipbookTextureSourceName(
    ExplosionFlipbookTextureSource source) {
  return flipbookSourceName(source);
}

}  // namespace karma::demo
