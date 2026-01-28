#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "karma/platform/events.h"

namespace karma::platform {
class Window;
}

namespace karma::input {

enum class Trigger {
  Down,
  Pressed
};

struct Binding {
  Trigger trigger = Trigger::Down;
  platform::Key key = platform::Key::Unknown;
  platform::MouseButton mouse = platform::MouseButton::Left;
  platform::Modifiers mods{};
  bool use_key = true;
};

class InputSystem {
 public:
  void setWindow(const platform::Window* window) { window_ = window; }

  void bindKey(const std::string& action, platform::Key key, Trigger trigger = Trigger::Down);
  void bindMouse(const std::string& action, platform::MouseButton button,
                 Trigger trigger = Trigger::Down);
  void setRequiredModifiers(const std::string& action, platform::Modifiers mods);

  void update(const std::vector<platform::Event>& events);

  bool actionDown(const std::string& action) const;
  bool actionPressed(const std::string& action) const;
  float mouseDeltaX() const { return mouse_delta_x_; }
  float mouseDeltaY() const { return mouse_delta_y_; }

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
