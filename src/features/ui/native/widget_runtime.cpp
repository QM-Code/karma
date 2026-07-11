#include "features/ui/native/widget_runtime.h"

#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace karma::ui::native::widget_runtime {
namespace {

using runtime_dom::contains;
using runtime_dom::DragInteraction;
using runtime_dom::Node;
using runtime_dom::nonEmpty;
using runtime_dom::ScrollbarPart;
using runtime_dom::overflowForAxis;
using runtime_dom::scrollableOverflow;
using runtime_dom::styleFloat;
using runtime_dom::styleString;

using string_utils::lower;
using string_utils::parseFiniteDouble;
using string_utils::trim;

bool enabledAttribute(const Node& node,
                      std::string_view name,
                      bool fallback) {
  const auto found = node.attributes.find(std::string(name));
  if (found == node.attributes.end()) return fallback;
  const std::string value = lower(trim(found->second));
  return value.empty() || value == "true" || value == "1" || value == name;
}

std::optional<platform::CursorShape> authoredCursorShape(
    std::string_view value) {
  const std::string cursor = lower(trim(value));
  if (cursor.empty() || cursor == "auto" || cursor == "default") {
    return cursor == "default"
               ? std::optional{platform::CursorShape::Default}
               : std::nullopt;
  }
  if (cursor == "pointer" || cursor == "hand") {
    return platform::CursorShape::Pointer;
  }
  if (cursor == "text" || cursor == "ibeam") {
    return platform::CursorShape::Text;
  }
  if (cursor == "crosshair") return platform::CursorShape::Crosshair;
  if (cursor == "move" || cursor == "all-scroll") {
    return platform::CursorShape::Move;
  }
  if (cursor == "resize-horizontal" || cursor == "ew-resize" ||
      cursor == "col-resize") {
    return platform::CursorShape::ResizeHorizontal;
  }
  if (cursor == "resize-vertical" || cursor == "ns-resize" ||
      cursor == "row-resize") {
    return platform::CursorShape::ResizeVertical;
  }
  if (cursor == "resize-diagonal-nw-se" || cursor == "nwse-resize") {
    return platform::CursorShape::ResizeDiagonalNwSe;
  }
  if (cursor == "resize-diagonal-ne-sw" || cursor == "nesw-resize") {
    return platform::CursorShape::ResizeDiagonalNeSw;
  }
  if (cursor == "not-allowed" || cursor == "disabled") {
    return platform::CursorShape::NotAllowed;
  }
  return std::nullopt;
}

}  // namespace

ScrollbarPart scrollbarPartAt(const Node& node, double x, double y) {
  if (nonEmpty(node.horizontal_scroll_thumb) &&
      contains(node.horizontal_scroll_thumb, x, y)) {
    return ScrollbarPart::HorizontalThumb;
  }
  if (nonEmpty(node.vertical_scroll_thumb) &&
      contains(node.vertical_scroll_thumb, x, y)) {
    return ScrollbarPart::VerticalThumb;
  }
  if (nonEmpty(node.horizontal_scroll_track) &&
      contains(node.horizontal_scroll_track, x, y)) {
    return ScrollbarPart::HorizontalTrack;
  }
  if (nonEmpty(node.vertical_scroll_track) &&
      contains(node.vertical_scroll_track, x, y)) {
    return ScrollbarPart::VerticalTrack;
  }
  if (nonEmpty(node.scrollbar_corner) && contains(node.scrollbar_corner, x, y)) {
    return ScrollbarPart::Corner;
  }
  return ScrollbarPart::None;
}

DragInteraction windowInteractionAt(const Node& node, double x, double y) {
  if (node.tag != "window" || !contains(node.layout, x, y)) {
    return DragInteraction::None;
  }
  if (nonEmpty(node.window_close_button) &&
      contains(node.window_close_button, x, y)) {
    return DragInteraction::WindowClose;
  }
  if (nonEmpty(node.window_collapse_button) &&
      contains(node.window_collapse_button, x, y)) {
    return DragInteraction::WindowCollapse;
  }
  const bool resizable = enabledAttribute(node, "resizable", true);
  const float grip = std::clamp(
      styleFloat(node, "window-resize-grip", 7.0f), 3.0f, 24.0f);
  const bool left = resizable && x < node.layout.x + grip;
  const bool right =
      resizable && x >= node.layout.x + node.layout.width - grip;
  const bool top = resizable && y < node.layout.y + grip;
  const bool bottom =
      resizable && y >= node.layout.y + node.layout.height - grip;
  if (top && left) return DragInteraction::WindowResizeTopLeft;
  if (top && right) return DragInteraction::WindowResizeTopRight;
  if (bottom && left) return DragInteraction::WindowResizeBottomLeft;
  if (bottom && right) return DragInteraction::WindowResizeBottomRight;
  if (left) return DragInteraction::WindowResizeLeft;
  if (right) return DragInteraction::WindowResizeRight;
  if (top) return DragInteraction::WindowResizeTop;
  if (bottom) return DragInteraction::WindowResizeBottom;
  const float titlebar = std::max(
      grip, styleFloat(node, "window-titlebar-height", 32.0f));
  return y < node.layout.y + titlebar ? DragInteraction::WindowMove
                                      : DragInteraction::None;
}

platform::CursorShape cursorForNode(const Node& node, double x, double y) {
  if (node.disabled) return platform::CursorShape::NotAllowed;
  DragInteraction interaction = node.drag_interaction;
  if (interaction == DragInteraction::None && node.tag == "window") {
    interaction = windowInteractionAt(node, x, y);
  }
  switch (interaction) {
    case DragInteraction::WindowMove: return platform::CursorShape::Move;
    case DragInteraction::WindowResizeLeft:
    case DragInteraction::WindowResizeRight:
      return platform::CursorShape::ResizeHorizontal;
    case DragInteraction::WindowResizeTop:
    case DragInteraction::WindowResizeBottom:
      return platform::CursorShape::ResizeVertical;
    case DragInteraction::WindowResizeTopLeft:
    case DragInteraction::WindowResizeBottomRight:
      return platform::CursorShape::ResizeDiagonalNwSe;
    case DragInteraction::WindowResizeTopRight:
    case DragInteraction::WindowResizeBottomLeft:
      return platform::CursorShape::ResizeDiagonalNeSw;
    case DragInteraction::WindowClose:
    case DragInteraction::WindowCollapse:
      return platform::CursorShape::Pointer;
    default: break;
  }
  if (const auto found = node.style.find("cursor"); found != node.style.end()) {
    if (const auto authored = authoredCursorShape(found->second)) {
      return *authored;
    }
  }
  const ScrollbarPart scrollbar = scrollbarPartAt(node, x, y);
  if (node.active_scrollbar_part != ScrollbarPart::None ||
      scrollbar != ScrollbarPart::None) {
    return platform::CursorShape::Pointer;
  }
  if (node.tag == "splitter") {
    const bool vertical = !node.attributes.contains("orientation") ||
                          lower(node.attributes.at("orientation")) !=
                              "horizontal";
    return vertical ? platform::CursorShape::ResizeHorizontal
                    : platform::CursorShape::ResizeVertical;
  }
  if (node.tag == "button" || node.tag == "toggle" ||
      node.tag == "slider" || node.tag == "select" ||
      node.tag == "option" || node.tag == "tab" ||
      node.tag == "disclosure" || node.tag == "tree-item" ||
      node.tag == "menu-item") {
    return platform::CursorShape::Pointer;
  }
  return platform::CursorShape::Default;
}

bool isVerticalSlider(const Node& slider) {
  const auto orientation = slider.attributes.find("orientation");
  return orientation != slider.attributes.end() &&
         lower(orientation->second) == "vertical";
}

double sliderStepPointerCoordinate(const Node& slider, int direction) {
  double minimum = 0.0;
  double maximum = 1.0;
  double step = 0.01;
  if (const auto found = slider.attributes.find("min");
      found != slider.attributes.end()) {
    minimum = parseFiniteDouble(found->second).value_or(minimum);
  }
  if (const auto found = slider.attributes.find("max");
      found != slider.attributes.end()) {
    maximum = parseFiniteDouble(found->second).value_or(maximum);
  }
  if (const auto found = slider.attributes.find("step");
      found != slider.attributes.end()) {
    step = parseFiniteDouble(found->second).value_or(step);
  }
  const double current = slider.control_value.asNumber().value_or(minimum);
  const double value = std::clamp(current + step * direction,
                                  std::min(minimum, maximum),
                                  std::max(minimum, maximum));
  const double ratio = maximum == minimum
                           ? 0.0
                           : (value - minimum) / (maximum - minimum);
  const double fraction = std::clamp(ratio, 0.0, 1.0);
  if (isVerticalSlider(slider)) {
    return slider.layout.y + slider.layout.height * (1.0 - fraction);
  }
  const double pointer_fraction =
      styleString(slider, "direction", "ltr") == "rtl"
          ? 1.0 - fraction
          : fraction;
  return slider.layout.x + pointer_fraction * slider.layout.width;
}

void updateWindowGeometry(Node& node) {
  node.window_titlebar = {};
  node.window_close_button = {};
  node.window_collapse_button = {};
  if (node.tag != "window") return;
  const float height = std::clamp(
      styleFloat(node, "window-titlebar-height", 32.0f), 0.0f,
      node.layout.height);
  node.window_titlebar = {
      node.layout.x, node.layout.y, node.layout.width, height};
  const float button = std::max(0.0f, height);
  float right = node.layout.x + node.layout.width;
  if (enabledAttribute(node, "closable", false) && button > 0.0f) {
    right -= button;
    node.window_close_button = {right, node.layout.y, button, button};
  }
  if (enabledAttribute(node, "collapsible", true) && button > 0.0f) {
    right -= button;
    node.window_collapse_button = {right, node.layout.y, button, button};
  }
}

void updateScrollbarGeometry(Node& node,
                             float content_width,
                             float content_height) {
  node.scroll_content_width = std::max(0.0f, content_width);
  node.scroll_content_height = std::max(0.0f, content_height);
  node.horizontal_scroll_track = {};
  node.horizontal_scroll_thumb = {};
  node.vertical_scroll_track = {};
  node.vertical_scroll_thumb = {};
  node.scrollbar_corner = {};

  const std::string visibility =
      styleString(node, "scrollbar-visibility", "auto");
  const std::string overflow_x = overflowForAxis(node, "x");
  const std::string overflow_y = overflowForAxis(node, "y");
  const bool scroll_x_enabled = scrollableOverflow(overflow_x);
  const bool scroll_y_enabled = scrollableOverflow(overflow_y);
  const bool paint_bars = visibility != "hidden";
  const bool always = visibility == "always";
  const bool gutter =
      styleString(node, "scrollbar-placement", "gutter") != "overlay";
  const float maximum_thickness =
      std::max(4.0f, std::min(node.layout.width, node.layout.height));
  const float shared_thickness = styleFloat(node, "scrollbar-width", 12.0f);
  const float vertical_width = std::clamp(
      styleFloat(node, "scrollbar-vertical-width", shared_thickness), 4.0f,
      maximum_thickness);
  const float horizontal_height = std::clamp(
      styleFloat(node, "scrollbar-horizontal-height", shared_thickness), 4.0f,
      maximum_thickness);
  const float shared_min_thumb =
      styleFloat(node, "scrollbar-min-thumb", 24.0f);
  const float horizontal_min_thumb = std::max(
      horizontal_height,
      styleFloat(node, "scrollbar-horizontal-min-thumb", shared_min_thumb));
  const float vertical_min_thumb = std::max(
      vertical_width,
      styleFloat(node, "scrollbar-vertical-min-thumb", shared_min_thumb));

  bool horizontal = scroll_x_enabled &&
                    (always || overflow_x == "scroll" ||
                     content_width > node.layout.width + 0.5f);
  bool vertical = scroll_y_enabled &&
                  (always || overflow_y == "scroll" ||
                   content_height > node.layout.height + 0.5f);
  if (gutter && paint_bars) {
    // A bar on one axis may make the other axis overflow. Two passes reach the
    // fixed point because only two axes participate.
    for (int pass = 0; pass < 2; ++pass) {
      const float viewport_width = std::max(
          0.0f, node.layout.width - (vertical ? vertical_width : 0.0f));
      const float viewport_height = std::max(
          0.0f,
          node.layout.height - (horizontal ? horizontal_height : 0.0f));
      horizontal = horizontal ||
                   (scroll_x_enabled &&
                    content_width > viewport_width + 0.5f);
      vertical = vertical ||
                 (scroll_y_enabled &&
                  content_height > viewport_height + 0.5f);
    }
  }

  const float reserved_x =
      gutter && paint_bars && vertical ? vertical_width : 0.0f;
  const float reserved_y =
      gutter && paint_bars && horizontal ? horizontal_height : 0.0f;
  node.scroll_viewport = node.layout;
  node.scroll_viewport.width =
      std::max(0.0f, node.layout.width - reserved_x);
  node.scroll_viewport.height =
      std::max(0.0f, node.layout.height - reserved_y);
  const bool rtl = styleString(node, "direction", "ltr") == "rtl";
  if (rtl && reserved_x > 0.0f) node.scroll_viewport.x += reserved_x;

  node.scroll_max_x =
      scroll_x_enabled
          ? std::max(0.0f, content_width - node.scroll_viewport.width)
          : 0.0f;
  node.scroll_max_y =
      scroll_y_enabled
          ? std::max(0.0f, content_height - node.scroll_viewport.height)
          : 0.0f;
  node.scroll_x = std::clamp(node.scroll_x, 0.0f, node.scroll_max_x);
  node.scroll_y = std::clamp(node.scroll_y, 0.0f, node.scroll_max_y);

  if (!paint_bars) return;
  horizontal = horizontal && node.layout.width > 0.0f;
  vertical = vertical && node.layout.height > 0.0f;
  const float vertical_x = rtl ? node.layout.x
                               : node.layout.x + node.layout.width - vertical_width;
  if (horizontal) {
    const float left_inset = vertical && rtl ? vertical_width : 0.0f;
    const float right_inset = vertical && !rtl ? vertical_width : 0.0f;
    node.horizontal_scroll_track = {
        node.layout.x + left_inset,
        node.layout.y + node.layout.height - horizontal_height,
        std::max(0.0f, node.layout.width - left_inset - right_inset),
        horizontal_height};
    const float track = node.horizontal_scroll_track.width;
    const float thumb = std::min(
        track,
        std::max(horizontal_min_thumb,
                 content_width > 0.0f
                     ? track * node.scroll_viewport.width / content_width
                     : track));
    const float travel = std::max(0.0f, track - thumb);
    const float position = node.scroll_max_x > 0.0f
                               ? travel * node.scroll_x / node.scroll_max_x
                               : 0.0f;
    node.horizontal_scroll_thumb = {
        node.horizontal_scroll_track.x + position,
        node.horizontal_scroll_track.y,
        thumb,
        horizontal_height};
  }
  if (vertical) {
    node.vertical_scroll_track = {
        vertical_x,
        node.layout.y,
        vertical_width,
        std::max(0.0f, node.layout.height -
                            (horizontal ? horizontal_height : 0.0f))};
    const float track = node.vertical_scroll_track.height;
    const float thumb = std::min(
        track,
        std::max(vertical_min_thumb,
                 content_height > 0.0f
                     ? track * node.scroll_viewport.height / content_height
                     : track));
    const float travel = std::max(0.0f, track - thumb);
    const float position = node.scroll_max_y > 0.0f
                               ? travel * node.scroll_y / node.scroll_max_y
                               : 0.0f;
    node.vertical_scroll_thumb = {
        node.vertical_scroll_track.x,
        node.vertical_scroll_track.y + position,
        vertical_width,
        thumb};
  }
  if (horizontal && vertical) {
    node.scrollbar_corner = {
        vertical_x,
        node.layout.y + node.layout.height - horizontal_height,
        vertical_width,
        horizontal_height};
  }
}

}  // namespace karma::ui::native::widget_runtime
