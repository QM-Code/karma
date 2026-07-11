#include "features/ui/native/layout_engine.h"

#include "features/ui/native/string_utils.h"

#include <yoga/Yoga.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>

namespace karma::ui::layout {
namespace {

constexpr std::size_t kMaxTracks = 4096;
constexpr std::size_t kMaxGridCells = 1024 * 1024;
constexpr float kEpsilon = 0.00001f;

std::string_view trimView(std::string_view source) {
  while (!source.empty() &&
         std::isspace(static_cast<unsigned char>(source.front())) != 0) {
    source.remove_prefix(1);
  }
  while (!source.empty() &&
         std::isspace(static_cast<unsigned char>(source.back())) != 0) {
    source.remove_suffix(1);
  }
  return source;
}

using native::string_utils::lower;

bool parseFloat(std::string_view source, float& result) {
  const std::string owned(trimView(source));
  if (owned.empty()) {
    return false;
  }
  char* end = nullptr;
  const float parsed = std::strtof(owned.c_str(), &end);
  if (end != owned.c_str() + owned.size() || !std::isfinite(parsed)) {
    return false;
  }
  result = parsed;
  return true;
}

bool startsWithFunction(std::string_view value, std::string_view name) {
  return value.size() > name.size() + 2 && value.substr(0, name.size()) == name &&
         value[name.size()] == '(' && value.back() == ')';
}

bool splitTopLevel(std::string_view source,
                   char separator,
                   std::vector<std::string_view>& output,
                   bool whitespace_separator,
                   std::string* error) {
  output.clear();
  int depth = 0;
  std::size_t token_begin = 0;
  bool in_token = false;
  for (std::size_t index = 0; index < source.size(); ++index) {
    const char ch = source[index];
    if (ch == '(') {
      ++depth;
      in_token = true;
    } else if (ch == ')') {
      --depth;
      if (depth < 0) {
        if (error != nullptr) {
          *error = "unexpected ')' in track list";
        }
        return false;
      }
      in_token = true;
    }

    const bool is_separator =
        depth == 0 &&
        (whitespace_separator
             ? std::isspace(static_cast<unsigned char>(ch)) != 0
             : ch == separator);
    if (is_separator) {
      if (in_token) {
        const auto token = trimView(source.substr(token_begin, index - token_begin));
        if (!token.empty()) {
          output.push_back(token);
        }
        in_token = false;
      }
      token_begin = index + 1;
    } else if (!in_token) {
      token_begin = index;
      in_token = true;
    }
  }
  if (depth != 0) {
    if (error != nullptr) {
      *error = "unclosed '(' in track list";
    }
    return false;
  }
  if (in_token) {
    const auto token = trimView(source.substr(token_begin));
    if (!token.empty()) {
      output.push_back(token);
    }
  }
  return true;
}

bool parseTrackToken(std::string_view token, GridTrack& output, std::string* error) {
  token = trimView(token);
  const std::string normalized = lower(token);
  if (startsWithFunction(normalized, "minmax")) {
    const auto arguments = token.substr(7, token.size() - 8);
    std::vector<std::string_view> parts;
    if (!splitTopLevel(arguments, ',', parts, false, error) || parts.size() != 2) {
      if (error != nullptr && error->empty()) {
        *error = "minmax() requires exactly two track breadths";
      }
      return false;
    }
    const auto minimum = parseLength(parts[0]);
    const auto maximum = parseLength(parts[1]);
    if (!minimum || !maximum || minimum->unit == LengthUnit::Fraction ||
        (maximum->unit == LengthUnit::Fraction && maximum->value <= 0.0f) ||
        minimum->value < 0.0f || maximum->value < 0.0f) {
      if (error != nullptr) {
        *error = "invalid minmax() breadth";
      }
      return false;
    }
    output = GridTrack::minmax(*minimum, *maximum);
    return true;
  }

  if (startsWithFunction(normalized, "repeat")) {
    const auto arguments = token.substr(7, token.size() - 8);
    std::vector<std::string_view> parts;
    if (!splitTopLevel(arguments, ',', parts, false, error) || parts.size() != 2) {
      if (error != nullptr && error->empty()) {
        *error = "repeat() requires a count and a track list";
      }
      return false;
    }
    std::size_t count = 0;
    const auto count_text = trimView(parts[0]);
    const auto [end, ec] =
        std::from_chars(count_text.data(), count_text.data() + count_text.size(), count);
    if (ec != std::errc{} || end != count_text.data() + count_text.size() || count == 0 ||
        count > kMaxTracks) {
      if (error != nullptr) {
        *error = "repeat() count must be an integer from 1 through 4096";
      }
      return false;
    }
    std::vector<GridTrack> repeated;
    if (!parseTrackList(parts[1], repeated, error) || repeated.empty()) {
      if (error != nullptr && error->empty()) {
        *error = "repeat() requires at least one repeated track";
      }
      return false;
    }
    output = GridTrack::repeat(count, std::move(repeated));
    return true;
  }

  const auto length = parseLength(token);
  if (!length || length->value < 0.0f ||
      (length->unit == LengthUnit::Fraction && length->value <= 0.0f)) {
    if (error != nullptr) {
      *error = "invalid Grid track breadth '" + std::string(token) + "'";
    }
    return false;
  }
  output.kind = GridTrack::Kind::Breadth;
  output.breadth = *length;
  return true;
}

float canonical(float value) {
  if (!std::isfinite(value) || std::abs(value) < kEpsilon) {
    return 0.0f;
  }
  return value;
}

float nonNegative(float value) {
  return std::max(0.0f, canonical(value));
}

float resolve(const Length& length, float reference, float automatic = 0.0f) {
  switch (length.unit) {
    case LengthUnit::Auto: return automatic;
    case LengthUnit::Pixels: return length.value;
    case LengthUnit::Percent: return reference * length.value * 0.01f;
    case LengthUnit::Fraction: return automatic;
  }
  return automatic;
}

float clampDimension(float value,
                     const Length& minimum,
                     const Length& maximum,
                     float reference) {
  if (minimum.unit != LengthUnit::Auto && minimum.unit != LengthUnit::Fraction) {
    value = std::max(value, resolve(minimum, reference));
  }
  if (maximum.unit != LengthUnit::Auto && maximum.unit != LengthUnit::Fraction) {
    value = std::min(value, resolve(maximum, reference));
  }
  return nonNegative(value);
}

Alignment resolvedAlignment(Alignment own, Alignment parent) {
  return own == Alignment::Auto ? parent : own;
}

struct PlacedItem {
  LayoutNode* node = nullptr;
  std::size_t column = 0;
  std::size_t row = 0;
  std::size_t column_span = 1;
  std::size_t row_span = 1;
  bool placed = false;
};

struct GridModel {
  std::vector<GridTrack> columns;
  std::vector<GridTrack> rows;
  std::vector<PlacedItem> items;
};

struct TrackState {
  float size = 0.0f;
  float cap = 0.0f;
  float flex = 0.0f;
  bool has_cap = false;
  bool accepts_intrinsic = false;
  bool auto_track = false;
};

struct AxisLayout {
  std::vector<TrackState> tracks;
  std::vector<float> positions;
  float gap = 0.0f;
  float extent = 0.0f;
};

class Calculation;

struct YogaEntry {
  Calculation* calculation = nullptr;
  LayoutNode* layout_node = nullptr;
  YGNodeRef yoga_node = nullptr;
  std::vector<YogaEntry*> children;
  bool grid_leaf = false;
};

struct YogaTree {
  YGNodeRef root = nullptr;
  std::vector<std::unique_ptr<YogaEntry>> entries;

  ~YogaTree() {
    if (root != nullptr) {
      YGNodeFreeRecursive(root);
    }
  }
};

YGAlign yogaAlign(Alignment alignment, bool allow_auto) {
  switch (alignment) {
    case Alignment::Auto: return allow_auto ? YGAlignAuto : YGAlignStretch;
    case Alignment::Start: return YGAlignFlexStart;
    case Alignment::Center: return YGAlignCenter;
    case Alignment::End: return YGAlignFlexEnd;
    case Alignment::Stretch: return YGAlignStretch;
    case Alignment::SpaceBetween: return YGAlignSpaceBetween;
    case Alignment::SpaceAround: return YGAlignSpaceAround;
    case Alignment::SpaceEvenly: return YGAlignSpaceEvenly;
  }
  return YGAlignFlexStart;
}

YGJustify yogaJustify(Alignment alignment) {
  switch (alignment) {
    case Alignment::Center: return YGJustifyCenter;
    case Alignment::End: return YGJustifyFlexEnd;
    case Alignment::SpaceBetween: return YGJustifySpaceBetween;
    case Alignment::SpaceAround: return YGJustifySpaceAround;
    case Alignment::SpaceEvenly: return YGJustifySpaceEvenly;
    case Alignment::Auto:
    case Alignment::Start:
    case Alignment::Stretch: return YGJustifyFlexStart;
  }
  return YGJustifyFlexStart;
}

YGFlexDirection yogaDirection(FlexDirection direction) {
  switch (direction) {
    case FlexDirection::Row: return YGFlexDirectionRow;
    case FlexDirection::RowReverse: return YGFlexDirectionRowReverse;
    case FlexDirection::Column: return YGFlexDirectionColumn;
    case FlexDirection::ColumnReverse: return YGFlexDirectionColumnReverse;
  }
  return YGFlexDirectionRow;
}

YGWrap yogaWrap(FlexWrap wrap) {
  switch (wrap) {
    case FlexWrap::NoWrap: return YGWrapNoWrap;
    case FlexWrap::Wrap: return YGWrapWrap;
    case FlexWrap::WrapReverse: return YGWrapWrapReverse;
  }
  return YGWrapNoWrap;
}

MeasureMode measureMode(YGMeasureMode mode) {
  switch (mode) {
    case YGMeasureModeExactly: return MeasureMode::Exactly;
    case YGMeasureModeAtMost: return MeasureMode::AtMost;
    case YGMeasureModeUndefined: return MeasureMode::Undefined;
  }
  return MeasureMode::Undefined;
}

void setYogaDimension(YGNodeRef node,
                      const Length& length,
                      void (*points)(YGNodeRef, float),
                      void (*percent)(YGNodeRef, float),
                      void (*automatic)(YGNodeRef)) {
  switch (length.unit) {
    case LengthUnit::Pixels: points(node, length.value); break;
    case LengthUnit::Percent: percent(node, length.value); break;
    case LengthUnit::Auto:
    case LengthUnit::Fraction: automatic(node); break;
  }
}

void setYogaMinDimension(YGNodeRef node,
                         const Length& length,
                         void (*points)(YGNodeRef, float),
                         void (*percent)(YGNodeRef, float)) {
  switch (length.unit) {
    case LengthUnit::Pixels: points(node, length.value); break;
    case LengthUnit::Percent: percent(node, length.value); break;
    case LengthUnit::Auto:
    case LengthUnit::Fraction: break;
  }
}

void setYogaEdge(YGNodeRef node,
                 YGEdge edge,
                 const Length& length,
                 void (*points)(YGNodeRef, YGEdge, float),
                 void (*percent)(YGNodeRef, YGEdge, float),
                 void (*automatic)(YGNodeRef, YGEdge)) {
  switch (length.unit) {
    case LengthUnit::Pixels: points(node, edge, length.value); break;
    case LengthUnit::Percent: percent(node, edge, length.value); break;
    case LengthUnit::Auto:
    case LengthUnit::Fraction:
      if (automatic != nullptr) {
        automatic(node, edge);
      }
      break;
  }
}

bool expandTracks(const std::vector<GridTrack>& input,
                  std::vector<GridTrack>& output,
                  std::string& error) {
  for (const auto& track : input) {
    if (track.kind != GridTrack::Kind::Repeat) {
      if (output.size() == kMaxTracks) {
        error = "Grid expands beyond 4096 tracks";
        return false;
      }
      output.push_back(track);
      continue;
    }
    if (track.repeat_count == 0 || track.repeated_tracks.empty()) {
      error = "Grid repeat requires a positive count and at least one track";
      return false;
    }
    for (std::size_t repeat = 0; repeat < track.repeat_count; ++repeat) {
      if (!expandTracks(track.repeated_tracks, output, error)) {
        return false;
      }
    }
  }
  return true;
}

const GridTrack& implicitTrack(const std::vector<GridTrack>& pattern,
                               std::size_t implicit_index) {
  static const GridTrack automatic = GridTrack::automatic();
  if (pattern.empty()) {
    return automatic;
  }
  return pattern[implicit_index % pattern.size()];
}

class Calculation {
 public:
  explicit Calculation(LayoutOptions options) : options_(options) {
    config_ = YGConfigNew();
    if (config_ != nullptr) {
      YGConfigSetUseWebDefaults(config_, true);
      YGConfigSetPointScaleFactor(config_, 0.0f);
    }
  }

  ~Calculation() {
    if (config_ != nullptr) {
      YGConfigFree(config_);
    }
  }

  bool ready() {
    if (config_ == nullptr) {
      error_ = "Yoga configuration allocation failed";
      return false;
    }
    return true;
  }

  const std::string& error() const { return error_; }

  bool validateAndReset(LayoutNode& root) {
    std::unordered_set<LayoutNode*> active;
    std::unordered_set<LayoutNode*> visited;
    return validateNode(root, active, visited);
  }

  bool layoutRoot(LayoutNode& root) {
    if (root.style.display == Display::None) {
      return true;
    }
    const float width = clampDimension(
        resolve(root.style.width, options_.available_size.width,
                options_.available_size.width),
        root.style.min_width, root.style.max_width, options_.available_size.width);
    const float height = clampDimension(
        resolve(root.style.height, options_.available_size.height,
                options_.available_size.height),
        root.style.min_height, root.style.max_height, options_.available_size.height);
    return layoutAssigned(root, {.x = 0.0f, .y = 0.0f, .width = width, .height = height});
  }

  Size preferredSize(LayoutNode& node,
                     const MeasureConstraints& constraints,
                     bool constraints_are_content = false) {
    if (node.style.display == Display::None) {
      return {};
    }

    const float inset_reference = constraints.width_mode == MeasureMode::Undefined
                                      ? 0.0f
                                      : constraints.available_width;
    const float horizontal_inset =
        resolve(node.style.padding.left, inset_reference) +
        resolve(node.style.padding.right, inset_reference) + node.style.border.left +
        node.style.border.right;
    const float vertical_inset =
        resolve(node.style.padding.top, inset_reference) +
        resolve(node.style.padding.bottom, inset_reference) + node.style.border.top +
        node.style.border.bottom;
    MeasureConstraints callback_constraints = constraints;
    if (!constraints_are_content) {
      if (callback_constraints.width_mode != MeasureMode::Undefined) {
        callback_constraints.available_width =
            nonNegative(callback_constraints.available_width - horizontal_inset);
      }
      if (callback_constraints.height_mode != MeasureMode::Undefined) {
        callback_constraints.available_height =
            nonNegative(callback_constraints.available_height - vertical_inset);
      }
    }

    Size result;
    bool content_intrinsic = false;
    if (node.measure) {
      try {
        result = node.measure(callback_constraints);
        content_intrinsic = true;
      } catch (...) {
        if (error_.empty()) {
          error_ = "layout measure callback threw an exception";
        }
        return {};
      }
    } else if (node.style.display == Display::Grid) {
      result = preferredGridSize(node, callback_constraints);
    } else if (node.children.empty()) {
      result = node.intrinsic_size;
      content_intrinsic = true;
    } else {
      result = preferredFlowSize(node, callback_constraints);
    }

    const float width_reference = constraints.width_mode == MeasureMode::Undefined
                                      ? result.width
                                      : constraints.available_width +
                                            (constraints_are_content
                                                 ? horizontal_inset
                                                 : 0.0f);
    const float height_reference = constraints.height_mode == MeasureMode::Undefined
                                       ? result.height
                                       : constraints.available_height +
                                             (constraints_are_content
                                                  ? vertical_inset
                                                  : 0.0f);
    if (content_intrinsic) {
      result.width += horizontal_inset;
      result.height += vertical_inset;
    }
    if (node.style.width.unit != LengthUnit::Auto &&
        node.style.width.unit != LengthUnit::Fraction) {
      result.width = resolve(node.style.width, width_reference, result.width);
    }
    if (node.style.height.unit != LengthUnit::Auto &&
        node.style.height.unit != LengthUnit::Fraction) {
      result.height = resolve(node.style.height, height_reference, result.height);
    }
    result.width = clampDimension(result.width, node.style.min_width,
                                  node.style.max_width, width_reference);
    result.height = clampDimension(result.height, node.style.min_height,
                                   node.style.max_height, height_reference);
    if (constraints.width_mode == MeasureMode::Exactly) {
      result.width = width_reference;
    } else if (constraints.width_mode == MeasureMode::AtMost) {
      result.width = std::min(result.width, width_reference);
    }
    if (constraints.height_mode == MeasureMode::Exactly) {
      result.height = height_reference;
    } else if (constraints.height_mode == MeasureMode::AtMost) {
      result.height = std::min(result.height, height_reference);
    }
    result.width = nonNegative(result.width);
    result.height = nonNegative(result.height);
    return result;
  }

 private:
  static YGSize yogaMeasure(YGNodeConstRef yoga_node,
                            float width,
                            YGMeasureMode width_mode,
                            float height,
                            YGMeasureMode height_mode) {
    auto* entry = static_cast<YogaEntry*>(YGNodeGetContext(yoga_node));
    if (entry == nullptr || entry->calculation == nullptr || entry->layout_node == nullptr) {
      return {};
    }
    const MeasureConstraints constraints{
        .available_width = YGFloatIsUndefined(width) ? 0.0f : width,
        .available_height = YGFloatIsUndefined(height) ? 0.0f : height,
        .width_mode = measureMode(width_mode),
        .height_mode = measureMode(height_mode),
    };
    Size measured =
        entry->calculation->preferredSize(*entry->layout_node, constraints, true);
    // preferredSize() is a border-box result because Grid consumes it
    // directly. Yoga measure callbacks return content size and Yoga adds the
    // measured node's padding and border itself.
    const auto& style = entry->layout_node->style;
    const float reference = constraints.width_mode == MeasureMode::Undefined
                                ? 0.0f
                                : constraints.available_width;
    measured.width = nonNegative(
        measured.width - resolve(style.padding.left, reference) -
        resolve(style.padding.right, reference) - style.border.left -
        style.border.right);
    measured.height = nonNegative(
        measured.height - resolve(style.padding.top, reference) -
        resolve(style.padding.bottom, reference) - style.border.top -
        style.border.bottom);
    return {.width = measured.width, .height = measured.height};
  }

  bool validateNode(LayoutNode& node,
                    std::unordered_set<LayoutNode*>& active,
                    std::unordered_set<LayoutNode*>& visited) {
    if (active.contains(&node)) {
      error_ = "layout tree contains a cycle";
      return false;
    }
    if (!visited.insert(&node).second) {
      error_ = "a LayoutNode is attached more than once";
      return false;
    }
    active.insert(&node);
    node.rect = {};
    if (!validateStyle(node.style)) {
      return false;
    }
    for (LayoutNode* child : node.children) {
      if (child == nullptr) {
        error_ = "layout tree contains a null child";
        return false;
      }
      if (!validateNode(*child, active, visited)) {
        return false;
      }
    }
    active.erase(&node);
    return true;
  }

  bool validateStyle(const LayoutStyle& style) {
    const auto validLength = [](const Length& length, bool allow_negative,
                                bool allow_fraction) {
      return std::isfinite(length.value) &&
             (allow_negative || length.value >= 0.0f) &&
             (allow_fraction || length.unit != LengthUnit::Fraction) &&
             (length.unit != LengthUnit::Fraction || length.value > 0.0f);
    };
    const Length dimensions[] = {
        style.width,      style.height,     style.min_width, style.min_height,
        style.max_width,  style.max_height, style.left,      style.top,
        style.right,      style.bottom,     style.flex_basis, style.column_gap,
        style.row_gap,
    };
    for (const auto& length : dimensions) {
      if (!validLength(length, false, false)) {
        error_ = "layout dimensions must be finite, non-negative, and non-fractional";
        return false;
      }
    }
    const Length margins[] = {style.margin.left, style.margin.top, style.margin.right,
                              style.margin.bottom};
    for (const auto& margin : margins) {
      if (!validLength(margin, true, false)) {
        error_ = "layout margins must be finite and non-fractional";
        return false;
      }
    }
    const Length paddings[] = {style.padding.left, style.padding.top, style.padding.right,
                               style.padding.bottom};
    for (const auto& padding : paddings) {
      if (!validLength(padding, false, false)) {
        error_ = "layout padding must be finite, non-negative, and non-fractional";
        return false;
      }
    }
    if (!std::isfinite(style.flex_grow) || !std::isfinite(style.flex_shrink) ||
        style.flex_grow < 0.0f || style.flex_shrink < 0.0f ||
        !std::isfinite(style.border.left) || !std::isfinite(style.border.top) ||
        !std::isfinite(style.border.right) || !std::isfinite(style.border.bottom) ||
        style.border.left < 0.0f || style.border.top < 0.0f ||
        style.border.right < 0.0f || style.border.bottom < 0.0f) {
      error_ = "invalid Flex factor or border width";
      return false;
    }
    if (style.grid_column.span == 0 || style.grid_row.span == 0 ||
        style.grid_column.start > kMaxTracks || style.grid_row.start > kMaxTracks ||
        style.grid_column.span > kMaxTracks || style.grid_row.span > kMaxTracks) {
      error_ = "Grid placement lines and spans must be between 1 and 4096";
      return false;
    }
    const auto validTrackList = [&](const std::vector<GridTrack>& definitions,
                                    bool require_track,
                                    std::string_view property) {
      std::vector<GridTrack> expanded;
      if (!expandTracks(definitions, expanded, error_)) {
        return false;
      }
      if (require_track && expanded.empty()) {
        error_ = std::string(property) + " must contain a track";
        return false;
      }
      const auto validBreadth = [](const Length& breadth, bool minimum) {
        return std::isfinite(breadth.value) && breadth.value >= 0.0f &&
               (!minimum || breadth.unit != LengthUnit::Fraction) &&
               (breadth.unit != LengthUnit::Fraction || breadth.value > 0.0f);
      };
      for (const GridTrack& track : expanded) {
        const bool valid =
            track.kind == GridTrack::Kind::Breadth
                ? validBreadth(track.breadth, false)
                : track.kind == GridTrack::Kind::MinMax &&
                      validBreadth(track.minimum, true) &&
                      validBreadth(track.maximum, false);
        if (!valid) {
          error_ = std::string(property) + " contains an invalid track breadth";
          return false;
        }
      }
      return true;
    };
    if (!validTrackList(style.grid_template_columns, false,
                        "grid-template-columns") ||
        !validTrackList(style.grid_template_rows, false, "grid-template-rows") ||
        !validTrackList(style.grid_auto_columns, true, "grid-auto-columns") ||
        !validTrackList(style.grid_auto_rows, true, "grid-auto-rows")) {
      return false;
    }
    return true;
  }

  void applyYogaStyle(YogaEntry& entry, bool forced_root) {
    auto& style = entry.layout_node->style;
    YGNodeRef node = entry.yoga_node;
    YGNodeStyleSetBoxSizing(node, YGBoxSizingBorderBox);
    YGNodeStyleSetDisplay(node,
                          style.display == Display::None ? YGDisplayNone : YGDisplayFlex);
    YGNodeStyleSetPositionType(
        node, style.position == Position::Absolute ? YGPositionTypeAbsolute
                                                   : YGPositionTypeRelative);
    const FlexDirection direction =
        style.display == Display::Block ? FlexDirection::Column : style.flex_direction;
    YGNodeStyleSetFlexDirection(node, yogaDirection(direction));
    YGNodeStyleSetFlexWrap(node, yogaWrap(style.flex_wrap));
    YGNodeStyleSetFlexGrow(node, style.flex_grow);
    YGNodeStyleSetFlexShrink(node, style.flex_shrink);
    setYogaDimension(node, style.flex_basis, YGNodeStyleSetFlexBasis,
                     YGNodeStyleSetFlexBasisPercent, YGNodeStyleSetFlexBasisAuto);
    YGNodeStyleSetJustifyContent(node, yogaJustify(style.justify_content));
    YGNodeStyleSetAlignContent(node, yogaAlign(style.align_content, false));
    YGNodeStyleSetAlignItems(node, yogaAlign(style.align_items, false));
    YGNodeStyleSetAlignSelf(node, yogaAlign(style.align_self, true));

    setYogaDimension(node, style.width, YGNodeStyleSetWidth,
                     YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto);
    setYogaDimension(node, style.height, YGNodeStyleSetHeight,
                     YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto);
    setYogaMinDimension(node, style.min_width, YGNodeStyleSetMinWidth,
                        YGNodeStyleSetMinWidthPercent);
    setYogaMinDimension(node, style.min_height, YGNodeStyleSetMinHeight,
                        YGNodeStyleSetMinHeightPercent);
    setYogaMinDimension(node, style.max_width, YGNodeStyleSetMaxWidth,
                        YGNodeStyleSetMaxWidthPercent);
    setYogaMinDimension(node, style.max_height, YGNodeStyleSetMaxHeight,
                        YGNodeStyleSetMaxHeightPercent);

    setYogaEdge(node, YGEdgeLeft, style.left, YGNodeStyleSetPosition,
                YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);
    setYogaEdge(node, YGEdgeTop, style.top, YGNodeStyleSetPosition,
                YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);
    setYogaEdge(node, YGEdgeRight, style.right, YGNodeStyleSetPosition,
                YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);
    setYogaEdge(node, YGEdgeBottom, style.bottom, YGNodeStyleSetPosition,
                YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);

    if (!forced_root) {
      setYogaEdge(node, YGEdgeLeft, style.margin.left, YGNodeStyleSetMargin,
                  YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
      setYogaEdge(node, YGEdgeTop, style.margin.top, YGNodeStyleSetMargin,
                  YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
      setYogaEdge(node, YGEdgeRight, style.margin.right, YGNodeStyleSetMargin,
                  YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
      setYogaEdge(node, YGEdgeBottom, style.margin.bottom, YGNodeStyleSetMargin,
                  YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
    }
    setYogaEdge(node, YGEdgeLeft, style.padding.left, YGNodeStyleSetPadding,
                YGNodeStyleSetPaddingPercent, nullptr);
    setYogaEdge(node, YGEdgeTop, style.padding.top, YGNodeStyleSetPadding,
                YGNodeStyleSetPaddingPercent, nullptr);
    setYogaEdge(node, YGEdgeRight, style.padding.right, YGNodeStyleSetPadding,
                YGNodeStyleSetPaddingPercent, nullptr);
    setYogaEdge(node, YGEdgeBottom, style.padding.bottom, YGNodeStyleSetPadding,
                YGNodeStyleSetPaddingPercent, nullptr);
    YGNodeStyleSetBorder(node, YGEdgeLeft, style.border.left);
    YGNodeStyleSetBorder(node, YGEdgeTop, style.border.top);
    YGNodeStyleSetBorder(node, YGEdgeRight, style.border.right);
    YGNodeStyleSetBorder(node, YGEdgeBottom, style.border.bottom);

    if (style.column_gap.unit == LengthUnit::Percent) {
      YGNodeStyleSetGapPercent(node, YGGutterColumn, style.column_gap.value);
    } else {
      YGNodeStyleSetGap(node, YGGutterColumn,
                        resolve(style.column_gap, 0.0f, 0.0f));
    }
    if (style.row_gap.unit == LengthUnit::Percent) {
      YGNodeStyleSetGapPercent(node, YGGutterRow, style.row_gap.value);
    } else {
      YGNodeStyleSetGap(node, YGGutterRow, resolve(style.row_gap, 0.0f, 0.0f));
    }
  }

  YogaEntry* buildYogaNode(LayoutNode& node, YogaTree& tree, bool forced_root) {
    auto entry = std::make_unique<YogaEntry>();
    entry->calculation = this;
    entry->layout_node = &node;
    entry->yoga_node = YGNodeNewWithConfig(config_);
    if (entry->yoga_node == nullptr) {
      error_ = "Yoga node allocation failed";
      return nullptr;
    }
    YogaEntry* result = entry.get();
    tree.entries.push_back(std::move(entry));
    YGNodeSetContext(result->yoga_node, result);
    applyYogaStyle(*result, forced_root);

    result->grid_leaf = node.style.display == Display::Grid;
    const bool measured_leaf = result->grid_leaf ||
                               (node.children.empty() &&
                                (node.measure || node.intrinsic_size.width > 0.0f ||
                                 node.intrinsic_size.height > 0.0f));
    if (measured_leaf && node.style.display != Display::None) {
      YGNodeSetMeasureFunc(result->yoga_node, &Calculation::yogaMeasure);
      return result;
    }

    for (LayoutNode* child : node.children) {
      YogaEntry* child_entry = buildYogaNode(*child, tree, false);
      if (child_entry == nullptr) {
        return nullptr;
      }
      YGNodeInsertChild(result->yoga_node, child_entry->yoga_node,
                        result->children.size());
      result->children.push_back(child_entry);
    }
    return result;
  }

  void copyYogaResults(YogaEntry& entry,
                       float parent_x,
                       float parent_y,
                       std::vector<LayoutNode*>& grid_leaves) {
    const float x = parent_x + YGNodeLayoutGetLeft(entry.yoga_node);
    const float y = parent_y + YGNodeLayoutGetTop(entry.yoga_node);
    entry.layout_node->rect = {
        .x = canonical(x),
        .y = canonical(y),
        .width = nonNegative(YGNodeLayoutGetWidth(entry.yoga_node)),
        .height = nonNegative(YGNodeLayoutGetHeight(entry.yoga_node)),
    };
    if (entry.grid_leaf && entry.layout_node->style.display != Display::None) {
      grid_leaves.push_back(entry.layout_node);
    }
    for (YogaEntry* child : entry.children) {
      copyYogaResults(*child, x, y, grid_leaves);
    }
  }

  bool layoutAssigned(LayoutNode& node, Rect assigned) {
    if (!error_.empty()) {
      return false;
    }
    assigned.x = canonical(assigned.x);
    assigned.y = canonical(assigned.y);
    assigned.width = nonNegative(assigned.width);
    assigned.height = nonNegative(assigned.height);
    if (node.style.display == Display::None) {
      node.rect = {};
      return true;
    }
    if (node.style.display == Display::Grid) {
      node.rect = assigned;
      return layoutGrid(node);
    }

    YogaTree tree;
    YogaEntry* root = buildYogaNode(node, tree, true);
    if (root == nullptr) {
      return false;
    }
    tree.root = root->yoga_node;
    YGNodeStyleSetWidth(tree.root, assigned.width);
    YGNodeStyleSetHeight(tree.root, assigned.height);
    YGNodeCalculateLayout(
        tree.root, assigned.width, assigned.height,
        options_.direction == WritingDirection::RightToLeft ? YGDirectionRTL
                                                            : YGDirectionLTR);
    std::vector<LayoutNode*> grid_leaves;
    copyYogaResults(*root, assigned.x, assigned.y, grid_leaves);
    for (LayoutNode* grid : grid_leaves) {
      if (!layoutGrid(*grid)) {
        return false;
      }
    }
    return error_.empty();
  }

  Size preferredFlowSize(LayoutNode& node, const MeasureConstraints& constraints) {
    const float width_reference = constraints.width_mode == MeasureMode::Undefined
                                      ? 0.0f
                                      : constraints.available_width;
    const float padding_left = resolve(node.style.padding.left, width_reference);
    const float padding_right = resolve(node.style.padding.right, width_reference);
    const float padding_top = resolve(node.style.padding.top, width_reference);
    const float padding_bottom = resolve(node.style.padding.bottom, width_reference);
    const float horizontal_inset = padding_left + padding_right + node.style.border.left +
                                   node.style.border.right;
    const float vertical_inset = padding_top + padding_bottom + node.style.border.top +
                                 node.style.border.bottom;
    const bool row = node.style.display == Display::Flex &&
                     (node.style.flex_direction == FlexDirection::Row ||
                      node.style.flex_direction == FlexDirection::RowReverse);
    const float gap = resolve(row ? node.style.column_gap : node.style.row_gap,
                              row ? constraints.available_width
                                  : constraints.available_height);
    float main = 0.0f;
    float cross = 0.0f;
    std::size_t visible = 0;
    for (LayoutNode* child : node.children) {
      if (child->style.display == Display::None || child->style.position == Position::Absolute) {
        continue;
      }
      const Size child_size = preferredSize(*child, {});
      const float margin_horizontal = resolve(child->style.margin.left, width_reference) +
                                      resolve(child->style.margin.right, width_reference);
      const float margin_vertical = resolve(child->style.margin.top, width_reference) +
                                    resolve(child->style.margin.bottom, width_reference);
      const float child_main = row ? child_size.width + margin_horizontal
                                   : child_size.height + margin_vertical;
      const float child_cross = row ? child_size.height + margin_vertical
                                    : child_size.width + margin_horizontal;
      main += child_main;
      cross = std::max(cross, child_cross);
      ++visible;
    }
    if (visible > 1) {
      main += gap * static_cast<float>(visible - 1);
    }
    return row ? Size{.width = main + horizontal_inset,
                      .height = cross + vertical_inset}
               : Size{.width = cross + horizontal_inset,
                      .height = main + vertical_inset};
  }

  bool ensureGridSize(std::vector<std::vector<std::uint8_t>>& occupancy,
                      std::size_t rows,
                      std::size_t columns) {
    if (rows == 0 || columns == 0 || rows > kMaxTracks || columns > kMaxTracks ||
        rows > kMaxGridCells / columns) {
      error_ = "Grid placement exceeds the supported 4096-track/1M-cell limit";
      return false;
    }
    for (auto& row : occupancy) {
      row.resize(columns, 0);
    }
    occupancy.resize(rows, std::vector<std::uint8_t>(columns, 0));
    return true;
  }

  static bool areaFree(const std::vector<std::vector<std::uint8_t>>& occupancy,
                       std::size_t row,
                       std::size_t column,
                       std::size_t row_span,
                       std::size_t column_span) {
    if (row + row_span > occupancy.size() || occupancy.empty() ||
        column + column_span > occupancy.front().size()) {
      return false;
    }
    for (std::size_t y = row; y < row + row_span; ++y) {
      for (std::size_t x = column; x < column + column_span; ++x) {
        if (occupancy[y][x] != 0) {
          return false;
        }
      }
    }
    return true;
  }

  static void occupy(std::vector<std::vector<std::uint8_t>>& occupancy,
                     const PlacedItem& item) {
    for (std::size_t y = item.row; y < item.row + item.row_span; ++y) {
      for (std::size_t x = item.column; x < item.column + item.column_span; ++x) {
        occupancy[y][x] = 1;
      }
    }
  }

  bool buildGridModel(LayoutNode& node, GridModel& model) {
    if (!expandTracks(node.style.grid_template_columns, model.columns, error_) ||
        !expandTracks(node.style.grid_template_rows, model.rows, error_)) {
      return false;
    }
    std::vector<GridTrack> auto_columns;
    std::vector<GridTrack> auto_rows;
    if (!expandTracks(node.style.grid_auto_columns, auto_columns, error_) ||
        !expandTracks(node.style.grid_auto_rows, auto_rows, error_)) {
      return false;
    }
    if (auto_columns.empty()) {
      auto_columns.push_back(GridTrack::automatic());
    }
    if (auto_rows.empty()) {
      auto_rows.push_back(GridTrack::automatic());
    }

    const std::size_t explicit_columns = model.columns.size();
    const std::size_t explicit_rows = model.rows.size();
    std::size_t column_count = std::max<std::size_t>(1, explicit_columns);
    std::size_t row_count = std::max<std::size_t>(1, explicit_rows);
    for (LayoutNode* child : node.children) {
      if (child->style.display == Display::None) {
        continue;
      }
      PlacedItem item{
          .node = child,
          .column_span = std::max<std::size_t>(1, child->style.grid_column.span),
          .row_span = std::max<std::size_t>(1, child->style.grid_row.span),
      };
      if (child->style.grid_column.start > 0) {
        item.column = child->style.grid_column.start - 1;
        column_count = std::max(column_count, item.column + item.column_span);
      }
      if (child->style.grid_row.start > 0) {
        item.row = child->style.grid_row.start - 1;
        row_count = std::max(row_count, item.row + item.row_span);
      }
      column_count = std::max(column_count, item.column_span);
      row_count = std::max(row_count, item.row_span);
      model.items.push_back(item);
    }

    std::vector<std::vector<std::uint8_t>> occupancy;
    if (!ensureGridSize(occupancy, row_count, column_count)) {
      return false;
    }

    // Definite items reserve cells before automatic placement, independent of
    // source order. Explicit overlap remains legal.
    for (auto& item : model.items) {
      const bool definite_column = item.node->style.grid_column.start > 0;
      const bool definite_row = item.node->style.grid_row.start > 0;
      if (definite_column && definite_row) {
        item.placed = true;
        occupy(occupancy, item);
      }
    }

    for (auto& item : model.items) {
      if (item.placed) {
        continue;
      }
      const bool definite_column = item.node->style.grid_column.start > 0;
      const bool definite_row = item.node->style.grid_row.start > 0;
      if (definite_row) {
        std::size_t column = 0;
        while (true) {
          if (column + item.column_span > occupancy.front().size() &&
              !ensureGridSize(occupancy, occupancy.size(),
                              column + item.column_span)) {
            return false;
          }
          if (areaFree(occupancy, item.row, column, item.row_span,
                       item.column_span)) {
            item.column = column;
            break;
          }
          ++column;
        }
        item.placed = true;
        occupy(occupancy, item);
      } else if (definite_column) {
        std::size_t row = 0;
        while (true) {
          if (row + item.row_span > occupancy.size() &&
              !ensureGridSize(occupancy, row + item.row_span,
                              occupancy.front().size())) {
            return false;
          }
          if (areaFree(occupancy, row, item.column, item.row_span,
                       item.column_span)) {
            item.row = row;
            break;
          }
          ++row;
        }
        item.placed = true;
        occupy(occupancy, item);
      }
    }

    std::size_t cursor_row = 0;
    std::size_t cursor_column = 0;
    for (auto& item : model.items) {
      if (item.placed) {
        continue;
      }
      if (node.style.grid_auto_flow == GridAutoFlow::Row) {
        if (item.column_span > occupancy.front().size() &&
            !ensureGridSize(occupancy, occupancy.size(), item.column_span)) {
          return false;
        }
        while (true) {
          if (cursor_column + item.column_span > occupancy.front().size()) {
            ++cursor_row;
            cursor_column = 0;
          }
          if (cursor_row + item.row_span > occupancy.size() &&
              !ensureGridSize(occupancy, cursor_row + item.row_span,
                              occupancy.front().size())) {
            return false;
          }
          if (areaFree(occupancy, cursor_row, cursor_column, item.row_span,
                       item.column_span)) {
            item.row = cursor_row;
            item.column = cursor_column;
            break;
          }
          ++cursor_column;
        }
        cursor_column += item.column_span;
        if (cursor_column >= occupancy.front().size()) {
          ++cursor_row;
          cursor_column = 0;
        }
      } else {
        if (item.row_span > occupancy.size() &&
            !ensureGridSize(occupancy, item.row_span, occupancy.front().size())) {
          return false;
        }
        while (true) {
          if (cursor_row + item.row_span > occupancy.size()) {
            ++cursor_column;
            cursor_row = 0;
          }
          if (cursor_column + item.column_span > occupancy.front().size() &&
              !ensureGridSize(occupancy, occupancy.size(),
                              cursor_column + item.column_span)) {
            return false;
          }
          if (areaFree(occupancy, cursor_row, cursor_column, item.row_span,
                       item.column_span)) {
            item.row = cursor_row;
            item.column = cursor_column;
            break;
          }
          ++cursor_row;
        }
        cursor_row += item.row_span;
        if (cursor_row >= occupancy.size()) {
          ++cursor_column;
          cursor_row = 0;
        }
      }
      item.placed = true;
      occupy(occupancy, item);
    }

    while (model.columns.size() < occupancy.front().size()) {
      model.columns.push_back(
          implicitTrack(auto_columns, model.columns.size() - explicit_columns));
    }
    while (model.rows.size() < occupancy.size()) {
      model.rows.push_back(implicitTrack(auto_rows, model.rows.size() - explicit_rows));
    }
    return true;
  }

  TrackState initialTrackState(const GridTrack& track,
                               float available,
                               bool available_known) {
    const auto definite = [&](const Length& breadth, bool& known) {
      switch (breadth.unit) {
        case LengthUnit::Pixels:
          known = true;
          return breadth.value;
        case LengthUnit::Percent:
          known = available_known;
          return available_known ? available * breadth.value * 0.01f : 0.0f;
        case LengthUnit::Auto:
        case LengthUnit::Fraction:
          known = false;
          return 0.0f;
      }
      known = false;
      return 0.0f;
    };

    TrackState state;
    if (track.kind == GridTrack::Kind::Breadth) {
      bool known = false;
      state.size = nonNegative(definite(track.breadth, known));
      if (track.breadth.unit == LengthUnit::Fraction) {
        state.flex = track.breadth.value;
        state.accepts_intrinsic = true;
      } else if (track.breadth.unit == LengthUnit::Auto || !known) {
        state.auto_track = true;
        state.accepts_intrinsic = true;
      } else {
        state.has_cap = true;
        state.cap = state.size;
      }
      return state;
    }

    if (track.kind == GridTrack::Kind::MinMax) {
      bool minimum_known = false;
      state.size = nonNegative(definite(track.minimum, minimum_known));
      state.accepts_intrinsic = !minimum_known || track.minimum.unit == LengthUnit::Auto;
      bool maximum_known = false;
      const float maximum = nonNegative(definite(track.maximum, maximum_known));
      if (track.maximum.unit == LengthUnit::Fraction) {
        state.flex = track.maximum.value;
      } else if (track.maximum.unit == LengthUnit::Auto || !maximum_known) {
        state.auto_track = true;
        state.accepts_intrinsic = true;
      } else {
        state.has_cap = true;
        state.cap = std::max(state.size, maximum);
      }
      return state;
    }

    state.auto_track = true;
    state.accepts_intrinsic = true;
    return state;
  }

  AxisLayout sizeAxis(const GridModel& model,
                      bool columns,
                      float available,
                      bool available_known,
                      float gap,
                      Alignment content_alignment,
                      const AxisLayout* column_constraints = nullptr) {
    const auto& definitions = columns ? model.columns : model.rows;
    AxisLayout result;
    result.gap = gap;
    result.tracks.reserve(definitions.size());
    for (const auto& definition : definitions) {
      result.tracks.push_back(initialTrackState(definition, available, available_known));
    }

    for (const auto& item : model.items) {
      const std::size_t start = columns ? item.column : item.row;
      const std::size_t span = columns ? item.column_span : item.row_span;
      MeasureConstraints constraints;
      if (!columns && column_constraints != nullptr &&
          item.column + item.column_span <= column_constraints->tracks.size()) {
        const std::size_t last_column = item.column + item.column_span - 1;
        const float cell_width =
            column_constraints->positions[last_column] +
            column_constraints->tracks[last_column].size -
            column_constraints->positions[item.column];
        constraints.available_width = nonNegative(
            cell_width - resolve(item.node->style.margin.left, cell_width) -
            resolve(item.node->style.margin.right, cell_width));
        constraints.width_mode = MeasureMode::AtMost;
      }
      const Size preferred = preferredSize(*item.node, constraints);
      const float margin = columns
                               ? resolve(item.node->style.margin.left, available) +
                                     resolve(item.node->style.margin.right, available)
                               : resolve(item.node->style.margin.top, available) +
                                     resolve(item.node->style.margin.bottom, available);
      const float contribution =
          nonNegative((columns ? preferred.width : preferred.height) + margin);
      if (start >= result.tracks.size() || start + span > result.tracks.size()) {
        continue;
      }
      float current = gap * static_cast<float>(span > 0 ? span - 1 : 0);
      for (std::size_t index = start; index < start + span; ++index) {
        current += result.tracks[index].size;
      }
      float deficit = contribution - current;
      if (deficit <= kEpsilon) {
        continue;
      }
      std::vector<std::size_t> candidates;
      for (std::size_t index = start; index < start + span; ++index) {
        if (result.tracks[index].accepts_intrinsic || result.tracks[index].flex > 0.0f) {
          candidates.push_back(index);
        }
      }
      if (candidates.empty()) {
        continue;
      }
      while (deficit > kEpsilon && !candidates.empty()) {
        const float share = deficit / static_cast<float>(candidates.size());
        float consumed = 0.0f;
        std::vector<std::size_t> remaining;
        for (std::size_t index : candidates) {
          auto& track = result.tracks[index];
          const float capacity = track.has_cap
                                     ? std::max(0.0f, track.cap - track.size)
                                     : std::numeric_limits<float>::infinity();
          const float addition = std::min(share, capacity);
          track.size += addition;
          consumed += addition;
          if (!track.has_cap || capacity - addition > kEpsilon) {
            remaining.push_back(index);
          }
        }
        if (consumed <= kEpsilon) {
          break;
        }
        deficit -= consumed;
        candidates = std::move(remaining);
      }
    }

    const float total_gap =
        gap * static_cast<float>(result.tracks.empty() ? 0 : result.tracks.size() - 1);
    float base_total = total_gap;
    for (const auto& track : result.tracks) {
      base_total += track.size;
    }

    float free = available_known ? std::max(0.0f, available - base_total) : 0.0f;
    std::vector<std::size_t> capped;
    for (std::size_t index = 0; index < result.tracks.size(); ++index) {
      const auto& track = result.tracks[index];
      if (track.has_cap && track.cap > track.size + kEpsilon) {
        capped.push_back(index);
      }
    }
    while (free > kEpsilon && !capped.empty()) {
      const float share = free / static_cast<float>(capped.size());
      float consumed = 0.0f;
      std::vector<std::size_t> remaining;
      for (std::size_t index : capped) {
        auto& track = result.tracks[index];
        const float addition = std::min(share, track.cap - track.size);
        track.size += addition;
        consumed += addition;
        if (track.cap - track.size > kEpsilon) {
          remaining.push_back(index);
        }
      }
      if (consumed <= kEpsilon) {
        break;
      }
      free -= consumed;
      capped = std::move(remaining);
    }

    std::vector<std::size_t> flexible;
    for (std::size_t index = 0; index < result.tracks.size(); ++index) {
      if (result.tracks[index].flex > 0.0f) {
        flexible.push_back(index);
      }
    }
    if (available_known && !flexible.empty()) {
      float non_flexible = total_gap;
      for (std::size_t index = 0; index < result.tracks.size(); ++index) {
        if (result.tracks[index].flex <= 0.0f) {
          non_flexible += result.tracks[index].size;
        }
      }
      std::vector<std::size_t> unresolved = flexible;
      float frozen = 0.0f;
      while (!unresolved.empty()) {
        const float weight = std::accumulate(
            unresolved.begin(), unresolved.end(), 0.0f,
            [&](float value, std::size_t index) {
              return value + result.tracks[index].flex;
            });
        const float unit = weight > 0.0f
                               ? std::max(0.0f, available - non_flexible - frozen) / weight
                               : 0.0f;
        bool froze_track = false;
        std::vector<std::size_t> next;
        for (std::size_t index : unresolved) {
          auto& track = result.tracks[index];
          const float allocation = unit * track.flex;
          if (track.size > allocation + kEpsilon) {
            frozen += track.size;
            froze_track = true;
          } else {
            next.push_back(index);
          }
        }
        if (!froze_track) {
          for (std::size_t index : unresolved) {
            auto& track = result.tracks[index];
            track.size = std::max(track.size, unit * track.flex);
          }
          break;
        }
        unresolved = std::move(next);
      }
    }

    float used = total_gap;
    for (const auto& track : result.tracks) {
      used += track.size;
    }
    float leftover = available_known ? std::max(0.0f, available - used) : 0.0f;
    if (content_alignment == Alignment::Stretch && leftover > kEpsilon) {
      std::vector<std::size_t> automatic;
      for (std::size_t index = 0; index < result.tracks.size(); ++index) {
        if (result.tracks[index].auto_track && result.tracks[index].flex <= 0.0f) {
          automatic.push_back(index);
        }
      }
      if (!automatic.empty()) {
        const float addition = leftover / static_cast<float>(automatic.size());
        for (std::size_t index : automatic) {
          result.tracks[index].size += addition;
        }
        used += leftover;
        leftover = 0.0f;
      }
    }

    float leading = 0.0f;
    float extra_gap = 0.0f;
    switch (content_alignment) {
      case Alignment::Center: leading = leftover * 0.5f; break;
      case Alignment::End: leading = leftover; break;
      case Alignment::SpaceBetween:
        if (result.tracks.size() > 1) {
          extra_gap = leftover / static_cast<float>(result.tracks.size() - 1);
        }
        break;
      case Alignment::SpaceAround:
        if (!result.tracks.empty()) {
          extra_gap = leftover / static_cast<float>(result.tracks.size());
          leading = extra_gap * 0.5f;
        }
        break;
      case Alignment::SpaceEvenly:
        extra_gap = leftover / static_cast<float>(result.tracks.size() + 1);
        leading = extra_gap;
        break;
      case Alignment::Auto:
      case Alignment::Start:
      case Alignment::Stretch: break;
    }

    result.positions.resize(result.tracks.size());
    float cursor = leading;
    for (std::size_t index = 0; index < result.tracks.size(); ++index) {
      result.positions[index] = cursor;
      cursor += result.tracks[index].size;
      if (index + 1 < result.tracks.size()) {
        cursor += gap + extra_gap;
      }
    }
    result.extent = cursor;
    return result;
  }

  Size preferredGridSize(LayoutNode& node, const MeasureConstraints& constraints) {
    GridModel model;
    if (!buildGridModel(node, model)) {
      return {};
    }
    const float reference_width = constraints.width_mode == MeasureMode::Undefined
                                      ? 0.0f
                                      : constraints.available_width;
    const float reference_height = constraints.height_mode == MeasureMode::Undefined
                                       ? 0.0f
                                       : constraints.available_height;
    const float column_gap = resolve(node.style.column_gap, reference_width);
    const float row_gap = resolve(node.style.row_gap, reference_height);
    const AxisLayout columns = sizeAxis(model, true, 0.0f, false, column_gap,
                                        Alignment::Start);
    const AxisLayout rows = sizeAxis(model, false, 0.0f, false, row_gap,
                                     Alignment::Start, &columns);
    const float padding_left = resolve(node.style.padding.left, reference_width);
    const float padding_right = resolve(node.style.padding.right, reference_width);
    const float padding_top = resolve(node.style.padding.top, reference_width);
    const float padding_bottom = resolve(node.style.padding.bottom, reference_width);
    return {
        .width = columns.extent + padding_left + padding_right + node.style.border.left +
                 node.style.border.right,
        .height = rows.extent + padding_top + padding_bottom + node.style.border.top +
                  node.style.border.bottom,
    };
  }

  bool layoutGrid(LayoutNode& node) {
    GridModel model;
    if (!buildGridModel(node, model)) {
      return false;
    }
    const float reference_width = node.rect.width;
    const float padding_left = resolve(node.style.padding.left, reference_width);
    const float padding_right = resolve(node.style.padding.right, reference_width);
    const float padding_top = resolve(node.style.padding.top, reference_width);
    const float padding_bottom = resolve(node.style.padding.bottom, reference_width);
    const Rect content{
        .x = node.rect.x + node.style.border.left + padding_left,
        .y = node.rect.y + node.style.border.top + padding_top,
        .width = nonNegative(node.rect.width - node.style.border.left -
                             node.style.border.right - padding_left - padding_right),
        .height = nonNegative(node.rect.height - node.style.border.top -
                              node.style.border.bottom - padding_top - padding_bottom),
    };
    const float column_gap = resolve(node.style.column_gap, content.width);
    const float row_gap = resolve(node.style.row_gap, content.height);
    const AxisLayout columns = sizeAxis(model, true, content.width, true, column_gap,
                                        node.style.justify_content);
    const AxisLayout rows = sizeAxis(model, false, content.height, true, row_gap,
                                     node.style.align_content, &columns);

    for (const auto& item : model.items) {
      if (item.column + item.column_span > columns.tracks.size() ||
          item.row + item.row_span > rows.tracks.size()) {
        error_ = "internal Grid placement is outside its sized tracks";
        return false;
      }
      const std::size_t last_column = item.column + item.column_span - 1;
      const std::size_t last_row = item.row + item.row_span - 1;
      const float normal_x = columns.positions[item.column];
      const float normal_right =
          columns.positions[last_column] + columns.tracks[last_column].size;
      const float cell_x = options_.direction == WritingDirection::RightToLeft
                               ? content.x + content.width - normal_right
                               : content.x + normal_x;
      const float cell_y = content.y + rows.positions[item.row];
      const float cell_width = nonNegative(normal_right - normal_x);
      const float cell_height = nonNegative(
          rows.positions[last_row] + rows.tracks[last_row].size -
          rows.positions[item.row]);

      const float margin_left = resolve(item.node->style.margin.left, content.width);
      const float margin_right = resolve(item.node->style.margin.right, content.width);
      const float margin_top = resolve(item.node->style.margin.top, content.width);
      const float margin_bottom = resolve(item.node->style.margin.bottom, content.width);
      const float available_width =
          nonNegative(cell_width - margin_left - margin_right);
      const float available_height =
          nonNegative(cell_height - margin_top - margin_bottom);
      const Size preferred = preferredSize(
          *item.node,
          {.available_width = available_width,
           .available_height = available_height,
           .width_mode = MeasureMode::AtMost,
           .height_mode = MeasureMode::AtMost});
      const Alignment horizontal =
          resolvedAlignment(item.node->style.justify_self, node.style.justify_items);
      const Alignment vertical =
          resolvedAlignment(item.node->style.align_self, node.style.align_items);

      float width = item.node->style.width.unit == LengthUnit::Auto
                        ? (horizontal == Alignment::Stretch ? available_width
                                                           : preferred.width)
                        : resolve(item.node->style.width, available_width,
                                  preferred.width);
      float height = item.node->style.height.unit == LengthUnit::Auto
                         ? (vertical == Alignment::Stretch ? available_height
                                                          : preferred.height)
                         : resolve(item.node->style.height, available_height,
                                   preferred.height);
      width = clampDimension(width, item.node->style.min_width,
                             item.node->style.max_width, available_width);
      height = clampDimension(height, item.node->style.min_height,
                              item.node->style.max_height, available_height);

      float x = cell_x + margin_left;
      float y = cell_y + margin_top;
      if (horizontal == Alignment::Center) {
        x += (available_width - width) * 0.5f;
      } else if (horizontal == Alignment::End) {
        x += available_width - width;
      }
      if (vertical == Alignment::Center) {
        y += (available_height - height) * 0.5f;
      } else if (vertical == Alignment::End) {
        y += available_height - height;
      }
      if (!layoutAssigned(*item.node, {.x = x, .y = y, .width = width, .height = height})) {
        return false;
      }
    }
    return error_.empty();
  }

  LayoutOptions options_;
  YGConfigRef config_ = nullptr;
  std::string error_;
};

}  // namespace

std::optional<Length> parseLength(std::string_view source) {
  const std::string normalized = lower(trimView(source));
  if (normalized.empty()) {
    return std::nullopt;
  }
  if (normalized == "auto") {
    return Length::automatic();
  }

  struct Suffix {
    std::string_view text;
    LengthUnit unit;
  };
  constexpr Suffix suffixes[] = {
      {"px", LengthUnit::Pixels},
      {"%", LengthUnit::Percent},
      {"fr", LengthUnit::Fraction},
  };
  for (const auto& suffix : suffixes) {
    if (normalized.size() > suffix.text.size() &&
        normalized.substr(normalized.size() - suffix.text.size()) == suffix.text) {
      float value = 0.0f;
      if (!parseFloat(std::string_view(normalized).substr(
                          0, normalized.size() - suffix.text.size()),
                      value)) {
        return std::nullopt;
      }
      return Length{.value = value, .unit = suffix.unit};
    }
  }
  float value = 0.0f;
  if (!parseFloat(normalized, value)) {
    return std::nullopt;
  }
  return Length::pixels(value);
}

GridTrack GridTrack::pixels(float value) {
  GridTrack result;
  result.breadth = Length::pixels(value);
  return result;
}

GridTrack GridTrack::percent(float value) {
  GridTrack result;
  result.breadth = Length::percent(value);
  return result;
}

GridTrack GridTrack::fraction(float value) {
  GridTrack result;
  result.breadth = Length::fraction(value);
  return result;
}

GridTrack GridTrack::automatic() {
  return {};
}

GridTrack GridTrack::minmax(Length minimum_value, Length maximum_value) {
  GridTrack result;
  result.kind = Kind::MinMax;
  result.minimum = minimum_value;
  result.maximum = maximum_value;
  return result;
}

GridTrack GridTrack::repeat(std::size_t count, std::vector<GridTrack> tracks) {
  GridTrack result;
  result.kind = Kind::Repeat;
  result.repeat_count = count;
  result.repeated_tracks = std::move(tracks);
  return result;
}

bool parseTrackList(std::string_view source,
                    std::vector<GridTrack>& output,
                    std::string* error) {
  output.clear();
  if (error != nullptr) {
    error->clear();
  }
  std::vector<std::string_view> tokens;
  if (!splitTopLevel(trimView(source), '\0', tokens, true, error)) {
    return false;
  }
  if (tokens.empty()) {
    if (error != nullptr) {
      *error = "track list is empty";
    }
    return false;
  }
  for (const auto token : tokens) {
    GridTrack track;
    if (!parseTrackToken(token, track, error)) {
      output.clear();
      return false;
    }
    output.push_back(std::move(track));
  }
  return true;
}

LayoutResult LayoutEngine::calculate(LayoutNode& root,
                                     const LayoutOptions& options) const {
  if (!std::isfinite(options.available_size.width) ||
      !std::isfinite(options.available_size.height) ||
      options.available_size.width < 0.0f || options.available_size.height < 0.0f) {
    return {.success = false,
            .error = "available layout size must be finite and non-negative"};
  }
  Calculation calculation(options);
  if (!calculation.ready() || !calculation.validateAndReset(root) ||
      !calculation.layoutRoot(root)) {
    return {.success = false, .error = calculation.error()};
  }
  return {.success = true, .error = {}};
}

}  // namespace karma::ui::layout
