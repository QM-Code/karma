#include "demo_asset_paths.h"
#include "karma/karma.h"
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

struct ScheduledEffectRestart {
  ecs::Entity entity{};
  float trigger_time = 0.0f;
};

constexpr float kExplosionVisualScale = 1.5f;

float scaleExplosionValue(float value) {
  return value * kExplosionVisualScale;
}

float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float smoothStep01(float value) {
  const float t = saturate(value);
  return t * t * (3.0f - 2.0f * t);
}

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

components::TransformComponent makeTransform(const math::Vec3& position, const math::Quat& rotation) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  transform.setRotation(rotation);
  return transform;
}

ecs::Entity createParticleEffectEntity(ecs::World& world,
                                       std::string_view name,
                                       std::string_view effect_key,
                                       const components::TransformComponent& transform,
                                       bool playing) {
  return particles::createEffectEntity(world,
                                       particles::ParticleEffectEntityDesc{
                                           .name = name,
                                           .effect_key = effect_key,
                                           .transform = transform,
                                           .enabled = true,
                                           .playing = playing,
                                       });
}

void setEntityPositionIfAlive(ecs::World& world, ecs::Entity entity, const math::Vec3& position) {
  if (!world.isAlive(entity) || !world.has<components::TransformComponent>(entity)) {
    return;
  }
  world.get<components::TransformComponent>(entity).setPosition(position);
}

void destroyTextureIfValid(renderer::GraphicsDevice* graphics, renderer::TextureId& texture) {
  if (graphics == nullptr || texture == renderer::kInvalidTexture) {
    return;
  }
  graphics->destroyTexture(texture);
  texture = renderer::kInvalidTexture;
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0f));
}

renderer::TextureId loadTextureRGBA8(renderer::GraphicsDevice& graphics,
                                     const std::filesystem::path& path) {
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &components, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    if (pixels != nullptr) {
      stbi_image_free(pixels);
    }
    return renderer::kInvalidTexture;
  }
  const renderer::TextureId texture = graphics.createTextureRGBA8(width, height, pixels);
  stbi_image_free(pixels);
  return texture;
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
  image.pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
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
      float derived_alpha = 1.0f - std::exp(-alpha_signal * std::max(config.alpha_signal_scale, 0.0f));
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

renderer::TextureId buildSequenceAtlas(renderer::GraphicsDevice& graphics,
                                       const SequenceAtlasBuildConfig& config) {
  if (config.atlas_columns <= 0 || config.atlas_rows <= 0 ||
      config.frame_width <= 0 || config.frame_height <= 0) {
    return renderer::kInvalidTexture;
  }
  const int atlas_frames = config.atlas_columns * config.atlas_rows;
  const int atlas_width =
      config.atlas_columns * config.frame_width +
      (config.atlas_columns - 1) * config.atlas_spacing + config.atlas_border * 2;
  const int atlas_height =
      config.atlas_rows * config.frame_height +
      (config.atlas_rows - 1) * config.atlas_spacing + config.atlas_border * 2;
  std::vector<std::filesystem::path> source_frames;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(config.sequence_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    if (entry.path().extension() == ".exr") {
      source_frames.push_back(entry.path());
    }
  }
  if (source_frames.empty()) {
    return renderer::kInvalidTexture;
  }
  std::sort(source_frames.begin(), source_frames.end());

  const size_t first_index = std::min(config.first_frame_index, source_frames.size() - 1u);
  const size_t last_index =
      std::max(first_index, std::min(config.last_frame_index, source_frames.size() - 1u));
  std::vector<std::uint8_t> atlas_pixels(
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
      return renderer::kInvalidTexture;
    }
    const int column = atlas_frame % config.atlas_columns;
    const int row = atlas_frame / config.atlas_columns;
    const int dst_x = config.atlas_border + column * (config.frame_width + config.atlas_spacing);
    const int dst_y = config.atlas_border + row * (config.frame_height + config.atlas_spacing);
    writePackedExplosionFrame(atlas_pixels,
                              atlas_width,
                              atlas_height,
                              dst_x,
                              dst_y,
                              config.frame_width,
                              config.frame_height,
                              source,
                              config);
  }

  return graphics.createTextureRGBA8(atlas_width, atlas_height, atlas_pixels.data());
}

renderer::TextureId buildExplosionSequenceAtlas(renderer::GraphicsDevice& graphics,
                                                const std::filesystem::path& sequence_dir) {
  return buildSequenceAtlas(graphics,
                            SequenceAtlasBuildConfig{
                                .sequence_dir = sequence_dir,
                                .first_frame_index = 1u,
                                .last_frame_index = 74u,
                                .atlas_columns = 5,
                                .atlas_rows = 5,
                                .frame_width = 200,
                                .frame_height = 200,
                                .atlas_border = 4,
                                .atlas_spacing = 4,
                                .alpha_signal_bias = 0.0f,
                                .alpha_signal_scale = 2.0f,
                                .alpha_signal_power = 1.0f,
                                .alpha_output_scale = 1.2f,
                                .source_alpha_floor = 0.35f,
                                .use_luminance_for_alpha = false,
                                .multiply_source_alpha = false,
                            });
}

renderer::TextureId buildExplosionSmokeSequenceAtlas(renderer::GraphicsDevice& graphics,
                                                     const std::filesystem::path& sequence_dir) {
  return buildSequenceAtlas(graphics,
                            SequenceAtlasBuildConfig{
                                .sequence_dir = sequence_dir,
                                .first_frame_index = 10u,
                                .last_frame_index = 92u,
                                .atlas_columns = 5,
                                .atlas_rows = 5,
                                .frame_width = 200,
                                .frame_height = 200,
                                .atlas_border = 4,
                                .atlas_spacing = 4,
                                .alpha_signal_bias = 0.185f,
                                .alpha_signal_scale = 8.0f,
                                .alpha_signal_power = 1.15f,
                                .alpha_output_scale = 1.0f,
                                .source_alpha_floor = 0.0f,
                                .use_luminance_for_alpha = true,
                                .multiply_source_alpha = true,
                            });
}

std::vector<std::uint8_t> buildSparkAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
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
  constexpr float kTau = 6.2831853f;
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 0 ? static_cast<float>(frame) / static_cast<float>(frame_count)
                                    : 0.0f;
    const float ring_radius = 0.28f + 0.08f * std::sin(t * kTau);
    const float core_radius = 0.58f - 0.05f * std::cos(t * kTau);
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
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
  constexpr float kTau = 6.2831853f;
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float swirl =
            0.22f * std::sin((px * 2.1f + py * 1.3f + t * 0.7f) * kTau) +
            0.18f * std::cos((py * 1.7f - px * 1.1f - t * 0.4f) * kTau);
        const float stretch_y = (0.88f - t * 0.22f);
        const float radius = std::sqrt(px * px * (1.15f + t * 0.25f) +
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
  constexpr float kTau = 6.2831853f;
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
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
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    const float ring_radius = 0.14f + t * 0.56f;
    const float ring_width = 0.20f - t * 0.08f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        const float ring = saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width, 0.02f));
        const float breakup =
            0.65f + 0.35f * std::sin((px * 7.0f + py * 6.0f + t * 2.4f) * 3.14159265f) *
                         std::cos((px * 5.0f - py * 4.0f - t * 1.3f) * 3.14159265f);
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
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    const float ring_radius = 0.06f + t * 0.82f;
    const float ring_width = 0.075f + (1.0f - t) * 0.045f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        const float ring = saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width, 0.015f));
        const float halo =
            saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width * 2.8f, 0.03f));
        const float core = saturate(1.0f - radius / std::max(ring_radius + ring_width * 0.8f, 0.01f));
        const float breakup =
            0.84f + 0.16f * std::sin((px * 7.0f + py * 5.0f + t * 2.2f) * 3.14159265f) *
                         std::cos((px * 4.0f - py * 6.0f - t * 1.2f) * 3.14159265f);
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
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px * 1.12f + py * py * 0.92f);
        const float char_core = saturate(1.0f - radius * (1.35f + t * 0.08f));
        const float soot =
            saturate(0.52f - std::abs(radius - (0.42f + t * 0.05f)) * 1.8f +
                     0.14f * std::sin((px * 5.4f + py * 4.1f + t * 1.8f) * 3.14159265f));
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
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = frame_count > 1 ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
                                    : 0.0f;
    const float skew = -0.28f + 0.18f * static_cast<float>(frame);
    const float notch = 0.18f + 0.06f * static_cast<float>(frame % 2);
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float qx = px + py * skew;
        const float radius = std::max(std::abs(qx) * (1.0f + t * 0.2f),
                                      std::abs(py) * (0.82f + t * 0.1f));
        const float silhouette = saturate(1.0f - (radius - 0.42f) * 6.2f);
        const float chipped = saturate((qx + py) * 2.2f + notch);
        const float crack =
            0.5f + 0.5f * std::sin((qx * 8.5f - py * 6.2f + t * 1.6f) * 3.14159265f);
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

void registerParticleEffects(particles::ParticleLibrary& particle_effects,
                             renderer::TextureId spark_texture,
                             renderer::TextureId glow_texture,
                             renderer::TextureId smoke_texture,
                             renderer::TextureId heat_texture,
                             renderer::TextureId explosion_flipbook_texture,
                             renderer::TextureId explosion_smoke_flipbook_texture,
                             renderer::TextureId dust_ring_texture,
                             renderer::TextureId shock_ring_texture,
                             renderer::TextureId scorch_texture,
                             renderer::TextureId debris_texture) {
  particle_effects.clear();
  particle_effects.clearTextureAliases();
  particle_effects.registerTextureAliases({
      {"spark_atlas", spark_texture},
      {"glow_atlas", glow_texture},
      {"smoke_atlas", smoke_texture},
      {"heat_atlas", heat_texture},
      {"dust_ring_atlas", dust_ring_texture},
      {"shock_ring_atlas", shock_ring_texture},
      {"scorch_atlas", scorch_texture},
      {"debris_atlas", debris_texture},
  });
  if (explosion_flipbook_texture != renderer::kInvalidTexture) {
    particle_effects.registerTextureAlias("explosion00_flipbook", explosion_flipbook_texture);
  }
  if (explosion_smoke_flipbook_texture != renderer::kInvalidTexture) {
    particle_effects.registerTextureAlias("explosion01_smoke_flipbook",
                                          explosion_smoke_flipbook_texture);
  }
  particle_effects.registerEffectFiles({
      {"spark_fountain", resolveExampleAssetPath("particles/spark_fountain.kpeffect")},
      {"glow_halo", resolveExampleAssetPath("particles/glow_halo.kpeffect")},
      {"smoke_plume", resolveExampleAssetPath("particles/smoke_plume.kpeffect")},
      {"explosion_flash", resolveExampleAssetPath("particles/explosion_flash.kpeffect")},
      {"explosion_fireball", resolveExampleAssetPath("particles/explosion_fireball.kpeffect")},
      {"explosion_smoke", resolveExampleAssetPath("particles/explosion_smoke.kpeffect")},
      {"explosion_heat", resolveExampleAssetPath("particles/explosion_heat.kpeffect")},
      {"explosion_shock_ring", resolveExampleAssetPath("particles/explosion_shock_ring.kpeffect")},
      {"explosion_dust_ring", resolveExampleAssetPath("particles/explosion_dust_ring.kpeffect")},
      {"explosion_scorch", resolveExampleAssetPath("particles/explosion_scorch.kpeffect")},
      {"explosion_debris", resolveExampleAssetPath("particles/explosion_debris.kpeffect")},
      {"explosion_embers", resolveExampleAssetPath("particles/explosion_embers.kpeffect")},
  });
  if (explosion_flipbook_texture != renderer::kInvalidTexture) {
    particle_effects.registerEffectFile(
        "explosion_core_flipbook",
        resolveExampleAssetPath("particles/explosion_core_flipbook.kpeffect"));
  }
  if (explosion_smoke_flipbook_texture != renderer::kInvalidTexture) {
    particle_effects.registerEffectFile(
        "explosion_smoke_flipbook",
        resolveExampleAssetPath("particles/explosion_smoke_flipbook.kpeffect"));
  }
}

}  // namespace

class ParticleExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("toggle_particles", platform::Key::Space, input::Trigger::Pressed);
    input->bindKey("trigger_explosion", platform::Key::E, input::Trigger::Pressed);

    const std::string world_mesh = resolveExampleAssetPath("world.glb").string();
    const std::string environment_map =
        resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();
    const int atlas_frame_size = 64;
    const int atlas_frames = 4;
    const std::filesystem::path authored_explosion_sequence_path =
        resolveExampleAssetPath("Explosion00-sequence-exr");
    const std::filesystem::path authored_explosion_smoke_sequence_path =
        resolveExampleAssetPath("Explosion01-light-nofire-sequence-exr");
    const std::filesystem::path authored_explosion_flipbook_path =
        resolveExampleAssetPath("Explosion00-flipbooks/Explosion00_5x5.tga");

    const std::vector<std::uint8_t> spark_atlas = buildSparkAtlas(atlas_frame_size, atlas_frames);
    spark_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                  atlas_frame_size,
                                                  spark_atlas.data());

    const std::vector<std::uint8_t> glow_atlas = buildGlowAtlas(atlas_frame_size, atlas_frames);
    glow_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                 atlas_frame_size,
                                                 glow_atlas.data());

    const std::vector<std::uint8_t> smoke_atlas = buildSmokeAtlas(atlas_frame_size, atlas_frames);
    smoke_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                  atlas_frame_size,
                                                  smoke_atlas.data());

    const std::vector<std::uint8_t> heat_atlas = buildHeatAtlas(atlas_frame_size, atlas_frames);
    heat_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                 atlas_frame_size,
                                                 heat_atlas.data());

    explosion_flipbook_texture_ =
        buildExplosionSequenceAtlas(*graphics, authored_explosion_sequence_path);
    if (explosion_flipbook_texture_ == renderer::kInvalidTexture) {
      spdlog::warn("Explosion00 EXR sequence atlas build failed; falling back to TGA flipbook");
      explosion_flipbook_texture_ = loadTextureRGBA8(*graphics, authored_explosion_flipbook_path);
    } else {
      spdlog::info("Built Explosion00 fire atlas from EXR sequence");
    }
    explosion_smoke_flipbook_texture_ =
        buildExplosionSmokeSequenceAtlas(*graphics, authored_explosion_smoke_sequence_path);
    if (explosion_smoke_flipbook_texture_ == renderer::kInvalidTexture) {
      spdlog::warn("Explosion01 smoke EXR sequence atlas build failed; using existing procedural smoke only");
    } else {
      spdlog::info("Built Explosion01 smoke atlas from EXR sequence");
    }

    const std::vector<std::uint8_t> dust_ring_atlas =
        buildDustRingAtlas(atlas_frame_size, atlas_frames);
    dust_ring_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                      atlas_frame_size,
                                                      dust_ring_atlas.data());

    const std::vector<std::uint8_t> shock_ring_atlas =
        buildShockRingAtlas(atlas_frame_size, atlas_frames);
    shock_ring_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                       atlas_frame_size,
                                                       shock_ring_atlas.data());

    const std::vector<std::uint8_t> scorch_atlas =
        buildScorchAtlas(atlas_frame_size, atlas_frames);
    scorch_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                   atlas_frame_size,
                                                   scorch_atlas.data());

    const std::vector<std::uint8_t> debris_atlas =
        buildDebrisAtlas(atlas_frame_size, atlas_frames);
    debris_texture_ = graphics->createTextureRGBA8(atlas_frame_size * atlas_frames,
                                                   atlas_frame_size,
                                                   debris_atlas.data());
    if (particle_effects) {
      registerParticleEffects(*particle_effects,
                              spark_texture_,
                              glow_texture_,
                              smoke_texture_,
                              heat_texture_,
                              explosion_flipbook_texture_,
                              explosion_smoke_flipbook_texture_,
                              dust_ring_texture_,
                              shock_ring_texture_,
                              scorch_texture_,
                              debris_texture_);
    }

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_key = world_mesh});

    auto environment_entity = world->createEntity();
    world->setName(environment_entity, "Environment");
    world->add(environment_entity, components::EnvironmentComponent{
        .environment_map = environment_map,
        .intensity = 0.35f,
        .draw_skybox = true,
    });

    auto sun_entity = world->createEntity();
    world->setName(sun_entity, "Sun");
    components::TransformComponent sun_xform{};
    sun_xform.setPosition({0.0f, 50.0f, 0.0f});
    sun_xform.setRotation(math::fromYawPitch(0.55f, -0.95f));
    world->add(sun_entity, sun_xform);
    world->add(sun_entity, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 0.98f, 0.92f, 1.0f},
        .intensity = 0.95f,
        .shadow_extent = 60.0f,
    });

    auto explosion_light_entity = world->createEntity();
    world->setName(explosion_light_entity, "Explosion Light");
    components::TransformComponent explosion_light_xform{};
    explosion_light_xform.setPosition({3.2f, 1.2f, 0.0f});
    world->add(explosion_light_entity, explosion_light_xform);
    world->add(explosion_light_entity, components::LightComponent{
                                           .type = components::LightComponent::Type::Point,
                                           .color = {1.0f, 0.75f, 0.45f, 1.0f},
                                           .intensity = 0.0f,
                                           .range = 0.1f,
                                       });
    explosion_light_entity_ = explosion_light_entity;

    if (particle_effects) {
      fountain_entity_ = createParticleEffectEntity(
          *world,
          "Spark Fountain",
          "spark_fountain",
          makeTransform({0.0f, 0.55f, 0.0f}, math::fromYawPitch(0.0f, -0.10f)),
          true);
      glow_entity_ = createParticleEffectEntity(
          *world,
          "Glow Halo",
          "glow_halo",
          makeTransform({0.0f, 1.15f, 0.0f}),
          true);
      smoke_entity_ = createParticleEffectEntity(
          *world,
          "Smoke Plume",
          "smoke_plume",
          makeTransform({0.0f, 1.35f, 0.0f}),
          true);

      explosion_flash_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Flash",
          "explosion_flash",
          makeTransform({3.2f, 0.8f, 0.0f}),
          false);
      explosion_fireball_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Fireball",
          "explosion_fireball",
          makeTransform({3.2f, 0.75f, 0.0f}),
          false);
      explosion_heat_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Heat",
          "explosion_heat",
          makeTransform({3.2f, 0.82f, 0.0f}),
          false);
      if (explosion_flipbook_texture_ != renderer::kInvalidTexture) {
        explosion_core_flipbook_entity_ = createParticleEffectEntity(
            *world,
            "Explosion Core Flipbook",
            "explosion_core_flipbook",
            makeTransform({3.2f, 0.88f, 0.0f}),
            false);
      }
      if (explosion_smoke_flipbook_texture_ != renderer::kInvalidTexture) {
        explosion_smoke_flipbook_entity_ = createParticleEffectEntity(
            *world,
            "Explosion Smoke Flipbook",
            "explosion_smoke_flipbook",
            makeTransform({3.2f, 0.92f, 0.0f}),
            false);
      }
      explosion_embers_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Embers",
          "explosion_embers",
          makeTransform({3.2f, 0.90f, 0.0f}),
          false);
      explosion_shock_ring_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Shock Ring",
          "explosion_shock_ring",
          makeTransform({3.2f, 0.09f, 0.0f}),
          false);
      explosion_debris_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Debris",
          "explosion_debris",
          makeTransform({3.2f, 0.86f, 0.0f}),
          false);
      explosion_dust_ring_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Dust Ring",
          "explosion_dust_ring",
          makeTransform({3.2f, 0.04f, 0.0f}),
          false);
      explosion_smoke_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Smoke",
          "explosion_smoke",
          makeTransform({3.2f, 0.85f, 0.0f}),
          false);
      explosion_scorch_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Scorch",
          "explosion_scorch",
          makeTransform({3.2f, 0.03f, 0.0f}),
          false);
    }

    auto camera_entity = world->createEntity();
    world->setName(camera_entity, "Camera");
    components::TransformComponent camera_xform{};
    camera_xform.setPosition({0.0f, 5.8f, 14.0f});
    camera_xform.setRotation(math::fromYawPitch(0.0f, -0.28f));
    world->add(camera_entity, camera_xform);
    world->add(camera_entity, components::CameraComponent{
        .near_clip = 0.05f,
        .far_clip = 180.0f,
        .is_primary = true,
    });
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    time_ += dt;

    if (input->actionPressed("toggle_particles")) {
      particles_enabled_ = !particles_enabled_;
      particles::setEffectPlaying(*world, fountain_entity_, particles_enabled_);
      particles::setEffectPlaying(*world, glow_entity_, particles_enabled_);
      particles::setEffectPlaying(*world, smoke_entity_, particles_enabled_);
      if (!particles_enabled_) {
        scheduled_restarts_.clear();
        explosion_light_active_ = false;
      }
    }

    if (world->isAlive(fountain_entity_)) {
      auto& transform = world->get<components::TransformComponent>(fountain_entity_);
      const float orbit = time_ * 0.65f;
      transform.setPosition({
          std::sin(orbit) * 1.8f,
          0.55f + 0.18f * std::sin(time_ * 1.7f),
          std::cos(orbit) * 1.8f});
      transform.setRotation(math::fromYawPitch(time_ * 2.2f, -0.08f));
    }

    if (world->isAlive(glow_entity_) && world->isAlive(fountain_entity_)) {
      const auto fountain_position =
          world->get<components::TransformComponent>(fountain_entity_).getPosition();
      auto& glow_transform = world->get<components::TransformComponent>(glow_entity_);
      glow_transform.setPosition({
          fountain_position.x,
          fountain_position.y + 0.65f,
          fountain_position.z});
    }

    if (world->isAlive(smoke_entity_) && world->isAlive(fountain_entity_)) {
      const auto fountain_position =
          world->get<components::TransformComponent>(fountain_entity_).getPosition();
      auto& smoke_transform = world->get<components::TransformComponent>(smoke_entity_);
      smoke_transform.setPosition({
          fountain_position.x,
          fountain_position.y + 0.25f,
          fountain_position.z});
    }

    const math::Vec3 roaming_explosion_origin{
        std::sin(time_ * 0.46f + 1.2f) * 3.35f,
        0.72f + 0.14f * std::sin(time_ * 0.95f),
        std::cos(time_ * 0.46f + 1.2f) * 3.35f};
    if (!isExplosionSequenceActive()) {
      explosion_anchor_position_ = roaming_explosion_origin;
    }

    const bool should_trigger_explosion =
        particles_enabled_ &&
        (input->actionPressed("trigger_explosion") || time_ >= next_explosion_time_);
    if (should_trigger_explosion) {
      triggerExplosion(roaming_explosion_origin);
      next_explosion_time_ = time_ + 3.6f;
    }

    updateExplosionTransforms();
    serviceScheduledRestarts();
    updateExplosionLight();

  }

  void onShutdown() override {
    destroyTextureIfValid(graphics, spark_texture_);
    destroyTextureIfValid(graphics, glow_texture_);
    destroyTextureIfValid(graphics, smoke_texture_);
    destroyTextureIfValid(graphics, heat_texture_);
    destroyTextureIfValid(graphics, explosion_flipbook_texture_);
    destroyTextureIfValid(graphics, explosion_smoke_flipbook_texture_);
    destroyTextureIfValid(graphics, dust_ring_texture_);
    destroyTextureIfValid(graphics, shock_ring_texture_);
    destroyTextureIfValid(graphics, scorch_texture_);
    destroyTextureIfValid(graphics, debris_texture_);
  }

 private:
  math::Vec3 explosionGroundPosition() const {
    return {explosion_anchor_position_.x, 0.04f, explosion_anchor_position_.z};
  }

  bool isExplosionSequenceActive() const {
    return !scheduled_restarts_.empty() || explosion_light_active_;
  }

  void updateExplosionTransforms() {
    setEntityPositionIfAlive(*world, explosion_flash_entity_, explosion_anchor_position_);
    setEntityPositionIfAlive(*world, explosion_fireball_entity_, explosion_anchor_position_);
    setEntityPositionIfAlive(
        *world,
        explosion_heat_entity_,
        {explosion_anchor_position_.x,
         explosion_anchor_position_.y + scaleExplosionValue(0.06f),
         explosion_anchor_position_.z});
    setEntityPositionIfAlive(
        *world,
        explosion_core_flipbook_entity_,
        {explosion_anchor_position_.x,
         explosion_anchor_position_.y + scaleExplosionValue(0.08f),
         explosion_anchor_position_.z});
    setEntityPositionIfAlive(
        *world,
        explosion_smoke_flipbook_entity_,
        {explosion_anchor_position_.x,
         explosion_anchor_position_.y + scaleExplosionValue(0.18f),
         explosion_anchor_position_.z});
    setEntityPositionIfAlive(
        *world,
        explosion_embers_entity_,
        {explosion_anchor_position_.x,
         explosion_anchor_position_.y + scaleExplosionValue(0.12f),
         explosion_anchor_position_.z});
    const math::Vec3 ground_position = explosionGroundPosition();
    setEntityPositionIfAlive(
        *world,
        explosion_shock_ring_entity_,
        {ground_position.x,
         ground_position.y + scaleExplosionValue(0.05f),
         ground_position.z});
    setEntityPositionIfAlive(
        *world,
        explosion_debris_entity_,
        {explosion_anchor_position_.x,
         explosion_anchor_position_.y + scaleExplosionValue(0.10f),
         explosion_anchor_position_.z});
    setEntityPositionIfAlive(*world, explosion_dust_ring_entity_, ground_position);
    setEntityPositionIfAlive(
        *world,
        explosion_smoke_entity_,
        {explosion_anchor_position_.x,
         explosion_anchor_position_.y + scaleExplosionValue(0.10f),
         explosion_anchor_position_.z});
    setEntityPositionIfAlive(
        *world,
        explosion_scorch_entity_,
        {ground_position.x,
         ground_position.y - 0.01f,
         ground_position.z});
  }

  void queueEffectRestart(ecs::Entity entity, float delay_seconds) {
    scheduled_restarts_.push_back(ScheduledEffectRestart{
        .entity = entity,
        .trigger_time = time_ + std::max(delay_seconds, 0.0f),
    });
  }

  void serviceScheduledRestarts() {
    for (auto it = scheduled_restarts_.begin(); it != scheduled_restarts_.end();) {
      if (time_ < it->trigger_time) {
        ++it;
        continue;
      }
      restartEffectEntity(it->entity);
      it = scheduled_restarts_.erase(it);
    }
  }

  void restartEffectEntity(ecs::Entity entity) {
    particles::restartEffect(*world, entity);
  }

  void updateExplosionLight() {
    if (!world->isAlive(explosion_light_entity_) ||
        !world->has<components::TransformComponent>(explosion_light_entity_) ||
        !world->has<components::LightComponent>(explosion_light_entity_)) {
      return;
    }

    auto& transform = world->get<components::TransformComponent>(explosion_light_entity_);
    auto& light = world->get<components::LightComponent>(explosion_light_entity_);
    transform.setPosition({
        explosion_anchor_position_.x,
        explosion_anchor_position_.y + scaleExplosionValue(0.45f),
        explosion_anchor_position_.z});

    if (!explosion_light_active_ || !particles_enabled_) {
      light.intensity = 0.0f;
      light.range = 0.1f;
      return;
    }

    const float duration =
        std::max(explosion_light_end_time_ - explosion_light_start_time_, 0.001f);
    float t = saturate((time_ - explosion_light_start_time_) / duration);
    if (time_ >= explosion_light_end_time_) {
      explosion_light_active_ = false;
      t = 1.0f;
    }

    const float fade = 1.0f - smoothStep01(t);
    light.color = {
        1.0f,
        0.86f - 0.38f * t,
        0.52f - 0.40f * t,
        1.0f,
    };
    light.intensity = 78.0f * std::pow(fade, 2.0f);
    light.range = scaleExplosionValue(10.0f + 24.0f * std::pow(fade, 0.50f));
  }

  void triggerExplosion(const math::Vec3& origin) {
    explosion_anchor_position_ = origin;
    explosion_light_active_ = true;
    explosion_light_start_time_ = time_;
    explosion_light_end_time_ = time_ + 0.64f;
    queueEffectRestart(explosion_flash_entity_, 0.00f);
    if (world->isAlive(explosion_core_flipbook_entity_)) {
      queueEffectRestart(explosion_core_flipbook_entity_, 0.01f);
    }
    queueEffectRestart(explosion_fireball_entity_, 0.03f);
    queueEffectRestart(explosion_heat_entity_, 0.04f);
    queueEffectRestart(explosion_embers_entity_, 0.05f);
    queueEffectRestart(explosion_shock_ring_entity_, 0.04f);
    queueEffectRestart(explosion_debris_entity_, 0.05f);
    queueEffectRestart(explosion_dust_ring_entity_, 0.05f);
    if (world->isAlive(explosion_smoke_flipbook_entity_)) {
      queueEffectRestart(explosion_smoke_flipbook_entity_, 0.08f);
    }
    queueEffectRestart(explosion_smoke_entity_, 0.10f);
    queueEffectRestart(explosion_scorch_entity_, 0.12f);
  }

  ecs::Entity fountain_entity_{};
  ecs::Entity glow_entity_{};
  ecs::Entity smoke_entity_{};
  ecs::Entity explosion_flash_entity_{};
  ecs::Entity explosion_fireball_entity_{};
  ecs::Entity explosion_heat_entity_{};
  ecs::Entity explosion_core_flipbook_entity_{};
  ecs::Entity explosion_smoke_flipbook_entity_{};
  ecs::Entity explosion_embers_entity_{};
  ecs::Entity explosion_shock_ring_entity_{};
  ecs::Entity explosion_debris_entity_{};
  ecs::Entity explosion_dust_ring_entity_{};
  ecs::Entity explosion_smoke_entity_{};
  ecs::Entity explosion_scorch_entity_{};
  ecs::Entity explosion_light_entity_{};
  renderer::TextureId spark_texture_ = renderer::kInvalidTexture;
  renderer::TextureId glow_texture_ = renderer::kInvalidTexture;
  renderer::TextureId smoke_texture_ = renderer::kInvalidTexture;
  renderer::TextureId heat_texture_ = renderer::kInvalidTexture;
  renderer::TextureId explosion_flipbook_texture_ = renderer::kInvalidTexture;
  renderer::TextureId explosion_smoke_flipbook_texture_ = renderer::kInvalidTexture;
  renderer::TextureId dust_ring_texture_ = renderer::kInvalidTexture;
  renderer::TextureId shock_ring_texture_ = renderer::kInvalidTexture;
  renderer::TextureId scorch_texture_ = renderer::kInvalidTexture;
  renderer::TextureId debris_texture_ = renderer::kInvalidTexture;
  std::vector<ScheduledEffectRestart> scheduled_restarts_;
  bool particles_enabled_ = true;
  bool explosion_light_active_ = false;
  math::Vec3 explosion_anchor_position_{};
  float explosion_light_start_time_ = 0.0f;
  float explosion_light_end_time_ = 0.0f;
  float next_explosion_time_ = 1.4f;
  float time_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::ParticleExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Particle Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.shadow_bias = 0.0006f;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
