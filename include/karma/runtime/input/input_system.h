#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "karma/platform/window/events.h"

namespace karma::platform {
class Window;
}

namespace karma::input {

/// \ingroup karma_runtime
/// Input trigger mode for action bindings.
enum class Trigger {
  Down,
  Pressed
};

/// \ingroup karma_runtime
/// Key or mouse binding for one named action.
struct Binding {
  Trigger trigger = Trigger::Down;
  platform::Key key = platform::Key::Unknown;
  platform::MouseButton mouse = platform::MouseButton::Left;
  platform::Modifiers mods{};
  bool use_key = true;
};

/// \ingroup karma_runtime
/// Per-frame action and mouse input state.
///
/// Bind actions once, call `update()` with platform events each frame, query
/// actions during gameplay/UI, then let `EngineApp` clear transient state after
/// systems and UI have consumed it.
class InputSystem {
 public:
  /// Sets the platform window used for current key/mouse state.
  void setWindow(const platform::Window* window) { window_ = window; }

  /// Binds a keyboard key to an action.
  void bindKey(const std::string& action, platform::Key key, Trigger trigger = Trigger::Down);
  /// Binds a mouse button to an action.
  void bindMouse(const std::string& action, platform::MouseButton button,
                 Trigger trigger = Trigger::Down);
  /// Requires modifiers for an existing action binding.
  void setRequiredModifiers(const std::string& action, platform::Modifiers mods);

  /// Consumes platform events and updates action/mouse state.
  void update(const std::vector<platform::Event>& events);

  /// Returns true while an action is currently down.
  bool actionDown(const std::string& action) const;
  /// Returns true only on the frame an action was pressed.
  bool actionPressed(const std::string& action) const;
  /// Mouse movement delta since the previous update.
  float mouseDeltaX() const { return mouse_delta_x_; }
  /// Mouse movement delta since the previous update.
  float mouseDeltaY() const { return mouse_delta_y_; }
  /// Writes the latest mouse position if one is known.
  bool mousePosition(double& x, double& y) const {
    if (!has_mouse_pos_) {
      return false;
    }
    x = last_mouse_x_;
    y = last_mouse_y_;
    return true;
  }

  /// Clears transient per-frame state.
  void clear();

 private:
  bool matchesModifiers(const platform::Modifiers& event_mods,
                        const platform::Modifiers& required_mods) const;

  std::unordered_map<std::string, std::vector<Binding>> bindings_;
  std::unordered_set<std::string> pressed_this_frame_;
  std::unordered_set<std::string> down_this_frame_;
  const platform::Window* window_ = nullptr;
  float mouse_delta_x_ = 0.0f;
  float mouse_delta_y_ = 0.0f;
  bool has_mouse_pos_ = false;
  double last_mouse_x_ = 0.0;
  double last_mouse_y_ = 0.0;
};

}  // namespace karma::input
