#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

namespace karma::ui::native {

struct CanvasPoint {
  float x = 0.0f;
  float y = 0.0f;

  friend bool operator==(const CanvasPoint&, const CanvasPoint&) = default;
};

struct CanvasRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  friend bool operator==(const CanvasRect&, const CanvasRect&) = default;
};

struct SafeAreaInsets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  friend bool operator==(const SafeAreaInsets&, const SafeAreaInsets&) = default;
};

enum class CanvasScaleMode {
  Logical,
  Fit,
  Fill,
  Stretch,
  PixelPerfect,
};

/// Immutable authoring contract parsed from a document's top-level `canvas`.
struct CanvasSpec {
  CanvasScaleMode scale_mode = CanvasScaleMode::Logical;
  float reference_width = 0.0f;
  float reference_height = 0.0f;
  bool use_platform_safe_area = false;
};

/// Resolved canvas for one window frame.
///
/// DOM geometry is stored in `layout_rect` coordinates. Multiplication by
/// scale_x/scale_y maps it directly to window-logical coordinates. Keeping the
/// origin in the layout rect (instead of a separate paint translation) makes
/// layout, paint, hit testing, and clipping share one deterministic transform.
struct CanvasLayout {
  CanvasScaleMode scale_mode = CanvasScaleMode::Logical;
  CanvasRect safe_window_rect{};
  CanvasRect layout_rect{};
  CanvasRect layout_clip{};
  float scale_x = 1.0f;
  float scale_y = 1.0f;

  [[nodiscard]] CanvasPoint windowToLayout(float x, float y) const;
  [[nodiscard]] CanvasPoint layoutToWindow(float x, float y) const;
  [[nodiscard]] CanvasRect layoutToWindow(const CanvasRect& rect) const;
};

struct CanvasParseResult {
  std::optional<CanvasSpec> value;
  std::string error;

  explicit operator bool() const { return value.has_value(); }
};

/// Parses a document canvas. A missing/null value is the logical, full-window
/// default. Invalid supplied values fail instead of silently changing scale.
CanvasParseResult parseCanvasSpec(const nlohmann::json* canvas);

CanvasLayout resolveCanvas(const CanvasSpec& spec,
                           float logical_width,
                           float logical_height,
                           SafeAreaInsets platform_safe_area = {});

struct AnchorSpec {
  CanvasPoint minimum{0.5f, 0.5f};
  CanvasPoint maximum{0.5f, 0.5f};
  CanvasPoint pivot{0.5f, 0.5f};
  CanvasPoint position{};
  float offset_left = 0.0f;
  float offset_top = 0.0f;
  float offset_right = 0.0f;
  float offset_bottom = 0.0f;
};

struct AnchorParseResult {
  std::optional<AnchorSpec> value;
  std::string error;

  /// True for both a valid anchor and an object which does not declare one.
  explicit operator bool() const { return error.empty(); }
};

/// Parses normalized anchor constraints from a node `layout` object. `value`
/// remains empty when the layout has no `anchors` member.
AnchorParseResult parseAnchorSpec(const nlohmann::json& layout);

/// Resolves a child against its parent's content rectangle. A coincident
/// min/max anchor preserves the measured size and uses pivot. A separated pair
/// stretches between the two anchor edges and applies inward offsets.
CanvasRect resolveAnchor(const CanvasRect& parent_content,
                         const CanvasRect& measured,
                         const AnchorSpec& anchor);

}  // namespace karma::ui::native
