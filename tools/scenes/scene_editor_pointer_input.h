#pragma once

#include "scene_editor_viewport.h"

#include <optional>

namespace karma::tools::scene_editor {

/// Input state consumed exclusively by the Scene Editor viewport.
///
/// Graphical editor frames prefer the ImGui snapshot because UI capture may
/// intentionally filter the engine InputSystem before editor update runs.
/// Keyboard movement is included so an explicitly active RMB fly gesture is
/// not interrupted when ImGui takes keyboard navigation focus.
struct ViewportInputSnapshot {
  bool primary_down = false;
  bool primary_pressed = false;
  bool middle_down = false;
  bool right_down = false;
  bool orbit_modifier_down = false;
  bool fast_down = false;
  bool move_forward = false;
  bool move_backward = false;
  bool move_left = false;
  bool move_right = false;
  bool move_down = false;
  bool move_up = false;
  float delta_x = 0.0f;
  float delta_y = 0.0f;
  float wheel = 0.0f;
};

/// Selects one complete viewport-input snapshot without mixing states from
/// different frame clocks. The InputSystem value is only a fallback when no
/// ImGui context exists.
ViewportInputSnapshot resolveViewportInputSnapshot(
    std::optional<ViewportInputSnapshot> imgui,
    ViewportInputSnapshot input_fallback,
    bool app_focus_lost = false) noexcept;

/// Returns whether a finite pointer position lies in the viewport's half-open
/// content rectangle. This geometric check is independent of ImGui window
/// hover bookkeeping, which is unreliable for draw-list-backed viewport
/// images followed by a zero-ID layout item.
bool viewportContainsPointer(const ViewportRect& viewport,
                             float pointer_x,
                             float pointer_y) noexcept;

/// Tracks ownership of a mouse-button gesture. A press beginning in the
/// viewport remains viewport-owned if the pointer leaves its rectangle, and a
/// gesture beginning over another editor panel never transfers ownership.
bool updateViewportButtonOwnership(bool currently_owned,
                                   bool button_down,
                                   bool button_pressed,
                                   bool viewport_hovered) noexcept;

}  // namespace karma::tools::scene_editor
