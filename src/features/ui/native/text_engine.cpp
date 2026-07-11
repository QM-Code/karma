#include "features/ui/native/text_engine.h"

#include "content/assets/asset_ui_source_import.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/utf16.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace karma::ui::native {
namespace {

constexpr UChar32 kReplacementCharacter = 0xfffdu;

void setError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

float fixed26Dot6(hb_position_t value) {
  return static_cast<float>(value) / 64.0f;
}

float positivePixelSize(float value) {
  return std::isfinite(value) && value > 0.0f ? value : 16.0f;
}

struct UtfText {
  struct Grapheme {
    int32_t begin = 0;
    int32_t end = 0;
  };

  std::vector<UChar> units;
  std::vector<std::size_t> unit_to_byte;
  std::vector<Grapheme> graphemes;
  std::unordered_set<int32_t> line_breaks;

  std::size_t byteOffset(int32_t unit_offset) const {
    if (unit_offset <= 0) {
      return 0u;
    }
    const std::size_t offset = static_cast<std::size_t>(unit_offset);
    return offset < unit_to_byte.size() ? unit_to_byte[offset]
                                        : unit_to_byte.back();
  }
};

bool collectBreaks(UtfText& text,
                   UBreakIteratorType type,
                   std::string_view locale,
                   std::string* error) {
  UErrorCode status = U_ZERO_ERROR;
  const std::string locale_owned(locale);
  static constexpr UChar empty_text = 0;
  UBreakIterator* iterator = ubrk_open(type,
                                       locale_owned.empty() ? nullptr : locale_owned.c_str(),
                                       text.units.empty() ? &empty_text : text.units.data(),
                                       static_cast<int32_t>(text.units.size()),
                                       &status);
  if (U_FAILURE(status) || iterator == nullptr) {
    setError(error, std::string("ICU could not create a break iterator: ") +
                        u_errorName(status));
    return false;
  }

  int32_t previous = ubrk_first(iterator);
  for (int32_t next = ubrk_next(iterator); next != UBRK_DONE;
       previous = next, next = ubrk_next(iterator)) {
    if (type == UBRK_CHARACTER) {
      text.graphemes.push_back({previous, next});
    } else {
      text.line_breaks.insert(next);
    }
  }
  ubrk_close(iterator);
  return true;
}

std::optional<UtfText> decodeUtf8(std::string_view source,
                                  std::string_view locale,
                                  std::string* error) {
  UtfText result;
  if (source.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
    setError(error, "text is too large for the Unicode shaping backend");
    return std::nullopt;
  }

  int32_t offset = 0;
  const int32_t length = static_cast<int32_t>(source.size());
  while (offset < length) {
    const int32_t byte_begin = offset;
    UChar32 codepoint = 0;
    U8_NEXT(source.data(), offset, length, codepoint);
    if (codepoint < 0) {
      setError(error, "text is not valid UTF-8");
      return std::nullopt;
    }
    if (U16_LENGTH(codepoint) == 1) {
      result.unit_to_byte.push_back(static_cast<std::size_t>(byte_begin));
      result.units.push_back(static_cast<UChar>(codepoint));
    } else {
      result.unit_to_byte.push_back(static_cast<std::size_t>(byte_begin));
      result.units.push_back(U16_LEAD(codepoint));
      result.unit_to_byte.push_back(static_cast<std::size_t>(byte_begin));
      result.units.push_back(U16_TRAIL(codepoint));
    }
  }
  result.unit_to_byte.push_back(source.size());

  if (!collectBreaks(result, UBRK_CHARACTER, locale, error) ||
      !collectBreaks(result, UBRK_LINE, locale, error)) {
    return std::nullopt;
  }
  return result;
}

struct Paragraph {
  int32_t begin = 0;
  int32_t end = 0;
};

std::vector<Paragraph> splitParagraphs(const std::vector<UChar>& units) {
  std::vector<Paragraph> output;
  int32_t begin = 0;
  int32_t offset = 0;
  const int32_t length = static_cast<int32_t>(units.size());
  while (offset < length) {
    const int32_t codepoint_begin = offset;
    UChar32 codepoint = 0;
    U16_NEXT(units.data(), offset, length, codepoint);
    if (codepoint != '\r' && codepoint != '\n' && codepoint != 0x2028 &&
        codepoint != 0x2029) {
      continue;
    }
    output.push_back({begin, codepoint_begin});
    if (codepoint == '\r' && offset < length && units[offset] == '\n') {
      ++offset;
    }
    begin = offset;
  }
  output.push_back({begin, length});
  return output;
}

std::string makeShapeCacheKey(const ShapeRequest& request,
                              std::uint64_t generation) {
  std::string key;
  auto appendBytes = [&](const auto& value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    key.append(bytes, sizeof(value));
  };
  auto appendString = [&](std::string_view value) {
    const std::uint64_t size = value.size();
    appendBytes(size);
    key.append(value);
  };
  appendBytes(generation);
  appendString(request.text_utf8);
  appendString(request.locale);
  appendBytes(std::bit_cast<std::uint32_t>(request.pixel_size));
  appendBytes(std::bit_cast<std::uint32_t>(request.max_width));
  appendBytes(std::bit_cast<std::uint32_t>(request.line_height));
  appendBytes(std::bit_cast<std::uint32_t>(request.letter_spacing));
  appendBytes(request.direction);
  const std::uint64_t font_count = request.font_keys.size();
  appendBytes(font_count);
  for (const std::string& font : request.font_keys) {
    appendString(font);
  }
  return key;
}

struct GlyphCacheKey {
  FontId font = 0u;
  std::uint32_t glyph = 0u;
  std::uint32_t pixel_size_bits = 0u;
  bool tofu = false;

  bool operator==(const GlyphCacheKey&) const = default;
};

struct GlyphCacheKeyHash {
  std::size_t operator()(const GlyphCacheKey& key) const noexcept {
    std::size_t hash = std::hash<FontId>{}(key.font);
    hash ^= static_cast<std::size_t>(key.glyph) + 0x9e3779b9u + (hash << 6u) +
            (hash >> 2u);
    hash ^= static_cast<std::size_t>(key.pixel_size_bits) + 0x9e3779b9u +
            (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<std::size_t>(key.tofu) + 0x9e3779b9u + (hash << 6u) +
            (hash >> 2u);
    return hash;
  }
};

}  // namespace

struct TextEngine::Impl {
  struct Font {
    FontId id = 0u;
    std::string key;
    std::vector<std::uint8_t> bytes;
    FT_Face face = nullptr;
    hb_font_t* harfbuzz = nullptr;

    ~Font() {
      if (harfbuzz != nullptr) {
        hb_font_destroy(harfbuzz);
      }
      if (face != nullptr) {
        FT_Done_Face(face);
      }
    }
  };

  struct FontChoice {
    int32_t begin = 0;
    int32_t end = 0;
    Font* font = nullptr;
    bool replacement = false;
    bool tofu = false;
  };

  struct ShapeCacheEntry {
    ShapedText shaped;
    std::uint64_t last_use = 0u;
  };

  struct GlyphCacheEntry {
    GlyphBitmap bitmap;
    std::uint64_t last_use = 0u;
  };

  explicit Impl(TextEngineConfig requested_config) : config(requested_config) {
    if (FT_Init_FreeType(&freetype) != 0) {
      freetype = nullptr;
    }
  }

  ~Impl() {
    fonts.clear();
    if (freetype != nullptr) {
      FT_Done_FreeType(freetype);
    }
  }

  TextEngineConfig config;
  FT_Library freetype = nullptr;
  std::unordered_map<std::string, std::unique_ptr<Font>> fonts;
  std::unordered_map<FontId, Font*> fonts_by_id;
  std::vector<FontId> font_order;
  FontId next_font_id = 1u;
  std::uint64_t generation = 1u;
  std::uint64_t use_clock = 0u;
  std::unordered_map<std::string, ShapeCacheEntry> shape_cache;
  std::unordered_map<GlyphCacheKey, GlyphCacheEntry, GlyphCacheKeyHash> glyph_cache;
  std::size_t glyph_cache_bytes = 0u;
  mutable std::mutex mutex;

  void invalidateFontCaches() {
    ++generation;
    shape_cache.clear();
    glyph_cache.clear();
    glyph_cache_bytes = 0u;
  }

  bool setSize(Font& font, float pixel_size, std::string* error) {
    const auto rounded = static_cast<FT_UInt>(
        std::clamp(std::lround(positivePixelSize(pixel_size)), 1l, 65535l));
    if (FT_Set_Pixel_Sizes(font.face, 0u, rounded) != 0) {
      setError(error, "FreeType could not select the requested font size");
      return false;
    }
    hb_ft_font_changed(font.harfbuzz);
    return true;
  }

  static bool codepointNeedsGlyph(UChar32 codepoint) {
    return !u_isISOControl(codepoint) &&
           !u_hasBinaryProperty(codepoint, UCHAR_DEFAULT_IGNORABLE_CODE_POINT);
  }

  static bool supports(Font& font,
                       const std::vector<UChar>& units,
                       int32_t begin,
                       int32_t end) {
    for (int32_t offset = begin; offset < end;) {
      UChar32 codepoint = 0;
      U16_NEXT(units.data(), offset, end, codepoint);
      if (codepointNeedsGlyph(codepoint) &&
          FT_Get_Char_Index(font.face, static_cast<FT_ULong>(codepoint)) == 0u) {
        return false;
      }
    }
    return true;
  }

  std::vector<Font*> requestedFonts(const ShapeRequest& request) {
    std::vector<Font*> output;
    std::unordered_set<FontId> seen;
    if (!request.font_keys.empty()) {
      for (const std::string& key : request.font_keys) {
        const auto found = fonts.find(key);
        if (found != fonts.end() && seen.insert(found->second->id).second) {
          output.push_back(found->second.get());
        }
      }
      return output;
    }
    for (FontId id : font_order) {
      const auto found = fonts_by_id.find(id);
      if (found != fonts_by_id.end()) {
        output.push_back(found->second);
      }
    }
    return output;
  }

  std::vector<FontChoice> chooseFonts(const UtfText& text,
                                      int32_t begin,
                                      int32_t end,
                                      const std::vector<Font*>& fallback) {
    std::vector<FontChoice> choices;
    for (const UtfText::Grapheme& grapheme : text.graphemes) {
      if (grapheme.end <= begin || grapheme.begin >= end) {
        continue;
      }
      const int32_t cluster_begin = std::max(begin, grapheme.begin);
      const int32_t cluster_end = std::min(end, grapheme.end);
      Font* selected = nullptr;
      for (Font* font : fallback) {
        if (supports(*font, text.units, cluster_begin, cluster_end)) {
          selected = font;
          break;
        }
      }

      bool replacement = false;
      bool tofu = false;
      if (selected == nullptr) {
        for (Font* font : fallback) {
          if (FT_Get_Char_Index(font->face, kReplacementCharacter) != 0u) {
            selected = font;
            replacement = true;
            break;
          }
        }
      }
      if (selected == nullptr) {
        selected = fallback.empty() ? nullptr : fallback.front();
        tofu = true;
      }

      if (!choices.empty() && !replacement && !tofu &&
          choices.back().font == selected && !choices.back().replacement &&
          !choices.back().tofu && choices.back().end == cluster_begin) {
        choices.back().end = cluster_end;
      } else {
        choices.push_back({cluster_begin, cluster_end, selected, replacement, tofu});
      }
    }
    return choices;
  }

  static std::pair<float, float> metrics(Font* font, float pixel_size) {
    if (font == nullptr || font->face == nullptr || font->face->size == nullptr) {
      return {positivePixelSize(pixel_size) * 0.8f,
              positivePixelSize(pixel_size) * 0.2f};
    }
    const float ascent = std::max(0.0f, static_cast<float>(
        font->face->size->metrics.ascender) / 64.0f);
    const float descent = std::max(0.0f, -static_cast<float>(
        font->face->size->metrics.descender) / 64.0f);
    return {ascent, descent};
  }

  bool shapeSegment(const UtfText& text,
                    const FontChoice& choice,
                    int32_t begin,
                    int32_t end,
                    TextDirection direction,
                    const ShapeRequest& request,
                    ShapedRun& run,
                    float& ascent,
                    float& descent,
                    std::string* error) {
    run.font = choice.font == nullptr ? 0u : choice.font->id;
    run.direction = direction;
    run.utf8_begin = text.byteOffset(begin);
    run.utf8_end = text.byteOffset(end);

    if (choice.tofu || choice.font == nullptr) {
      const float size = positivePixelSize(request.pixel_size);
      const float advance = size * 0.6f + request.letter_spacing;
      run.glyphs.push_back({.font = run.font,
                            .glyph = 0u,
                            .cluster_begin = run.utf8_begin,
                            .cluster_end = run.utf8_end,
                            .x_advance = advance,
                            .tofu = true});
      run.width = advance;
      const auto [font_ascent, font_descent] = metrics(choice.font, size);
      ascent = std::max(ascent, font_ascent);
      descent = std::max(descent, font_descent);
      return true;
    }

    Font& font = *choice.font;
    if (!setSize(font, request.pixel_size, error)) {
      return false;
    }
    const auto [font_ascent, font_descent] = metrics(&font, request.pixel_size);
    ascent = std::max(ascent, font_ascent);
    descent = std::max(descent, font_descent);

    hb_buffer_t* buffer = hb_buffer_create();
    if (buffer == nullptr) {
      setError(error, "HarfBuzz could not allocate a shaping buffer");
      return false;
    }
    hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_set_direction(buffer, direction == TextDirection::RightToLeft
                                       ? HB_DIRECTION_RTL
                                       : HB_DIRECTION_LTR);
    if (!request.locale.empty()) {
      hb_buffer_set_language(
          buffer, hb_language_from_string(request.locale.data(),
                                          static_cast<int>(request.locale.size())));
    }

    if (choice.replacement) {
      const std::uint32_t replacement = kReplacementCharacter;
      hb_buffer_add_utf32(buffer, &replacement, 1, 0u, 1);
    } else {
      hb_buffer_add_utf16(buffer,
                          reinterpret_cast<const std::uint16_t*>(text.units.data()),
                          static_cast<int>(text.units.size()), begin, end - begin);
    }
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font.harfbuzz, buffer, nullptr, 0u);

    unsigned glyph_count = 0u;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(buffer, &glyph_count);

    std::vector<std::uint32_t> clusters;
    if (!choice.replacement) {
      clusters.reserve(glyph_count + 1u);
      for (unsigned index = 0u; index < glyph_count; ++index) {
        std::uint32_t cluster = infos[index].cluster;
        if (cluster < static_cast<std::uint32_t>(begin)) {
          cluster += static_cast<std::uint32_t>(begin);
        }
        cluster = std::clamp(cluster, static_cast<std::uint32_t>(begin),
                             static_cast<std::uint32_t>(end));
        clusters.push_back(cluster);
      }
      clusters.push_back(static_cast<std::uint32_t>(end));
      std::sort(clusters.begin(), clusters.end());
      clusters.erase(std::unique(clusters.begin(), clusters.end()), clusters.end());
    }

    float pen = 0.0f;
    for (unsigned index = 0u; index < glyph_count; ++index) {
      int32_t cluster_begin = begin;
      int32_t cluster_end = end;
      if (!choice.replacement) {
        std::uint32_t cluster = infos[index].cluster;
        if (cluster < static_cast<std::uint32_t>(begin)) {
          cluster += static_cast<std::uint32_t>(begin);
        }
        cluster = std::clamp(cluster, static_cast<std::uint32_t>(begin),
                             static_cast<std::uint32_t>(end));
        cluster_begin = static_cast<int32_t>(cluster);
        const auto next = std::upper_bound(clusters.begin(), clusters.end(), cluster);
        cluster_end = next == clusters.end() ? end : static_cast<int32_t>(*next);
      }

      ShapedGlyph glyph;
      glyph.font = font.id;
      glyph.glyph = infos[index].codepoint;
      glyph.cluster_begin = text.byteOffset(cluster_begin);
      glyph.cluster_end = text.byteOffset(cluster_end);
      glyph.x = pen;
      glyph.x_advance = fixed26Dot6(positions[index].x_advance);
      glyph.y_advance = -fixed26Dot6(positions[index].y_advance);
      glyph.x_offset = fixed26Dot6(positions[index].x_offset);
      glyph.y_offset = -fixed26Dot6(positions[index].y_offset);
      glyph.replacement = choice.replacement;
      run.glyphs.push_back(glyph);
      pen += glyph.x_advance;
    }
    hb_buffer_destroy(buffer);

    if (run.glyphs.empty()) {
      const float advance = positivePixelSize(request.pixel_size) * 0.6f;
      run.glyphs.push_back({.font = font.id,
                            .cluster_begin = run.utf8_begin,
                            .cluster_end = run.utf8_end,
                            .x_advance = advance,
                            .replacement = choice.replacement,
                            .tofu = true});
      pen = advance;
    }

    if (request.letter_spacing != 0.0f) {
      for (std::size_t index = 0u; index < run.glyphs.size(); ++index) {
        if (index + 1u == run.glyphs.size() ||
            run.glyphs[index + 1u].cluster_begin !=
                run.glyphs[index].cluster_begin) {
          run.glyphs[index].x_advance += request.letter_spacing;
          pen += request.letter_spacing;
        }
      }
      float updated_pen = 0.0f;
      for (ShapedGlyph& glyph : run.glyphs) {
        glyph.x = updated_pen;
        updated_pen += glyph.x_advance;
      }
    }
    run.width = pen;
    return true;
  }

  std::optional<ShapedLine> buildLine(const UtfText& text,
                                      int32_t begin,
                                      int32_t end,
                                      const ShapeRequest& request,
                                      const std::vector<Font*>& fallback,
                                      std::string* error) {
    ShapedLine line;
    line.utf8_begin = text.byteOffset(begin);
    line.utf8_end = text.byteOffset(end);
    float ascent = 0.0f;
    float descent = 0.0f;

    if (begin < end) {
      std::vector<FontChoice> choices = chooseFonts(text, begin, end, fallback);
      UErrorCode status = U_ZERO_ERROR;
      UBiDi* bidi = ubidi_openSized(end - begin, 0, &status);
      if (U_FAILURE(status) || bidi == nullptr) {
        setError(error, std::string("ICU could not allocate a bidi paragraph: ") +
                            u_errorName(status));
        return std::nullopt;
      }
      const UBiDiLevel paragraph_level =
          request.direction == TextDirection::LeftToRight
              ? 0
              : (request.direction == TextDirection::RightToLeft ? 1
                                                                  : UBIDI_DEFAULT_LTR);
      ubidi_setPara(bidi, text.units.data() + begin, end - begin, paragraph_level,
                    nullptr, &status);
      if (U_FAILURE(status)) {
        setError(error, std::string("ICU bidi analysis failed: ") + u_errorName(status));
        ubidi_close(bidi);
        return std::nullopt;
      }

      const int32_t visual_run_count = ubidi_countRuns(bidi, &status);
      if (U_FAILURE(status)) {
        setError(error,
                 std::string("ICU could not enumerate bidi runs: ") + u_errorName(status));
        ubidi_close(bidi);
        return std::nullopt;
      }

      float line_pen = 0.0f;
      for (int32_t visual_index = 0; visual_index < visual_run_count; ++visual_index) {
        int32_t logical_start = 0;
        int32_t run_length = 0;
        const UBiDiDirection bidi_direction =
            ubidi_getVisualRun(bidi, visual_index, &logical_start, &run_length);
        const int32_t run_begin = begin + logical_start;
        const int32_t run_end = run_begin + run_length;
        const TextDirection direction = bidi_direction == UBIDI_RTL
                                            ? TextDirection::RightToLeft
                                            : TextDirection::LeftToRight;

        std::vector<FontChoice> intersections;
        for (const FontChoice& choice : choices) {
          if (choice.end <= run_begin || choice.begin >= run_end) {
            continue;
          }
          FontChoice intersection = choice;
          intersection.begin = std::max(choice.begin, run_begin);
          intersection.end = std::min(choice.end, run_end);
          intersections.push_back(intersection);
        }
        if (direction == TextDirection::RightToLeft) {
          std::reverse(intersections.begin(), intersections.end());
        }

        for (const FontChoice& choice : intersections) {
          ShapedRun run;
          run.x = line_pen;
          if (!shapeSegment(text, choice, choice.begin, choice.end, direction, request,
                            run, ascent, descent, error)) {
            ubidi_close(bidi);
            return std::nullopt;
          }
          for (ShapedGlyph& glyph : run.glyphs) {
            glyph.x += line_pen;
          }
          line_pen += run.width;
          line.runs.push_back(std::move(run));
        }
      }
      line.width = line_pen;
      ubidi_close(bidi);
    }

    if (ascent <= 0.0f && descent <= 0.0f) {
      ascent = positivePixelSize(request.pixel_size) * 0.8f;
      descent = positivePixelSize(request.pixel_size) * 0.2f;
    }
    const float natural_height = std::max(1.0f, ascent + descent);
    line.height = std::isfinite(request.line_height) && request.line_height > 0.0f
                      ? request.line_height
                      : natural_height;
    line.baseline = ascent + (line.height - natural_height) * 0.5f;
    for (ShapedRun& run : line.runs) {
      for (ShapedGlyph& glyph : run.glyphs) {
        glyph.y = line.baseline;
      }
    }
    return line;
  }

  std::optional<ShapedText> shapeUncached(const ShapeRequest& request,
                                          std::string* error) {
    auto decoded = decodeUtf8(request.text_utf8, request.locale, error);
    if (!decoded.has_value()) {
      return std::nullopt;
    }
    const UtfText& text = *decoded;
    const std::vector<Font*> fallback = requestedFonts(request);
    const float max_width = std::isfinite(request.max_width) && request.max_width > 0.0f
                                ? request.max_width
                                : 0.0f;

    ShapedText output;
    output.font_generation = generation;
    float y = 0.0f;
    for (const Paragraph& paragraph : splitParagraphs(text.units)) {
      int32_t line_begin = paragraph.begin;
      if (line_begin == paragraph.end) {
        auto empty = buildLine(text, line_begin, line_begin, request, fallback, error);
        if (!empty.has_value()) {
          return std::nullopt;
        }
        empty->y = y;
        empty->baseline += y;
        y += empty->height;
        output.lines.push_back(std::move(*empty));
        continue;
      }

      while (line_begin < paragraph.end) {
        int32_t line_end = paragraph.end;
        if (max_width > 0.0f) {
          int32_t last_fit = line_begin;
          for (int32_t candidate = line_begin + 1; candidate <= paragraph.end;
               ++candidate) {
            if (candidate != paragraph.end && !text.line_breaks.contains(candidate)) {
              continue;
            }
            auto measured =
                buildLine(text, line_begin, candidate, request, fallback, error);
            if (!measured.has_value()) {
              return std::nullopt;
            }
            if (measured->width <= max_width) {
              last_fit = candidate;
              continue;
            }
            break;
          }

          if (last_fit == line_begin) {
            for (const UtfText::Grapheme& grapheme : text.graphemes) {
              if (grapheme.end <= line_begin || grapheme.end > paragraph.end) {
                continue;
              }
              auto measured =
                  buildLine(text, line_begin, grapheme.end, request, fallback, error);
              if (!measured.has_value()) {
                return std::nullopt;
              }
              if (measured->width <= max_width || last_fit == line_begin) {
                last_fit = grapheme.end;
              }
              if (measured->width > max_width) {
                break;
              }
            }
          }
          line_end = std::max(line_begin + 1, last_fit);
          line_end = std::min(line_end, paragraph.end);
        }

        auto shaped = buildLine(text, line_begin, line_end, request, fallback, error);
        if (!shaped.has_value()) {
          return std::nullopt;
        }
        shaped->y = y;
        shaped->baseline += y;
        for (ShapedRun& run : shaped->runs) {
          for (ShapedGlyph& glyph : run.glyphs) {
            glyph.y += y;
          }
        }
        output.width = std::max(output.width, shaped->width);
        y += shaped->height;
        output.lines.push_back(std::move(*shaped));
        line_begin = line_end;
      }
    }
    output.height = y;
    return output;
  }

  static GlyphBitmap makeTofu(float pixel_size) {
    const float size = positivePixelSize(pixel_size);
    GlyphBitmap bitmap;
    bitmap.format = GlyphPixelFormat::R8;
    bitmap.width = static_cast<std::uint32_t>(std::max(3l, std::lround(size * 0.6f)));
    bitmap.height = static_cast<std::uint32_t>(std::max(3l, std::lround(size * 0.8f)));
    bitmap.stride = bitmap.width;
    bitmap.bearing_y = static_cast<int>(bitmap.height);
    bitmap.advance_x = size * 0.6f;
    bitmap.pixels.assign(static_cast<std::size_t>(bitmap.stride) * bitmap.height, 0u);
    const std::uint32_t thickness =
        std::max(1u, static_cast<std::uint32_t>(std::lround(size / 12.0f)));
    for (std::uint32_t y = 0u; y < bitmap.height; ++y) {
      for (std::uint32_t x = 0u; x < bitmap.width; ++x) {
        if (x < thickness || y < thickness || x + thickness >= bitmap.width ||
            y + thickness >= bitmap.height) {
          bitmap.pixels[static_cast<std::size_t>(y) * bitmap.stride + x] = 255u;
        }
      }
    }
    return bitmap;
  }

  std::optional<GlyphBitmap> rasterizeUncached(const ShapedGlyph& glyph,
                                               float pixel_size,
                                               std::string* error) {
    if (glyph.tofu) {
      return makeTofu(pixel_size);
    }
    const auto found = fonts_by_id.find(glyph.font);
    if (found == fonts_by_id.end()) {
      setError(error, "glyph references a font that is no longer registered");
      return std::nullopt;
    }
    Font& font = *found->second;
    if (!setSize(font, pixel_size, error)) {
      return std::nullopt;
    }
    if (FT_Load_Glyph(font.face, glyph.glyph, FT_LOAD_DEFAULT | FT_LOAD_COLOR) != 0) {
      setError(error, "FreeType could not load the shaped glyph");
      return std::nullopt;
    }
    if (font.face->glyph->format != FT_GLYPH_FORMAT_BITMAP &&
        FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
      setError(error, "FreeType could not rasterize the shaped glyph");
      return std::nullopt;
    }

    const FT_GlyphSlot slot = font.face->glyph;
    const FT_Bitmap& source = slot->bitmap;
    GlyphBitmap bitmap;
    bitmap.width = source.width;
    bitmap.height = source.rows;
    bitmap.bearing_x = slot->bitmap_left;
    bitmap.bearing_y = slot->bitmap_top;
    bitmap.advance_x = static_cast<float>(slot->advance.x) / 64.0f;

    const auto sourceRow = [&](std::uint32_t row) {
      if (source.pitch >= 0) {
        return source.buffer + static_cast<std::size_t>(row) * source.pitch;
      }
      return source.buffer +
             static_cast<std::size_t>(source.rows - 1u - row) * (-source.pitch);
    };

    if (source.pixel_mode == FT_PIXEL_MODE_BGRA) {
      bitmap.format = GlyphPixelFormat::Rgba8;
      bitmap.stride = bitmap.width * 4u;
      bitmap.pixels.resize(static_cast<std::size_t>(bitmap.stride) * bitmap.height);
      for (std::uint32_t y = 0u; y < bitmap.height; ++y) {
        const std::uint8_t* input = sourceRow(y);
        std::uint8_t* output = bitmap.pixels.data() +
                               static_cast<std::size_t>(y) * bitmap.stride;
        for (std::uint32_t x = 0u; x < bitmap.width; ++x) {
          const std::uint8_t alpha = input[x * 4u + 3u];
          auto straight = [alpha](std::uint8_t channel) -> std::uint8_t {
            return alpha == 0u
                       ? 0u
                       : static_cast<std::uint8_t>(std::min(
                             255u, (static_cast<unsigned>(channel) * 255u +
                                    static_cast<unsigned>(alpha) / 2u) /
                                       static_cast<unsigned>(alpha)));
          };
          output[x * 4u + 0u] = straight(input[x * 4u + 2u]);
          output[x * 4u + 1u] = straight(input[x * 4u + 1u]);
          output[x * 4u + 2u] = straight(input[x * 4u + 0u]);
          output[x * 4u + 3u] = alpha;
        }
      }
      return bitmap;
    }

    bitmap.format = GlyphPixelFormat::R8;
    bitmap.stride = bitmap.width;
    bitmap.pixels.assign(static_cast<std::size_t>(bitmap.stride) * bitmap.height, 0u);
    for (std::uint32_t y = 0u; y < bitmap.height; ++y) {
      const std::uint8_t* input = sourceRow(y);
      std::uint8_t* output = bitmap.pixels.data() +
                             static_cast<std::size_t>(y) * bitmap.stride;
      if (source.pixel_mode == FT_PIXEL_MODE_GRAY) {
        for (std::uint32_t x = 0u; x < bitmap.width; ++x) {
          output[x] = source.num_grays <= 1u
                          ? 0u
                          : static_cast<std::uint8_t>(
                                static_cast<unsigned>(input[x]) * 255u /
                                static_cast<unsigned>(source.num_grays - 1u));
        }
      } else if (source.pixel_mode == FT_PIXEL_MODE_MONO) {
        for (std::uint32_t x = 0u; x < bitmap.width; ++x) {
          output[x] = (input[x >> 3u] & (0x80u >> (x & 7u))) != 0u ? 255u : 0u;
        }
      } else {
        setError(error, "FreeType returned an unsupported glyph bitmap format");
        return std::nullopt;
      }
    }
    return bitmap;
  }

  void trimShapeCache() {
    const std::size_t budget = config.shaped_cache_entries;
    while (shape_cache.size() > budget) {
      const auto oldest = std::min_element(
          shape_cache.begin(), shape_cache.end(), [](const auto& left, const auto& right) {
            return left.second.last_use < right.second.last_use;
          });
      if (oldest == shape_cache.end()) {
        break;
      }
      shape_cache.erase(oldest);
    }
  }

  void trimGlyphCache() {
    while (glyph_cache_bytes > config.glyph_cache_bytes && !glyph_cache.empty()) {
      const auto oldest = std::min_element(
          glyph_cache.begin(), glyph_cache.end(), [](const auto& left, const auto& right) {
            return left.second.last_use < right.second.last_use;
          });
      if (oldest == glyph_cache.end()) {
        break;
      }
      glyph_cache_bytes -= oldest->second.bitmap.pixels.size();
      glyph_cache.erase(oldest);
    }
  }
};

TextEngine::TextEngine(TextEngineConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

TextEngine::~TextEngine() = default;
TextEngine::TextEngine(TextEngine&&) noexcept = default;
TextEngine& TextEngine::operator=(TextEngine&&) noexcept = default;

std::optional<FontId> TextEngine::registerFont(std::string key,
                                               const assets::FontAsset& asset,
                                               std::uint32_t face_index,
                                               std::string* error) {
  std::scoped_lock lock(impl_->mutex);
  if (error != nullptr) {
    error->clear();
  }
  if (key.empty()) {
    setError(error, "font registration key must not be empty");
    return std::nullopt;
  }
  if (impl_->freetype == nullptr) {
    setError(error, "FreeType is not available");
    return std::nullopt;
  }
  if (!assets::detail::validateFontBytes(asset.bytes, error)) {
    return std::nullopt;
  }
  if (asset.bytes.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) {
    setError(error, "font asset is too large for FreeType");
    return std::nullopt;
  }

  auto font = std::make_unique<Impl::Font>();
  font->id = impl_->next_font_id++;
  font->key = key;
  font->bytes = asset.bytes;
  const FT_Error face_error = FT_New_Memory_Face(
      impl_->freetype, font->bytes.data(), static_cast<FT_Long>(font->bytes.size()),
      static_cast<FT_Long>(face_index), &font->face);
  if (face_error != 0 || font->face == nullptr) {
    setError(error, "FreeType rejected the font asset or collection face index");
    return std::nullopt;
  }
  if (font->face->charmap == nullptr) {
    FT_Select_Charmap(font->face, FT_ENCODING_UNICODE);
  }
  font->harfbuzz = hb_ft_font_create_referenced(font->face);
  if (font->harfbuzz == nullptr) {
    setError(error, "HarfBuzz could not create a font from the FreeType face");
    return std::nullopt;
  }

  if (const auto existing = impl_->fonts.find(key); existing != impl_->fonts.end()) {
    const FontId old_id = existing->second->id;
    impl_->fonts_by_id.erase(old_id);
    impl_->font_order.erase(
        std::remove(impl_->font_order.begin(), impl_->font_order.end(), old_id),
        impl_->font_order.end());
    impl_->fonts.erase(existing);
  }
  const FontId id = font->id;
  impl_->fonts_by_id[id] = font.get();
  impl_->font_order.push_back(id);
  impl_->fonts.emplace(std::move(key), std::move(font));
  impl_->invalidateFontCaches();
  return id;
}

bool TextEngine::unregisterFont(std::string_view key) {
  std::scoped_lock lock(impl_->mutex);
  const auto found = impl_->fonts.find(std::string(key));
  if (found == impl_->fonts.end()) {
    return false;
  }
  const FontId id = found->second->id;
  impl_->fonts_by_id.erase(id);
  impl_->font_order.erase(std::remove(impl_->font_order.begin(), impl_->font_order.end(), id),
                          impl_->font_order.end());
  impl_->fonts.erase(found);
  impl_->invalidateFontCaches();
  return true;
}

void TextEngine::clearFonts() {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->fonts.empty()) {
    return;
  }
  impl_->fonts_by_id.clear();
  impl_->font_order.clear();
  impl_->fonts.clear();
  impl_->invalidateFontCaches();
}

std::optional<FontId> TextEngine::findFont(std::string_view key) const {
  std::scoped_lock lock(impl_->mutex);
  const auto found = impl_->fonts.find(std::string(key));
  return found == impl_->fonts.end() ? std::nullopt
                                    : std::optional<FontId>(found->second->id);
}

std::uint64_t TextEngine::fontGeneration() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->generation;
}

std::optional<ShapedText> TextEngine::shape(const ShapeRequest& request,
                                            std::string* error) {
  std::scoped_lock lock(impl_->mutex);
  if (error != nullptr) {
    error->clear();
  }
  const std::string cache_key = makeShapeCacheKey(request, impl_->generation);
  if (const auto cached = impl_->shape_cache.find(cache_key);
      cached != impl_->shape_cache.end()) {
    cached->second.last_use = ++impl_->use_clock;
    return cached->second.shaped;
  }
  auto shaped = impl_->shapeUncached(request, error);
  if (!shaped.has_value()) {
    return std::nullopt;
  }
  if (impl_->config.shaped_cache_entries > 0u) {
    impl_->shape_cache.emplace(cache_key,
                               Impl::ShapeCacheEntry{*shaped, ++impl_->use_clock});
    impl_->trimShapeCache();
  }
  return shaped;
}

std::optional<GlyphBitmap> TextEngine::rasterize(const ShapedGlyph& glyph,
                                                 float pixel_size,
                                                 std::string* error) {
  std::scoped_lock lock(impl_->mutex);
  if (error != nullptr) {
    error->clear();
  }
  const float normalized_size = positivePixelSize(pixel_size);
  const GlyphCacheKey key{glyph.font, glyph.glyph,
                          std::bit_cast<std::uint32_t>(normalized_size), glyph.tofu};
  if (const auto cached = impl_->glyph_cache.find(key);
      cached != impl_->glyph_cache.end()) {
    cached->second.last_use = ++impl_->use_clock;
    return cached->second.bitmap;
  }
  auto bitmap = impl_->rasterizeUncached(glyph, normalized_size, error);
  if (!bitmap.has_value()) {
    return std::nullopt;
  }
  if (impl_->config.glyph_cache_bytes > 0u &&
      bitmap->pixels.size() <= impl_->config.glyph_cache_bytes) {
    impl_->glyph_cache_bytes += bitmap->pixels.size();
    impl_->glyph_cache.emplace(key, Impl::GlyphCacheEntry{*bitmap, ++impl_->use_clock});
    impl_->trimGlyphCache();
  }
  return bitmap;
}

void TextEngine::clearCaches() {
  std::scoped_lock lock(impl_->mutex);
  impl_->shape_cache.clear();
  impl_->glyph_cache.clear();
  impl_->glyph_cache_bytes = 0u;
}

std::size_t TextEngine::shapedCacheSize() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->shape_cache.size();
}

std::size_t TextEngine::glyphCacheBytes() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->glyph_cache_bytes;
}

}  // namespace karma::ui::native
