#include "explosion_prefab_package.h"

#include "demo_asset_paths.h"
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/karma.h"

namespace karma::demo {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr int kAtlasFrameSize = 64;
constexpr int kAtlasFrameCount = 4;
constexpr int kFlipbookColumns = 5;
constexpr int kFlipbookRows = 5;
constexpr int kFlipbookFrameSize = 200;
constexpr int kFlipbookBorder = 4;
constexpr int kFlipbookSpacing = 4;
constexpr float kLightPulseDuration = 0.64f;

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
};

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

void destroyTextureIfValid(renderer::GraphicsDevice* graphics, renderer::TextureId& texture) {
  if (graphics == nullptr || texture == renderer::kInvalidTexture) {
    return;
  }
  graphics->destroyTexture(texture);
  texture = renderer::kInvalidTexture;
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

std::vector<std::uint8_t> buildExplosionSmokeFlipbookAtlas() {
  const int atlas_width = kFlipbookColumns * kFlipbookFrameSize +
                          (kFlipbookColumns - 1) * kFlipbookSpacing + kFlipbookBorder * 2;
  const int atlas_height = kFlipbookRows * kFlipbookFrameSize +
                           (kFlipbookRows - 1) * kFlipbookSpacing + kFlipbookBorder * 2;
  std::vector<std::uint8_t> pixels(
      static_cast<size_t>(atlas_width) * static_cast<size_t>(atlas_height) * 4u,
      0u);

  for (int frame = 0; frame < kFlipbookColumns * kFlipbookRows; ++frame) {
    const float t = static_cast<float>(frame) /
                    static_cast<float>(std::max(kFlipbookColumns * kFlipbookRows - 1, 1));
    const int column = frame % kFlipbookColumns;
    const int row = frame / kFlipbookColumns;
    const int frame_x = kFlipbookBorder + column * (kFlipbookFrameSize + kFlipbookSpacing);
    const int frame_y = kFlipbookBorder + row * (kFlipbookFrameSize + kFlipbookSpacing);

    std::vector<std::array<std::uint8_t, 4>> frame_pixels(
        static_cast<size_t>(kFlipbookFrameSize) * static_cast<size_t>(kFlipbookFrameSize));

    for (int y = 0; y < kFlipbookFrameSize; ++y) {
      for (int x = 0; x < kFlipbookFrameSize; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(kFlipbookFrameSize) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kFlipbookFrameSize) * 2.0f;
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
        frame_pixels[static_cast<size_t>(y) * static_cast<size_t>(kFlipbookFrameSize) +
                     static_cast<size_t>(x)] = rgba;
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x + x, frame_y + y, rgba);
      }
    }

    const auto sample_frame_pixel = [&](int x, int y) -> const std::array<std::uint8_t, 4>& {
      const int clamped_x = std::clamp(x, 0, kFlipbookFrameSize - 1);
      const int clamped_y = std::clamp(y, 0, kFlipbookFrameSize - 1);
      return frame_pixels[static_cast<size_t>(clamped_y) * static_cast<size_t>(kFlipbookFrameSize) +
                          static_cast<size_t>(clamped_x)];
    };

    for (int y = 0; y < kFlipbookFrameSize; ++y) {
      const auto& left = sample_frame_pixel(0, y);
      const auto& right = sample_frame_pixel(kFlipbookFrameSize - 1, y);
      for (int bleed = 1; bleed <= 2; ++bleed) {
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x - bleed, frame_y + y, left);
        writeAtlasPixel(
            pixels, atlas_width, atlas_height, frame_x + kFlipbookFrameSize - 1 + bleed, frame_y + y, right);
      }
    }
    for (int x = 0; x < kFlipbookFrameSize; ++x) {
      const auto& top = sample_frame_pixel(x, 0);
      const auto& bottom = sample_frame_pixel(x, kFlipbookFrameSize - 1);
      for (int bleed = 1; bleed <= 2; ++bleed) {
        writeAtlasPixel(pixels, atlas_width, atlas_height, frame_x + x, frame_y - bleed, top);
        writeAtlasPixel(
            pixels, atlas_width, atlas_height, frame_x + x, frame_y + kFlipbookFrameSize - 1 + bleed, bottom);
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
  if (state->explosion_flipbook_texture == renderer::kInvalidTexture) {
    state->explosion_flipbook_texture = loadTextureRGBA8(
        *context.graphics,
        resolveExampleAssetPath("Explosion00-flipbooks/Explosion00_5x5.tga"));
    if (state->explosion_flipbook_texture == renderer::kInvalidTexture) {
      spdlog::error("Explosion prefab package failed to load Explosion00_5x5.tga");
      return false;
    }
  }
  if (state->explosion_smoke_flipbook_texture == renderer::kInvalidTexture) {
    const auto atlas = buildExplosionSmokeFlipbookAtlas();
    const int atlas_width = kFlipbookColumns * kFlipbookFrameSize +
                            (kFlipbookColumns - 1) * kFlipbookSpacing + kFlipbookBorder * 2;
    const int atlas_height = kFlipbookRows * kFlipbookFrameSize +
                             (kFlipbookRows - 1) * kFlipbookSpacing + kFlipbookBorder * 2;
    state->explosion_smoke_flipbook_texture =
        context.graphics->createTextureRGBA8(atlas_width, atlas_height, atlas.data());
  }

  context.particle_effects->registerTextureAliases({
      {"spark_atlas", state->spark_texture},
      {"glow_atlas", state->glow_texture},
      {"smoke_atlas", state->smoke_texture},
      {"heat_atlas", state->heat_texture},
      {"dust_ring_atlas", state->dust_ring_texture},
      {"shock_ring_atlas", state->shock_ring_texture},
      {"scorch_atlas", state->scorch_texture},
      {"debris_atlas", state->debris_texture},
      {"explosion00_flipbook", state->explosion_flipbook_texture},
      {"explosion01_smoke_flipbook", state->explosion_smoke_flipbook_texture},
  });

  return context.particle_effects->registerEffectFiles({
      {"explosion_flash", resolveExampleAssetPath("particles/explosion_flash.kpeffect")},
      {"explosion_fireball", resolveExampleAssetPath("particles/explosion_fireball.kpeffect")},
      {"explosion_smoke", resolveExampleAssetPath("particles/explosion_smoke.kpeffect")},
      {"explosion_heat", resolveExampleAssetPath("particles/explosion_heat.kpeffect")},
      {"explosion_shock_ring", resolveExampleAssetPath("particles/explosion_shock_ring.kpeffect")},
      {"explosion_dust_ring", resolveExampleAssetPath("particles/explosion_dust_ring.kpeffect")},
      {"explosion_scorch", resolveExampleAssetPath("particles/explosion_scorch.kpeffect")},
      {"explosion_debris", resolveExampleAssetPath("particles/explosion_debris.kpeffect")},
      {"explosion_embers", resolveExampleAssetPath("particles/explosion_embers.kpeffect")},
      {"explosion_core_flipbook",
       resolveExampleAssetPath("particles/explosion_core_flipbook.kpeffect")},
      {"explosion_smoke_flipbook",
       resolveExampleAssetPath("particles/explosion_smoke_flipbook.kpeffect")},
  });
}

void cleanupExplosionPackage(const prefabs::PrefabPackageContext& context,
                             const std::shared_ptr<ExplosionPackageState>& state) {
  if (context.particle_effects != nullptr) {
    context.particle_effects->unregisterEffect("explosion_flash");
    context.particle_effects->unregisterEffect("explosion_fireball");
    context.particle_effects->unregisterEffect("explosion_smoke");
    context.particle_effects->unregisterEffect("explosion_heat");
    context.particle_effects->unregisterEffect("explosion_shock_ring");
    context.particle_effects->unregisterEffect("explosion_dust_ring");
    context.particle_effects->unregisterEffect("explosion_scorch");
    context.particle_effects->unregisterEffect("explosion_debris");
    context.particle_effects->unregisterEffect("explosion_embers");
    context.particle_effects->unregisterEffect("explosion_core_flipbook");
    context.particle_effects->unregisterEffect("explosion_smoke_flipbook");
    context.particle_effects->unregisterTextureAlias("spark_atlas");
    context.particle_effects->unregisterTextureAlias("glow_atlas");
    context.particle_effects->unregisterTextureAlias("smoke_atlas");
    context.particle_effects->unregisterTextureAlias("heat_atlas");
    context.particle_effects->unregisterTextureAlias("dust_ring_atlas");
    context.particle_effects->unregisterTextureAlias("shock_ring_atlas");
    context.particle_effects->unregisterTextureAlias("scorch_atlas");
    context.particle_effects->unregisterTextureAlias("debris_atlas");
    context.particle_effects->unregisterTextureAlias("explosion00_flipbook");
    context.particle_effects->unregisterTextureAlias("explosion01_smoke_flipbook");
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
  light.color = controller.light_peak_color;

  if (!controller.light_active) {
    light.intensity = 0.0f;
    light.range = controller.light_off_range;
    return;
  }

  const float duration =
      std::max(controller.light_end_time - controller.light_start_time, 0.001f);
  float t = saturate((time_seconds - controller.light_start_time) / duration);
  if (time_seconds >= controller.light_end_time) {
    controller.light_active = false;
    light.intensity = 0.0f;
    light.range = controller.light_off_range;
    return;
  }

  const float fade = 1.0f - smoothStep01(t);
  light.intensity = controller.light_peak_intensity * std::pow(fade, 2.0f);
  light.range = std::max(controller.light_off_range,
                         controller.light_peak_range *
                             (0.35f + 0.65f * std::pow(fade, 0.50f)));
}

}  // namespace

bool registerExplosionPrefabPackage(prefabs::PrefabRegistry& registry) {
  const auto state = std::make_shared<ExplosionPackageState>();
  return registry.registerPrefab(
      std::string(kExplosionPrefabKey),
      prefabs::RegisteredPrefabDesc{
          .prefab_path = resolveExampleAssetPath("prefabs/explosion"),
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
  queueRestart(controller, controller.smoke_flipbook, 0.08f, time_seconds);
  queueRestart(controller, controller.smoke, 0.10f, time_seconds);
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

}  // namespace karma::demo
