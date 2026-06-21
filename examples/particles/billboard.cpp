#include "demo_asset_paths.h"
#include "karma/karma.h"
#include "karma/assets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

struct ScheduledEffectRestart {
  world::Entity entity{};
  float trigger_time = 0.0f;
};

constexpr float kExplosionVisualScale = 1.5f;
constexpr int kExplosionFlipbookColumns = 5;
constexpr int kExplosionFlipbookRows = 5;
constexpr int kExplosionFlipbookFrameSize = 400;
constexpr int kExplosionFlipbookBorder = 4;
constexpr int kExplosionFlipbookSpacing = 4;
constexpr int kFastCoreExplosionFlipbookFrameSize = 128;
constexpr int kFastCoreExplosionFlipbookBorder = 2;
constexpr int kFastCoreExplosionFlipbookSpacing = 2;
inline constexpr std::string_view kExplosionPackageAssetRoot = "prefabs/explosion";
inline constexpr std::string_view kExplosionEffectFlash = "prefabs/explosion/flash";
inline constexpr std::string_view kExplosionEffectFireball =
    "prefabs/explosion/fireball";
inline constexpr std::string_view kExplosionEffectSmoke = "prefabs/explosion/smoke";
inline constexpr std::string_view kExplosionEffectHeat = "prefabs/explosion/heat";
inline constexpr std::string_view kExplosionEffectShockRing =
    "prefabs/explosion/shock_ring";
inline constexpr std::string_view kExplosionEffectDustRing =
    "prefabs/explosion/dust_ring";
inline constexpr std::string_view kExplosionEffectScorch = "prefabs/explosion/scorch";
inline constexpr std::string_view kExplosionEffectDebris = "prefabs/explosion/debris";
inline constexpr std::string_view kExplosionEffectEmbers = "prefabs/explosion/embers";
inline constexpr std::string_view kExplosionEffectCoreFlipbook =
    "prefabs/explosion/core_flipbook";
inline constexpr std::string_view kExplosionEffectSmokeFlipbook =
    "prefabs/explosion/smoke_flipbook";
inline constexpr std::string_view kExplosionTextureSpark =
    "prefabs/explosion/spark_atlas";
inline constexpr std::string_view kExplosionTextureGlow =
    "prefabs/explosion/glow_atlas";
inline constexpr std::string_view kExplosionTextureSmoke =
    "prefabs/explosion/smoke_atlas";
inline constexpr std::string_view kExplosionTextureHeat =
    "prefabs/explosion/heat_atlas";
inline constexpr std::string_view kExplosionTextureDustRing =
    "prefabs/explosion/dust_ring_atlas";
inline constexpr std::string_view kExplosionTextureShockRing =
    "prefabs/explosion/shock_ring_atlas";
inline constexpr std::string_view kExplosionTextureScorch =
    "prefabs/explosion/scorch_atlas";
inline constexpr std::string_view kExplosionTextureDebris =
    "prefabs/explosion/debris_atlas";
inline constexpr std::string_view kExplosionTextureCoreFlipbook =
    "prefabs/explosion/explosion00_flipbook";
inline constexpr std::string_view kExplosionTextureSmokeFlipbook =
    "prefabs/explosion/explosion01_smoke_flipbook";

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

int explosionFlipbookAtlasWidth(int frame_size = kExplosionFlipbookFrameSize,
                                int border = kExplosionFlipbookBorder,
                                int spacing = kExplosionFlipbookSpacing) {
  return kExplosionFlipbookColumns * frame_size +
         (kExplosionFlipbookColumns - 1) * spacing + border * 2;
}

int explosionFlipbookAtlasHeight(int frame_size = kExplosionFlipbookFrameSize,
                                 int border = kExplosionFlipbookBorder,
                                 int spacing = kExplosionFlipbookSpacing) {
  return kExplosionFlipbookRows * frame_size +
         (kExplosionFlipbookRows - 1) * spacing + border * 2;
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

world::Entity createParticleEffectEntity(world::World& world,
                                       std::string_view name,
                                       std::string_view effect_key,
                                       const components::TransformComponent& transform,
                                       bool playing) {
  return visual::particles::createEffectEntity(world,
                                       visual::particles::ParticleEffectEntityDesc{
                                           .name = name,
                                           .effect_key = effect_key,
                                           .transform = transform,
                                           .enabled = true,
                                           .playing = playing,
                                       });
}

void setEntityPositionIfAlive(world::World& world, world::Entity entity, const math::Vec3& position) {
  if (!world.isAlive(entity) || !world.has<components::TransformComponent>(entity)) {
    return;
  }
  world.get<components::TransformComponent>(entity).setPosition(position);
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0f));
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
  constexpr float kPi = 3.14159265f;
  const int atlas_width = explosionFlipbookAtlasWidth(frame_size, border, spacing);
  const int atlas_height = explosionFlipbookAtlasHeight(frame_size, border, spacing);
  std::vector<std::uint8_t> pixels(
      static_cast<size_t>(atlas_width) * static_cast<size_t>(atlas_height) * 4u, 0u);

  for (int frame = 0; frame < kExplosionFlipbookColumns * kExplosionFlipbookRows; ++frame) {
    const float t = static_cast<float>(frame) /
                    static_cast<float>(std::max(kExplosionFlipbookColumns *
                                                    kExplosionFlipbookRows - 1,
                                                1));
    const int column = frame % kExplosionFlipbookColumns;
    const int row = frame / kExplosionFlipbookColumns;
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
        const float plume = saturate(1.0f - radius * (1.35f + t * 0.50f));
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

bool registerTextureAsset(assets::AssetRegistry& assets,
                          std::string_view key,
                          int width,
                          int height,
                          const std::vector<std::uint8_t>& pixels) {
  if (pixels.empty()) {
    return false;
  }
  assets::TextureAsset texture{};
  texture.desc.width = width;
  texture.desc.height = height;
  texture.desc.format = rendering::TextureFormat::RGBA8;
  texture.bytes = pixels;
  return assets.registerTextureAsset(std::string(key), std::move(texture));
}

void registerParticleEffects(assets::AssetRegistry& assets,
                             bool has_explosion_flipbook_texture,
                             bool has_explosion_smoke_flipbook_texture,
                             bool use_fast_flipbook_effects) {
  auto import_package = [&](std::string_view path) {
    std::string diagnostic;
    if (!assets::importAssetPackage(assets, resolveExampleAssetPath(path), &diagnostic)) {
      spdlog::error("Failed to import particle package '{}': {}",
                    path,
                    diagnostic);
    }
  };

  import_package("particles/billboard_standalone");
  import_package("particles/billboard_explosion_base");
  if (has_explosion_flipbook_texture || has_explosion_smoke_flipbook_texture) {
    import_package(use_fast_flipbook_effects
                       ? "particles/billboard_explosion_flipbook_fast"
                       : "particles/billboard_explosion_flipbook");
  }
}

}  // namespace

class ParticleExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("toggle_particles", platform::Key::Space, app::Trigger::Pressed);
    input->bindKey("trigger_explosion", platform::Key::E, app::Trigger::Pressed);

    const std::string world_mesh = importExampleMeshAsset(assets, "world.glb");
    const std::string environment_map =
        registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");
    const int atlas_frame_size = 64;
    const int atlas_frames = 4;
    const bool use_fast_flipbook_effects = true;
    const int fallback_frame_size = kFastCoreExplosionFlipbookFrameSize;
    const int fallback_border = kFastCoreExplosionFlipbookBorder;
    const int fallback_spacing = kFastCoreExplosionFlipbookSpacing;
    spdlog::info("Particle example using fast procedural explosion flipbook");

    const std::vector<std::uint8_t> spark_atlas = buildSparkAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> glow_atlas = buildGlowAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> smoke_atlas = buildSmokeAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> heat_atlas = buildHeatAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> explosion_flipbook_atlas =
        buildExplosionCoreFlipbookAtlas(fallback_frame_size,
                                        fallback_border,
                                        fallback_spacing);
    spdlog::info("Particle example fast mode skips Explosion01 smoke flipbook");

    const std::vector<std::uint8_t> dust_ring_atlas =
        buildDustRingAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> shock_ring_atlas =
        buildShockRingAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> scorch_atlas =
        buildScorchAtlas(atlas_frame_size, atlas_frames);
    const std::vector<std::uint8_t> debris_atlas =
        buildDebrisAtlas(atlas_frame_size, atlas_frames);
    bool has_explosion_flipbook_texture = false;
    bool has_explosion_smoke_flipbook_texture = false;
    if (assets) {
      const int atlas_width = atlas_frame_size * atlas_frames;
      registerTextureAsset(*assets, "spark_atlas", atlas_width, atlas_frame_size, spark_atlas);
      registerTextureAsset(*assets, "glow_atlas", atlas_width, atlas_frame_size, glow_atlas);
      registerTextureAsset(*assets, "smoke_atlas", atlas_width, atlas_frame_size, smoke_atlas);
      registerTextureAsset(*assets, kExplosionTextureSpark, atlas_width, atlas_frame_size, spark_atlas);
      registerTextureAsset(*assets, kExplosionTextureGlow, atlas_width, atlas_frame_size, glow_atlas);
      registerTextureAsset(*assets, kExplosionTextureSmoke, atlas_width, atlas_frame_size, smoke_atlas);
      registerTextureAsset(*assets, "heat_atlas", atlas_width, atlas_frame_size, heat_atlas);
      registerTextureAsset(*assets, "dust_ring_atlas", atlas_width, atlas_frame_size, dust_ring_atlas);
      registerTextureAsset(*assets, "shock_ring_atlas", atlas_width, atlas_frame_size, shock_ring_atlas);
      registerTextureAsset(*assets, "scorch_atlas", atlas_width, atlas_frame_size, scorch_atlas);
      registerTextureAsset(*assets, "debris_atlas", atlas_width, atlas_frame_size, debris_atlas);
      registerTextureAsset(*assets, kExplosionTextureHeat, atlas_width, atlas_frame_size, heat_atlas);
      registerTextureAsset(*assets, kExplosionTextureDustRing, atlas_width, atlas_frame_size, dust_ring_atlas);
      registerTextureAsset(*assets, kExplosionTextureShockRing, atlas_width, atlas_frame_size, shock_ring_atlas);
      registerTextureAsset(*assets, kExplosionTextureScorch, atlas_width, atlas_frame_size, scorch_atlas);
      registerTextureAsset(*assets, kExplosionTextureDebris, atlas_width, atlas_frame_size, debris_atlas);
      has_explosion_flipbook_texture =
          registerTextureAsset(*assets,
                               kExplosionTextureCoreFlipbook,
                               explosionFlipbookAtlasWidth(fallback_frame_size,
                                                           fallback_border,
                                                           fallback_spacing),
                               explosionFlipbookAtlasHeight(fallback_frame_size,
                                                            fallback_border,
                                                            fallback_spacing),
                               explosion_flipbook_atlas);
      registerParticleEffects(*assets,
                              has_explosion_flipbook_texture,
                              has_explosion_smoke_flipbook_texture,
                              use_fast_flipbook_effects);
    }

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_asset_key = world_mesh});

    auto environment_entity = world->createEntity();
    world->setName(environment_entity, "Environment");
    world->add(environment_entity, components::EnvironmentComponent{
        .environment_map_asset_key = environment_map,
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

    if (assets) {
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
          kExplosionEffectFlash,
          makeTransform({3.2f, 0.8f, 0.0f}),
          false);
      explosion_fireball_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Fireball",
          kExplosionEffectFireball,
          makeTransform({3.2f, 0.75f, 0.0f}),
          false);
      explosion_heat_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Heat",
          kExplosionEffectHeat,
          makeTransform({3.2f, 0.82f, 0.0f}),
          false);
      if (has_explosion_flipbook_texture) {
        explosion_core_flipbook_entity_ = createParticleEffectEntity(
            *world,
            "Explosion Core Flipbook",
            kExplosionEffectCoreFlipbook,
            makeTransform({3.2f, 0.88f, 0.0f}),
            false);
      }
      if (has_explosion_smoke_flipbook_texture) {
        explosion_smoke_flipbook_entity_ = createParticleEffectEntity(
            *world,
            "Explosion Smoke Flipbook",
            kExplosionEffectSmokeFlipbook,
            makeTransform({3.2f, 0.92f, 0.0f}),
            false);
      }
      explosion_embers_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Embers",
          kExplosionEffectEmbers,
          makeTransform({3.2f, 0.90f, 0.0f}),
          false);
      explosion_shock_ring_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Shock Ring",
          kExplosionEffectShockRing,
          makeTransform({3.2f, 0.09f, 0.0f}),
          false);
      explosion_debris_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Debris",
          kExplosionEffectDebris,
          makeTransform({3.2f, 0.86f, 0.0f}),
          false);
      explosion_dust_ring_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Dust Ring",
          kExplosionEffectDustRing,
          makeTransform({3.2f, 0.04f, 0.0f}),
          false);
      explosion_smoke_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Smoke",
          kExplosionEffectSmoke,
          makeTransform({3.2f, 0.85f, 0.0f}),
          false);
      explosion_scorch_entity_ = createParticleEffectEntity(
          *world,
          "Explosion Scorch",
          kExplosionEffectScorch,
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
      visual::particles::setEffectPlaying(*world, fountain_entity_, particles_enabled_);
      visual::particles::setEffectPlaying(*world, glow_entity_, particles_enabled_);
      visual::particles::setEffectPlaying(*world, smoke_entity_, particles_enabled_);
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
    if (assets != nullptr) {
      const std::array<std::string_view, 17> texture_keys{
          "spark_atlas",
          "glow_atlas",
          "smoke_atlas",
          kExplosionTextureSpark,
          kExplosionTextureGlow,
          kExplosionTextureSmoke,
          "heat_atlas",
          "dust_ring_atlas",
          "shock_ring_atlas",
          "scorch_atlas",
          "debris_atlas",
          kExplosionTextureHeat,
          kExplosionTextureDustRing,
          kExplosionTextureShockRing,
          kExplosionTextureScorch,
          kExplosionTextureDebris,
          kExplosionTextureCoreFlipbook,
      };
      for (std::string_view key : texture_keys) {
        assets->unregisterTextureAsset(std::string(key));
      }
      assets->unregisterTextureAsset(std::string(kExplosionTextureSmokeFlipbook));
    }
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

  void queueEffectRestart(world::Entity entity, float delay_seconds) {
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

  void restartEffectEntity(world::Entity entity) {
    visual::particles::restartEffect(*world, entity);
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
      queueEffectRestart(explosion_smoke_flipbook_entity_, 0.22f);
    }
    queueEffectRestart(explosion_smoke_entity_, 0.24f);
    queueEffectRestart(explosion_scorch_entity_, 0.12f);
  }

  world::Entity fountain_entity_{};
  world::Entity glow_entity_{};
  world::Entity smoke_entity_{};
  world::Entity explosion_flash_entity_{};
  world::Entity explosion_fireball_entity_{};
  world::Entity explosion_heat_entity_{};
  world::Entity explosion_core_flipbook_entity_{};
  world::Entity explosion_smoke_flipbook_entity_{};
  world::Entity explosion_embers_entity_{};
  world::Entity explosion_shock_ring_entity_{};
  world::Entity explosion_debris_entity_{};
  world::Entity explosion_dust_ring_entity_{};
  world::Entity explosion_smoke_entity_{};
  world::Entity explosion_scorch_entity_{};
  world::Entity explosion_light_entity_{};
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
