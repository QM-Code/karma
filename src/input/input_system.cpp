#include "karma/input/input_system.h"

#include "karma/platform/window.h"

namespace karma::input {

namespace {
bool isKeyDown(const platform::Window& window, platform::Key key) {
  return window.isKeyDown(key);
}

bool isMouseDown(const platform::Window& window, platform::MouseButton button) {
  return window.isMouseDown(button);
}

bool isKeyEvent(const platform::Event& event, platform::Key key, platform::EventType type) {
  return event.type == type && event.key == key;
}

bool isMouseEvent(const platform::Event& event, platform::MouseButton button,
                  platform::EventType type) {
  return event.type == type && event.mouseButton == button;
}
}

void InputSystem::bindKey(const std::string& action, platform::Key key, Trigger trigger) {
  bindings_[action].push_back(Binding{.trigger = trigger, .key = key, .use_key = true});
}

void InputSystem::bindMouse(const std::string& action, platform::MouseButton button,
                            Trigger trigger) {
  bindings_[action].push_back(
      Binding{.trigger = trigger, .mouse = button, .use_key = false});
}

void InputSystem::setRequiredModifiers(const std::string& action, platform::Modifiers mods) {
  for (auto& binding : bindings_[action]) {
    binding.mods = mods;
  }
}

bool InputSystem::matchesModifiers(const platform::Modifiers& event_mods,
                                   const platform::Modifiers& required_mods) const {
  if (required_mods.shift && !event_mods.shift) {
    return false;
  }
  if (required_mods.control && !event_mods.control) {
    return false;
  }
  if (required_mods.alt && !event_mods.alt) {
    return false;
  }
  if (required_mods.super && !event_mods.super) {
    return false;
  }
  return true;
}

void InputSystem::update(const std::vector<platform::Event>& events) {
  pressed_this_frame_.clear();
  down_this_frame_.clear();
  mouse_delta_x_ = 0.0f;
  mouse_delta_y_ = 0.0f;

  if (window_) {
    for (const auto& [action, bindings] : bindings_) {
      for (const auto& binding : bindings) {
        if (binding.trigger == Trigger::Down) {
          if (binding.use_key && isKeyDown(*window_, binding.key)) {
            down_this_frame_.insert(action);
          } else if (!binding.use_key && isMouseDown(*window_, binding.mouse)) {
            down_this_frame_.insert(action);
          }
        }
      }
    }
  }

  for (const auto& event : events) {
    if (event.type == platform::EventType::MouseMove) {
      if (has_mouse_pos_) {
        mouse_delta_x_ += static_cast<float>(event.x - last_mouse_x_);
        mouse_delta_y_ += static_cast<float>(event.y - last_mouse_y_);
      }
      last_mouse_x_ = event.x;
      last_mouse_y_ = event.y;
      has_mouse_pos_ = true;
    }
    for (const auto& [action, bindings] : bindings_) {
      for (const auto& binding : bindings) {
        if (binding.trigger != Trigger::Pressed) {
          continue;
        }
        if (!matchesModifiers(event.mods, binding.mods)) {
          continue;
        }
        if (binding.use_key &&
            isKeyEvent(event, binding.key, platform::EventType::KeyDown)) {
          pressed_this_frame_.insert(action);
        }
        if (!binding.use_key &&
            isMouseEvent(event, binding.mouse, platform::EventType::MouseButtonDown)) {
          pressed_this_frame_.insert(action);
        }
      }
    }
  }
}

bool InputSystem::actionDown(const std::string& action) const {
  return down_this_frame_.find(action) != down_this_frame_.end();
}

bool InputSystem::actionPressed(const std::string& action) const {
  return pressed_this_frame_.find(action) != pressed_this_frame_.end();
}

void InputSystem::clear() {
  pressed_this_frame_.clear();
  down_this_frame_.clear();
  mouse_delta_x_ = 0.0f;
  mouse_delta_y_ = 0.0f;
}

}  // namespace karma::input
