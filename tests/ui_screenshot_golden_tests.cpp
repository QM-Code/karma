#include "karma/assets.h"
#include "karma/ui.h"
#include "features/ui/native/font_face.h"
#include "features/ui/native/paint_engine.h"
#include "features/ui/native/svg_rasterizer.h"
#include "features/ui/native/text_engine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::detail {

struct SystemTestAccess {
  static void buildFrame(System& system,
                         float dt,
                         int logical_width,
                         int logical_height,
                         int framebuffer_width,
                         int framebuffer_height,
                         float scale_x,
                         float scale_y,
                         rendering::UIDrawData& output) {
    system.buildFrame(dt, logical_width, logical_height, framebuffer_width,
                      framebuffer_height, scale_x, scale_y, output);
  }
};

}  // namespace karma::ui::detail

namespace {

using karma::rendering::UIDrawCmd;
using karma::rendering::UIDrawData;
using karma::rendering::UIVertex;

constexpr int kLogicalWidth = 1280;
constexpr int kLogicalHeight = 720;

struct Rgba {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 0;
};

struct Canvas {
  int width = 0;
  int height = 0;
  std::vector<Rgba> pixels;
};

struct ScreenshotResult {
  Canvas canvas;
  std::uint64_t hash = 0;
  std::size_t vertices = 0;
  std::size_t indices = 0;
  std::size_t commands = 0;
  std::size_t semantic_nodes = 0;
  std::size_t shaped_glyphs = 0;
  std::size_t glyph_bitmap_pixels = 0;
  std::size_t svg_bitmap_pixels = 0;
  std::size_t right_to_left_runs = 0;
  float panel_x = 0.0f;
};

struct GoldenCase {
  std::string_view name;
  float scale;
  bool right_to_left;
  std::uint64_t expected_hash;
};

struct NineSliceGoldenCase {
  std::string_view name;
  float scale;
  std::uint64_t expected_hash;
};

// These hashes cover the final, CPU-rasterized RGBA framebuffer. System supplies
// retained layout and untextured paint geometry; the same native TextEngine and
// SvgRasterizer used by the GPU path supply deterministic packaged-asset pixels.
constexpr std::array<GoldenCase, 4> kGoldenCases{{
    {"native_menu_1x", 1.0f, false, 0x4671783eb1901fcbULL},
    {"native_menu_1_5x", 1.5f, false, 0xbfb606a7ba328048ULL},
    {"native_menu_2x", 2.0f, false, 0x440a4701282fa667ULL},
    {"native_menu_rtl_1x", 1.0f, true, 0x460c81884cc3e389ULL},
}};

// These goldens rasterize a high-contrast synthetic atlas through the exact
// repeated nine-slice mesh used by native UI. Unlike geometry-only assertions,
// the final pixels cover UV cropping on the last `repeat` tile, complete
// `round` motifs, and shared physical-pixel endpoints at fractional DPI scales.
constexpr std::array<NineSliceGoldenCase, 3> kNineSliceGoldenCases{{
    {"nine_slice_repeat_round_1x", 1.0f, 0x05fc596eae693735ULL},
    {"nine_slice_repeat_round_1_5x", 1.5f, 0x0a28f50104f49e49ULL},
    {"nine_slice_repeat_round_2x", 2.0f, 0xe471f993f0e1297cULL},
}};

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string message) {
  if (!condition) fail(std::move(message));
}

Rgba unpack(std::uint32_t rgba) {
  return {.red = static_cast<std::uint8_t>(rgba & 0xffu),
          .green = static_cast<std::uint8_t>((rgba >> 8u) & 0xffu),
          .blue = static_cast<std::uint8_t>((rgba >> 16u) & 0xffu),
          .alpha = static_cast<std::uint8_t>((rgba >> 24u) & 0xffu)};
}

double edge(const UIVertex& start, const UIVertex& end, double x, double y) {
  return (static_cast<double>(end.x) - start.x) * (y - start.y) -
         (static_cast<double>(end.y) - start.y) * (x - start.x);
}

bool isTopLeft(const UIVertex& start, const UIVertex& end) {
  const double dy = static_cast<double>(end.y) - start.y;
  const double dx = static_cast<double>(end.x) - start.x;
  return dy < 0.0 || (dy == 0.0 && dx > 0.0);
}

bool insideEdge(double value, bool top_left) {
  constexpr double epsilon = 1.0e-9;
  return value > epsilon || (std::abs(value) <= epsilon && top_left);
}

void blendPixel(Rgba source,
                karma::rendering::UIBlendMode blend_mode,
                Rgba& destination) {
  const float source_alpha = static_cast<float>(source.alpha) / 255.0f;
  const float inverse_alpha = 1.0f - source_alpha;
  auto blend_channel = [&](std::uint8_t source_channel,
                           std::uint8_t destination_channel) {
    const float source_value = static_cast<float>(source_channel);
    const float result = blend_mode == karma::rendering::UIBlendMode::PremultipliedAlpha
                             ? source_value + destination_channel * inverse_alpha
                             : source_value * source_alpha +
                                   destination_channel * inverse_alpha;
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(result), 0l, 255l));
  };
  destination.red = blend_channel(source.red, destination.red);
  destination.green = blend_channel(source.green, destination.green);
  destination.blue = blend_channel(source.blue, destination.blue);
  destination.alpha = static_cast<std::uint8_t>(std::clamp(
      std::lround(source.alpha + destination.alpha * inverse_alpha), 0l, 255l));
}

struct PixelBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct NativeContentStats {
  std::size_t shaped_glyphs = 0;
  std::size_t glyph_bitmap_pixels = 0;
  std::size_t svg_bitmap_pixels = 0;
  std::size_t right_to_left_runs = 0;
};

PixelBounds physicalBounds(const karma::ui::AccessibilityBounds& bounds,
                           float scale,
                           const Canvas& canvas) {
  return {
      .left = std::clamp(static_cast<int>(std::floor(bounds.x * scale)), 0,
                         canvas.width),
      .top = std::clamp(static_cast<int>(std::floor(bounds.y * scale)), 0,
                        canvas.height),
      .right = std::clamp(
          static_cast<int>(std::ceil((bounds.x + bounds.width) * scale)), 0,
          canvas.width),
      .bottom = std::clamp(
          static_cast<int>(std::ceil((bounds.y + bounds.height) * scale)), 0,
          canvas.height),
  };
}

void compositePixel(Canvas& canvas,
                    const PixelBounds& clip,
                    int x,
                    int y,
                    Rgba source) {
  if (source.alpha == 0u || x < clip.left || x >= clip.right || y < clip.top ||
      y >= clip.bottom) {
    return;
  }
  blendPixel(source, karma::rendering::UIBlendMode::StraightAlpha,
             canvas.pixels[static_cast<std::size_t>(y) * canvas.width + x]);
}

bool isTextBearingRole(karma::ui::AccessibilityRole role) {
  return role == karma::ui::AccessibilityRole::Text ||
         role == karma::ui::AccessibilityRole::Button ||
         role == karma::ui::AccessibilityRole::Option;
}

Rgba pilotTextColor(const karma::ui::AccessibilityNode& node,
                    std::string_view title,
                    std::string_view status) {
  if (node.name == title) return {0xffu, 0xffu, 0xffu, 0xffu};
  if (node.name == status) return {0xaau, 0xb9u, 0xd4u, 0xffu};
  return {0xf4u, 0xf7u, 0xffu, 0xffu};
}

void compositeGlyphBitmap(Canvas& canvas,
                          const PixelBounds& clip,
                          int origin_x,
                          int origin_y,
                          const karma::ui::native::GlyphBitmap& bitmap,
                          Rgba text_color,
                          NativeContentStats& stats) {
  for (std::uint32_t y = 0u; y < bitmap.height; ++y) {
    for (std::uint32_t x = 0u; x < bitmap.width; ++x) {
      const std::size_t offset = static_cast<std::size_t>(y) * bitmap.stride;
      Rgba source = text_color;
      if (bitmap.format == karma::ui::native::GlyphPixelFormat::R8) {
        const std::uint8_t coverage = bitmap.pixels[offset + x];
        source.alpha = static_cast<std::uint8_t>(
            static_cast<unsigned>(text_color.alpha) * coverage / 255u);
      } else {
        const std::size_t pixel = offset + static_cast<std::size_t>(x) * 4u;
        source = {.red = bitmap.pixels[pixel + 0u],
                  .green = bitmap.pixels[pixel + 1u],
                  .blue = bitmap.pixels[pixel + 2u],
                  .alpha = bitmap.pixels[pixel + 3u]};
      }
      if (source.alpha != 0u) ++stats.glyph_bitmap_pixels;
      compositePixel(canvas, clip, origin_x + static_cast<int>(x),
                     origin_y + static_cast<int>(y), source);
    }
  }
}

NativeContentStats compositeNativeContent(
    Canvas& canvas,
    const karma::ui::AccessibilityTree& tree,
    const karma::assets::AssetRegistry& assets,
    float scale,
    bool right_to_left,
    std::string_view title,
    std::string_view status) {
  using namespace karma::ui::native;

  const karma::assets::FontAsset* font = assets.findFontAsset("ui/demo/font");
  require(font != nullptr, "native pilot font asset was not imported");
  const karma::assets::SvgAsset* svg = assets.findSvgAsset("ui/demo/icon");
  require(svg != nullptr, "native pilot SVG asset was not imported");

  TextEngine text_engine;
  const std::string font_key = fontRegistrationKey("ui/demo/font", 0u);
  std::string error;
  require(text_engine.registerFont(font_key, *font, 0u, &error).has_value(),
          "failed to register packaged pilot font: " + error);
  SvgRasterizer svg_rasterizer;
  NativeContentStats stats;

  for (const karma::ui::AccessibilityNode& node : tree.nodes) {
    const PixelBounds clip = physicalBounds(node.bounds, scale, canvas);
    if (clip.right <= clip.left || clip.bottom <= clip.top) continue;

    if (node.role == karma::ui::AccessibilityRole::Image) {
      const std::uint32_t width = static_cast<std::uint32_t>(std::max(
          1l, std::lround(std::max(0.0f, node.bounds.width) * scale)));
      const std::uint32_t height = static_cast<std::uint32_t>(std::max(
          1l, std::lround(std::max(0.0f, node.bounds.height) * scale)));
      const auto raster = svg_rasterizer.rasterize(
          "ui/demo/icon", assets.version(), *svg,
          {.physical_width = width,
           .physical_height = height,
           .dpi_scale_x = scale,
           .dpi_scale_y = scale,
           .tint = SvgTint{.red = 0xf4u,
                           .green = 0xf7u,
                           .blue = 0xffu,
                           .alpha = 0xffu}},
          &error);
      require(raster != nullptr, "failed to rasterize packaged pilot SVG: " + error);
      const int origin_x = static_cast<int>(std::lround(node.bounds.x * scale));
      const int origin_y = static_cast<int>(std::lround(node.bounds.y * scale));
      for (std::uint32_t y = 0u; y < raster->height; ++y) {
        for (std::uint32_t x = 0u; x < raster->width; ++x) {
          const std::size_t offset = static_cast<std::size_t>(y) * raster->stride +
                                     static_cast<std::size_t>(x) * 4u;
          const Rgba source{.red = raster->pixels[offset + 0u],
                            .green = raster->pixels[offset + 1u],
                            .blue = raster->pixels[offset + 2u],
                            .alpha = raster->pixels[offset + 3u]};
          if (source.alpha != 0u) ++stats.svg_bitmap_pixels;
          compositePixel(canvas, clip, origin_x + static_cast<int>(x),
                         origin_y + static_cast<int>(y), source);
        }
      }
      continue;
    }

    if (!isTextBearingRole(node.role) || node.name.empty()) continue;
    const float font_size = node.name == title ? 32.0f : 18.0f;
    const float padding = node.role == karma::ui::AccessibilityRole::Button
                              ? 10.0f
                              : 0.0f;
    const float available_width =
        std::max(0.0f, node.bounds.width - padding * 2.0f);
    ShapeRequest request{.text_utf8 = node.name,
                         .font_keys = {font_key},
                         .locale = right_to_left ? "ar" : "en",
                         .pixel_size = font_size,
                         .max_width = available_width,
                         .line_height = font_size * 1.2f,
                         .direction = right_to_left
                                          ? TextDirection::RightToLeft
                                          : TextDirection::Auto};
    const auto shaped = text_engine.shape(request, &error);
    require(shaped.has_value(), "failed to shape pilot text: " + error);
    const float content_x = node.bounds.x + padding;
    const float content_y = node.bounds.y + padding;
    for (const ShapedLine& line : shaped->lines) {
      const bool line_rtl = right_to_left ||
                            (!line.runs.empty() &&
                             line.runs.front().direction ==
                                 TextDirection::RightToLeft);
      const float line_offset =
          line_rtl ? std::max(0.0f, available_width - line.width) : 0.0f;
      for (const ShapedRun& run : line.runs) {
        if (run.direction == TextDirection::RightToLeft) {
          ++stats.right_to_left_runs;
        }
        for (const ShapedGlyph& glyph : run.glyphs) {
          ++stats.shaped_glyphs;
          const auto bitmap = text_engine.rasterize(glyph, font_size * scale, &error);
          require(bitmap.has_value(), "failed to rasterize shaped pilot glyph: " + error);
          if (bitmap->width == 0u || bitmap->height == 0u) continue;
          const int x = static_cast<int>(std::lround(
                            (content_x + line_offset + glyph.x + glyph.x_offset) *
                            scale)) +
                        bitmap->bearing_x;
          const int y = static_cast<int>(std::lround(
                            (content_y + glyph.y + glyph.y_offset) * scale)) -
                        bitmap->bearing_y;
          compositeGlyphBitmap(canvas, clip, x, y, *bitmap,
                               pilotTextColor(node, title, status), stats);
        }
      }
    }
  }
  return stats;
}

void rasterizeTriangle(const UIDrawCmd& command,
                       UIVertex first,
                       UIVertex second,
                       UIVertex third,
                       Canvas& canvas) {
  require(command.texture == 0u,
          "device-less System unexpectedly emitted a GPU-textured command");
  double area = edge(first, second, third.x, third.y);
  if (!std::isfinite(area) || std::abs(area) <= 1.0e-12) return;
  if (area < 0.0) {
    std::swap(second, third);
    area = -area;
  }

  int left = std::max(0, static_cast<int>(std::floor(
                             std::min({first.x, second.x, third.x}))));
  int top = std::max(0, static_cast<int>(std::floor(
                            std::min({first.y, second.y, third.y}))));
  int right = std::min(canvas.width, static_cast<int>(std::ceil(
                                         std::max({first.x, second.x, third.x}))));
  int bottom = std::min(canvas.height, static_cast<int>(std::ceil(
                                           std::max({first.y, second.y, third.y}))));
  if (command.scissor_enabled) {
    left = std::max(left, command.scissor_x);
    top = std::max(top, command.scissor_y);
    right = std::min(right, command.scissor_x + command.scissor_w);
    bottom = std::min(bottom, command.scissor_y + command.scissor_h);
  }
  if (right <= left || bottom <= top) return;

  const Rgba first_color = unpack(first.rgba);
  const Rgba second_color = unpack(second.rgba);
  const Rgba third_color = unpack(third.rgba);
  const bool edge_first_top_left = isTopLeft(second, third);
  const bool edge_second_top_left = isTopLeft(third, first);
  const bool edge_third_top_left = isTopLeft(first, second);
  for (int y = top; y < bottom; ++y) {
    const double sample_y = static_cast<double>(y) + 0.5;
    for (int x = left; x < right; ++x) {
      const double sample_x = static_cast<double>(x) + 0.5;
      const double first_weight = edge(second, third, sample_x, sample_y);
      const double second_weight = edge(third, first, sample_x, sample_y);
      const double third_weight = edge(first, second, sample_x, sample_y);
      if (!insideEdge(first_weight, edge_first_top_left) ||
          !insideEdge(second_weight, edge_second_top_left) ||
          !insideEdge(third_weight, edge_third_top_left)) {
        continue;
      }
      const double inverse_area = 1.0 / area;
      auto interpolate = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround((first_weight * a + second_weight * b + third_weight * c) *
                        inverse_area),
            0l, 255l));
      };
      Rgba source{.red = interpolate(first_color.red, second_color.red,
                                     third_color.red),
                  .green = interpolate(first_color.green, second_color.green,
                                       third_color.green),
                  .blue = interpolate(first_color.blue, second_color.blue,
                                      third_color.blue),
                  .alpha = interpolate(first_color.alpha, second_color.alpha,
                                       third_color.alpha)};

      blendPixel(source, command.blend_mode,
                 canvas.pixels[static_cast<std::size_t>(y) * canvas.width + x]);
    }
  }
}

Canvas rasterize(const UIDrawData& draw_data, int width, int height) {
  require(width > 0 && height > 0, "invalid screenshot dimensions");
  Canvas canvas{.width = width,
                .height = height,
                .pixels = std::vector<Rgba>(
                    static_cast<std::size_t>(width) * height,
                    Rgba{0x05u, 0x08u, 0x0eu, 0xffu})};
  for (const UIDrawCmd& command : draw_data.commands) {
    const std::size_t begin = command.index_offset;
    const std::size_t end = begin + command.index_count;
    require(end <= draw_data.indices.size() && command.index_count % 3u == 0u,
            "invalid command reached CPU UI rasterizer");
    for (std::size_t offset = begin; offset < end; offset += 3u) {
      const std::uint32_t first = draw_data.indices[offset];
      const std::uint32_t second = draw_data.indices[offset + 1u];
      const std::uint32_t third = draw_data.indices[offset + 2u];
      require(first < draw_data.vertices.size() && second < draw_data.vertices.size() &&
                  third < draw_data.vertices.size(),
              "invalid index reached CPU UI rasterizer");
      rasterizeTriangle(command, draw_data.vertices[first],
                        draw_data.vertices[second], draw_data.vertices[third], canvas);
    }
  }
  return canvas;
}

struct NineSliceRasterVertex {
  double x = 0.0;
  double y = 0.0;
  double u = 0.0;
  double v = 0.0;
};

double edge(const NineSliceRasterVertex& start,
            const NineSliceRasterVertex& end,
            double x,
            double y) {
  return (end.x - start.x) * (y - start.y) -
         (end.y - start.y) * (x - start.x);
}

bool isTopLeft(const NineSliceRasterVertex& start,
               const NineSliceRasterVertex& end) {
  const double dy = end.y - start.y;
  const double dx = end.x - start.x;
  return dy < 0.0 || (dy == 0.0 && dx > 0.0);
}

Rgba sampleNineSliceAtlas(double u, double v) {
  constexpr int width = 31;
  constexpr int height = 29;
  const int x = std::clamp(static_cast<int>(std::floor(u * width)), 0,
                           width - 1);
  const int y = std::clamp(static_cast<int>(std::floor(v * height)), 0,
                           height - 1);

  // Keep every region and every source texel visually distinct.  In
  // particular, the center bands expose a mistakenly stretched motif and the
  // x/y ramps expose an incorrectly cropped final repeat tile.
  const int column = x < 5 ? 0 : (x >= 27 ? 2 : 1);
  const int row = y < 7 ? 0 : (y >= 23 ? 2 : 1);
  const int region = row * 3 + column;
  const int checker = ((x / 2) ^ (y / 2)) & 1;
  return {
      .red = static_cast<std::uint8_t>(
          24 + ((region * 37 + x * 11 + checker * 53) % 208)),
      .green = static_cast<std::uint8_t>(
          20 + ((region * 61 + y * 17 + checker * 29) % 212)),
      .blue = static_cast<std::uint8_t>(
          28 + ((region * 23 + x * 7 + y * 13 + checker * 71) % 204)),
      .alpha = 0xffu};
}

void rasterizeNineSliceTriangle(const NineSliceRasterVertex& authored_first,
                                const NineSliceRasterVertex& authored_second,
                                const NineSliceRasterVertex& authored_third,
                                Canvas& canvas,
                                std::vector<std::uint16_t>& coverage) {
  NineSliceRasterVertex first = authored_first;
  NineSliceRasterVertex second = authored_second;
  NineSliceRasterVertex third = authored_third;
  double area = edge(first, second, third.x, third.y);
  if (!std::isfinite(area) || std::abs(area) <= 1.0e-12) return;
  if (area < 0.0) {
    std::swap(second, third);
    area = -area;
  }

  const int left = std::max(
      0, static_cast<int>(std::floor(std::min({first.x, second.x, third.x}))));
  const int top = std::max(
      0, static_cast<int>(std::floor(std::min({first.y, second.y, third.y}))));
  const int right = std::min(
      canvas.width,
      static_cast<int>(std::ceil(std::max({first.x, second.x, third.x}))));
  const int bottom = std::min(
      canvas.height,
      static_cast<int>(std::ceil(std::max({first.y, second.y, third.y}))));
  const bool first_top_left = isTopLeft(second, third);
  const bool second_top_left = isTopLeft(third, first);
  const bool third_top_left = isTopLeft(first, second);
  const double inverse_area = 1.0 / area;
  for (int y = top; y < bottom; ++y) {
    const double sample_y = static_cast<double>(y) + 0.5;
    for (int x = left; x < right; ++x) {
      const double sample_x = static_cast<double>(x) + 0.5;
      const double first_weight = edge(second, third, sample_x, sample_y);
      const double second_weight = edge(third, first, sample_x, sample_y);
      const double third_weight = edge(first, second, sample_x, sample_y);
      if (!insideEdge(first_weight, first_top_left) ||
          !insideEdge(second_weight, second_top_left) ||
          !insideEdge(third_weight, third_top_left)) {
        continue;
      }
      const double u = (first_weight * first.u + second_weight * second.u +
                        third_weight * third.u) *
                       inverse_area;
      const double v = (first_weight * first.v + second_weight * second.v +
                        third_weight * third.v) *
                       inverse_area;
      const std::size_t offset =
          static_cast<std::size_t>(y) * canvas.width + x;
      ++coverage[offset];
      canvas.pixels[offset] = sampleNineSliceAtlas(u, v);
    }
  }
}

struct NineSliceScreenshotResult {
  Canvas canvas;
  std::uint64_t hash = 0;
  std::size_t cells = 0;
};

std::uint64_t hashCanvas(const Canvas& canvas);

NineSliceScreenshotResult renderNineSliceGolden(float scale) {
  using namespace karma::ui::paint;
  require(scale > 0.0f, "nine-slice golden scale must be positive");

  constexpr Rect destination{3.25f, 2.75f, 63.5f, 47.5f};
  NineSliceBuildStatus status;
  const Mesh mesh = nineSlicePanel(
      destination,
      {.source_size = {31.0f, 29.0f},
       .source_slices = {5.0f, 7.0f, 4.0f, 6.0f},
       .destination_slices = Insets{7.0f, 6.0f, 8.0f, 5.0f},
       .repeat = {NineSliceRepeatMode::Repeat, NineSliceRepeatMode::Round},
       .pixel_scale = {scale, scale}},
      {}, &status);
  require(mesh.valid() && status.generated_cells > 9u,
          "tiled nine-slice golden produced no repeated cells");
  require(!status.cell_limit_reduced,
          "small nine-slice golden unexpectedly reached the cell limit");

  auto physical = [scale](const Vertex& vertex) {
    const double raw_x = static_cast<double>(vertex.position.x) * scale;
    const double raw_y = static_cast<double>(vertex.position.y) * scale;
    require(std::abs(raw_x - std::round(raw_x)) < 1.0e-4 &&
                std::abs(raw_y - std::round(raw_y)) < 1.0e-4,
            "nine-slice endpoint was not snapped to a framebuffer pixel");
    return NineSliceRasterVertex{.x = std::round(raw_x),
                                 .y = std::round(raw_y),
                                 .u = vertex.uv.x,
                                 .v = vertex.uv.y};
  };

  int outer_left = std::numeric_limits<int>::max();
  int outer_top = std::numeric_limits<int>::max();
  int outer_right = std::numeric_limits<int>::min();
  int outer_bottom = std::numeric_limits<int>::min();
  for (const Vertex& vertex : mesh.vertices) {
    const NineSliceRasterVertex transformed = physical(vertex);
    outer_left = std::min(outer_left, static_cast<int>(transformed.x));
    outer_top = std::min(outer_top, static_cast<int>(transformed.y));
    outer_right = std::max(outer_right, static_cast<int>(transformed.x));
    outer_bottom = std::max(outer_bottom, static_cast<int>(transformed.y));
  }
  constexpr int padding = 4;
  Canvas canvas{.width = outer_right + padding,
                .height = outer_bottom + padding,
                .pixels = std::vector<Rgba>(
                    static_cast<std::size_t>(outer_right + padding) *
                        (outer_bottom + padding),
                    Rgba{0xf1u, 0x0du, 0xe5u, 0xffu})};
  std::vector<std::uint16_t> coverage(canvas.pixels.size(), 0u);
  for (std::size_t index = 0u; index < mesh.indices.size(); index += 3u) {
    rasterizeNineSliceTriangle(physical(mesh.vertices[mesh.indices[index]]),
                               physical(mesh.vertices[mesh.indices[index + 1u]]),
                               physical(mesh.vertices[mesh.indices[index + 2u]]),
                               canvas, coverage);
  }

  for (int y = 0; y < canvas.height; ++y) {
    for (int x = 0; x < canvas.width; ++x) {
      const bool inside = x >= outer_left && x < outer_right &&
                          y >= outer_top && y < outer_bottom;
      const std::size_t offset =
          static_cast<std::size_t>(y) * canvas.width + x;
      require(coverage[offset] == (inside ? 1u : 0u),
              "tiled nine-slice raster contains a seam or overlapping cell");
    }
  }

  const std::uint64_t hash = hashCanvas(canvas);
  return {.canvas = std::move(canvas),
          .hash = hash,
          .cells = status.generated_cells};
}

std::uint64_t hashCanvas(const Canvas& canvas) {
  std::uint64_t hash = 14695981039346656037ULL;
  auto add = [&](std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  for (int shift = 0; shift < 32; shift += 8) {
    add(static_cast<std::uint8_t>(static_cast<std::uint32_t>(canvas.width) >> shift));
    add(static_cast<std::uint8_t>(static_cast<std::uint32_t>(canvas.height) >> shift));
  }
  for (const Rgba& pixel : canvas.pixels) {
    add(pixel.red);
    add(pixel.green);
    add(pixel.blue);
    add(pixel.alpha);
  }
  return hash;
}

bool writePpm(const std::filesystem::path& path, const Canvas& canvas) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream) return false;
  stream << "P6\n" << canvas.width << ' ' << canvas.height << "\n255\n";
  for (const Rgba& pixel : canvas.pixels) {
    const std::array<char, 3> rgb{static_cast<char>(pixel.red),
                                  static_cast<char>(pixel.green),
                                  static_cast<char>(pixel.blue)};
    stream.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
  }
  return static_cast<bool>(stream);
}

ScreenshotResult renderPilot(const GoldenCase& golden) {
  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  const auto package = karma::assets::importAssetPackage(
      assets, std::filesystem::path(KARMA_UI_GOLDEN_ASSET_DIR), &diagnostic);
  require(package.has_value(), "failed to import native pilot package: " + diagnostic);

  if (golden.right_to_left) {
    const auto* source = assets.findUiThemeAsset("ui/demo/theme");
    require(source != nullptr, "native pilot theme was not imported");
    karma::assets::UiThemeAsset rtl = *source;
    nlohmann::json theme = nlohmann::json::parse(rtl.canonical_json_utf8);
    theme["defaults"]["panel"]["appearance"]["text"]["direction"] = "rtl";
    theme["defaults"]["text"]["appearance"]["text"]["direction"] = "rtl";
    theme["defaults"]["text"]["appearance"]["text"]["align"] = "start";
    theme["defaults"]["button"]["appearance"]["text"]["direction"] = "rtl";
    theme["defaults"]["button"]["appearance"]["text"]["align"] = "start";
    rtl.canonical_json_utf8 = theme.dump();
    require(assets.registerUiThemeAsset("ui/demo/theme", std::move(rtl)),
            "failed to register RTL pilot theme");
  }

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  config.motion_scale = 0.0f;
  config.locale = golden.right_to_left ? "ar" : "en";
  karma::ui::System system(assets, nullptr, config);
  const auto opened = system.open("ui/demo/main_menu",
                                  {.layer = 100, .visible = true, .modal = true});
  require(static_cast<bool>(opened), "failed to open native pilot JSON document");
  require(opened.diagnostics.empty(),
          "native pilot JSON document opened with diagnostics");

  const std::string title = golden.right_to_left ? "واجهة كارما" : "Karma Native UI";
  const std::string status = golden.right_to_left ? "جاهز" : "Ready";
  require(system.set(opened.document, "title", title), "failed to bind title");
  require(system.set(opened.document, "status", status), "failed to bind status");
  require(system.set(opened.document, "settings.volume", 0.75),
          "failed to bind volume");
  require(system.set(opened.document, "settings.fullscreen", false),
          "failed to bind fullscreen");
  karma::ui::Value::Array saves;
  if (golden.right_to_left) {
    saves.emplace_back(karma::ui::Value::Object{{"id", 1}, {"name", "ميناء السماء"}});
    saves.emplace_back(karma::ui::Value::Object{{"id", 2}, {"name", "مختبر القيود"}});
  } else {
    saves.emplace_back(karma::ui::Value::Object{{"id", 1}, {"name", "Sky Harbor"}});
    saves.emplace_back(
        karma::ui::Value::Object{{"id", 2}, {"name", "Constraint Lab"}});
  }
  require(system.set(opened.document, "saves", std::move(saves)),
          "failed to bind save slots");

  const int framebuffer_width = static_cast<int>(std::lround(kLogicalWidth * golden.scale));
  const int framebuffer_height = static_cast<int>(std::lround(kLogicalHeight * golden.scale));
  UIDrawData draw_data;
  karma::ui::detail::SystemTestAccess::buildFrame(
      system, 0.0f, kLogicalWidth, kLogicalHeight, framebuffer_width,
      framebuffer_height, golden.scale, golden.scale, draw_data);
  require(karma::rendering::validateUIDrawData(draw_data),
          "native pilot produced invalid UI draw data");

  const auto& accessibility = system.accessibilityTree();
  require(!accessibility.roots.empty() && accessibility.nodes.size() >= 10u,
          "native pilot accessibility tree is incomplete");
  float panel_x = -1.0f;
  for (const auto& node : accessibility.nodes) {
    if (node.role == karma::ui::AccessibilityRole::Group &&
        std::abs(node.bounds.width - 520.0f) < 1.0f &&
        std::abs(node.bounds.height - 570.0f) < 1.0f) {
      panel_x = node.bounds.x;
      break;
    }
  }
  require(panel_x >= 0.0f, "native pilot panel was not laid out");

  const std::size_t vertices = draw_data.vertices.size();
  const std::size_t indices = draw_data.indices.size();
  const std::size_t commands = draw_data.commands.size();
  Canvas canvas = rasterize(draw_data, framebuffer_width, framebuffer_height);
  const NativeContentStats native_content = compositeNativeContent(
      canvas, accessibility, assets, golden.scale, golden.right_to_left, title,
      status);
  require(native_content.shaped_glyphs > 0u &&
              native_content.glyph_bitmap_pixels > 0u,
          "native pilot produced no packaged-font pixels");
  require(native_content.svg_bitmap_pixels > 0u,
          "native pilot produced no packaged-SVG pixels");
  if (golden.right_to_left) {
    require(native_content.right_to_left_runs > 0u,
            "RTL pilot produced no right-to-left shaped runs");
  }
  const std::uint64_t hash = hashCanvas(canvas);
  return {.canvas = std::move(canvas),
          .hash = hash,
          .vertices = vertices,
          .indices = indices,
          .commands = commands,
          .semantic_nodes = accessibility.nodes.size(),
          .shaped_glyphs = native_content.shaped_glyphs,
          .glyph_bitmap_pixels = native_content.glyph_bitmap_pixels,
          .svg_bitmap_pixels = native_content.svg_bitmap_pixels,
          .right_to_left_runs = native_content.right_to_left_runs,
          .panel_x = panel_x};
}

std::string hexHash(std::uint64_t hash) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << hash
         << "ULL";
  return stream.str();
}

}  // namespace

int main() {
  try {
    bool matches = true;
    float ltr_panel_x = -1.0f;
    float rtl_panel_x = -1.0f;
    for (const GoldenCase& golden : kGoldenCases) {
      ScreenshotResult result = renderPilot(golden);
      if (golden.right_to_left) {
        rtl_panel_x = result.panel_x;
      } else if (ltr_panel_x < 0.0f) {
        ltr_panel_x = result.panel_x;
      }
      if (result.hash == golden.expected_hash) continue;
      matches = false;
      const std::filesystem::path artifact =
          std::filesystem::current_path() /
          ("karma_ui_golden_" + std::string(golden.name) + ".ppm");
      std::cerr << golden.name << " screenshot mismatch\n"
                << "  expected: " << hexHash(golden.expected_hash) << '\n'
                << "  actual:   " << hexHash(result.hash) << '\n'
                << "  frame:    " << result.canvas.width << 'x'
                << result.canvas.height << "\n"
                << "  native:   " << result.vertices << " vertices, "
                << result.indices << " indices, " << result.commands
                << " commands\n"
                << "  semantic: " << result.semantic_nodes
                << " nodes, panel x=" << result.panel_x << '\n'
                << "  native assets: " << result.shaped_glyphs
                << " shaped glyphs, " << result.glyph_bitmap_pixels
                << " glyph pixels, " << result.svg_bitmap_pixels
                << " SVG pixels, " << result.right_to_left_runs
                << " RTL runs\n";
      if (writePpm(artifact, result.canvas)) {
        std::cerr << "  artifact: " << artifact.string() << '\n';
      }
      std::cerr << "  replacement: {\"" << golden.name << "\", "
                << golden.scale << "f, "
                << (golden.right_to_left ? "true" : "false") << ", "
                << hexHash(result.hash) << "},\n";
    }
    require(ltr_panel_x >= 0.0f && rtl_panel_x >= 0.0f &&
                rtl_panel_x > ltr_panel_x + 100.0f,
            "RTL pilot did not mirror the flex-start panel placement");
    for (const NineSliceGoldenCase& golden : kNineSliceGoldenCases) {
      NineSliceScreenshotResult result = renderNineSliceGolden(golden.scale);
      if (result.hash == golden.expected_hash) continue;
      matches = false;
      const std::filesystem::path artifact =
          std::filesystem::current_path() /
          ("karma_ui_golden_" + std::string(golden.name) + ".ppm");
      std::cerr << golden.name << " raster mismatch\n"
                << "  expected: " << hexHash(golden.expected_hash) << '\n'
                << "  actual:   " << hexHash(result.hash) << '\n'
                << "  frame:    " << result.canvas.width << 'x'
                << result.canvas.height << '\n'
                << "  cells:    " << result.cells << '\n';
      if (writePpm(artifact, result.canvas)) {
        std::cerr << "  artifact: " << artifact.string() << '\n';
      }
      std::cerr << "  replacement: {\"" << golden.name << "\", "
                << golden.scale << "f, " << hexHash(result.hash) << "},\n";
    }
    if (!matches) return 1;
    std::cout << "native UI screenshot goldens passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "native UI screenshot golden test failed: " << error.what()
              << '\n';
    return 1;
  }
}
