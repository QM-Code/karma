#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::layout {

/// Logical-coordinate size used by the retained layout tree.
struct Size {
  float width = 0.0f;
  float height = 0.0f;

  friend constexpr bool operator==(Size, Size) = default;
};

/// A deterministic border-box result in logical coordinates.
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  friend constexpr bool operator==(Rect, Rect) = default;
};

enum class LengthUnit {
  Auto,
  Pixels,
  Percent,
  Fraction,
};

struct Length {
  float value = 0.0f;
  LengthUnit unit = LengthUnit::Auto;

  [[nodiscard]] static constexpr Length automatic() { return {}; }
  [[nodiscard]] static constexpr Length pixels(float value) {
    return {.value = value, .unit = LengthUnit::Pixels};
  }
  [[nodiscard]] static constexpr Length percent(float value) {
    return {.value = value, .unit = LengthUnit::Percent};
  }
  [[nodiscard]] static constexpr Length fraction(float value = 1.0f) {
    return {.value = value, .unit = LengthUnit::Fraction};
  }

  friend constexpr bool operator==(Length, Length) = default;
};

/// Parses `auto`, unitless/px, percent, and fr lengths.
[[nodiscard]] std::optional<Length> parseLength(std::string_view source);

struct Edges {
  Length left = Length::pixels(0.0f);
  Length top = Length::pixels(0.0f);
  Length right = Length::pixels(0.0f);
  Length bottom = Length::pixels(0.0f);
};

struct BorderEdges {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

/// A single explicit or implicit Grid track declaration.
struct GridTrack {
  enum class Kind {
    Breadth,
    MinMax,
    Repeat,
  };

  Kind kind = Kind::Breadth;
  Length breadth = Length::automatic();
  Length minimum = Length::automatic();
  Length maximum = Length::automatic();
  std::size_t repeat_count = 0;
  std::vector<GridTrack> repeated_tracks;

  [[nodiscard]] static GridTrack pixels(float value);
  [[nodiscard]] static GridTrack percent(float value);
  [[nodiscard]] static GridTrack fraction(float value = 1.0f);
  [[nodiscard]] static GridTrack automatic();
  [[nodiscard]] static GridTrack minmax(Length minimum, Length maximum);
  [[nodiscard]] static GridTrack repeat(std::size_t count,
                                        std::vector<GridTrack> tracks);
};

/// Parses the practical KSS Grid subset, including fixed repeat() and minmax().
[[nodiscard]] bool parseTrackList(std::string_view source,
                                  std::vector<GridTrack>& output,
                                  std::string* error = nullptr);

enum class Display {
  None,
  Block,
  Flex,
  Grid,
};

enum class Position {
  Relative,
  Absolute,
};

enum class FlexDirection {
  Row,
  RowReverse,
  Column,
  ColumnReverse,
};

enum class FlexWrap {
  NoWrap,
  Wrap,
  WrapReverse,
};

enum class Alignment {
  Auto,
  Start,
  Center,
  End,
  Stretch,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
};

enum class GridAutoFlow {
  Row,
  Column,
};

enum class WritingDirection {
  LeftToRight,
  RightToLeft,
};

struct GridPlacement {
  /// One-based track line. Zero requests automatic placement.
  std::size_t start = 0;
  std::size_t span = 1;
};

/// Computed KSS values consumed by the layout engine.
struct LayoutStyle {
  Display display = Display::Block;
  Position position = Position::Relative;

  Length width = Length::automatic();
  Length height = Length::automatic();
  Length min_width = Length::automatic();
  Length min_height = Length::automatic();
  Length max_width = Length::automatic();
  Length max_height = Length::automatic();
  Length left = Length::automatic();
  Length top = Length::automatic();
  Length right = Length::automatic();
  Length bottom = Length::automatic();
  Edges margin;
  Edges padding;
  BorderEdges border;

  FlexDirection flex_direction = FlexDirection::Row;
  FlexWrap flex_wrap = FlexWrap::NoWrap;
  float flex_grow = 0.0f;
  float flex_shrink = 1.0f;
  Length flex_basis = Length::automatic();
  Alignment justify_content = Alignment::Start;
  Alignment align_content = Alignment::Stretch;
  Alignment align_items = Alignment::Stretch;
  Alignment align_self = Alignment::Auto;
  Length column_gap = Length::pixels(0.0f);
  Length row_gap = Length::pixels(0.0f);

  std::vector<GridTrack> grid_template_columns;
  std::vector<GridTrack> grid_template_rows;
  std::vector<GridTrack> grid_auto_columns{GridTrack::automatic()};
  std::vector<GridTrack> grid_auto_rows{GridTrack::automatic()};
  GridAutoFlow grid_auto_flow = GridAutoFlow::Row;
  GridPlacement grid_column;
  GridPlacement grid_row;
  Alignment justify_items = Alignment::Stretch;
  Alignment justify_self = Alignment::Auto;
};

enum class MeasureMode {
  Undefined,
  Exactly,
  AtMost,
};

struct MeasureConstraints {
  float available_width = 0.0f;
  float available_height = 0.0f;
  MeasureMode width_mode = MeasureMode::Undefined;
  MeasureMode height_mode = MeasureMode::Undefined;
};

/// Measures intrinsic content-box size. Constraints are content-box limits;
/// padding and border are accounted for by LayoutEngine.
using MeasureCallback = std::function<Size(const MeasureConstraints&)>;

/// Retained layout proxy. Children are non-owning and must remain stable for a
/// calculate() call. user_data lets a DOM adapter retain its source-node link.
struct LayoutNode {
  LayoutStyle style;
  /// Intrinsic content-box size used when no measure callback is installed.
  Size intrinsic_size;
  MeasureCallback measure;
  std::vector<LayoutNode*> children;
  Rect rect;
  void* user_data = nullptr;

  void appendChild(LayoutNode& child) { children.push_back(&child); }
};

struct LayoutOptions {
  Size available_size;
  WritingDirection direction = WritingDirection::LeftToRight;
};

struct LayoutResult {
  bool success = false;
  std::string error;

  explicit operator bool() const { return success; }
};

/// Yoga-backed Flex/Block plus Karma Grid. The engine owns no LayoutNodes.
class LayoutEngine {
 public:
  [[nodiscard]] LayoutResult calculate(LayoutNode& root,
                                       const LayoutOptions& options) const;
};

}  // namespace karma::ui::layout
