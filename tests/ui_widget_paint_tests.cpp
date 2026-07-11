#include "features/ui/native/widget_paint.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

namespace paint = karma::ui::paint;
namespace widgets = karma::ui::widget_paint;

bool near(float left, float right, float epsilon = 1.0e-4f) {
  return std::abs(left - right) <= epsilon;
}

void expectRect(paint::Rect actual,
                float x,
                float y,
                float width,
                float height) {
  assert(near(actual.x, x));
  assert(near(actual.y, y));
  assert(near(actual.width, width));
  assert(near(actual.height, height));
}

void expectInside(const paint::Mesh& mesh, paint::Rect bounds) {
  assert(mesh.valid());
  for (const paint::Vertex& vertex : mesh.vertices) {
    assert(vertex.position.x >= bounds.x - 1.0e-3f);
    assert(vertex.position.x <= bounds.x + bounds.width + 1.0e-3f);
    assert(vertex.position.y >= bounds.y - 1.0e-3f);
    assert(vertex.position.y <= bounds.y + bounds.height + 1.0e-3f);
  }
}

void expectColor(const paint::Mesh& mesh, karma::math::Color expected) {
  assert(!mesh.vertices.empty());
  for (const paint::Vertex& vertex : mesh.vertices) {
    assert(near(vertex.color.r, expected.r));
    assert(near(vertex.color.g, expected.g));
    assert(near(vertex.color.b, expected.b));
    assert(near(vertex.color.a, expected.a));
  }
}

paint::Vec2 strokeBegin(const paint::Mesh& mesh, std::size_t segment) {
  const std::size_t first = segment * 4u;
  assert(mesh.vertices.size() >= first + 4u);
  return {(mesh.vertices[first].position.x +
           mesh.vertices[first + 1u].position.x) * 0.5f,
          (mesh.vertices[first].position.y +
           mesh.vertices[first + 1u].position.y) * 0.5f};
}

paint::Vec2 strokeEnd(const paint::Mesh& mesh, std::size_t segment) {
  const std::size_t first = segment * 4u;
  assert(mesh.vertices.size() >= first + 4u);
  return {(mesh.vertices[first + 2u].position.x +
           mesh.vertices[first + 3u].position.x) * 0.5f,
          (mesh.vertices[first + 2u].position.y +
           mesh.vertices[first + 3u].position.y) * 0.5f};
}

void testToggleTrackAndCheckmark() {
  widgets::TogglePaintRequest request{
      .bounds = {10.0f, 20.0f, 24.0f, 24.0f},
      .style = {.track_color = {0.1f, 0.2f, 0.3f, 1.0f},
                .checked_track_color = {0.2f, 0.6f, 0.9f, 1.0f},
                .checkmark_color = {0.9f, 0.8f, 0.7f, 1.0f},
                .track_radii = {4.0f, 4.0f, 4.0f, 4.0f},
                .track_inset = 1.0f,
                .checkmark_inset = 4.0f,
                .checkmark_thickness = 2.0f,
                .corner_segments = 3u}};
  const widgets::TogglePaintResult off = widgets::toggle(request);
  expectRect(off.track_bounds, 11.0f, 21.0f, 22.0f, 22.0f);
  expectInside(off.track, request.bounds);
  expectColor(off.track, request.style.track_color);
  assert(off.checkmark.empty());

  request.checked = true;
  const widgets::TogglePaintResult on = widgets::toggle(request);
  expectInside(on.track, request.bounds);
  expectColor(on.track, request.style.checked_track_color);
  assert(on.checkmark.vertices.size() == 8u);
  assert(on.checkmark.indices.size() == 12u);
  expectInside(on.checkmark, on.track_bounds);
  expectColor(on.checkmark, request.style.checkmark_color);
  const paint::Vec2 first = strokeBegin(on.checkmark, 0u);
  const paint::Vec2 joint = strokeEnd(on.checkmark, 0u);
  const paint::Vec2 last = strokeEnd(on.checkmark, 1u);
  assert(joint.x > first.x && joint.y > first.y);
  assert(last.x > joint.x && last.y < joint.y);
}

void testHorizontalAndVerticalSlider() {
  widgets::SliderPaintRequest request{
      .bounds = {10.0f, 20.0f, 110.0f, 20.0f},
      .fraction = 0.25f,
      .style = {.track_color = {0.1f, 0.1f, 0.1f, 1.0f},
                .fill_color = {0.2f, 0.5f, 0.9f, 1.0f},
                .thumb_color = {0.9f, 0.9f, 0.9f, 1.0f},
                .track_radii = {2.0f, 2.0f, 2.0f, 2.0f},
                .fill_radii = {2.0f, 2.0f, 2.0f, 2.0f},
                .thumb_radii = {6.0f, 6.0f, 6.0f, 6.0f},
                .edge_inset = 2.0f,
                .track_thickness = 4.0f,
                .thumb_size = 12.0f,
                .corner_segments = 3u}};
  const widgets::SliderPaintResult ltr = widgets::slider(request);
  expectRect(ltr.track_bounds, 18.0f, 28.0f, 94.0f, 4.0f);
  expectRect(ltr.fill_bounds, 18.0f, 28.0f, 23.5f, 4.0f);
  expectRect(ltr.thumb_bounds, 35.5f, 24.0f, 12.0f, 12.0f);
  expectInside(ltr.track, request.bounds);
  expectInside(ltr.fill, request.bounds);
  expectInside(ltr.thumb, request.bounds);
  expectColor(ltr.fill, request.style.fill_color);

  request.direction = widgets::LayoutDirection::RightToLeft;
  const widgets::SliderPaintResult rtl = widgets::slider(request);
  expectRect(rtl.fill_bounds, 88.5f, 28.0f, 23.5f, 4.0f);
  expectRect(rtl.thumb_bounds, 82.5f, 24.0f, 12.0f, 12.0f);
  assert(near(ltr.thumb_bounds.x + rtl.thumb_bounds.x,
              10.0f + 120.0f - 12.0f));

  request = {.bounds = {0.0f, 0.0f, 20.0f, 120.0f},
             .fraction = 0.25f,
             .orientation = widgets::Orientation::Vertical,
             .style = {.track_thickness = 4.0f,
                       .thumb_size = 10.0f,
                       .corner_segments = 2u}};
  const widgets::SliderPaintResult vertical = widgets::slider(request);
  expectRect(vertical.track_bounds, 8.0f, 5.0f, 4.0f, 110.0f);
  expectRect(vertical.fill_bounds, 8.0f, 87.5f, 4.0f, 27.5f);
  expectRect(vertical.thumb_bounds, 5.0f, 82.5f, 10.0f, 10.0f);
  expectInside(vertical.track, request.bounds);
  expectInside(vertical.fill, request.bounds);
  expectInside(vertical.thumb, request.bounds);

  request.fraction = std::numeric_limits<float>::quiet_NaN();
  const widgets::SliderPaintResult nan_value = widgets::slider(request);
  expectRect(nan_value.thumb_bounds, 5.0f, 110.0f, 10.0f, 10.0f);
  request.style.track_thickness = -1.0f;
  const widgets::SliderPaintResult invalid = widgets::slider(request);
  assert(invalid.track.empty() && invalid.fill.empty() && invalid.thumb.empty());
}

void testProgressDirections() {
  widgets::ProgressPaintRequest request{
      .bounds = {5.0f, 10.0f, 200.0f, 20.0f},
      .fraction = 0.25f,
      .style = {.track_color = {0.1f, 0.2f, 0.3f, 1.0f},
                .fill_color = {0.8f, 0.4f, 0.2f, 1.0f},
                .track_radii = {4.0f, 4.0f, 4.0f, 4.0f},
                .fill_radii = {2.0f, 2.0f, 2.0f, 2.0f},
                .inset = 2.0f,
                .corner_segments = 3u}};
  const widgets::ProgressPaintResult ltr = widgets::progress(request);
  expectRect(ltr.track_bounds, 7.0f, 12.0f, 196.0f, 16.0f);
  expectRect(ltr.fill_bounds, 7.0f, 12.0f, 49.0f, 16.0f);
  expectInside(ltr.track, request.bounds);
  expectInside(ltr.fill, request.bounds);
  expectColor(ltr.fill, request.style.fill_color);

  request.direction = widgets::LayoutDirection::RightToLeft;
  const widgets::ProgressPaintResult rtl = widgets::progress(request);
  expectRect(rtl.fill_bounds, 154.0f, 12.0f, 49.0f, 16.0f);

  request = {.bounds = {0.0f, 0.0f, 20.0f, 100.0f},
             .fraction = 0.4f,
             .orientation = widgets::Orientation::Vertical};
  const widgets::ProgressPaintResult vertical = widgets::progress(request);
  expectRect(vertical.fill_bounds, 0.0f, 60.0f, 20.0f, 40.0f);
  expectInside(vertical.track, request.bounds);
  expectInside(vertical.fill, request.bounds);

  request.fraction = -10.0f;
  const widgets::ProgressPaintResult empty = widgets::progress(request);
  assert(empty.fill.empty());
  request.fraction = 10.0f;
  const widgets::ProgressPaintResult full = widgets::progress(request);
  assert(full.fill_bounds == full.track_bounds);
}

void testSelectAndDisclosureChevrons() {
  const paint::Rect bounds{0.0f, 0.0f, 100.0f, 32.0f};
  widgets::SelectArrowPaintRequest select{
      .bounds = bounds,
      .style = {.color = {0.3f, 0.7f, 0.9f, 1.0f},
                .size = 12.0f,
                .thickness = 2.0f,
                .edge_inset = 8.0f}};
  const widgets::ChevronPaintResult down = widgets::selectArrow(select);
  expectRect(down.glyph_bounds, 80.0f, 10.0f, 12.0f, 12.0f);
  assert(down.glyph.vertices.size() == 8u);
  assert(down.glyph.indices.size() == 12u);
  expectInside(down.glyph, down.glyph_bounds);
  expectColor(down.glyph, select.style.color);
  assert(strokeEnd(down.glyph, 0u).y > strokeBegin(down.glyph, 0u).y);

  select.expanded = true;
  const widgets::ChevronPaintResult up = widgets::selectArrow(select);
  assert(strokeEnd(up.glyph, 0u).y < strokeBegin(up.glyph, 0u).y);
  select.direction = widgets::LayoutDirection::RightToLeft;
  const widgets::ChevronPaintResult rtl_select = widgets::selectArrow(select);
  expectRect(rtl_select.glyph_bounds, 8.0f, 10.0f, 12.0f, 12.0f);

  widgets::DisclosureChevronPaintRequest disclosure{
      .bounds = bounds,
      .style = select.style};
  const widgets::ChevronPaintResult right =
      widgets::disclosureChevron(disclosure);
  expectRect(right.glyph_bounds, 8.0f, 10.0f, 12.0f, 12.0f);
  assert(strokeEnd(right.glyph, 0u).x > strokeBegin(right.glyph, 0u).x);

  disclosure.direction = widgets::LayoutDirection::RightToLeft;
  const widgets::ChevronPaintResult left =
      widgets::disclosureChevron(disclosure);
  expectRect(left.glyph_bounds, 80.0f, 10.0f, 12.0f, 12.0f);
  assert(strokeEnd(left.glyph, 0u).x < strokeBegin(left.glyph, 0u).x);
  disclosure.expanded = true;
  const widgets::ChevronPaintResult expanded =
      widgets::disclosureChevron(disclosure);
  assert(strokeEnd(expanded.glyph, 0u).y >
         strokeBegin(expanded.glyph, 0u).y);
}

void testSplitterGripOrientations() {
  widgets::SplitterGripPaintRequest request{
      .bounds = {0.0f, 0.0f, 30.0f, 50.0f},
      .orientation = widgets::Orientation::Vertical,
      .style = {.color = {0.5f, 0.6f, 0.7f, 1.0f},
                .mark_radii = {1.0f, 1.0f, 1.0f, 1.0f},
                .mark_length = 12.0f,
                .mark_thickness = 2.0f,
                .gap = 3.0f,
                .mark_count = 3u,
                .corner_segments = 2u}};
  const widgets::SplitterGripPaintResult vertical =
      widgets::splitterGrip(request);
  assert(vertical.mark_bounds.size() == 3u);
  expectRect(vertical.mark_bounds[0], 9.0f, 19.0f, 12.0f, 2.0f);
  expectRect(vertical.mark_bounds[1], 9.0f, 24.0f, 12.0f, 2.0f);
  expectRect(vertical.mark_bounds[2], 9.0f, 29.0f, 12.0f, 2.0f);
  expectInside(vertical.grip, request.bounds);
  expectColor(vertical.grip, request.style.color);

  request.bounds = {0.0f, 0.0f, 50.0f, 30.0f};
  request.orientation = widgets::Orientation::Horizontal;
  const widgets::SplitterGripPaintResult horizontal =
      widgets::splitterGrip(request);
  assert(horizontal.mark_bounds.size() == 3u);
  expectRect(horizontal.mark_bounds[0], 19.0f, 9.0f, 2.0f, 12.0f);
  expectRect(horizontal.mark_bounds[1], 24.0f, 9.0f, 2.0f, 12.0f);
  expectRect(horizontal.mark_bounds[2], 29.0f, 9.0f, 2.0f, 12.0f);
  expectInside(horizontal.grip, request.bounds);

  request.style.mark_count = 100u;
  const widgets::SplitterGripPaintResult bounded =
      widgets::splitterGrip(request);
  assert(bounded.mark_bounds.size() == 64u);
  expectInside(bounded.grip, request.bounds);
}

void testInvalidBoundsAreEmpty() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  assert(widgets::toggle({.bounds = {nan, 0.0f, 10.0f, 10.0f}})
             .track.empty());
  assert(widgets::slider({.bounds = {0.0f, 0.0f, -1.0f, 10.0f}})
             .track.empty());
  assert(widgets::progress({.bounds = {0.0f, 0.0f, 10.0f, 0.0f}})
             .track.empty());
  assert(widgets::selectArrow({.bounds = {0.0f, 0.0f, 10.0f, 10.0f},
                               .style = {.thickness = nan}})
             .glyph.empty());
  assert(widgets::splitterGrip({.bounds = {0.0f, 0.0f, 10.0f, 10.0f},
                                .style = {.mark_count = 0u}})
             .grip.empty());
}

}  // namespace

int main() {
  testToggleTrackAndCheckmark();
  testHorizontalAndVerticalSlider();
  testProgressDirections();
  testSelectAndDisclosureChevrons();
  testSplitterGripOrientations();
  testInvalidBoundsAreEmpty();
  std::cout << "ui widget paint tests passed\n";
  return 0;
}
