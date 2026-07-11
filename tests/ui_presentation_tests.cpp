#include "features/ui/native/font_face.h"
#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/presentation_runtime.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/svg_rasterizer.h"
#include "features/ui/native/text_engine.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<const karma::ui::native::ShapedGlyph*> glyphs(
    const karma::ui::native::ShapedText& text) {
  std::vector<const karma::ui::native::ShapedGlyph*> output;
  for (const auto& line : text.lines) {
    for (const auto& run : line.runs) {
      for (const auto& glyph : run.glyphs) {
        output.push_back(&glyph);
      }
    }
  }
  return output;
}

bool isUtf8Boundary(std::string_view text, std::size_t offset) {
  return offset == 0u || offset == text.size() ||
         (static_cast<unsigned char>(text[offset]) & 0xc0u) != 0x80u;
}

std::uint16_t readBigU16(const std::vector<std::uint8_t>& bytes,
                         std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(bytes[offset]) << 8u) | bytes[offset + 1u]);
}

std::uint32_t readBigU32(const std::vector<std::uint8_t>& bytes,
                         std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
         (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u) |
         (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u) |
         bytes[offset + 3u];
}

void writeBigU32(std::vector<std::uint8_t>& bytes,
                 std::size_t offset,
                 std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 24u);
  bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 16u);
  bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 8u);
  bytes[offset + 3u] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> makeTwoFaceCollection(
    const std::vector<std::uint8_t>& sfnt) {
  assert(sfnt.size() >= 12u);
  constexpr std::size_t header_size = 20u;
  const std::size_t first_offset = header_size;
  const std::size_t second_offset = (first_offset + sfnt.size() + 3u) & ~3u;
  assert(second_offset <= std::numeric_limits<std::uint32_t>::max());
  std::vector<std::uint8_t> collection(second_offset + sfnt.size(), 0u);
  std::copy(sfnt.begin(), sfnt.end(), collection.begin() + first_offset);
  std::copy(sfnt.begin(), sfnt.end(), collection.begin() + second_offset);
  writeBigU32(collection, 0u, 0x74746366u);  // ttcf
  writeBigU32(collection, 4u, 0x00010000u);
  writeBigU32(collection, 8u, 2u);
  writeBigU32(collection, 12u, static_cast<std::uint32_t>(first_offset));
  writeBigU32(collection, 16u, static_cast<std::uint32_t>(second_offset));

  const std::uint16_t table_count = readBigU16(sfnt, 4u);
  assert(12u + static_cast<std::size_t>(table_count) * 16u <= sfnt.size());
  for (const std::size_t face_offset : {first_offset, second_offset}) {
    for (std::uint16_t table = 0u; table < table_count; ++table) {
      const std::size_t record = face_offset + 12u + table * 16u;
      const std::uint32_t original_offset = readBigU32(collection, record + 8u);
      writeBigU32(collection, record + 8u,
                  original_offset + static_cast<std::uint32_t>(face_offset));
    }
  }
  return collection;
}

karma::rendering::UIDrawData triangleFragment(
    std::uint32_t color,
    karma::rendering::UITextureHandle texture,
    int scissor_x = 0) {
  karma::rendering::UIDrawData result;
  result.vertices = {
      {.x = 0.0f, .y = 0.0f, .rgba = color},
      {.x = 1.0f, .y = 0.0f, .rgba = color},
      {.x = 0.0f, .y = 1.0f, .rgba = color},
  };
  result.indices = {0u, 1u, 2u};
  result.commands.push_back({.index_offset = 0u,
                             .index_count = 3u,
                             .scissor_enabled = true,
                             .scissor_x = scissor_x,
                             .scissor_y = 2,
                             .scissor_w = 100,
                             .scissor_h = 80,
                             .texture = texture});
  return result;
}

void testRetainedDrawDataAssembly() {
  using namespace karma::ui::native::presentation;

  karma::rendering::UIDrawData assembled =
      triangleFragment(0xff102030u, 7u);
  const karma::rendering::UIDrawData compatible =
      triangleFragment(0xff405060u, 7u);
  assert(appendRebased(assembled, compatible));
  assert(assembled.vertices.size() == 6u);
  assert(assembled.indices ==
         std::vector<std::uint32_t>({0u, 1u, 2u, 3u, 4u, 5u}));
  assert(assembled.commands.size() == 1u);
  assert(assembled.commands.front().index_offset == 0u);
  assert(assembled.commands.front().index_count == 6u);
  assert(karma::rendering::validateUIDrawData(assembled));

  const karma::rendering::UIDrawData different_clip =
      triangleFragment(0xff708090u, 7u, 1);
  assert(appendRebased(assembled, different_clip));
  assert(assembled.commands.size() == 2u);
  assert(assembled.commands.back().index_offset == 6u);
  assert(assembled.commands.back().index_count == 3u);
  assert(karma::rendering::validateUIDrawData(assembled));

  karma::rendering::UIDrawData empty_fragment;
  const std::size_t vertices_before = assembled.vertices.size();
  assert(appendRebased(assembled, empty_fragment));
  assert(assembled.vertices.size() == vertices_before);
  assert(!appendRebased(assembled, assembled));

  karma::rendering::UIDrawData malformed = compatible;
  malformed.indices.back() = 99u;
  const std::size_t commands_before = assembled.commands.size();
  const std::size_t indices_before = assembled.indices.size();
  const std::uint32_t last_count_before =
      assembled.commands.back().index_count;
  assert(!appendRebased(assembled, malformed));
  assert(assembled.vertices.size() == vertices_before);
  assert(assembled.indices.size() == indices_before);
  assert(assembled.commands.size() == commands_before);
  assert(assembled.commands.back().index_count == last_count_before);
}

void testRetainedDrawDataByteCost() {
  using karma::ui::native::presentation::canRetainFragment;
  using karma::ui::native::presentation::retainedByteCost;

  karma::rendering::UIDrawData retained;
  retained.vertices.reserve(11u);
  retained.indices.reserve(13u);
  retained.commands.reserve(17u);
  const std::size_t expected =
      retained.vertices.capacity() * sizeof(karma::rendering::UIVertex) +
      retained.indices.capacity() * sizeof(std::uint32_t) +
      retained.commands.capacity() * sizeof(karma::rendering::UIDrawCmd);
  assert(retainedByteCost(retained) == expected);
  assert(canRetainFragment(retained, expected, 7u, 7u));
  assert(!canRetainFragment(retained, expected, 7u, 8u));
  assert(!canRetainFragment(retained, 0u, 7u, 7u));
  assert(expected > 0u);
  assert(!canRetainFragment(retained, expected - 1u, 7u, 7u));
}

void testRetainedFragmentEvictionPolicy() {
  using karma::ui::native::presentation::RetainedFragmentUsage;
  using karma::ui::native::presentation::selectRetainedEvictions;

  const std::vector<RetainedFragmentUsage> lru{
      {.bytes = 40u, .last_use_frame = 2u, .tree_depth = 0u},
      {.bytes = 30u, .last_use_frame = 5u, .tree_depth = 1u},
      {.bytes = 20u, .last_use_frame = 9u, .tree_depth = 2u},
  };
  assert(selectRetainedEvictions(lru, 90u).empty());
  assert(selectRetainedEvictions(lru, 50u) ==
         std::vector<std::size_t>({0u}));
  assert(selectRetainedEvictions(lru, 49u) ==
         std::vector<std::size_t>({0u, 1u}));
  assert(selectRetainedEvictions(lru, 0u) ==
         std::vector<std::size_t>({0u, 1u, 2u}));

  // Equal-age subtree fragments are cheaper to regenerate than their parent;
  // depth wins first, then byte cost, with input order as the final tie-break.
  const std::vector<RetainedFragmentUsage> tied{
      {.bytes = 20u, .last_use_frame = 7u, .tree_depth = 1u},
      {.bytes = 10u, .last_use_frame = 7u, .tree_depth = 3u},
      {.bytes = 30u, .last_use_frame = 7u, .tree_depth = 3u},
      {.bytes = 20u, .last_use_frame = 7u, .tree_depth = 1u},
  };
  assert(selectRetainedEvictions(tied, 40u) ==
         std::vector<std::size_t>({2u, 1u}));

  // Selection sums from the retained end, so even adversarial sizes cannot
  // overflow and an individually oversized fragment is always evicted.
  const std::vector<RetainedFragmentUsage> oversized{
      {.bytes = std::numeric_limits<std::size_t>::max(),
       .last_use_frame = 1u,
       .tree_depth = 0u},
      {.bytes = 1u, .last_use_frame = 2u, .tree_depth = 0u},
  };
  assert(selectRetainedEvictions(oversized, 1u) ==
         std::vector<std::size_t>({0u}));
}

void testRetainedFragmentTreeBudgetEnforcement() {
  using karma::ui::native::presentation::enforceRetainedPaintBudget;
  using karma::ui::native::presentation::retainedByteCost;
  using karma::ui::native::runtime_dom::Node;
  using karma::ui::native::runtime_dom::TemplateInstance;

  Node root;
  root.retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>(
          triangleFragment(0xff102030u, 1u));
  root.retained_paint_revision = 11u;
  root.retained_last_use_frame = 9u;

  auto ordinary = std::make_unique<Node>();
  ordinary->retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>(
          triangleFragment(0xff203040u, 2u));
  ordinary->retained_paint_revision = 12u;
  ordinary->retained_last_use_frame = 1u;
  Node* ordinary_ptr = ordinary.get();
  root.children.push_back(std::move(ordinary));

  auto repeated_template = std::make_unique<Node>();
  repeated_template->template_node = true;
  repeated_template->instances.emplace_back();
  auto repeated = std::make_unique<Node>();
  repeated->retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>(
          triangleFragment(0xff304050u, 3u));
  repeated->retained_paint_revision = 13u;
  repeated->retained_last_use_frame = 5u;
  Node* repeated_ptr = repeated.get();
  repeated_template->instances.front().children.push_back(std::move(repeated));
  root.children.push_back(std::move(repeated_template));

  const std::size_t root_bytes = retainedByteCost(*root.retained_fragment);
  const std::size_t repeated_bytes =
      retainedByteCost(*repeated_ptr->retained_fragment);
  std::vector<Node*> roots{&root, nullptr};
  assert(enforceRetainedPaintBudget(roots, root_bytes + repeated_bytes) == 1u);
  assert(!ordinary_ptr->retained_fragment);
  assert(ordinary_ptr->retained_paint_revision == 0u);
  assert(root.retained_fragment && root.retained_paint_revision == 11u);
  assert(repeated_ptr->retained_fragment &&
         repeated_ptr->retained_paint_revision == 13u);

  assert(enforceRetainedPaintBudget(roots, 0u) == 2u);
  assert(!root.retained_fragment && root.retained_paint_revision == 0u);
  assert(!repeated_ptr->retained_fragment &&
         repeated_ptr->retained_paint_revision == 0u);
  assert(enforceRetainedPaintBudget(roots, 0u) == 0u);
}

void testFramebufferScissor() {
  using karma::ui::native::presentation::FramebufferScissor;
  using karma::ui::native::presentation::LogicalClip;
  using karma::ui::native::presentation::framebufferScissor;

  const auto scaled = framebufferScissor({.x = 2.25f,
                                           .y = 3.5f,
                                           .width = 10.25f,
                                           .height = 4.5f},
                                          1.5f, 2.0f, 100, 80);
  assert(scaled.has_value());
  const FramebufferScissor expected_scaled{
      .x = 3, .y = 7, .width = 16, .height = 9};
  assert(*scaled == expected_scaled);

  const auto clamped = framebufferScissor({.x = -5.0f,
                                            .y = -4.0f,
                                            .width = 20.0f,
                                            .height = 12.0f},
                                           2.0f, 2.0f, 24, 10);
  assert(clamped.has_value());
  const FramebufferScissor expected_clamped{
      .x = 0, .y = 0, .width = 24, .height = 10};
  assert(*clamped == expected_clamped);
  assert(!framebufferScissor({.x = 101.0f,
                              .y = 0.0f,
                              .width = 5.0f,
                              .height = 5.0f},
                             1.0f, 1.0f, 100, 80));
  assert(!framebufferScissor({.x = 0.0f,
                              .y = 0.0f,
                              .width = 5.0f,
                              .height = 5.0f},
                             0.0f, 1.0f, 100, 80));
  assert(!framebufferScissor({.x = 0.0f,
                              .y = 0.0f,
                              .width = std::numeric_limits<float>::infinity(),
                              .height = 5.0f},
                             1.0f, 1.0f, 100, 80));
}

void testFontFaceVariantsAndCollections() {
  using namespace karma::ui::native;
  // Version-2 JSON themes retain font variants as typed descriptors rather
  // than parsing CSS @font-face declarations.
  const std::vector<FontFaceDefinition> faces{
      {.family = "karma sans",
       .asset_key = "ui/regular",
       .source_order = 10u},
      {.family = "karma sans",
       .asset_key = "ui/italic",
       .style = FontFaceStyle::Italic,
       .face_index = 2u,
       .source_order = 11u},
      {.family = "karma sans",
       .asset_key = "ui/bold",
       .weight = 700,
       .source_order = 12u},
      {.family = "karma sans",
       .asset_key = "ui/bold-italic",
       .weight = 700,
       .style = FontFaceStyle::Italic,
       .face_index = 4u,
       .source_order = 13u},
  };
  assert(faces.size() == 4u);
  assert(faces[0].family == "karma sans");
  assert(faces[0].weight == 400 && faces[0].style == FontFaceStyle::Normal);
  assert(faces[1].face_index == 2u && faces[1].style == FontFaceStyle::Italic);
  assert(faces[3].weight == 700 && faces[3].face_index == 4u);
  assert(faces[3].source_order == 13u);
  assert(selectBestFontFace(faces, "KARMA SANS", 700, FontFaceStyle::Italic)
             ->asset_key == "ui/bold-italic");
  assert(selectBestFontFace(faces, "karma sans", 700, FontFaceStyle::Normal)
             ->asset_key == "ui/bold");
  assert(selectBestFontFace(faces, "karma sans", 500, FontFaceStyle::Normal)
             ->asset_key == "ui/regular");
  assert(selectBestFontFace(faces, "missing", 400, FontFaceStyle::Normal) ==
         nullptr);
  assert(fontRegistrationKey("ui/collection", 0u) !=
         fontRegistrationKey("ui/collection", 1u));

  const auto font_bytes =
      readBytes(std::filesystem::path(KARMA_TEST_ASSET_DIR) / "Roboto-Black.ttf");
  karma::assets::FontAsset collection{.bytes = makeTwoFaceCollection(font_bytes)};
  TextEngine engine;
  std::string error;
  const auto first = engine.registerFont("ui/collection#face=0", collection, 0u,
                                         &error);
  assert(first.has_value() && error.empty());
  const auto second = engine.registerFont("ui/collection#face=1", collection, 1u,
                                          &error);
  assert(second.has_value() && error.empty() && second != first);
  assert(!engine.registerFont("ui/collection#face=2", collection, 2u, &error));
  assert(!error.empty());
}

void assertValidClusters(const karma::ui::native::ShapedText& shaped,
                         std::string_view source) {
  for (const auto& line : shaped.lines) {
    assert(line.utf8_begin <= line.utf8_end);
    assert(line.utf8_end <= source.size());
    assert(isUtf8Boundary(source, line.utf8_begin));
    assert(isUtf8Boundary(source, line.utf8_end));
    for (const auto& run : line.runs) {
      assert(run.utf8_begin <= run.utf8_end);
      assert(run.utf8_begin >= line.utf8_begin);
      assert(run.utf8_end <= line.utf8_end);
      assert(isUtf8Boundary(source, run.utf8_begin));
      assert(isUtf8Boundary(source, run.utf8_end));
      for (const auto& glyph : run.glyphs) {
        assert(glyph.cluster_begin < glyph.cluster_end);
        assert(glyph.cluster_begin >= run.utf8_begin);
        assert(glyph.cluster_end <= run.utf8_end);
        assert(isUtf8Boundary(source, glyph.cluster_begin));
        assert(isUtf8Boundary(source, glyph.cluster_end));
      }
    }
  }
}

void testTextShaping() {
  using namespace karma::ui::native;
  const auto font_bytes =
      readBytes(std::filesystem::path(KARMA_TEST_ASSET_DIR) / "Roboto-Black.ttf");
  karma::assets::FontAsset asset{.bytes = font_bytes};

  TextEngine engine;
  std::string error;
  const auto font = engine.registerFont("ui/roboto", asset, 0u, &error);
  assert(font.has_value());
  assert(error.empty());

  ShapeRequest combining{.text_utf8 = "e\xcc\x81",
                         .font_keys = {"ui/roboto"},
                         .locale = "en",
                         .pixel_size = 24.0f};
  const auto combined = engine.shape(combining, &error);
  assert(combined.has_value());
  assertValidClusters(*combined, combining.text_utf8);
  const auto combined_glyphs = glyphs(*combined);
  assert(!combined_glyphs.empty());
  assert(combined_glyphs.front()->cluster_begin == 0u);
  assert(combined_glyphs.front()->cluster_end == combining.text_utf8.size());
  assert(!combined_glyphs.front()->tofu);

  // An absent first family must fall through to the registered asset font.
  ShapeRequest fallback{.text_utf8 = "Karma",
                        .font_keys = {"ui/absent", "ui/roboto"},
                        .pixel_size = 18.0f};
  const auto fallen_back = engine.shape(fallback, &error);
  assert(fallen_back.has_value());
  for (const ShapedGlyph* glyph : glyphs(*fallen_back)) {
    assert(glyph->font == *font);
    assert(!glyph->tofu);
  }

  ShapeRequest wrapped{.text_utf8 = "alpha beta gamma",
                       .font_keys = {"ui/roboto"},
                       .locale = "en",
                       .pixel_size = 18.0f,
                       .max_width = 60.0f};
  const auto wrapped_text = engine.shape(wrapped, &error);
  assert(wrapped_text.has_value());
  assert(wrapped_text->lines.size() >= 2u);
  for (const ShapedLine& line : wrapped_text->lines) {
    assert(line.width <= wrapped.max_width);
  }

  // HarfBuzz must keep the Latin ffi ligature mapped to its complete source
  // cluster rather than exposing an arbitrary byte inside it.
  ShapeRequest ligature{.text_utf8 = "office",
                        .font_keys = {"ui/roboto"},
                        .locale = "en",
                        .pixel_size = 24.0f};
  const auto ligature_text = engine.shape(ligature, &error);
  assert(ligature_text.has_value());
  assertValidClusters(*ligature_text, ligature.text_utf8);
  bool found_ffi_cluster = false;
  for (const ShapedGlyph* glyph : glyphs(*ligature_text)) {
    found_ffi_cluster |= glyph->cluster_begin == 1u && glyph->cluster_end == 4u;
  }
  assert(found_ffi_cluster);

  ShapeRequest rtl{.text_utf8 = "\xd7\x90\xd7\x91\xd7\x92",
                   .font_keys = {"ui/roboto"},
                   .locale = "he",
                   .pixel_size = 20.0f,
                   .direction = TextDirection::RightToLeft};
  const auto bidi = engine.shape(rtl, &error);
  assert(bidi.has_value());
  assertValidClusters(*bidi, rtl.text_utf8);
  const auto bidi_glyphs = glyphs(*bidi);
  assert(bidi_glyphs.size() >= 3u);
  assert(bidi_glyphs.front()->cluster_begin > bidi_glyphs.back()->cluster_begin);

  // A missing character is reshaped as U+FFFD when the fallback font has it.
  ShapeRequest replacement{.text_utf8 = "\xf0\x9f\xa7\x8c",
                           .font_keys = {"ui/roboto"},
                           .pixel_size = 20.0f};
  const auto replaced_text = engine.shape(replacement, &error);
  assert(replaced_text.has_value());
  const auto replaced_glyphs = glyphs(*replaced_text);
  assert(replaced_glyphs.size() == 1u);
  assert(replaced_glyphs.front()->replacement);
  assert(!replaced_glyphs.front()->tofu);
  assert(replaced_glyphs.front()->cluster_begin == 0u);
  assert(replaced_glyphs.front()->cluster_end == replacement.text_utf8.size());

  // No registered family for a grapheme produces an atlas-ready engine tofu.
  ShapeRequest missing{.text_utf8 = "\xf0\x9f\xa7\x8c",
                       .font_keys = {"ui/not-registered"},
                       .pixel_size = 20.0f};
  const auto missing_text = engine.shape(missing, &error);
  assert(missing_text.has_value());
  const auto missing_glyphs = glyphs(*missing_text);
  assert(missing_glyphs.size() == 1u);
  assert(missing_glyphs.front()->tofu);
  const auto tofu = engine.rasterize(*missing_glyphs.front(), 20.0f, &error);
  assert(tofu.has_value());
  assert(tofu->format == GlyphPixelFormat::R8);
  assert(!tofu->pixels.empty());

  const auto latin_glyphs = glyphs(*fallen_back);
  const auto bitmap = engine.rasterize(*latin_glyphs.front(), 18.0f, &error);
  assert(bitmap.has_value());
  assert(bitmap->format == GlyphPixelFormat::R8);
  assert(!bitmap->pixels.empty());
  assert(engine.shapedCacheSize() >= 6u);
}

void testUnicodeSegmentationAndCacheInvalidation() {
  using namespace karma::ui::native;
  const auto font_bytes =
      readBytes(std::filesystem::path(KARMA_TEST_ASSET_DIR) / "Roboto-Black.ttf");
  karma::assets::FontAsset asset{.bytes = font_bytes};

  TextEngine engine({.shaped_cache_entries = 3u, .glyph_cache_bytes = 4096u});
  std::string error = "stale diagnostic";
  const auto original_font = engine.registerFont("ui/roboto", asset, 0u, &error);
  assert(original_font.has_value());
  assert(error.empty());
  const std::uint64_t initial_generation = engine.fontGeneration();

  // Auto bidi must preserve distinct visual run directions in mixed text.
  ShapeRequest mixed{.text_utf8 = "abc \xd7\x90\xd7\x91\xd7\x92 123",
                     .font_keys = {"ui/roboto"},
                     .locale = "he",
                     .pixel_size = 18.0f,
                     .direction = TextDirection::Auto};
  const auto mixed_text = engine.shape(mixed, &error);
  assert(mixed_text.has_value());
  assertValidClusters(*mixed_text, mixed.text_utf8);
  std::unordered_set<TextDirection> directions;
  for (const auto& line : mixed_text->lines) {
    for (const auto& run : line.runs) {
      directions.insert(run.direction);
    }
  }
  assert(directions.contains(TextDirection::LeftToRight));
  assert(directions.contains(TextDirection::RightToLeft));

  // Arabic bidi is exercised independently from font coverage. When the
  // packaged family lacks a grapheme, each replacement retains the complete
  // UTF-8 source cluster and visual RTL ordering.
  ShapeRequest arabic{.text_utf8 = "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
                      .font_keys = {"ui/roboto"},
                      .locale = "ar",
                      .pixel_size = 20.0f,
                      .direction = TextDirection::RightToLeft};
  const auto arabic_text = engine.shape(arabic, &error);
  assert(arabic_text.has_value());
  assertValidClusters(*arabic_text, arabic.text_utf8);
  const auto arabic_glyphs = glyphs(*arabic_text);
  assert(arabic_glyphs.size() >= 4u);
  assert(arabic_glyphs.front()->cluster_begin >
         arabic_glyphs.back()->cluster_begin);

  // The Devanagari conjunct is a single extended grapheme. Fallback or tofu
  // must happen for the cluster as a whole, never between its combining units.
  ShapeRequest indic{.text_utf8 = "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7\xe0\xa4\xbf",
                     .font_keys = {"ui/roboto"},
                     .locale = "hi",
                     .pixel_size = 20.0f};
  const auto indic_text = engine.shape(indic, &error);
  assert(indic_text.has_value());
  assertValidClusters(*indic_text, indic.text_utf8);
  const auto indic_glyphs = glyphs(*indic_text);
  assert(indic_glyphs.size() == 1u);
  assert(indic_glyphs.front()->cluster_begin == 0u);
  assert(indic_glyphs.front()->cluster_end == indic.text_utf8.size());

  // ICU offers line opportunities between CJK ideographs and at Thai word
  // boundaries. The engine may fall back to replacement glyphs, but all line
  // and glyph ranges still need to land on UTF-8 boundaries.
  for (ShapeRequest request : {
           ShapeRequest{.text_utf8 = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89\xe5\x9b\x9b\xe4\xba\x94\xe5\x85\xad",
                        .font_keys = {"ui/roboto"},
                        .locale = "zh",
                        .pixel_size = 16.0f,
                        .max_width = 22.0f},
           ShapeRequest{.text_utf8 = "\xe0\xb8\xa0\xe0\xb8\xb2\xe0\xb8\xa9\xe0\xb8\xb2\xe0\xb9\x84\xe0\xb8\x97\xe0\xb8\xa2\xe0\xb8\xa0\xe0\xb8\xb2\xe0\xb8\xa9\xe0\xb8\xb2\xe0\xb9\x84\xe0\xb8\x97\xe0\xb8\xa2",
                        .font_keys = {"ui/roboto"},
                        .locale = "th",
                        .pixel_size = 16.0f,
                        .max_width = 45.0f}}) {
    const auto shaped = engine.shape(request, &error);
    assert(shaped.has_value());
    assert(shaped->lines.size() >= 2u);
    assertValidClusters(*shaped, request.text_utf8);
    for (const auto& line : shaped->lines) {
      assert(line.width <= request.max_width);
    }
  }

  ShapeRequest invalid{.text_utf8 = std::string("\xc3\x28", 2u),
                       .font_keys = {"ui/roboto"}};
  assert(!engine.shape(invalid, &error));
  assert(!error.empty());

  // The cache is bounded and is invalidated atomically with a font
  // replacement. Glyph handles from the previous generation then fail rather
  // than rasterizing through a dangling FreeType face.
  assert(engine.shapedCacheSize() <= 3u);
  const ShapedGlyph stale_glyph = *glyphs(*mixed_text).front();
  error = "stale diagnostic";
  const auto replacement_font = engine.registerFont("ui/roboto", asset, 0u, &error);
  assert(replacement_font.has_value());
  assert(*replacement_font != *original_font);
  assert(error.empty());
  assert(engine.fontGeneration() > initial_generation);
  assert(engine.shapedCacheSize() == 0u);
  assert(engine.glyphCacheBytes() == 0u);
  assert(!engine.rasterize(stale_glyph, 18.0f, &error));
  assert(!error.empty());

  const auto current = engine.shape(
      ShapeRequest{.text_utf8 = "cache", .font_keys = {"ui/roboto"}}, &error);
  assert(current.has_value());
  const auto current_glyphs = glyphs(*current);
  assert(!current_glyphs.empty());
  assert(engine.rasterize(*current_glyphs.front(), 18.0f, &error));
  assert(engine.glyphCacheBytes() <= 4096u);
  engine.clearCaches();
  assert(engine.shapedCacheSize() == 0u);
  assert(engine.glyphCacheBytes() == 0u);
}

void testSvgRasterCache() {
  using namespace karma::ui::native;
  const std::string source = R"SVG(<svg xmlns="http://www.w3.org/2000/svg"
      width="16" height="16" viewBox="0 0 16 16">
    <rect width="16" height="16" rx="2" fill="#4080ff"/>
  </svg>)SVG";
  const karma::assets::SvgAsset asset{
      .source_utf8 = source, .content_hash = karma::assets::hashString(source)};

  SvgRasterizer rasterizer({.cache_budget_bytes = 1024u * 1024u,
                            .max_raster_bytes = 1024u * 1024u,
                            .max_entries = 8u,
                            .max_dimension = 256u});
  const SvgRasterRequest request{.physical_width = 32u,
                                 .physical_height = 24u,
                                 .dpi_scale_x = 1.5f,
                                 .dpi_scale_y = 1.5f};
  std::string error;
  const auto first = rasterizer.rasterize("ui/icon", 1u, asset, request, &error);
  assert(first);
  assert(first->width == 32u && first->height == 24u);
  assert(first->stride == 32u * 4u);
  assert(first->pixels.size() == 32u * 24u * 4u);
  const auto cached = rasterizer.rasterize("ui/icon", 1u, asset, request, &error);
  assert(cached == first);

  const auto next_generation =
      rasterizer.rasterize("ui/icon", 2u, asset, request, &error);
  assert(next_generation);
  assert(next_generation != first);
  assert(rasterizer.cacheSize() == 2u);

  SvgRasterRequest tinted = request;
  tinted.tint = SvgTint{.red = 255u, .green = 128u, .blue = 128u, .alpha = 192u};
  const auto tint_raster = rasterizer.rasterize("ui/icon", 2u, asset, tinted, &error);
  assert(tint_raster && tint_raster != next_generation);

  rasterizer.invalidate("ui/icon");
  assert(rasterizer.cacheSize() == 0u);
  assert(rasterizer.cacheBytes() == 0u);

  const karma::assets::SvgAsset unsafe{
      .source_utf8 = "<svg xmlns=\"http://www.w3.org/2000/svg\"><script/></svg>"};
  assert(!rasterizer.rasterize("ui/unsafe", 1u, unsafe, request, &error));
  assert(!error.empty());
}

void testSvgSafetyAndEviction() {
  using namespace karma::ui::native;
  const std::string blue_source = R"SVG(<svg xmlns="http://www.w3.org/2000/svg"
      width="16" height="16"><rect width="16" height="16" fill="#4080ff"/></svg>)SVG";
  const std::string red_source = R"SVG(<svg xmlns="http://www.w3.org/2000/svg"
      width="16" height="16"><rect width="16" height="16" fill="#ff2040"/></svg>)SVG";
  const karma::assets::SvgAsset blue{
      .source_utf8 = blue_source,
      .content_hash = karma::assets::hashString(blue_source)};
  const karma::assets::SvgAsset red{
      .source_utf8 = red_source,
      .content_hash = karma::assets::hashString(red_source)};
  const SvgRasterRequest request{.physical_width = 16u, .physical_height = 16u};
  constexpr std::size_t raster_bytes = 16u * 16u * 4u;
  std::string error;

  // Two entries fit. Touching A before inserting C must evict B, proving the
  // cache uses recency rather than insertion order.
  SvgRasterizer lru({.cache_budget_bytes = raster_bytes * 2u,
                     .max_raster_bytes = raster_bytes,
                     .max_entries = 3u,
                     .max_dimension = 16u});
  const auto a = lru.rasterize("ui/a", 1u, blue, request, &error);
  const auto b = lru.rasterize("ui/b", 1u, blue, request, &error);
  assert(a && b);
  assert(lru.cacheSize() == 2u);
  assert(lru.cacheBytes() == raster_bytes * 2u);
  assert(lru.rasterize("ui/a", 1u, blue, request, &error) == a);
  const auto c = lru.rasterize("ui/c", 1u, blue, request, &error);
  assert(c);
  assert(lru.cacheSize() == 2u);
  assert(lru.rasterize("ui/a", 1u, blue, request, &error) == a);
  const auto b_after_eviction =
      lru.rasterize("ui/b", 1u, blue, request, &error);
  assert(b_after_eviction && b_after_eviction != b);
  assert(lru.cacheSize() == 2u);

  // Content and generation are independent key components. Invalidation by
  // asset key removes every retained version without invalidating shared
  // rasters already handed to callers.
  const auto changed_content = lru.rasterize("ui/a", 1u, red, request, &error);
  assert(changed_content && changed_content != a);
  const auto changed_generation =
      lru.rasterize("ui/a", 2u, red, request, &error);
  assert(changed_generation && changed_generation != changed_content);
  lru.invalidate("ui/a");
  assert(a->pixels.size() == raster_bytes);
  const std::size_t after_invalidate = lru.cacheSize();
  assert(after_invalidate <= 1u);

  // Per-raster safety is separate from the total cache budget, and failures
  // never add a partial entry.
  SvgRasterizer too_small({.cache_budget_bytes = raster_bytes * 4u,
                           .max_raster_bytes = raster_bytes - 1u,
                           .max_entries = 4u,
                           .max_dimension = 16u});
  assert(!too_small.rasterize("ui/large", 1u, blue, request, &error));
  assert(error.find("per-image byte limit") != std::string::npos);
  assert(too_small.cacheSize() == 0u);
  assert(too_small.cacheBytes() == 0u);

  SvgRasterizer dimension_limit({.cache_budget_bytes = raster_bytes,
                                 .max_raster_bytes = raster_bytes * 4u,
                                 .max_entries = 1u,
                                 .max_dimension = 15u});
  assert(!dimension_limit.rasterize("ui/wide", 1u, blue, request, &error));
  assert(error.find("dimensions") != std::string::npos);

  // A valid raster larger than the total budget is returned but deliberately
  // not cached, so the caller does not lose rendering just because caching is
  // disabled or tightly constrained.
  SvgRasterizer uncached({.cache_budget_bytes = raster_bytes - 1u,
                          .max_raster_bytes = raster_bytes,
                          .max_entries = 4u,
                          .max_dimension = 16u});
  const auto transient_a =
      uncached.rasterize("ui/transient", 1u, blue, request, &error);
  const auto transient_b =
      uncached.rasterize("ui/transient", 1u, blue, request, &error);
  assert(transient_a && transient_b && transient_a != transient_b);
  assert(uncached.cacheSize() == 0u);
  assert(uncached.cacheBytes() == 0u);
  uncached.clear();
  assert(uncached.cacheSize() == 0u);
}

void testProviderFreePresentationResourceLifecycle() {
  using karma::ui::ImageSource;
  using karma::ui::native::PresentationResources;
  using karma::ui::native::runtime_dom::Node;

  karma::assets::AssetRegistry assets;
  karma::ui::native::TextEngine text_engine;
  const std::vector<std::uint8_t> font_bytes =
      readBytes(std::filesystem::path(KARMA_TEST_ASSET_DIR) /
                "Roboto-Black.ttf");
  const auto external_font = text_engine.registerFont(
      "external-font", karma::assets::FontAsset{.bytes = font_bytes});
  assert(external_font.has_value());
  PresentationResources resources(assets, nullptr, text_engine);
  assert(resources.frame() == 0u);
  assert(resources.resourceGeneration() == 1u);

  resources.advanceFrame();
  assert(resources.frame() == 1u);
  assert(resources.resourceGeneration() == 1u);

  assert(assets.registerSvgAsset(
      "ui/resource-icon",
      {.source_utf8 =
           R"(<svg xmlns="http://www.w3.org/2000/svg" width="13" height="7"/>)",
       .content_hash = "first"}));
  const auto first_size =
      resources.intrinsicSize(ImageSource::asset("ui/resource-icon"));
  assert(first_size.has_value());
  assert(first_size->first == 13.0f && first_size->second == 7.0f);
  assert(resources.resolveTexture(ImageSource::asset("ui/resource-icon"),
                                  13.0f, 7.0f, {1, 1, 1, 1}, 1.0f,
                                  1.0f) == 0u);

  assert(assets.unregisterSvgAsset("ui/resource-icon"));
  assert(assets.registerSvgAsset(
      "ui/resource-icon",
      {.source_utf8 =
           R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="9"/>)",
       .content_hash = "second"}));
  const auto refreshed_size =
      resources.intrinsicSize(ImageSource::asset("ui/resource-icon"));
  assert(refreshed_size.has_value());
  assert(refreshed_size->first == 4.0f && refreshed_size->second == 9.0f);

  karma::assets::FontAsset font{
      .bytes = font_bytes,
      .content_hash = "font-v1",
  };
  assert(assets.registerFontAsset("ui/resource-font", std::move(font)));
  Node root;
  root.font_sources.push_back({.registration_key = "ui/resource-font#face=0",
                               .asset_key = "ui/resource-font",
                               .face_index = 0u});
  const std::uint64_t before_font = resources.resourceGeneration();
  assert(resources.ensureTreeFonts(root));
  assert(resources.resourceGeneration() != before_font);
  assert(text_engine.findFont("ui/resource-font#face=0").has_value());
  const std::uint64_t registered_generation = resources.resourceGeneration();
  assert(!resources.ensureTreeFonts(root));
  assert(resources.resourceGeneration() == registered_generation);

  assert(assets.unregisterFontAsset("ui/resource-font"));
  assert(resources.ensureTreeFonts(root));
  assert(resources.resourceGeneration() != registered_generation);
  assert(!text_engine.findFont("ui/resource-font#face=0").has_value());

  karma::rendering::TextureDesc desc{
      .width = 1,
      .height = 1,
      .format = karma::rendering::TextureFormat::RGBA8,
  };
  karma::rendering::TextureUploadData upload;
  assert(!resources.createImage(desc, upload).valid());
  assert(!resources.updateImage({}, upload));
  assert(!resources.destroyImage({}));

  // Re-register an owned key, then let the borrower replace that exact key.
  // Shutdown may unregister only the FontId that this resource owner installed.
  assert(assets.registerFontAsset(
      "ui/resource-font", karma::assets::FontAsset{.bytes = font_bytes}));
  assert(resources.ensureTreeFonts(root));
  const auto replacement_font = text_engine.registerFont(
      "ui/resource-font#face=0",
      karma::assets::FontAsset{.bytes = font_bytes});
  assert(replacement_font.has_value());

  const std::uint64_t frame_before_shutdown = resources.frame();
  resources.shutdown();
  const std::uint64_t generation_after_shutdown =
      resources.resourceGeneration();
  assert(text_engine.findFont("external-font") == external_font);
  assert(text_engine.findFont("ui/resource-font#face=0") == replacement_font);

  // Shutdown is terminal and idempotent. Every mutating or resolving entry
  // point becomes a safe no-op, while diagnostic counters remain stable.
  resources.shutdown();
  resources.advanceFrame();
  resources.pruneCachedTextures();
  assert(resources.frame() == frame_before_shutdown);
  assert(resources.resourceGeneration() == generation_after_shutdown);
  assert(!resources.ensureTreeFonts(root));
  assert(!resources.ensureGlyphPlacement({}, 16.0f).has_value());
  assert(!resources
              .intrinsicSize(ImageSource::asset("ui/resource-icon"))
              .has_value());
  assert(resources.resolveTexture(ImageSource::asset("ui/resource-icon"),
                                  4.0f, 9.0f, {1, 1, 1, 1}, 1.0f,
                                  1.0f) == 0u);
  assert(!resources.createImage(desc, upload).valid());
  assert(!resources.updateImage({}, upload));
  assert(!resources.destroyImage({}));
}

}  // namespace

int main() {
  testRetainedDrawDataAssembly();
  testRetainedDrawDataByteCost();
  testRetainedFragmentEvictionPolicy();
  testRetainedFragmentTreeBudgetEnforcement();
  testFramebufferScissor();
  testFontFaceVariantsAndCollections();
  testTextShaping();
  testUnicodeSegmentationAndCacheInvalidation();
  testSvgRasterCache();
  testSvgSafetyAndEviction();
  testProviderFreePresentationResourceLifecycle();
  std::cout << "UI presentation tests passed\n";
  return 0;
}
