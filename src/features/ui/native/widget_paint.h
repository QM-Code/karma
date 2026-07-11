#pragma once

#include "features/ui/native/paint_engine.h"

#include <cstddef>
#include <vector>

namespace karma::ui::widget_paint {

enum class Orientation {
  Horizontal,
  Vertical,
};

enum class LayoutDirection {
  LeftToRight,
  RightToLeft,
};

struct ToggleStyle {
  math::Color track_color{0.12f, 0.15f, 0.20f, 1.0f};
  math::Color checked_track_color{0.24f, 0.56f, 1.0f, 1.0f};
  math::Color checkmark_color{1.0f, 1.0f, 1.0f, 1.0f};
  paint::CornerRadii track_radii{5.0f, 5.0f, 5.0f, 5.0f};
  float track_inset = 0.0f;
  float checkmark_inset = 5.0f;
  float checkmark_thickness = 2.0f;
  std::size_t corner_segments = 6u;
};

struct TogglePaintRequest {
  paint::Rect bounds;
  bool checked = false;
  ToggleStyle style;
};

struct TogglePaintResult {
  paint::Rect track_bounds;
  paint::Mesh track;
  paint::Mesh checkmark;
};

[[nodiscard]] TogglePaintResult toggle(const TogglePaintRequest& request);

struct SliderStyle {
  math::Color track_color{0.12f, 0.15f, 0.20f, 1.0f};
  math::Color fill_color{0.24f, 0.56f, 1.0f, 1.0f};
  math::Color thumb_color{0.94f, 0.96f, 1.0f, 1.0f};
  paint::CornerRadii track_radii{2.0f, 2.0f, 2.0f, 2.0f};
  paint::CornerRadii fill_radii{2.0f, 2.0f, 2.0f, 2.0f};
  paint::CornerRadii thumb_radii{8.0f, 8.0f, 8.0f, 8.0f};
  float edge_inset = 0.0f;
  float track_thickness = 4.0f;
  float thumb_size = 16.0f;
  std::size_t corner_segments = 6u;
};

struct SliderPaintRequest {
  paint::Rect bounds;
  float fraction = 0.0f;
  Orientation orientation = Orientation::Horizontal;
  LayoutDirection direction = LayoutDirection::LeftToRight;
  SliderStyle style;
};

struct SliderPaintResult {
  paint::Rect track_bounds;
  paint::Rect fill_bounds;
  paint::Rect thumb_bounds;
  paint::Mesh track;
  paint::Mesh fill;
  paint::Mesh thumb;
};

/// Horizontal values grow from the leading edge; RTL reverses that edge.
/// Vertical values grow bottom-to-top and ignore layout direction.
[[nodiscard]] SliderPaintResult slider(const SliderPaintRequest& request);

struct ProgressStyle {
  math::Color track_color{0.12f, 0.15f, 0.20f, 1.0f};
  math::Color fill_color{0.24f, 0.56f, 1.0f, 1.0f};
  paint::CornerRadii track_radii{3.0f, 3.0f, 3.0f, 3.0f};
  paint::CornerRadii fill_radii{3.0f, 3.0f, 3.0f, 3.0f};
  float inset = 0.0f;
  std::size_t corner_segments = 6u;
};

struct ProgressPaintRequest {
  paint::Rect bounds;
  float fraction = 0.0f;
  Orientation orientation = Orientation::Horizontal;
  LayoutDirection direction = LayoutDirection::LeftToRight;
  ProgressStyle style;
};

struct ProgressPaintResult {
  paint::Rect track_bounds;
  paint::Rect fill_bounds;
  paint::Mesh track;
  paint::Mesh fill;
};

[[nodiscard]] ProgressPaintResult progress(const ProgressPaintRequest& request);

struct ChevronStyle {
  math::Color color{0.94f, 0.96f, 1.0f, 1.0f};
  float size = 12.0f;
  float thickness = 2.0f;
  float edge_inset = 8.0f;
};

struct SelectArrowPaintRequest {
  paint::Rect bounds;
  bool expanded = false;
  LayoutDirection direction = LayoutDirection::LeftToRight;
  ChevronStyle style;
};

struct ChevronPaintResult {
  paint::Rect glyph_bounds;
  paint::Mesh glyph;
};

/// Places a down/up chevron at the trailing edge of a select control.
[[nodiscard]] ChevronPaintResult selectArrow(
    const SelectArrowPaintRequest& request);

struct DisclosureChevronPaintRequest {
  paint::Rect bounds;
  bool expanded = false;
  LayoutDirection direction = LayoutDirection::LeftToRight;
  ChevronStyle style;
};

/// Places an expanded-down or collapsed-leading chevron at the leading edge.
[[nodiscard]] ChevronPaintResult disclosureChevron(
    const DisclosureChevronPaintRequest& request);

struct SplitterGripStyle {
  math::Color color{0.48f, 0.54f, 0.64f, 1.0f};
  paint::CornerRadii mark_radii{1.0f, 1.0f, 1.0f, 1.0f};
  float mark_length = 12.0f;
  float mark_thickness = 2.0f;
  float gap = 3.0f;
  std::size_t mark_count = 3u;
  std::size_t corner_segments = 4u;
};

struct SplitterGripPaintRequest {
  paint::Rect bounds;
  Orientation orientation = Orientation::Vertical;
  SplitterGripStyle style;
};

struct SplitterGripPaintResult {
  std::vector<paint::Rect> mark_bounds;
  paint::Mesh grip;
};

/// A vertical splitter receives stacked horizontal marks; a horizontal
/// splitter receives side-by-side vertical marks.
[[nodiscard]] SplitterGripPaintResult splitterGrip(
    const SplitterGripPaintRequest& request);

}  // namespace karma::ui::widget_paint
