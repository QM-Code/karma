#include "features/ui/native/widget_paint.h"

#include <algorithm>
#include <cmath>

namespace karma::ui::widget_paint {
namespace {

constexpr float kEpsilon = 1.0e-6f;
constexpr std::size_t kMaximumGripMarks = 64u;

bool finite(float value) { return std::isfinite(value); }

bool finite(paint::Rect value) {
  return finite(value.x) && finite(value.y) && finite(value.width) &&
         finite(value.height);
}

bool drawable(paint::Rect value) {
  return finite(value) && value.width > 0.0f && value.height > 0.0f;
}

bool visible(math::Color color) {
  return math::isFinite(color) && color.a > 0.0f;
}

float fraction(float value) {
  return finite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

paint::Rect inset(paint::Rect bounds, float amount) {
  if (!drawable(bounds) || !finite(amount) || amount < 0.0f) return {};
  const float x = std::min(amount, bounds.width * 0.5f);
  const float y = std::min(amount, bounds.height * 0.5f);
  return {.x = bounds.x + x,
          .y = bounds.y + y,
          .width = std::max(0.0f, bounds.width - x * 2.0f),
          .height = std::max(0.0f, bounds.height - y * 2.0f)};
}

paint::Mesh rounded(paint::Rect bounds,
                    paint::CornerRadii radii,
                    math::Color color,
                    std::size_t segments) {
  if (!drawable(bounds) || !visible(color)) return {};
  return paint::roundedRectFill(bounds, radii, color,
                                std::max<std::size_t>(1u, segments));
}

paint::Mesh stroke(paint::Vec2 begin,
                   paint::Vec2 end,
                   float thickness,
                   math::Color color) {
  paint::Mesh output;
  if (!finite(begin.x) || !finite(begin.y) || !finite(end.x) ||
      !finite(end.y) || !finite(thickness) || thickness <= 0.0f ||
      !visible(color)) {
    return output;
  }
  const float dx = end.x - begin.x;
  const float dy = end.y - begin.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length <= kEpsilon) return output;
  const float nx = -dy / length * thickness * 0.5f;
  const float ny = dx / length * thickness * 0.5f;
  output.vertices = {
      {{begin.x + nx, begin.y + ny}, {0.0f, 0.0f}, color},
      {{begin.x - nx, begin.y - ny}, {0.0f, 1.0f}, color},
      {{end.x - nx, end.y - ny}, {1.0f, 1.0f}, color},
      {{end.x + nx, end.y + ny}, {1.0f, 0.0f}, color},
  };
  output.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  return output;
}

paint::Mesh twoSegmentGlyph(paint::Vec2 first,
                            paint::Vec2 middle,
                            paint::Vec2 last,
                            float thickness,
                            math::Color color) {
  paint::Mesh output = stroke(first, middle, thickness, color);
  const paint::Mesh second = stroke(middle, last, thickness, color);
  if (!output.append(second)) return {};
  return output;
}

paint::Rect glyphBox(paint::Rect bounds,
                     float requested_size,
                     float edge_inset,
                     bool leading,
                     LayoutDirection direction) {
  if (!drawable(bounds) || !finite(requested_size) || requested_size <= 0.0f ||
      !finite(edge_inset) || edge_inset < 0.0f) {
    return {};
  }
  const float horizontal_space = std::max(0.0f, bounds.width - edge_inset * 2.0f);
  const float vertical_space = bounds.height;
  const float size = std::min({requested_size, horizontal_space, vertical_space});
  if (size <= 0.0f) return {};
  const bool left = leading == (direction == LayoutDirection::LeftToRight);
  return {.x = left ? bounds.x + edge_inset
                    : bounds.x + bounds.width - edge_inset - size,
          .y = bounds.y + (bounds.height - size) * 0.5f,
          .width = size,
          .height = size};
}

}  // namespace

TogglePaintResult toggle(const TogglePaintRequest& request) {
  TogglePaintResult output;
  if (!drawable(request.bounds) || !finite(request.style.track_inset) ||
      request.style.track_inset < 0.0f ||
      !finite(request.style.checkmark_inset) ||
      request.style.checkmark_inset < 0.0f ||
      !finite(request.style.checkmark_thickness) ||
      request.style.checkmark_thickness <= 0.0f) {
    return output;
  }
  output.track_bounds = inset(request.bounds, request.style.track_inset);
  output.track = rounded(
      output.track_bounds, request.style.track_radii,
      request.checked ? request.style.checked_track_color
                      : request.style.track_color,
      request.style.corner_segments);
  if (!request.checked || !drawable(output.track_bounds)) return output;

  const float stroke_margin = request.style.checkmark_inset +
                              request.style.checkmark_thickness * 0.5f;
  const paint::Rect glyph = inset(output.track_bounds, stroke_margin);
  if (!drawable(glyph)) return output;
  const paint::Vec2 first{glyph.x, glyph.y + glyph.height * 0.55f};
  const paint::Vec2 middle{glyph.x + glyph.width * 0.38f,
                           glyph.y + glyph.height};
  const paint::Vec2 last{glyph.x + glyph.width, glyph.y};
  output.checkmark = twoSegmentGlyph(first, middle, last,
                                     request.style.checkmark_thickness,
                                     request.style.checkmark_color);
  return output;
}

SliderPaintResult slider(const SliderPaintRequest& request) {
  SliderPaintResult output;
  if (!drawable(request.bounds) || !finite(request.style.edge_inset) ||
      request.style.edge_inset < 0.0f ||
      !finite(request.style.track_thickness) ||
      request.style.track_thickness <= 0.0f ||
      !finite(request.style.thumb_size) || request.style.thumb_size <= 0.0f) {
    return output;
  }
  const paint::Rect content = inset(request.bounds, request.style.edge_inset);
  if (!drawable(content)) return output;
  const float value = fraction(request.fraction);

  if (request.orientation == Orientation::Horizontal) {
    const float thumb_size = std::min(request.style.thumb_size,
                                      std::min(content.width, content.height));
    const float track_height = std::min(request.style.track_thickness,
                                        content.height);
    const float start = content.x + thumb_size * 0.5f;
    const float end = content.x + content.width - thumb_size * 0.5f;
    const float travel = std::max(0.0f, end - start);
    const bool rtl = request.direction == LayoutDirection::RightToLeft;
    const float center = rtl ? end - travel * value : start + travel * value;
    output.track_bounds = {.x = start,
                           .y = content.y + (content.height - track_height) * 0.5f,
                           .width = travel,
                           .height = track_height};
    output.fill_bounds = {.x = rtl ? center : start,
                          .y = output.track_bounds.y,
                          .width = rtl ? end - center : center - start,
                          .height = track_height};
    output.thumb_bounds = {.x = center - thumb_size * 0.5f,
                           .y = content.y + (content.height - thumb_size) * 0.5f,
                           .width = thumb_size,
                           .height = thumb_size};
  } else {
    const float thumb_size = std::min(request.style.thumb_size,
                                      std::min(content.width, content.height));
    const float track_width = std::min(request.style.track_thickness,
                                       content.width);
    const float start = content.y + thumb_size * 0.5f;
    const float end = content.y + content.height - thumb_size * 0.5f;
    const float travel = std::max(0.0f, end - start);
    const float center = end - travel * value;
    output.track_bounds = {.x = content.x + (content.width - track_width) * 0.5f,
                           .y = start,
                           .width = track_width,
                           .height = travel};
    output.fill_bounds = {.x = output.track_bounds.x,
                          .y = center,
                          .width = track_width,
                          .height = end - center};
    output.thumb_bounds = {.x = content.x + (content.width - thumb_size) * 0.5f,
                           .y = center - thumb_size * 0.5f,
                           .width = thumb_size,
                           .height = thumb_size};
  }
  output.track = rounded(output.track_bounds, request.style.track_radii,
                         request.style.track_color,
                         request.style.corner_segments);
  output.fill = rounded(output.fill_bounds, request.style.fill_radii,
                        request.style.fill_color,
                        request.style.corner_segments);
  output.thumb = rounded(output.thumb_bounds, request.style.thumb_radii,
                         request.style.thumb_color,
                         request.style.corner_segments);
  return output;
}

ProgressPaintResult progress(const ProgressPaintRequest& request) {
  ProgressPaintResult output;
  if (!drawable(request.bounds) || !finite(request.style.inset) ||
      request.style.inset < 0.0f) {
    return output;
  }
  output.track_bounds = inset(request.bounds, request.style.inset);
  if (!drawable(output.track_bounds)) return output;
  const float value = fraction(request.fraction);
  output.fill_bounds = output.track_bounds;
  if (request.orientation == Orientation::Horizontal) {
    output.fill_bounds.width *= value;
    if (request.direction == LayoutDirection::RightToLeft) {
      output.fill_bounds.x = output.track_bounds.x + output.track_bounds.width -
                             output.fill_bounds.width;
    }
  } else {
    output.fill_bounds.height *= value;
    output.fill_bounds.y = output.track_bounds.y + output.track_bounds.height -
                           output.fill_bounds.height;
  }
  output.track = rounded(output.track_bounds, request.style.track_radii,
                         request.style.track_color,
                         request.style.corner_segments);
  output.fill = rounded(output.fill_bounds, request.style.fill_radii,
                        request.style.fill_color,
                        request.style.corner_segments);
  return output;
}

ChevronPaintResult selectArrow(const SelectArrowPaintRequest& request) {
  ChevronPaintResult output;
  output.glyph_bounds = glyphBox(request.bounds, request.style.size,
                                 request.style.edge_inset, false,
                                 request.direction);
  if (!drawable(output.glyph_bounds) || !finite(request.style.thickness) ||
      request.style.thickness <= 0.0f) {
    return output;
  }
  const float margin = request.style.thickness * 0.5f;
  const paint::Rect glyph = inset(output.glyph_bounds, margin);
  if (!drawable(glyph)) return output;
  const float near_y = request.expanded ? glyph.y + glyph.height * 0.68f
                                        : glyph.y + glyph.height * 0.32f;
  const float point_y = request.expanded ? glyph.y + glyph.height * 0.32f
                                         : glyph.y + glyph.height * 0.68f;
  output.glyph = twoSegmentGlyph(
      {glyph.x, near_y},
      {glyph.x + glyph.width * 0.5f, point_y},
      {glyph.x + glyph.width, near_y}, request.style.thickness,
      request.style.color);
  return output;
}

ChevronPaintResult disclosureChevron(
    const DisclosureChevronPaintRequest& request) {
  ChevronPaintResult output;
  output.glyph_bounds = glyphBox(request.bounds, request.style.size,
                                 request.style.edge_inset, true,
                                 request.direction);
  if (!drawable(output.glyph_bounds) || !finite(request.style.thickness) ||
      request.style.thickness <= 0.0f) {
    return output;
  }
  const paint::Rect glyph = inset(output.glyph_bounds,
                                  request.style.thickness * 0.5f);
  if (!drawable(glyph)) return output;
  paint::Vec2 first;
  paint::Vec2 middle;
  paint::Vec2 last;
  if (request.expanded) {
    first = {glyph.x, glyph.y + glyph.height * 0.32f};
    middle = {glyph.x + glyph.width * 0.5f,
              glyph.y + glyph.height * 0.68f};
    last = {glyph.x + glyph.width, glyph.y + glyph.height * 0.32f};
  } else if (request.direction == LayoutDirection::LeftToRight) {
    first = {glyph.x + glyph.width * 0.32f, glyph.y};
    middle = {glyph.x + glyph.width * 0.68f,
              glyph.y + glyph.height * 0.5f};
    last = {glyph.x + glyph.width * 0.32f, glyph.y + glyph.height};
  } else {
    first = {glyph.x + glyph.width * 0.68f, glyph.y};
    middle = {glyph.x + glyph.width * 0.32f,
              glyph.y + glyph.height * 0.5f};
    last = {glyph.x + glyph.width * 0.68f, glyph.y + glyph.height};
  }
  output.glyph = twoSegmentGlyph(first, middle, last, request.style.thickness,
                                 request.style.color);
  return output;
}

SplitterGripPaintResult splitterGrip(
    const SplitterGripPaintRequest& request) {
  SplitterGripPaintResult output;
  if (!drawable(request.bounds) || !finite(request.style.mark_length) ||
      request.style.mark_length <= 0.0f ||
      !finite(request.style.mark_thickness) ||
      request.style.mark_thickness <= 0.0f || !finite(request.style.gap) ||
      request.style.gap < 0.0f || request.style.mark_count == 0u) {
    return output;
  }
  const std::size_t count = std::min(request.style.mark_count,
                                     kMaximumGripMarks);
  const bool vertical = request.orientation == Orientation::Vertical;
  const float group_space = vertical ? request.bounds.height
                                     : request.bounds.width;
  const float cross_space = vertical ? request.bounds.width
                                     : request.bounds.height;
  const float thickness = std::min(request.style.mark_thickness,
                                   group_space / static_cast<float>(count));
  const float available_gap = count > 1u
                                  ? std::max(0.0f,
                                             (group_space - thickness * count) /
                                                 static_cast<float>(count - 1u))
                                  : 0.0f;
  const float gap = count > 1u ? std::min(request.style.gap, available_gap)
                               : 0.0f;
  const float length = std::min(request.style.mark_length, cross_space);
  const float total = thickness * static_cast<float>(count) +
                      gap * static_cast<float>(count - 1u);
  const float start = (vertical ? request.bounds.y : request.bounds.x) +
                      (group_space - total) * 0.5f;
  output.mark_bounds.reserve(count);
  for (std::size_t index = 0u; index < count; ++index) {
    const float position = start + static_cast<float>(index) * (thickness + gap);
    paint::Rect mark;
    if (vertical) {
      mark = {.x = request.bounds.x + (request.bounds.width - length) * 0.5f,
              .y = position,
              .width = length,
              .height = thickness};
    } else {
      mark = {.x = position,
              .y = request.bounds.y + (request.bounds.height - length) * 0.5f,
              .width = thickness,
              .height = length};
    }
    output.mark_bounds.push_back(mark);
    const paint::Mesh mesh = rounded(mark, request.style.mark_radii,
                                     request.style.color,
                                     request.style.corner_segments);
    if (!output.grip.append(mesh)) return {};
  }
  return output;
}

}  // namespace karma::ui::widget_paint
