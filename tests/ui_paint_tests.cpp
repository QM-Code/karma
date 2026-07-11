#include "features/ui/native/paint_engine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {

using karma::ui::paint::Mesh;
using karma::ui::paint::Rect;
using karma::ui::paint::Vec2;

bool near(float left, float right, float epsilon = 1.0e-4f) {
  return std::abs(left - right) <= epsilon;
}

void assertInside(const Mesh& mesh, Rect box) {
  for (const auto& vertex : mesh.vertices) {
    assert(vertex.position.x >= box.x - 1.0e-3f);
    assert(vertex.position.x <= box.x + box.width + 1.0e-3f);
    assert(vertex.position.y >= box.y - 1.0e-3f);
    assert(vertex.position.y <= box.y + box.height + 1.0e-3f);
  }
}

void testMeshAndRoundedBoxes() {
  using namespace karma::ui::paint;
  std::string error;
  CornerRadii parsed_radii;
  assert(parseCornerRadii("4px 8px 12px 16px", parsed_radii, &error));
  assert(parsed_radii == CornerRadii(4.0f, 8.0f, 12.0f, 16.0f));
  assert(parseCornerRadii("5 10 15", parsed_radii, &error));
  assert(parsed_radii == CornerRadii(5.0f, 10.0f, 15.0f, 10.0f));
  assert(!parseCornerRadii("4px / 8px", parsed_radii, &error));
  assert(!parseCornerRadii("-1px", parsed_radii, &error));

  BorderWidths parsed_widths;
  assert(parseBorderWidths("1 2 3 4", parsed_widths, &error));
  assert(near(parsed_widths.left, 4.0f));
  assert(near(parsed_widths.top, 1.0f));
  assert(near(parsed_widths.right, 2.0f));
  assert(near(parsed_widths.bottom, 3.0f));
  assert(!parseBorderWidths("thin", parsed_widths, &error));

  const Rect box{10.0f, 20.0f, 100.0f, 50.0f};
  const CornerRadii normalized =
      normalizeCornerRadii(box, {80.0f, 80.0f, 80.0f, 80.0f});
  assert(near(normalized.top_left, 25.0f));
  assert(near(normalized.top_right, 25.0f));
  assert(near(normalized.bottom_right, 25.0f));
  assert(near(normalized.bottom_left, 25.0f));
  assert(normalizeCornerRadii({0, 0, -1, -1}, {4, 4, 4, 4}) ==
         CornerRadii(0, 0, 0, 0));

  const Mesh fill = roundedRectFill(
      box, {3.0f, 6.0f, 9.0f, 12.0f}, {0.2f, 0.4f, 0.6f, 0.8f}, 4u);
  assert(fill.valid());
  assert(fill.vertices.size() == 21u);
  assert(fill.indices.size() == 60u);
  assert(near(fill.vertices.front().position.x, 60.0f));
  assert(near(fill.vertices.front().position.y, 45.0f));
  assertInside(fill, box);
  for (const auto& vertex : fill.vertices) {
    assert(vertex.uv.x >= -1.0e-4f && vertex.uv.x <= 1.0001f);
    assert(vertex.uv.y >= -1.0e-4f && vertex.uv.y <= 1.0001f);
  }

  const Mesh border = roundedRectBorder(
      box, {15.0f, 10.0f, 5.0f, 20.0f}, {2.0f, 4.0f, 6.0f, 8.0f},
      {1.0f, 1.0f, 1.0f, 1.0f}, 3u);
  assert(border.valid());
  assert(border.vertices.size() == 32u);
  assert(border.indices.size() == 96u);
  assertInside(border, box);

  const Mesh solid_border = roundedRectBorder(
      {0.0f, 0.0f, 10.0f, 8.0f}, {}, {9.0f, 9.0f, 9.0f, 9.0f}, {}, 2u);
  assert(solid_border.valid());
  assert(!solid_border.empty());

  assert(roundedRectFill({0.0f, 0.0f, 0.0f, 10.0f}, {}, {}).empty());
  assert(roundedRectBorder(box, {}, {}, {}).empty());
  assert(roundedRectFill(
             box, {}, {std::numeric_limits<float>::quiet_NaN(), 0, 0, 1})
             .empty());

  Mesh combined;
  assert(combined.append(fill));
  assert(combined.append(border));
  assert(combined.valid());
  assert(combined.vertices.size() == fill.vertices.size() + border.vertices.size());
  assert(combined.indices[fill.indices.size()] >= fill.vertices.size());
  combined.clear();
  assert(combined.vertices.empty() && combined.indices.empty());

  Mesh broken = fill;
  broken.indices.push_back(99999u);
  assert(!broken.valid());
}

void testLinearGradients() {
  using namespace karma::ui::paint;
  LinearGradient parsed;
  std::string error;
  assert(parseLinearGradient(
      "linear-gradient(to right bottom, #f00 0%, rgba(0, 255, 0, .5), "
      "#0000ff 100%)",
      parsed, &error));
  assert(error.empty());
  assert(near(parsed.angle_degrees, 135.0f));
  assert(parsed.stops.size() == 3u);
  assert(near(parsed.stops[0].offset, 0.0f));
  assert(near(parsed.stops[1].offset, 0.5f));
  assert(near(parsed.stops[2].offset, 1.0f));
  assert(near(parsed.stops[1].color.g, 1.0f));
  assert(near(parsed.stops[1].color.a, 0.5f));

  assert(parseLinearGradient("LINEAR-GRADIENT(90deg, red 20%, blue 80%)",
                             parsed, &error));
  assert(parsed.stops.size() == 4u);  // Constant extensions cover the box.
  assert(near(parsed.stops.front().offset, 0.0f));
  assert(near(parsed.stops.back().offset, 1.0f));

  assert(!parseLinearGradient("linear-gradient(red)", parsed, &error));
  assert(!error.empty());
  assert(!parseLinearGradient("linear-gradient(to nowhere, red, blue)", parsed,
                              &error));
  assert(!parseLinearGradient("linear-gradient(red, url(x))", parsed, &error));

  const Rect box{0.0f, 0.0f, 100.0f, 20.0f};
  const LinearGradient horizontal{
      .angle_degrees = 90.0f,
      .stops = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
                {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}}};
  const Mesh mesh = linearGradientFill(box, horizontal);
  assert(mesh.valid());
  assert(mesh.vertices.size() == 4u);
  assert(mesh.indices.size() == 6u);
  assertInside(mesh, box);
  for (const auto& vertex : mesh.vertices) {
    if (near(vertex.position.x, 0.0f)) {
      assert(near(vertex.color.r, 1.0f));
      assert(near(vertex.color.b, 0.0f));
    }
    if (near(vertex.position.x, 100.0f)) {
      assert(near(vertex.color.r, 0.0f));
      assert(near(vertex.color.b, 1.0f));
    }
  }

  const LinearGradient three_stop{
      .angle_degrees = 180.0f,
      .stops = {{0.0f, {1, 0, 0, 1}},
                {0.4f, {0, 1, 0, 1}},
                {1.0f, {0, 0, 1, 1}}}};
  const Mesh split = linearGradientFill(box, three_stop);
  assert(split.valid());
  assert(split.indices.size() == 12u);
  assert(split.vertices.size() == 8u);
}

void testRadialGradients() {
  using namespace karma::ui::paint;
  RadialGradient parsed;
  std::string error;
  assert(parseRadialGradient(
      "radial-gradient(circle at left bottom, white 0%, #0008 75%, black)",
      parsed, &error));
  assert(error.empty());
  assert(parsed.circle);
  assert(near(parsed.center.x, 0.0f));
  assert(near(parsed.center.y, 1.0f));
  assert(parsed.stops.size() == 3u);
  assert(near(parsed.stops.back().offset, 1.0f));

  assert(parseRadialGradient(
      "radial-gradient(ellipse at 25% 75%, red, blue)", parsed, &error));
  assert(!parsed.circle);
  assert(near(parsed.center.x, 0.25f));
  assert(near(parsed.center.y, 0.75f));
  assert(!parseRadialGradient("radial-gradient(square, red, blue)", parsed,
                              &error));
  assert(!parseRadialGradient("radial-gradient(red)", parsed, &error));

  const RadialGradient centered{
      .center = {0.5f, 0.5f},
      .radius = {0.5f, 0.5f},
      .stops = {{0.0f, {1, 0, 0, 1}}, {1.0f, {0, 0, 1, 1}}}};
  const Rect box{5.0f, 7.0f, 80.0f, 40.0f};
  const Mesh mesh = radialGradientFill(box, centered, 4u, 4u);
  assert(mesh.valid());
  assert(mesh.vertices.size() == 25u);
  assert(mesh.indices.size() == 96u);
  assertInside(mesh, box);
  const auto& center = mesh.vertices[12u];
  assert(near(center.position.x, 45.0f));
  assert(near(center.position.y, 27.0f));
  assert(near(center.color.r, 1.0f));
  assert(near(center.color.b, 0.0f));
  assert(near(mesh.vertices.front().color.b, 1.0f));

  RadialGradient circle = centered;
  circle.circle = true;
  assert(radialGradientFill(box, circle, 2u, 2u).valid());
}

void testNineSlice() {
  using namespace karma::ui::paint;
  Insets insets;
  std::string error;
  assert(parseInsets("10px", insets, &error));
  assert(near(insets.left, 10.0f) && near(insets.bottom, 10.0f));
  assert(parseInsets("2 4px", insets, &error));
  assert(near(insets.top, 2.0f) && near(insets.right, 4.0f));
  assert(parseInsets("1 2 3", insets, &error));
  assert(near(insets.left, 2.0f) && near(insets.bottom, 3.0f));
  assert(parseInsets("1 2 3 4", insets, &error));
  assert(near(insets.left, 4.0f) && near(insets.top, 1.0f));
  assert(near(insets.right, 2.0f) && near(insets.bottom, 3.0f));
  assert(!parseInsets("1 2 3 4 5", insets, &error));
  assert(!parseInsets("-1", insets, &error));
  assert(!parseInsets("10%", insets, &error));

  NineSliceRepeat repeat;
  assert(parseNineSliceRepeat("stretch", repeat, &error));
  assert((repeat == NineSliceRepeat{NineSliceRepeatMode::Stretch,
                                    NineSliceRepeatMode::Stretch}));
  assert(parseNineSliceRepeat("round", repeat, &error));
  assert((repeat == NineSliceRepeat{NineSliceRepeatMode::Round,
                                    NineSliceRepeatMode::Round}));
  assert(parseNineSliceRepeat("REPEAT stretch", repeat, &error));
  assert((repeat == NineSliceRepeat{NineSliceRepeatMode::Repeat,
                                    NineSliceRepeatMode::Stretch}));
  const NineSliceRepeat last_valid = repeat;
  assert(!parseNineSliceRepeat("repeat round stretch", repeat, &error));
  assert(repeat == last_valid);
  assert(!parseNineSliceRepeat("space", repeat, &error));
  assert(repeat == last_valid);
  assert(!parseNineSliceRepeat("repeat, round", repeat, &error));
  assert(repeat == last_valid);

  const NineSlice slice{.source_size = {30.0f, 30.0f},
                        .source_slices = {10.0f, 10.0f, 10.0f, 10.0f},
                        .destination_slices = std::nullopt};
  const Rect destination{10.0f, 20.0f, 100.0f, 80.0f};
  const Mesh panel = nineSlicePanel(destination, slice);
  assert(panel.valid());
  assert(panel.vertices.size() == 16u);
  assert(panel.indices.size() == 54u);
  assertInside(panel, destination);
  assert(near(panel.vertices[0].uv.x, 0.0f));
  assert(near(panel.vertices[1].uv.x, 1.0f / 3.0f));
  assert(near(panel.vertices[2].uv.x, 2.0f / 3.0f));
  assert(near(panel.vertices[15].uv.y, 1.0f));

  NineSlice cropped = slice;
  cropped.source_uv = {0.25f, 0.1f, 0.5f, 0.8f};
  cropped.destination_slices = Insets{80.0f, 80.0f, 80.0f, 80.0f};
  const Mesh squeezed = nineSlicePanel({0.0f, 0.0f, 20.0f, 10.0f}, cropped);
  assert(squeezed.valid());
  assertInside(squeezed, {0.0f, 0.0f, 20.0f, 10.0f});
  assert(near(squeezed.vertices.front().uv.x, 0.25f));
  assert(near(squeezed.vertices.back().uv.x, 0.75f));
  assert(nineSlicePanel(destination, NineSlice{}).empty());

  NineSliceBuildStatus repeated_status;
  const Mesh repeated = nineSlicePanel(
      {0.0f, 0.0f, 35.0f, 40.0f},
      {.source_size = {30.0f, 30.0f},
       .source_slices = {10.0f, 10.0f, 10.0f, 10.0f},
       .source_uv = {0.25f, 0.1f, 0.5f, 0.8f},
       .repeat = {NineSliceRepeatMode::Repeat,
                  NineSliceRepeatMode::Stretch}},
      {}, &repeated_status);
  assert(repeated.valid());
  assert(repeated_status.generated_cells == 12u);
  assert(!repeated_status.cell_limit_reduced);
  assert(repeated.vertices.size() == repeated_status.generated_cells * 4u);
  assert(repeated.indices.size() == repeated_status.generated_cells * 6u);
  for (const auto& vertex : repeated.vertices) {
    // Repeated cells stay inside the authored atlas rectangle by half a source
    // texel, so the retained clamp sampler cannot bleed neighboring texels.
    assert(vertex.uv.x >= 0.25f + 0.5f * 0.5f / 30.0f - 0.0001f);
    assert(vertex.uv.x <= 0.75f - 0.5f * 0.5f / 30.0f + 0.0001f);
    assert(vertex.uv.y >= 0.1f + 0.8f * 0.5f / 30.0f - 0.0001f);
    assert(vertex.uv.y <= 0.9f - 0.8f * 0.5f / 30.0f + 0.0001f);
  }
  // Top-left is the first cell. The next two cells are the repeated top edge;
  // the second edge tile is cropped to half of the ten-pixel source motif.
  assert(near(repeated.vertices[8u].position.x, 20.0f));
  assert(near(repeated.vertices[9u].position.x, 25.0f));
  assert(near(repeated.vertices[8u].uv.x, 0.425f));
  assert(near(repeated.vertices[9u].uv.x,
              0.25f + 0.5f * 14.5f / 30.0f));

  NineSliceBuildStatus rounded_status;
  const Mesh rounded = nineSlicePanel(
      destination,
      {.source_size = {30.0f, 30.0f},
       .source_slices = {10.0f, 10.0f, 10.0f, 10.0f},
       .repeat = {NineSliceRepeatMode::Round,
                  NineSliceRepeatMode::Round}},
      {}, &rounded_status);
  assert(rounded.valid());
  assert(rounded_status.generated_cells == 80u);
  assert(!rounded_status.cell_limit_reduced);

  NineSliceBuildStatus asymmetric_status;
  const Rect asymmetric_box{0.0f, 0.0f, 100.0f, 80.0f};
  const Mesh asymmetric = nineSlicePanel(
      asymmetric_box,
      {.source_size = {40.0f, 30.0f},
       .source_slices = {4.0f, 6.0f, 8.0f, 3.0f},
       .destination_slices = Insets{8.0f, 12.0f, 4.0f, 6.0f},
       .repeat = {NineSliceRepeatMode::Repeat,
                  NineSliceRepeatMode::Repeat}},
      {}, &asymmetric_status);
  assert(asymmetric.valid());
  assertInside(asymmetric, asymmetric_box);
  assert(asymmetric_status.generated_cells == 28u);

  // The center inherits horizontal motif scale from the top/bottom edges and
  // vertical motif scale from the left/right edges.  For this asymmetric case
  // that gives two center columns matching the two top-edge columns, and six
  // center rows matching the six right-edge rows.
  // Cell order is row-major by nine-slice region, with four vertices per cell.
  assert(near(asymmetric.vertices[4u].position.x, 8.0f));
  assert(near(asymmetric.vertices[5u].position.x, 64.0f));
  constexpr std::size_t center_first_cell = 6u;
  assert(near(asymmetric.vertices[center_first_cell * 4u].position.x, 8.0f));
  assert(near(asymmetric.vertices[center_first_cell * 4u + 1u].position.x,
              64.0f));
  assert(near(asymmetric.vertices[center_first_cell * 4u].position.y, 12.0f));
  assert(near(asymmetric.vertices[center_first_cell * 4u + 3u].position.y,
              23.0f));

  // Repeated endpoints stay shared and land exactly on framebuffer pixels at
  // every supported showcase scale.  Treat each four-vertex cell as a
  // rectangle and verify that its physical endpoints are integral and that
  // every scanline interval is covered without a gap.
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    const Rect scaled_box{0.25f, 0.25f, 63.5f, 47.5f};
    const Mesh scaled = nineSlicePanel(
        scaled_box,
        {.source_size = {31.0f, 29.0f},
         .source_slices = {5.0f, 7.0f, 4.0f, 6.0f},
         .destination_slices = Insets{6.0f, 8.0f, 5.0f, 7.0f},
         .repeat = {NineSliceRepeatMode::Repeat,
                    NineSliceRepeatMode::Round},
         .pixel_scale = {scale, scale}});
    assert(scaled.valid());
    assert(scaled.vertices.size() % 4u == 0u);
    for (const auto& vertex : scaled.vertices) {
      assert(near(vertex.position.x * scale,
                  std::round(vertex.position.x * scale)));
      assert(near(vertex.position.y * scale,
                  std::round(vertex.position.y * scale)));
    }
    const int physical_left =
        static_cast<int>(std::round(scaled_box.x * scale));
    const int physical_top =
        static_cast<int>(std::round(scaled_box.y * scale));
    const int physical_right = static_cast<int>(
        std::round((scaled_box.x + scaled_box.width) * scale));
    const int physical_bottom = static_cast<int>(
        std::round((scaled_box.y + scaled_box.height) * scale));
    for (int y = physical_top; y < physical_bottom; ++y) {
      for (int x = physical_left; x < physical_right; ++x) {
        const float sample_x = static_cast<float>(x) + 0.5f;
        const float sample_y = static_cast<float>(y) + 0.5f;
        std::size_t coverage = 0u;
        for (std::size_t cell = 0u; cell < scaled.vertices.size(); cell += 4u) {
          const float cell_left = scaled.vertices[cell].position.x * scale;
          const float cell_top = scaled.vertices[cell].position.y * scale;
          const float cell_right = scaled.vertices[cell + 1u].position.x * scale;
          const float cell_bottom = scaled.vertices[cell + 3u].position.y * scale;
          if (sample_x >= cell_left && sample_x < cell_right &&
              sample_y >= cell_top && sample_y < cell_bottom) {
            ++coverage;
          }
        }
        assert(coverage == 1u);
      }
    }
  }

  NineSliceBuildStatus tiny_status;
  const Rect tiny_box{0.0f, 0.0f, 20.0f, 10.0f};
  const Mesh tiny = nineSlicePanel(
      tiny_box,
      {.source_size = {30.0f, 30.0f},
       .source_slices = {10.0f, 10.0f, 10.0f, 10.0f},
       .destination_slices = Insets{80.0f, 80.0f, 80.0f, 80.0f},
       .repeat = {NineSliceRepeatMode::Repeat,
                  NineSliceRepeatMode::Repeat}},
      {}, &tiny_status);
  assert(tiny.valid());
  assertInside(tiny, tiny_box);
  assert(tiny_status.generated_cells == 4u);

  NineSliceBuildStatus limited_status;
  const Mesh limited = nineSlicePanel(
      {0.0f, 0.0f, 20'000.0f, 20'000.0f},
      {.source_size = {3.0f, 3.0f},
       .source_slices = {1.0f, 1.0f, 1.0f, 1.0f},
       .repeat = {NineSliceRepeatMode::Repeat,
                  NineSliceRepeatMode::Repeat}},
      {}, &limited_status);
  assert(limited.valid());
  assert(limited_status.cell_limit_reduced);
  assert(limited_status.generated_cells <= kNineSliceCellLimit);
  assert(limited.vertices.size() == limited_status.generated_cells * 4u);
  assert(limited.indices.size() == limited_status.generated_cells * 6u);
}

void testObjectPlacement() {
  using namespace karma::ui::paint;
  assert(parseObjectFit("CONTAIN") == ObjectFit::Contain);
  assert(parseObjectFit("scale-down") == ObjectFit::ScaleDown);
  assert(!parseObjectFit("stretch"));

  ObjectPosition position;
  std::string error;
  assert(parseObjectPosition("left top", position, &error));
  assert(near(position.x, 0.0f) && near(position.y, 0.0f));
  assert(parseObjectPosition("top right", position, &error));
  assert(near(position.x, 1.0f) && near(position.y, 0.0f));
  assert(parseObjectPosition("top 20%", position, &error));
  assert(near(position.x, 0.2f) && near(position.y, 0.0f));
  assert(parseObjectPosition("25% 75%", position, &error));
  assert(near(position.x, 0.25f) && near(position.y, 0.75f));
  assert(parseObjectPosition("bottom", position, &error));
  assert(near(position.x, 0.5f) && near(position.y, 1.0f));
  assert(!parseObjectPosition("left right", position, &error));
  assert(!parseObjectPosition("10px 20px", position, &error));

  const Rect box{0.0f, 0.0f, 100.0f, 100.0f};
  const Vec2 wide{200.0f, 100.0f};
  const ObjectPlacement contain = placeObject(box, wide, ObjectFit::Contain);
  assert(near(contain.destination.x, 0.0f));
  assert(near(contain.destination.y, 25.0f));
  assert(near(contain.destination.width, 100.0f));
  assert(near(contain.destination.height, 50.0f));
  assert(near(contain.uv.width, 1.0f));

  const ObjectPlacement cover = placeObject(box, wide, ObjectFit::Cover);
  assert(cover.destination == box);
  assert(near(cover.uv.x, 0.25f));
  assert(near(cover.uv.width, 0.5f));
  assert(near(cover.uv.height, 1.0f));
  const ObjectPlacement right_cover =
      placeObject(box, wide, ObjectFit::Cover, {1.0f, 0.5f});
  assert(near(right_cover.uv.x, 0.5f));

  const ObjectPlacement fill = placeObject(box, wide, ObjectFit::Fill);
  assert(fill.destination == box);
  const ObjectPlacement none =
      placeObject(box, {20.0f, 10.0f}, ObjectFit::None, {0.0f, 1.0f});
  assert(near(none.destination.x, 0.0f));
  assert(near(none.destination.y, 90.0f));
  const ObjectPlacement down =
      placeObject(box, {20.0f, 10.0f}, ObjectFit::ScaleDown);
  assert(near(down.destination.width, 20.0f));
  assert(placeObject(box, {}, ObjectFit::Contain).destination.width == 0.0f);
}

void testTransforms() {
  using namespace karma::ui::paint;
  Transform transform;
  std::string error;
  assert(parseTransform(
      "translate(50%, 10px) scale(2) rotate(90deg)", transform, &error));
  assert(error.empty());
  assert(transform.operations.size() == 3u);
  const Affine2D matrix =
      composeTransform(transform, {100.0f, 200.0f}, {0.0f, 0.0f});
  const Vec2 point = applyTransform(matrix, {1.0f, 0.0f});
  assert(near(point.x, 50.0f));
  assert(near(point.y, 12.0f));

  assert(parseTransform("scale(2)", transform, &error));
  const Affine2D around_center = composeTransform(transform, {100.0f, 50.0f});
  const Vec2 center = applyTransform(around_center, {50.0f, 25.0f});
  assert(near(center.x, 50.0f) && near(center.y, 25.0f));
  const Vec2 corner = applyTransform(around_center, {0.0f, 0.0f});
  assert(near(corner.x, -50.0f) && near(corner.y, -25.0f));

  assert(parseTransform("translateX(3px) translateY(25%) scaleX(.5) "
                        "scaleY(3) rotate(.5turn)",
                        transform, &error));
  assert(transform.operations.size() == 5u);
  assert(parseTransform("none", transform, &error));
  assert(transform.operations.empty());
  assert(!parseTransform("skew(10deg)", transform, &error));
  assert(!parseTransform("translate(10em)", transform, &error));
  assert(!parseTransform("rotate(90)", transform, &error));
  assert(!parseTransform("translate(1px,)", transform, &error));

  Mesh mesh = roundedRectFill({0.0f, 0.0f, 10.0f, 10.0f}, {}, {}, 1u);
  const auto original_uv = mesh.vertices.front().uv;
  applyTransform(Affine2D{.tx = 4.0f, .ty = -2.0f}, mesh);
  assert(near(mesh.vertices.front().position.x, 9.0f));
  assert(near(mesh.vertices.front().position.y, 3.0f));
  assert(mesh.vertices.front().uv == original_uv);
  assert(mesh.valid());
}

}  // namespace

int main() {
  testMeshAndRoundedBoxes();
  testLinearGradients();
  testRadialGradients();
  testNineSlice();
  testObjectPlacement();
  testTransforms();
  std::cout << "UI paint tests passed\n";
  return 0;
}
