#include "features/ui/native/document_layout_runtime.h"

#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/computed_style_values.h"
#include "features/ui/native/diagnostics.h"
#include "features/ui/native/layout_engine.h"
#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "features/ui/native/text_engine.h"
#include "features/ui/native/widget_runtime.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace karma::ui::native::document_layout_runtime {
namespace {

using computed_style_values::kDefaultFontSize;
using computed_style_values::Length;
using computed_style_values::nodeFontSize;
using computed_style_values::nodeLineHeight;
using computed_style_values::oneBoxValue;
using computed_style_values::parseLength;
using computed_style_values::resolveLength;
using computed_style_values::splitCommaList;
using computed_style_values::splitWhitespace;
using computed_style_values::styleLength;
using computed_style_values::Unit;
using runtime_dom::clipForOverflow;
using runtime_dom::clipsOverflow;
using runtime_dom::DocumentInstance;
using runtime_dom::forRuntimeChildren;
using runtime_dom::invalidatePaintTree;
using runtime_dom::isScrollContainer;
using runtime_dom::Node;
using runtime_dom::Rect;
using runtime_dom::styleFloat;
using runtime_dom::styleString;
using runtime_dom::visibleRuntimeChildren;
using string_utils::lower;
using string_utils::parseFiniteDouble;
using string_utils::trim;
using widget_runtime::updateScrollbarGeometry;
using widget_runtime::updateWindowGeometry;

struct Insets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

Insets boxInsets(const Node& node,
                 std::string_view property,
                 float reference,
                 float viewport_width,
                 float viewport_height,
                 float font_size) {
  return Insets{
      .left = oneBoxValue(node, property, "left", reference, viewport_width,
                          viewport_height, font_size),
      .top = oneBoxValue(node, property, "top", reference, viewport_width,
                         viewport_height, font_size),
      .right = oneBoxValue(node, property, "right", reference, viewport_width,
                           viewport_height, font_size),
      .bottom = oneBoxValue(node, property, "bottom", reference, viewport_width,
                            viewport_height, font_size),
  };
}

float intrinsicTextWidth(const Node& node, float font_size) {
  std::size_t codepoints = 0;
  for (unsigned char ch : node.text) {
    if ((ch & 0xc0u) != 0x80u) ++codepoints;
  }
  return static_cast<float>(codepoints) * font_size * 0.58f;
}

std::vector<Length> parseGridTracks(std::string text) {
  text = trim(text);
  std::vector<Length> tracks;
  if (lower(text).starts_with("repeat(")) {
    const std::size_t close = text.rfind(')');
    if (close != std::string::npos) {
      const auto parts = splitCommaList(std::string_view(text).substr(7, close - 7));
      const std::optional<double> count =
          parts.size() == 2u ? parseFiniteDouble(parts[0]) : std::nullopt;
      if (count.has_value() && *count >= 1.0 && *count <= 64.0) {
        const Length track = parseLength(parts[1]);
        for (int i = 0; i < static_cast<int>(*count); ++i) {
          tracks.push_back(track);
        }
        return tracks;
      }
    }
  }
  for (const std::string& item : splitWhitespace(text)) {
    if (lower(item).starts_with("minmax(")) {
      const std::size_t close = item.rfind(')');
      const auto parts = close == std::string::npos
                             ? std::vector<std::string>{}
                             : splitCommaList(std::string_view(item).substr(7, close - 7));
      tracks.push_back(parts.size() == 2u
                           ? parseLength(parts[1])
                           : Length{.value = 1, .unit = Unit::Fr});
    } else {
      tracks.push_back(parseLength(item));
    }
  }
  return tracks;
}

float layoutNodeRecursive(Node& node,
                          Rect available,
                          Rect inherited_clip,
                          float viewport_width,
                          float viewport_height,
                          bool root = false) {
  if (!node.present || node.collapsed_hidden ||
      styleString(node, "display", "block") == "none") {
    node.layout = {};
    node.clip = {};
    return 0.0f;
  }
  const float font_size = nodeFontSize(node);
  const Insets margin = boxInsets(node, "margin", available.width, viewport_width,
                                  viewport_height, font_size);
  const Insets padding = boxInsets(node, "padding", available.width, viewport_width,
                                   viewport_height, font_size);
  const bool absolute = styleString(node, "position", "relative") == "absolute";

  float x = available.x + margin.left;
  float y = available.y + margin.top;
  if (absolute) {
    x = available.x + resolveLength(styleLength(node, "left"), available.width,
                                    viewport_width, viewport_height, font_size,
                                    kDefaultFontSize, 0.0f) + margin.left;
    y = available.y + resolveLength(styleLength(node, "top"), available.height,
                                    viewport_width, viewport_height, font_size,
                                    kDefaultFontSize, 0.0f) + margin.top;
  }

  const Length width_length = styleLength(node, "width");
  const Length height_length = styleLength(node, "height");
  std::vector<Node*> children = visibleRuntimeChildren(node);
  float auto_width = std::max(0.0f, available.width - margin.left - margin.right);
  if ((node.tag == "text" || children.empty()) &&
      !node.text.empty()) {
    auto_width = std::min(
        auto_width,
        intrinsicTextWidth(node, font_size) + padding.left + padding.right);
  }
  float width = root ? available.width
                     : resolveLength(width_length, available.width, viewport_width,
                                     viewport_height, font_size, kDefaultFontSize,
                                     auto_width);
  width = std::max(0.0f, width);
  float explicit_height = root ? available.height
                               : resolveLength(height_length, available.height, viewport_width,
                                               viewport_height, font_size, kDefaultFontSize,
                                               -1.0f);
  const float content_width = std::max(0.0f, width - padding.left - padding.right);
  const float gap = resolveLength(styleLength(node, "gap"), content_width, viewport_width,
                                  viewport_height, font_size, kDefaultFontSize, 0.0f);
  const std::string display = styleString(node, "display", "block");
  const std::string direction = styleString(node, "flex-direction", "row");
  float children_height = 0.0f;

  Rect provisional{.x = x, .y = y, .width = width,
                   .height = explicit_height >= 0.0f ? explicit_height : available.height};
  Rect child_clip = inherited_clip;
  const bool clips_overflow = clipsOverflow(node);
  if (clips_overflow) {
    child_clip = clipForOverflow(node, inherited_clip, provisional);
  }
  const float child_origin_x = x + padding.left - node.scroll_x;
  const float child_origin_y = y + padding.top - node.scroll_y;

  if (display == "flex" && direction == "row" && !children.empty()) {
    float fixed_width = gap * static_cast<float>(children.size() - 1u);
    float total_grow = 0.0f;
    std::vector<float> widths(children.size(), -1.0f);
    for (std::size_t i = 0; i < children.size(); ++i) {
      const Length child_width = styleLength(*children[i], "width");
      if (child_width.unit != Unit::Auto && child_width.unit != Unit::Fr) {
        widths[i] = resolveLength(child_width, content_width, viewport_width, viewport_height,
                                  nodeFontSize(*children[i]), kDefaultFontSize, 0.0f);
        fixed_width += widths[i];
      } else {
        total_grow += std::max(0.0f, styleFloat(*children[i], "flex-grow", 1.0f));
      }
    }
    const float remaining = std::max(0.0f, content_width - fixed_width);
    float cursor_x = child_origin_x;
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (widths[i] < 0.0f) {
        const float grow = std::max(0.0f, styleFloat(*children[i], "flex-grow", 1.0f));
        widths[i] = total_grow > 0.0f ? remaining * grow / total_grow : 0.0f;
      }
      const float used = layoutNodeRecursive(
          *children[i],
          Rect{.x = cursor_x,
               .y = child_origin_y,
               .width = widths[i],
               .height = std::max(
                   0.0f,
                   available.height - padding.top - padding.bottom)},
          child_clip, viewport_width, viewport_height);
      children_height = std::max(children_height, used);
      cursor_x += widths[i] + gap;
    }
  } else if (display == "grid" && !children.empty()) {
    auto tracks = parseGridTracks(styleString(node, "grid-template-columns", "1fr"));
    if (tracks.empty()) tracks.push_back(Length{.value = 1.0f, .unit = Unit::Fr});
    float fixed = gap * static_cast<float>(tracks.size() - 1u);
    float fr_total = 0.0f;
    std::vector<float> columns(tracks.size(), 0.0f);
    for (std::size_t i = 0; i < tracks.size(); ++i) {
      if (tracks[i].unit == Unit::Fr || tracks[i].unit == Unit::Auto) {
        fr_total += tracks[i].unit == Unit::Fr ? std::max(0.0f, tracks[i].value) : 1.0f;
      } else {
        columns[i] = resolveLength(tracks[i], content_width, viewport_width, viewport_height,
                                   font_size, kDefaultFontSize, 0.0f);
        fixed += columns[i];
      }
    }
    const float remaining = std::max(0.0f, content_width - fixed);
    for (std::size_t i = 0; i < tracks.size(); ++i) {
      if (columns[i] == 0.0f) {
        const float fraction = tracks[i].unit == Unit::Fr ? std::max(0.0f, tracks[i].value) : 1.0f;
        columns[i] = fr_total > 0.0f ? remaining * fraction / fr_total : 0.0f;
      }
    }
    std::vector<float> column_x(columns.size(), child_origin_x);
    for (std::size_t i = 1; i < columns.size(); ++i) {
      column_x[i] = column_x[i - 1] + columns[i - 1] + gap;
    }
    float row_y = child_origin_y;
    float row_height = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
      const std::size_t column = i % columns.size();
      if (column == 0u && i != 0u) {
        row_y += row_height + gap;
        children_height += row_height + gap;
        row_height = 0.0f;
      }
      const float used = layoutNodeRecursive(
          *children[i], Rect{.x = column_x[column], .y = row_y, .width = columns[column],
                             .height = std::max(0.0f, available.height)},
          child_clip, viewport_width, viewport_height);
      row_height = std::max(row_height, used);
    }
    children_height += row_height;
  } else {
    float cursor_y = child_origin_y;
    for (Node* child : children) {
      const bool child_absolute = styleString(*child, "position", "relative") == "absolute";
      const float used = layoutNodeRecursive(
          *child, Rect{.x = child_origin_x, .y = cursor_y, .width = content_width,
                       .height = std::max(0.0f, available.height - (cursor_y - child_origin_y))},
          child_clip, viewport_width, viewport_height);
      if (!child_absolute) {
        cursor_y += used + gap;
        children_height += used + gap;
      }
    }
    if (!children.empty()) children_height = std::max(0.0f, children_height - gap);
  }

  float intrinsic_height = padding.top + padding.bottom + children_height;
  if (!node.text.empty()) {
    intrinsic_height = std::max(intrinsic_height,
                                padding.top + padding.bottom + nodeLineHeight(node, font_size));
  }
  float height = explicit_height >= 0.0f ? explicit_height : intrinsic_height;
  if (height <= 0.0f &&
      (node.tag == "div" || node.tag == "scroll" || node.tag == "list")) {
    height = children_height;
  }
  node.layout = Rect{.x = x, .y = y, .width = width, .height = std::max(0.0f, height)};
  updateWindowGeometry(node);
  node.clip = inherited_clip;
  if (clips_overflow) {
    node.clip = clipForOverflow(node, inherited_clip, node.layout);
  }
  return margin.top + node.layout.height + margin.bottom;
}

}  // namespace

LayoutResult layoutDocument(
    DocumentInstance& doc,
    const LayoutInputs& inputs,
    TextEngine& text_engine,
    PresentationResources& presentation_resources) {
  if (!doc.body || inputs.logical_width <= 0 || inputs.logical_height <= 0) return {};
  auto count_nodes = [&](auto&& self, const Node& node) -> std::size_t {
    std::size_t count = node.present ? 1u : 0u;
    forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
      count += self(self, child);
    });
    return count;
  };
  const std::size_t laid_out_nodes =
      count_nodes(count_nodes, *doc.body);
  const native::CanvasRect& canvas_rect = doc.canvas_layout.layout_rect;
  const native::CanvasRect& canvas_clip = doc.canvas_layout.layout_clip;
  const Rect viewport{.x = canvas_rect.x,
                      .y = canvas_rect.y,
                      .width = canvas_rect.width,
                      .height = canvas_rect.height};
  const Rect viewport_clip{.x = canvas_clip.x,
                           .y = canvas_clip.y,
                           .width = canvas_clip.width,
                           .height = canvas_clip.height};

  auto convertedLength = [&](const Node& node,
                             std::string_view property,
                             float reference) -> layout::Length {
    const auto found = node.style.find(std::string(property));
    if (found == node.style.end()) return layout::Length::automatic();
    const Length parsed = parseLength(found->second);
    switch (parsed.unit) {
      case Unit::Auto: return layout::Length::automatic();
      case Unit::Px: return layout::Length::pixels(parsed.value);
      case Unit::Percent: return layout::Length::percent(parsed.value);
      case Unit::Fr: return layout::Length::fraction(parsed.value);
      case Unit::Vw:
      case Unit::Vh:
      case Unit::Em:
      case Unit::Rem:
        return layout::Length::pixels(resolveLength(
            parsed, reference, viewport.width, viewport.height, nodeFontSize(node),
            kDefaultFontSize, 0.0f));
    }
    return layout::Length::automatic();
  };
  auto alignment = [](std::string value, layout::Alignment fallback) {
    value = lower(trim(value));
    if (value == "start" || value == "flex-start" || value == "left") {
      return layout::Alignment::Start;
    }
    if (value == "center") return layout::Alignment::Center;
    if (value == "end" || value == "flex-end" || value == "right") {
      return layout::Alignment::End;
    }
    if (value == "stretch") return layout::Alignment::Stretch;
    if (value == "space-between") return layout::Alignment::SpaceBetween;
    if (value == "space-around") return layout::Alignment::SpaceAround;
    if (value == "space-evenly") return layout::Alignment::SpaceEvenly;
    if (value == "auto") return layout::Alignment::Auto;
    return fallback;
  };
  auto parsePlacement = [](std::string value, layout::GridPlacement& placement) {
    value = lower(trim(value));
    if (value.empty() || value == "auto") return;
    const std::size_t slash = value.find('/');
    const std::string first = trim(std::string_view(value).substr(0u, slash));
    const std::string second = slash == std::string::npos
                                   ? std::string{}
                                   : trim(std::string_view(value).substr(slash + 1u));
    auto parse_positive = [](std::string_view text) -> std::size_t {
      const std::optional<double> number = parseFiniteDouble(text);
      return number.has_value() && *number >= 1.0 && *number <= 4096.0
                 ? static_cast<std::size_t>(*number)
                 : 0u;
    };
    if (first.starts_with("span ")) {
      placement.span = std::max<std::size_t>(1u, parse_positive(first.substr(5u)));
    } else {
      placement.start = parse_positive(first);
    }
    if (second.starts_with("span ")) {
      placement.span = std::max<std::size_t>(1u, parse_positive(second.substr(5u)));
    } else if (!second.empty() && placement.start > 0u) {
      const std::size_t end = parse_positive(second);
      if (end > placement.start) placement.span = end - placement.start;
    }
  };

  struct Adapter {
    Node* dom = nullptr;
    layout::LayoutNode layout_node;
    std::vector<std::unique_ptr<Adapter>> children;
  };
  auto buildAdapter = [&](auto&& self, Node& node) -> std::unique_ptr<Adapter> {
    auto adapter = std::make_unique<Adapter>();
    adapter->dom = &node;
    layout::LayoutStyle& style = adapter->layout_node.style;
    const std::string display = styleString(node, "display", "block");
    style.display = display == "none" ? layout::Display::None
                    : display == "flex" ? layout::Display::Flex
                    : display == "grid" ? layout::Display::Grid
                                         : layout::Display::Block;
    style.position = styleString(node, "position", "relative") == "absolute"
                         ? layout::Position::Absolute
                         : layout::Position::Relative;
    style.width = convertedLength(node, "width", viewport.width);
    style.height = convertedLength(node, "height", viewport.height);
    style.min_width = convertedLength(node, "min-width", viewport.width);
    style.min_height = convertedLength(node, "min-height", viewport.height);
    style.max_width = convertedLength(node, "max-width", viewport.width);
    style.max_height = convertedLength(node, "max-height", viewport.height);
    style.left = convertedLength(node, "left", viewport.width);
    style.top = convertedLength(node, "top", viewport.height);
    style.right = convertedLength(node, "right", viewport.width);
    style.bottom = convertedLength(node, "bottom", viewport.height);
    const float font_size = nodeFontSize(node);
    auto box = [&](std::string_view property, std::string_view side) {
      const std::string side_property = std::string(property) + "-" + std::string(side);
      auto found = node.style.find(side_property);
      std::string selected;
      if (found != node.style.end()) {
        selected = found->second;
      } else if ((found = node.style.find(std::string(property))) != node.style.end()) {
        const std::vector<std::string> values = splitWhitespace(found->second);
        if (values.empty()) return layout::Length::pixels(0.0f);
        std::size_t index = 0u;
        if (values.size() == 2u) index = (side == "left" || side == "right") ? 1u : 0u;
        if (values.size() == 3u) {
          index = side == "top" ? 0u : (side == "bottom" ? 2u : 1u);
        }
        if (values.size() >= 4u) {
          index = side == "top" ? 0u : side == "right" ? 1u
                                  : side == "bottom" ? 2u : 3u;
        }
        selected = values[std::min(index, values.size() - 1u)];
      } else {
        return layout::Length::pixels(0.0f);
      }
      const Length length = parseLength(selected);
      if (length.unit == Unit::Auto) {
        return property == "margin" ? layout::Length::automatic()
                                     : layout::Length::pixels(0.0f);
      }
      if (length.unit == Unit::Percent) return layout::Length::percent(length.value);
      if (length.unit == Unit::Px) return layout::Length::pixels(length.value);
      return layout::Length::pixels(resolveLength(
          length, viewport.width, viewport.width, viewport.height, font_size,
          kDefaultFontSize, 0.0f));
    };
    style.margin = {.left = box("margin", "left"),
                    .top = box("margin", "top"),
                    .right = box("margin", "right"),
                    .bottom = box("margin", "bottom")};
    style.padding = {.left = box("padding", "left"),
                     .top = box("padding", "top"),
                     .right = box("padding", "right"),
                     .bottom = box("padding", "bottom")};
    style.border = {
        .left = oneBoxValue(node, "border-width", "left", viewport.width,
                            viewport.width, viewport.height, font_size),
        .top = oneBoxValue(node, "border-width", "top", viewport.width,
                           viewport.width, viewport.height, font_size),
        .right = oneBoxValue(node, "border-width", "right", viewport.width,
                             viewport.width, viewport.height, font_size),
        .bottom = oneBoxValue(node, "border-width", "bottom", viewport.width,
                              viewport.width, viewport.height, font_size),
    };

    const std::string flex_direction = styleString(node, "flex-direction", "row");
    style.flex_direction = flex_direction == "row-reverse"
                               ? layout::FlexDirection::RowReverse
                           : flex_direction == "column"
                               ? layout::FlexDirection::Column
                           : flex_direction == "column-reverse"
                               ? layout::FlexDirection::ColumnReverse
                               : layout::FlexDirection::Row;
    const std::string flex_wrap = styleString(node, "flex-wrap", "nowrap");
    style.flex_wrap = flex_wrap == "wrap" ? layout::FlexWrap::Wrap
                      : flex_wrap == "wrap-reverse" ? layout::FlexWrap::WrapReverse
                                                     : layout::FlexWrap::NoWrap;
    style.flex_grow = std::max(0.0f, styleFloat(node, "flex-grow", 0.0f));
    style.flex_shrink = std::max(0.0f, styleFloat(node, "flex-shrink", 1.0f));
    style.flex_basis = convertedLength(node, "flex-basis", viewport.width);
    style.justify_content = alignment(styleString(node, "justify-content", "start"),
                                      layout::Alignment::Start);
    style.align_content = alignment(styleString(node, "align-content", "stretch"),
                                    layout::Alignment::Stretch);
    style.align_items = alignment(styleString(node, "align-items", "stretch"),
                                  layout::Alignment::Stretch);
    style.align_self = alignment(styleString(node, "align-self", "auto"),
                                 layout::Alignment::Auto);
    style.column_gap = node.style.contains("column-gap")
                           ? convertedLength(node, "column-gap", viewport.width)
                           : convertedLength(node, "gap", viewport.width);
    style.row_gap = node.style.contains("row-gap")
                        ? convertedLength(node, "row-gap", viewport.height)
                        : convertedLength(node, "gap", viewport.height);

    auto tracks = [&](std::string_view property, std::vector<layout::GridTrack>& output) {
      const auto found = node.style.find(std::string(property));
      if (found == node.style.end()) return;
      std::vector<layout::GridTrack> parsed;
      if (layout::parseTrackList(found->second, parsed)) output = std::move(parsed);
    };
    tracks("grid-template-columns", style.grid_template_columns);
    tracks("grid-template-rows", style.grid_template_rows);
    tracks("grid-auto-columns", style.grid_auto_columns);
    tracks("grid-auto-rows", style.grid_auto_rows);
    style.grid_auto_flow = styleString(node, "grid-auto-flow", "row") == "column"
                               ? layout::GridAutoFlow::Column
                               : layout::GridAutoFlow::Row;
    if (const auto found = node.style.find("grid-column"); found != node.style.end()) {
      parsePlacement(found->second, style.grid_column);
    } else if (node.style.contains("grid-column-start") ||
               node.style.contains("grid-column-end")) {
      const std::string start = node.style.contains("grid-column-start")
                                    ? node.style.at("grid-column-start")
                                    : "auto";
      const std::string end = node.style.contains("grid-column-end")
                                  ? node.style.at("grid-column-end")
                                  : "auto";
      parsePlacement(start + " / " + end, style.grid_column);
    }
    if (const auto found = node.style.find("grid-row"); found != node.style.end()) {
      parsePlacement(found->second, style.grid_row);
    } else if (node.style.contains("grid-row-start") ||
               node.style.contains("grid-row-end")) {
      const std::string start = node.style.contains("grid-row-start")
                                    ? node.style.at("grid-row-start")
                                    : "auto";
      const std::string end = node.style.contains("grid-row-end")
                                  ? node.style.at("grid-row-end")
                                  : "auto";
      parsePlacement(start + " / " + end, style.grid_row);
    }
    style.justify_items = alignment(styleString(node, "justify-items", "stretch"),
                                    layout::Alignment::Stretch);
    style.justify_self = alignment(styleString(node, "justify-self", "auto"),
                                   layout::Alignment::Auto);

    if (!node.text.empty()) {
      adapter->layout_node.measure =
          [&text_engine, &inputs, &node](
              const layout::MeasureConstraints& constraints) {
        native::ShapeRequest request;
        request.text_utf8 = node.text;
        request.font_keys = node.font_keys;
        if (request.font_keys.empty()) request.font_keys.emplace_back("__karma_no_font__");
        request.locale = styleString(node, "locale", std::string(inputs.locale));
        request.pixel_size = nodeFontSize(node);
        request.line_height = nodeLineHeight(node, request.pixel_size);
        request.letter_spacing = styleFloat(node, "letter-spacing", 0.0f);
        if (constraints.width_mode != layout::MeasureMode::Undefined &&
            styleString(node, "white-space", "normal") != "nowrap") {
          request.max_width = std::max(0.0f, constraints.available_width);
        }
        const std::string direction = styleString(node, "direction", "auto");
        request.direction = direction == "rtl" ? native::TextDirection::RightToLeft
                            : direction == "ltr" ? native::TextDirection::LeftToRight
                                                 : native::TextDirection::Auto;
        const auto shaped = text_engine.shape(request);
        if (shaped.has_value()) {
          return layout::Size{.width = shaped->width, .height = shaped->height};
        }
        return layout::Size{.width = intrinsicTextWidth(node, request.pixel_size),
                            .height = request.line_height};
      };
    } else if (const auto dimensions =
                   presentation_resources.intrinsicSize(node.image)) {
      adapter->layout_node.intrinsic_size = {
          .width = dimensions->first, .height = dimensions->second};
    }
    adapter->layout_node.user_data = &node;
    forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
      if (!child.present || child.collapsed_hidden) return;
      auto child_adapter = self(self, child);
      adapter->layout_node.appendChild(child_adapter->layout_node);
      adapter->children.push_back(std::move(child_adapter));
    });
    return adapter;
  };

  std::unique_ptr<Adapter> root = buildAdapter(buildAdapter, *doc.body);
  const layout::WritingDirection direction =
      styleString(*doc.body, "direction", "ltr") == "rtl"
          ? layout::WritingDirection::RightToLeft
          : layout::WritingDirection::LeftToRight;
  const layout::LayoutResult result = layout::LayoutEngine{}.calculate(
      root->layout_node,
      {.available_size = {.width = viewport.width, .height = viewport.height},
       .direction = direction});
  if (!result) {
    addDiagnostic(doc.diagnostics, doc.asset_key, "UI_LAYOUT_ERROR", result.error, 0,
                  DiagnosticSeverity::Warning);
    layoutNodeRecursive(*doc.body, viewport, viewport, viewport.width, viewport.height, true);
  } else {
    auto translate_adapter = [&](auto&& self,
                                 Adapter& adapter,
                                 float delta_x,
                                 float delta_y) -> void {
      adapter.layout_node.rect.x += delta_x;
      adapter.layout_node.rect.y += delta_y;
      for (auto& child : adapter.children) {
        self(self, *child, delta_x, delta_y);
      }
    };
    auto apply_anchors = [&](auto&& self, Adapter& parent) -> void {
      const layout::Rect& parent_rect = parent.layout_node.rect;
      const float parent_font_size = nodeFontSize(*parent.dom);
      auto inset = [&](std::string_view property, std::string_view side) {
        return oneBoxValue(*parent.dom, property, side, parent_rect.width,
                           viewport.width, viewport.height, parent_font_size);
      };
      const float left = inset("border-width", "left") + inset("padding", "left");
      const float top = inset("border-width", "top") + inset("padding", "top");
      const float right = inset("border-width", "right") + inset("padding", "right");
      const float bottom = inset("border-width", "bottom") + inset("padding", "bottom");
      const native::CanvasRect content{
          .x = parent_rect.x + left,
          .y = parent_rect.y + top,
          .width = std::max(0.0f, parent_rect.width - left - right),
          .height = std::max(0.0f, parent_rect.height - top - bottom)};

      for (auto& child : parent.children) {
        if (child->dom->anchor) {
          const layout::Rect before = child->layout_node.rect;
          const native::CanvasRect target = native::resolveAnchor(
              content,
              {.x = before.x, .y = before.y,
               .width = before.width, .height = before.height},
              *child->dom->anchor);
          const bool resized = std::abs(target.width - before.width) > 0.001f ||
                               std::abs(target.height - before.height) > 0.001f;
          if (resized) {
            layout::LayoutStyle saved = child->layout_node.style;
            child->layout_node.style.position = layout::Position::Relative;
            child->layout_node.style.left = layout::Length::automatic();
            child->layout_node.style.top = layout::Length::automatic();
            child->layout_node.style.right = layout::Length::automatic();
            child->layout_node.style.bottom = layout::Length::automatic();
            child->layout_node.style.width = layout::Length::pixels(target.width);
            child->layout_node.style.height = layout::Length::pixels(target.height);
            const layout::LayoutResult anchored_result = layout::LayoutEngine{}.calculate(
                child->layout_node,
                {.available_size = {.width = target.width, .height = target.height},
                 .direction = direction});
            child->layout_node.style = std::move(saved);
            if (!anchored_result) {
              addDiagnostic(doc.diagnostics, doc.asset_key, "UI_ANCHOR_LAYOUT_ERROR",
                            anchored_result.error, 0,
                            DiagnosticSeverity::Warning);
            }
          }
          translate_adapter(translate_adapter, *child,
                            target.x - child->layout_node.rect.x,
                            target.y - child->layout_node.rect.y);
        }
        self(self, *child);
      }
    };
    apply_anchors(apply_anchors, *root);

    auto apply = [&](auto&& self,
                     Adapter& adapter,
                     Rect inherited_clip,
                     float offset_x,
                     float offset_y) -> void {
      Node& node = *adapter.dom;
      node.layout = {.x = adapter.layout_node.rect.x + offset_x,
                     .y = adapter.layout_node.rect.y + offset_y,
                     .width = adapter.layout_node.rect.width,
                     .height = adapter.layout_node.rect.height};
      updateWindowGeometry(node);
      node.clip = inherited_clip;
      Rect child_clip = inherited_clip;
      if (isScrollContainer(node)) {
        float content_right = adapter.layout_node.rect.x;
        float content_bottom = adapter.layout_node.rect.y;
        for (const auto& child : adapter.children) {
          content_right = std::max(content_right,
                                   child->layout_node.rect.x + child->layout_node.rect.width);
          content_bottom = std::max(content_bottom,
                                    child->layout_node.rect.y + child->layout_node.rect.height);
        }
        const float content_width = content_right - adapter.layout_node.rect.x;
        float content_height = content_bottom - adapter.layout_node.rect.y;
        if (node.tag == "list" && node.virtual_item_extent > 0.0f) {
          content_height = std::max(
              content_height,
              static_cast<float>(node.virtual_total_count) *
                  node.virtual_item_extent);
        }
        updateScrollbarGeometry(node, content_width, content_height);
        child_clip = clipForOverflow(node, child_clip, node.scroll_viewport);
      } else {
        node.scroll_x = 0.0f;
        node.scroll_y = 0.0f;
        node.scroll_max_x = 0.0f;
        node.scroll_max_y = 0.0f;
        node.scroll_content_width = 0.0f;
        node.scroll_content_height = 0.0f;
        node.scroll_viewport = node.layout;
        node.horizontal_scroll_track = {};
        node.horizontal_scroll_thumb = {};
        node.vertical_scroll_track = {};
        node.vertical_scroll_thumb = {};
        node.scrollbar_corner = {};
        if (clipsOverflow(node)) {
          child_clip = clipForOverflow(node, child_clip, node.layout);
        }
      }
      const float child_offset_x = offset_x - node.scroll_x;
      const float child_offset_y = offset_y - node.scroll_y;
      for (auto& child : adapter.children) {
        self(self, *child, child_clip, child_offset_x, child_offset_y);
      }
    };
    apply(apply, *root, viewport_clip, viewport.x, viewport.y);
  }
  if (doc.body) invalidatePaintTree(*doc.body);
  doc.layout_revision = false;
  doc.measure_revision = false;
  doc.accessibility_revision = true;
  doc.placement_revision = true;
  doc.virtual_range_revision = true;
  return {.laid_out_nodes = laid_out_nodes, .performed = true};
}

}  // namespace karma::ui::native::document_layout_runtime
