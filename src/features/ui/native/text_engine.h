#pragma once

#include "karma/assets.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {

using FontId = std::uint64_t;

enum class TextDirection : std::uint8_t { Auto, LeftToRight, RightToLeft };
enum class GlyphPixelFormat : std::uint8_t { R8, Rgba8 };

struct TextEngineConfig {
  std::size_t shaped_cache_entries = 256u;
  std::size_t glyph_cache_bytes = 16u * 1024u * 1024u;
};

struct ShapeRequest {
  std::string text_utf8;
  /// Registered font keys, in fallback order. No platform font lookup occurs.
  std::vector<std::string> font_keys;
  std::string locale = "en";
  float pixel_size = 16.0f;
  float max_width = 0.0f;  // Zero means unbounded.
  float line_height = 0.0f;  // Zero derives it from the selected fonts.
  float letter_spacing = 0.0f;
  TextDirection direction = TextDirection::Auto;
};

struct ShapedGlyph {
  FontId font = 0u;
  std::uint32_t glyph = 0u;
  /// UTF-8 source range covered by this glyph/cluster.
  std::size_t cluster_begin = 0u;
  std::size_t cluster_end = 0u;
  /// Baseline pen position. Apply bitmap bearing_x and -bearing_y to draw.
  float x = 0.0f;
  float y = 0.0f;
  float x_advance = 0.0f;
  float y_advance = 0.0f;
  float x_offset = 0.0f;
  float y_offset = 0.0f;
  bool replacement = false;
  bool tofu = false;
};

struct ShapedRun {
  FontId font = 0u;
  TextDirection direction = TextDirection::LeftToRight;
  std::size_t utf8_begin = 0u;
  std::size_t utf8_end = 0u;
  float x = 0.0f;
  float width = 0.0f;
  std::vector<ShapedGlyph> glyphs;
};

struct ShapedLine {
  std::size_t utf8_begin = 0u;
  std::size_t utf8_end = 0u;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float baseline = 0.0f;
  std::vector<ShapedRun> runs;
};

struct ShapedText {
  std::vector<ShapedLine> lines;
  float width = 0.0f;
  float height = 0.0f;
  std::uint64_t font_generation = 0u;
};

struct GlyphBitmap {
  GlyphPixelFormat format = GlyphPixelFormat::R8;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::uint32_t stride = 0u;
  int bearing_x = 0;
  int bearing_y = 0;
  float advance_x = 0.0f;
  std::vector<std::uint8_t> pixels;
};

/// Owns deterministic font bytes, Unicode segmentation, shaping, and glyph
/// raster caches. All methods are serialized; callers may use it from workers.
class TextEngine {
 public:
  explicit TextEngine(TextEngineConfig config = {});
  ~TextEngine();
  TextEngine(TextEngine&&) noexcept;
  TextEngine& operator=(TextEngine&&) noexcept;
  TextEngine(const TextEngine&) = delete;
  TextEngine& operator=(const TextEngine&) = delete;

  std::optional<FontId> registerFont(std::string key,
                                     const assets::FontAsset& asset,
                                     std::uint32_t face_index = 0u,
                                     std::string* error = nullptr);
  bool unregisterFont(std::string_view key);
  void clearFonts();

  std::optional<FontId> findFont(std::string_view key) const;
  std::uint64_t fontGeneration() const;

  std::optional<ShapedText> shape(const ShapeRequest& request,
                                  std::string* error = nullptr);
  std::optional<GlyphBitmap> rasterize(const ShapedGlyph& glyph,
                                       float pixel_size,
                                       std::string* error = nullptr);

  void clearCaches();
  std::size_t shapedCacheSize() const;
  std::size_t glyphCacheBytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
