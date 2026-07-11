#include "features/ui/native/layout_engine.h"
#include "features/ui/native/canvas_layout.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

using karma::ui::layout::Alignment;
using karma::ui::layout::Display;
using karma::ui::layout::FlexDirection;
using karma::ui::layout::GridTrack;
using karma::ui::layout::LayoutEngine;
using karma::ui::layout::LayoutNode;
using karma::ui::layout::LayoutOptions;
using karma::ui::layout::Length;
using karma::ui::layout::LengthUnit;
using karma::ui::layout::Rect;
using karma::ui::layout::WritingDirection;

bool near(float left, float right, float epsilon = 0.001f) {
  return std::abs(left - right) <= epsilon;
}

void expectRect(const LayoutNode& node,
                float x,
                float y,
                float width,
                float height) {
  assert(near(node.rect.x, x));
  assert(near(node.rect.y, y));
  assert(near(node.rect.width, width));
  assert(near(node.rect.height, height));
}

void testTrackGrammar() {
  std::vector<GridTrack> tracks;
  std::string error;
  assert(karma::ui::layout::parseTrackList(
      "80px 25% minmax(40px, 2fr) repeat(2, 1fr auto)", tracks, &error));
  assert(error.empty());
  assert(tracks.size() == 4);
  assert(tracks[0].breadth == Length::pixels(80.0f));
  assert(tracks[1].breadth == Length::percent(25.0f));
  assert(tracks[2].kind == GridTrack::Kind::MinMax);
  assert(tracks[2].minimum == Length::pixels(40.0f));
  assert(tracks[2].maximum == Length::fraction(2.0f));
  assert(tracks[3].kind == GridTrack::Kind::Repeat);
  assert(tracks[3].repeat_count == 2);
  assert(tracks[3].repeated_tracks.size() == 2);

  assert(karma::ui::layout::parseLength("1.5fr")->unit ==
         LengthUnit::Fraction);
  assert(!karma::ui::layout::parseTrackList("repeat(auto-fit, 1fr)", tracks,
                                            &error));
  assert(!error.empty());
  assert(!karma::ui::layout::parseTrackList("minmax(1fr, 20px)", tracks,
                                            &error));
}

void testNestedBlockAndFlex() {
  LayoutNode root;
  root.style.display = Display::Flex;
  root.style.flex_direction = FlexDirection::Row;

  LayoutNode sidebar;
  sidebar.style.width = Length::pixels(100.0f);

  LayoutNode body;
  body.style.display = Display::Block;
  body.style.flex_grow = 1.0f;

  LayoutNode header;
  header.style.height = Length::pixels(40.0f);

  LayoutNode content;
  content.style.flex_grow = 1.0f;

  LayoutNode half_width;
  half_width.style.width = Length::percent(50.0f);
  half_width.style.height = Length::pixels(20.0f);

  root.appendChild(sidebar);
  root.appendChild(body);
  body.appendChild(header);
  body.appendChild(content);
  content.appendChild(half_width);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 600.0f, .height = 300.0f}});
  assert(result);
  expectRect(root, 0.0f, 0.0f, 600.0f, 300.0f);
  expectRect(sidebar, 0.0f, 0.0f, 100.0f, 300.0f);
  expectRect(body, 100.0f, 0.0f, 500.0f, 300.0f);
  expectRect(header, 100.0f, 0.0f, 500.0f, 40.0f);
  expectRect(content, 100.0f, 40.0f, 500.0f, 260.0f);
  expectRect(half_width, 100.0f, 40.0f, 250.0f, 20.0f);
}

void testExplicitGridTracksAndSpans() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {
      GridTrack::pixels(100.0f), GridTrack::fraction(1.0f),
      GridTrack::fraction(2.0f)};
  root.style.grid_template_rows = {GridTrack::percent(50.0f),
                                   GridTrack::fraction(1.0f)};
  root.style.column_gap = Length::pixels(10.0f);
  root.style.row_gap = Length::pixels(10.0f);

  LayoutNode first;
  first.style.grid_column.start = 1;
  first.style.grid_row.start = 1;
  LayoutNode second;
  second.style.grid_column.start = 2;
  second.style.grid_row.start = 1;
  LayoutNode spanning;
  spanning.style.grid_column.start = 2;
  spanning.style.grid_column.span = 2;
  spanning.style.grid_row.start = 2;
  root.appendChild(first);
  root.appendChild(second);
  root.appendChild(spanning);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 600.0f, .height = 300.0f}});
  assert(result);
  expectRect(first, 0.0f, 0.0f, 100.0f, 150.0f);
  expectRect(second, 110.0f, 0.0f, 160.0f, 150.0f);
  expectRect(spanning, 110.0f, 160.0f, 490.0f, 140.0f);
}

void testRepeatMinmaxAndImplicitTracks() {
  LayoutNode root;
  root.style.display = Display::Grid;
  assert(karma::ui::layout::parseTrackList(
      "repeat(2, minmax(80px, 1fr))", root.style.grid_template_columns));
  root.style.grid_template_rows = {GridTrack::pixels(40.0f)};
  root.style.grid_auto_rows = {GridTrack::pixels(30.0f)};
  root.style.column_gap = Length::pixels(10.0f);

  LayoutNode first;
  LayoutNode second;
  LayoutNode third;
  root.appendChild(first);
  root.appendChild(second);
  root.appendChild(third);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 500.0f, .height = 70.0f}});
  assert(result);
  expectRect(first, 0.0f, 0.0f, 245.0f, 40.0f);
  expectRect(second, 255.0f, 0.0f, 245.0f, 40.0f);
  expectRect(third, 0.0f, 40.0f, 245.0f, 30.0f);
}

void testNonDenseAutomaticPlacement() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {
      GridTrack::pixels(100.0f), GridTrack::pixels(100.0f),
      GridTrack::pixels(100.0f)};
  root.style.grid_auto_rows = {GridTrack::pixels(50.0f)};

  LayoutNode wide_first;
  wide_first.style.grid_column.span = 2;
  LayoutNode wide_second;
  wide_second.style.grid_column.span = 2;
  LayoutNode narrow_third;
  root.appendChild(wide_first);
  root.appendChild(wide_second);
  root.appendChild(narrow_third);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 300.0f, .height = 100.0f}});
  assert(result);
  expectRect(wide_first, 0.0f, 0.0f, 200.0f, 50.0f);
  expectRect(wide_second, 0.0f, 50.0f, 200.0f, 50.0f);
  // Non-dense placement continues at the cursor instead of filling row 1's hole.
  expectRect(narrow_third, 200.0f, 50.0f, 100.0f, 50.0f);
}

void testColumnAutomaticPlacement() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_rows = {GridTrack::pixels(50.0f),
                                   GridTrack::pixels(50.0f)};
  root.style.grid_auto_columns = {GridTrack::pixels(100.0f)};
  root.style.grid_auto_flow = karma::ui::layout::GridAutoFlow::Column;

  LayoutNode first;
  LayoutNode second;
  LayoutNode third;
  root.appendChild(first);
  root.appendChild(second);
  root.appendChild(third);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 200.0f, .height = 100.0f}});
  assert(result);
  expectRect(first, 0.0f, 0.0f, 100.0f, 50.0f);
  expectRect(second, 0.0f, 50.0f, 100.0f, 50.0f);
  expectRect(third, 100.0f, 0.0f, 100.0f, 50.0f);
}

void testIntrinsicTracksAndItemAlignment() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::automatic(),
                                      GridTrack::fraction(1.0f)};
  root.style.grid_template_rows = {GridTrack::pixels(60.0f)};

  LayoutNode intrinsic_track;
  intrinsic_track.intrinsic_size = {.width = 80.0f, .height = 10.0f};
  intrinsic_track.style.grid_column.start = 1;
  intrinsic_track.style.grid_row.start = 1;

  LayoutNode aligned;
  aligned.intrinsic_size = {.width = 40.0f, .height = 20.0f};
  aligned.style.grid_column.start = 2;
  aligned.style.grid_row.start = 1;
  aligned.style.justify_self = Alignment::End;
  aligned.style.align_self = Alignment::Center;

  root.appendChild(intrinsic_track);
  root.appendChild(aligned);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 300.0f, .height = 60.0f}});
  assert(result);
  expectRect(intrinsic_track, 0.0f, 0.0f, 80.0f, 60.0f);
  expectRect(aligned, 260.0f, 20.0f, 40.0f, 20.0f);
}

void testAutoRowRemeasuresAgainstColumnWidth() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::pixels(100.0f)};
  root.style.grid_template_rows = {GridTrack::automatic()};
  root.style.align_content = Alignment::Start;

  LayoutNode wrapping_text;
  wrapping_text.measure = [](const karma::ui::layout::MeasureConstraints& constraints) {
    const bool constrained =
        constraints.width_mode != karma::ui::layout::MeasureMode::Undefined;
    return karma::ui::layout::Size{
        .width = 160.0f,
        .height = constrained && constraints.available_width <= 100.0f ? 40.0f
                                                                        : 20.0f,
    };
  };
  root.appendChild(wrapping_text);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 100.0f, .height = 100.0f}});
  assert(result);
  expectRect(wrapping_text, 0.0f, 0.0f, 100.0f, 40.0f);
}

void testNestedGridAndFlex() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::fraction(1.0f)};
  root.style.grid_template_rows = {GridTrack::fraction(1.0f)};

  LayoutNode flex;
  flex.style.display = Display::Flex;
  flex.style.flex_direction = FlexDirection::Row;
  LayoutNode left;
  left.style.flex_grow = 1.0f;
  LayoutNode right;
  right.style.flex_grow = 1.0f;
  flex.appendChild(left);
  flex.appendChild(right);
  root.appendChild(flex);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 400.0f, .height = 100.0f}});
  assert(result);
  expectRect(flex, 0.0f, 0.0f, 400.0f, 100.0f);
  expectRect(left, 0.0f, 0.0f, 200.0f, 100.0f);
  expectRect(right, 200.0f, 0.0f, 200.0f, 100.0f);
}

void testFlexContainingGrid() {
  LayoutNode root;
  root.style.display = Display::Flex;
  root.style.flex_direction = FlexDirection::Row;

  LayoutNode sidebar;
  sidebar.style.width = Length::pixels(100.0f);
  LayoutNode grid;
  grid.style.display = Display::Grid;
  grid.style.flex_grow = 1.0f;
  grid.style.grid_template_columns = {GridTrack::fraction(1.0f),
                                      GridTrack::fraction(1.0f)};
  grid.style.grid_template_rows = {GridTrack::fraction(1.0f)};
  LayoutNode first;
  LayoutNode second;
  grid.appendChild(first);
  grid.appendChild(second);
  root.appendChild(sidebar);
  root.appendChild(grid);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 400.0f, .height = 100.0f}});
  assert(result);
  expectRect(sidebar, 0.0f, 0.0f, 100.0f, 100.0f);
  expectRect(grid, 100.0f, 0.0f, 300.0f, 100.0f);
  expectRect(first, 100.0f, 0.0f, 150.0f, 100.0f);
  expectRect(second, 250.0f, 0.0f, 150.0f, 100.0f);
}

void testMeasuredGridPaddingIsNotDoubleCounted() {
  LayoutNode root;
  root.style.display = Display::Flex;
  root.style.flex_direction = FlexDirection::Row;
  root.style.align_items = Alignment::Start;

  LayoutNode grid;
  grid.style.display = Display::Grid;
  grid.style.padding = {
      .left = Length::pixels(10.0f),
      .top = Length::pixels(10.0f),
      .right = Length::pixels(10.0f),
      .bottom = Length::pixels(10.0f),
  };
  grid.style.grid_template_columns = {GridTrack::pixels(100.0f)};
  grid.style.grid_template_rows = {GridTrack::pixels(20.0f)};
  LayoutNode item;
  grid.appendChild(item);
  root.appendChild(grid);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 300.0f, .height = 100.0f}});
  assert(result);
  expectRect(grid, 0.0f, 0.0f, 120.0f, 40.0f);
  expectRect(item, 10.0f, 10.0f, 100.0f, 20.0f);
}

void testIntrinsicGridItemIncludesItsPadding() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::pixels(100.0f)};
  root.style.grid_template_rows = {GridTrack::pixels(50.0f)};
  root.style.justify_items = Alignment::Start;
  root.style.align_items = Alignment::Start;

  LayoutNode item;
  item.intrinsic_size = {.width = 40.0f, .height = 20.0f};
  item.style.padding = {
      .left = Length::pixels(10.0f),
      .top = Length::pixels(5.0f),
      .right = Length::pixels(10.0f),
      .bottom = Length::pixels(5.0f),
  };
  root.appendChild(item);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 100.0f, .height = 50.0f}});
  assert(result);
  expectRect(item, 0.0f, 0.0f, 60.0f, 30.0f);
}

void testMeasureReceivesContentBoxConstraint() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::pixels(100.0f)};
  root.style.grid_template_rows = {GridTrack::pixels(50.0f)};
  root.style.justify_items = Alignment::Start;
  root.style.align_items = Alignment::Start;

  bool saw_inner_width = false;
  LayoutNode item;
  item.style.padding.left = Length::pixels(10.0f);
  item.style.padding.right = Length::pixels(10.0f);
  item.measure = [&](const karma::ui::layout::MeasureConstraints& constraints) {
    if (constraints.width_mode == karma::ui::layout::MeasureMode::AtMost &&
        near(constraints.available_width, 80.0f)) {
      saw_inner_width = true;
    }
    return karma::ui::layout::Size{.width = 80.0f, .height = 20.0f};
  };
  root.appendChild(item);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 100.0f, .height = 50.0f}});
  assert(result);
  assert(saw_inner_width);
  expectRect(item, 0.0f, 0.0f, 100.0f, 20.0f);
}

void testExplicitGridItemCanOverflowItsCell() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::pixels(100.0f)};
  root.style.grid_template_rows = {GridTrack::pixels(50.0f)};
  root.style.justify_items = Alignment::Start;
  root.style.align_items = Alignment::Start;

  LayoutNode item;
  item.style.width = Length::pixels(150.0f);
  item.style.height = Length::pixels(70.0f);
  root.appendChild(item);

  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 100.0f, .height = 50.0f}});
  assert(result);
  expectRect(item, 0.0f, 0.0f, 150.0f, 70.0f);
}

void testRtlAndDeterminism() {
  LayoutNode root;
  root.style.display = Display::Grid;
  root.style.grid_template_columns = {GridTrack::pixels(100.0f),
                                      GridTrack::pixels(100.0f)};
  root.style.grid_template_rows = {GridTrack::pixels(50.0f)};
  LayoutNode logical_first;
  logical_first.style.grid_column.start = 1;
  logical_first.style.grid_row.start = 1;
  LayoutNode logical_second;
  logical_second.style.grid_column.start = 2;
  logical_second.style.grid_row.start = 1;
  root.appendChild(logical_first);
  root.appendChild(logical_second);

  const LayoutOptions options{
      .available_size = {.width = 200.0f, .height = 50.0f},
      .direction = WritingDirection::RightToLeft,
  };
  const auto first_result = LayoutEngine{}.calculate(root, options);
  assert(first_result);
  expectRect(logical_first, 100.0f, 0.0f, 100.0f, 50.0f);
  expectRect(logical_second, 0.0f, 0.0f, 100.0f, 50.0f);
  const Rect first_rect = logical_first.rect;
  const Rect second_rect = logical_second.rect;

  const auto second_result = LayoutEngine{}.calculate(root, options);
  assert(second_result);
  assert(logical_first.rect == first_rect);
  assert(logical_second.rect == second_rect);
}

void testInvalidRetainedTree() {
  LayoutNode root;
  LayoutNode child;
  root.appendChild(child);
  root.appendChild(child);
  const auto result = LayoutEngine{}.calculate(
      root, LayoutOptions{.available_size = {.width = 10.0f, .height = 10.0f}});
  assert(!result);
  assert(result.error.find("more than once") != std::string::npos);

  LayoutNode invalid_grid;
  invalid_grid.style.display = Display::Grid;
  invalid_grid.style.grid_template_columns = {GridTrack::fraction(0.0f)};
  const auto invalid_track = LayoutEngine{}.calculate(
      invalid_grid,
      LayoutOptions{.available_size = {.width = 10.0f, .height = 10.0f}});
  assert(!invalid_track);
  assert(invalid_track.error.find("invalid track") != std::string::npos);
}

void testCanvasScaleModesAndSafeArea() {
  using karma::ui::native::CanvasScaleMode;
  using karma::ui::native::CanvasSpec;
  using karma::ui::native::SafeAreaInsets;
  using karma::ui::native::resolveCanvas;

  const CanvasSpec fit{.scale_mode = CanvasScaleMode::Fit,
                       .reference_width = 1280.0f,
                       .reference_height = 720.0f,
                       .use_platform_safe_area = true};
  const auto fitted = resolveCanvas(
      fit, 1920.0f, 1200.0f,
      SafeAreaInsets{.left = 40.0f, .top = 20.0f, .right = 40.0f, .bottom = 20.0f});
  assert(near(fitted.scale_x, 1.4375f));
  assert(near(fitted.scale_y, 1.4375f));
  assert(near(fitted.safe_window_rect.x, 40.0f));
  assert(near(fitted.safe_window_rect.width, 1840.0f));
  assert(near(fitted.layout_rect.width, 1280.0f));
  assert(near(fitted.layout_rect.height, 720.0f));
  const auto fitted_window = fitted.layoutToWindow(fitted.layout_rect);
  assert(near(fitted_window.x, 40.0f));
  assert(near(fitted_window.y, 82.5f));
  assert(near(fitted_window.width, 1840.0f));
  assert(near(fitted_window.height, 1035.0f));
  const auto round_trip = fitted.windowToLayout(fitted_window.x, fitted_window.y);
  assert(near(round_trip.x, fitted.layout_rect.x));
  assert(near(round_trip.y, fitted.layout_rect.y));

  CanvasSpec fill = fit;
  fill.scale_mode = CanvasScaleMode::Fill;
  const auto filled = resolveCanvas(fill, 1920.0f, 1200.0f);
  assert(near(filled.scale_x, 5.0f / 3.0f));
  assert(filled.layout_rect.x < 0.0f);
  assert(near(filled.layout_clip.width, 1152.0f));

  CanvasSpec stretch = fit;
  stretch.scale_mode = CanvasScaleMode::Stretch;
  stretch.use_platform_safe_area = false;
  const auto stretched = resolveCanvas(stretch, 1920.0f, 1080.0f);
  assert(near(stretched.scale_x, 1.5f));
  assert(near(stretched.scale_y, 1.5f));

  CanvasSpec pixels = fit;
  pixels.scale_mode = CanvasScaleMode::PixelPerfect;
  pixels.use_platform_safe_area = false;
  const auto pixel_2x = resolveCanvas(pixels, 3000.0f, 1800.0f);
  assert(near(pixel_2x.scale_x, 2.0f));
  const auto pixel_small = resolveCanvas(pixels, 640.0f, 360.0f);
  assert(near(pixel_small.scale_x, 0.5f));

  CanvasSpec logical;
  logical.use_platform_safe_area = true;
  const auto safe = resolveCanvas(
      logical, 100.0f, 80.0f,
      SafeAreaInsets{.left = 120.0f, .top = -5.0f, .right = 10.0f, .bottom = 90.0f});
  assert(near(safe.layout_rect.x, 100.0f));
  assert(near(safe.layout_rect.width, 0.0f));
  assert(near(safe.layout_rect.height, 0.0f));
}

void testCanvasAndAnchorParsing() {
  using nlohmann::json;
  using karma::ui::native::CanvasScaleMode;
  using karma::ui::native::parseAnchorSpec;
  using karma::ui::native::parseCanvasSpec;

  const json canvas = {{"reference_size", {1920, 1080}},
                       {"scale_mode", "pixel_perfect"},
                       {"safe_area", "platform"}};
  const auto parsed_canvas = parseCanvasSpec(&canvas);
  assert(parsed_canvas);
  assert(parsed_canvas.value->scale_mode == CanvasScaleMode::PixelPerfect);
  assert(parsed_canvas.value->use_platform_safe_area);

  const json missing_reference = {{"scale_mode", "fit"}};
  assert(!parseCanvasSpec(&missing_reference));
  const json bad_safe_area = {{"safe_area", "notch-ish"}};
  assert(!parseCanvasSpec(&bad_safe_area));

  const json anchored = {
      {"anchors", {{"min", {0.5, 0.0}}, {"max", {1.0, 1.0}}}},
      {"pivot", {1.0, 0.5}},
      {"position", {-12.0, 4.0}},
      {"offsets", {{"left", 8.0}, {"right", 16.0}}},
  };
  const auto parsed_anchor = parseAnchorSpec(anchored);
  assert(parsed_anchor);
  assert(parsed_anchor.value.has_value());
  assert(near(parsed_anchor.value->minimum.x, 0.5f));
  assert(near(parsed_anchor.value->offset_right, 16.0f));
  assert(!parseAnchorSpec(json{{"pivot", {0.5, 0.5}}}));
  const json reversed = {
      {"anchors", {{"min", {0.8, 0.0}}, {"max", {0.2, 1.0}}}}};
  assert(!parseAnchorSpec(reversed));
}

void testDeterministicAnchorConstraints() {
  using karma::ui::native::AnchorSpec;
  using karma::ui::native::CanvasRect;
  using karma::ui::native::resolveAnchor;

  const CanvasRect parent{.x = 100.0f, .y = 50.0f,
                          .width = 800.0f, .height = 600.0f};
  const CanvasRect measured{.width = 200.0f, .height = 80.0f};
  const AnchorSpec bottom_right{
      .minimum = {1.0f, 1.0f}, .maximum = {1.0f, 1.0f},
      .pivot = {1.0f, 1.0f}, .position = {-24.0f, -16.0f}};
  const CanvasRect fixed = resolveAnchor(parent, measured, bottom_right);
  assert(near(fixed.x, 676.0f));
  assert(near(fixed.y, 554.0f));
  assert(near(fixed.width, 200.0f));
  assert(near(fixed.height, 80.0f));

  const AnchorSpec stretched{
      .minimum = {0.0f, 0.25f}, .maximum = {1.0f, 0.75f},
      .position = {5.0f, -5.0f},
      .offset_left = 10.0f, .offset_top = 20.0f,
      .offset_right = 30.0f, .offset_bottom = 40.0f};
  const CanvasRect stretch = resolveAnchor(parent, measured, stretched);
  assert(near(stretch.x, 115.0f));
  assert(near(stretch.y, 215.0f));
  assert(near(stretch.width, 760.0f));
  assert(near(stretch.height, 240.0f));
  assert(stretch == resolveAnchor(parent, measured, stretched));
}

}  // namespace

int main() {
  testTrackGrammar();
  testNestedBlockAndFlex();
  testExplicitGridTracksAndSpans();
  testRepeatMinmaxAndImplicitTracks();
  testNonDenseAutomaticPlacement();
  testColumnAutomaticPlacement();
  testIntrinsicTracksAndItemAlignment();
  testAutoRowRemeasuresAgainstColumnWidth();
  testNestedGridAndFlex();
  testFlexContainingGrid();
  testMeasuredGridPaddingIsNotDoubleCounted();
  testIntrinsicGridItemIncludesItsPadding();
  testMeasureReceivesContentBoxConstraint();
  testExplicitGridItemCanOverflowItsCell();
  testRtlAndDeterminism();
  testInvalidRetainedTree();
  testCanvasScaleModesAndSafeArea();
  testCanvasAndAnchorParsing();
  testDeterministicAnchorConstraints();
  std::cout << "ui layout tests passed\n";
  return 0;
}
