#include "scene_editor_pointer_input.h"

#include <cmath>

namespace karma::tools::scene_editor {

ViewportInputSnapshot resolveViewportInputSnapshot(
    std::optional<ViewportInputSnapshot> imgui,
    ViewportInputSnapshot input_fallback,
    bool app_focus_lost) noexcept {
  if (app_focus_lost) {
    return {};
  }
  return imgui.value_or(input_fallback);
}

bool viewportContainsPointer(const ViewportRect& viewport,
                             float pointer_x,
                             float pointer_y) noexcept {
  if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      !std::isfinite(pointer_x) || !std::isfinite(pointer_y) ||
      viewport.width <= 0.0f || viewport.height <= 0.0f) {
    return false;
  }
  return pointer_x >= viewport.x && pointer_y >= viewport.y &&
         pointer_x < viewport.x + viewport.width &&
         pointer_y < viewport.y + viewport.height;
}

bool updateViewportButtonOwnership(bool currently_owned,
                                   bool button_down,
                                   bool button_pressed,
                                   bool viewport_hovered) noexcept {
  if (!button_down) {
    return false;
  }
  if (button_pressed) {
    return viewport_hovered;
  }
  return currently_owned;
}

}  // namespace karma::tools::scene_editor
