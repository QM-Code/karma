#include "karma/app.h"

#include "karma/platform.h"

#include <algorithm>

namespace karma::app {

namespace {
bool isKeyDown(const platform::Window& window, platform::Key key) {
  return window.isKeyDown(key);
}

bool isMouseDown(const platform::Window& window, platform::MouseButton button) {
  return window.isMouseDown(button);
}

platform::Modifiers currentModifiers(const platform::Window& window) {
  return {
      .shift = window.isKeyDown(platform::Key::LeftShift) ||
               window.isKeyDown(platform::Key::RightShift),
      .control = window.isKeyDown(platform::Key::LeftControl) ||
                 window.isKeyDown(platform::Key::RightControl),
      .alt = window.isKeyDown(platform::Key::LeftAlt) ||
             window.isKeyDown(platform::Key::RightAlt),
      .super = window.isKeyDown(platform::Key::LeftSuper) ||
               window.isKeyDown(platform::Key::RightSuper),
  };
}

bool isKeyEvent(const platform::Event& event, platform::Key key, platform::EventType type) {
  return event.type == type && event.key == key;
}

bool isMouseEvent(const platform::Event& event, platform::MouseButton button,
                  platform::EventType type) {
  return event.type == type && event.mouseButton == button;
}

bool isGamepadButtonEvent(const platform::Event& event,
                          platform::GamepadButton button,
                          platform::EventType type) {
  return event.type == type && event.gamepadButton == button;
}

bool axisActive(float value, float threshold, bool positive) {
  const float magnitude = std::max(threshold, 0.0f);
  return positive ? value >= magnitude : value <= -magnitude;
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

void InputSystem::bindGamepadButton(const std::string& action,
                                    platform::GamepadButton button,
                                    Trigger trigger) {
  bindings_[action].push_back(Binding{.trigger = trigger,
                                      .gamepad_button = button,
                                      .use_key = false,
                                      .use_gamepad_button = true});
}

void InputSystem::bindGamepadAxis(const std::string& action,
                                  platform::GamepadAxis axis,
                                  float threshold,
                                  bool positive,
                                  Trigger trigger) {
  bindings_[action].push_back(Binding{.trigger = trigger,
                                      .gamepad_axis = axis,
                                      .gamepad_axis_threshold = std::max(threshold, 0.0f),
                                      .gamepad_axis_positive = positive,
                                      .use_key = false,
                                      .use_gamepad_axis = true});
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

void InputSystem::update(const std::vector<platform::Event>& events,
                         const InputFilter& filter) {
  pressed_this_frame_.clear();
  down_this_frame_.clear();
  mouse_delta_x_ = 0.0f;
  mouse_delta_y_ = 0.0f;

  const bool focus_lost = std::any_of(
      events.begin(), events.end(), [](const platform::Event& event) {
        return event.type == platform::EventType::WindowFocus && !event.focused;
      });

  if (window_ && !focus_lost) {
    platform::Modifiers modifiers = currentModifiers(*window_);
    if (filter.suppresses(platform::Key::LeftShift) ||
        filter.suppresses(platform::Key::RightShift)) {
      modifiers.shift = false;
    }
    if (filter.suppresses(platform::Key::LeftControl) ||
        filter.suppresses(platform::Key::RightControl)) {
      modifiers.control = false;
    }
    if (filter.suppresses(platform::Key::LeftAlt) ||
        filter.suppresses(platform::Key::RightAlt)) {
      modifiers.alt = false;
    }
    if (filter.suppresses(platform::Key::LeftSuper) ||
        filter.suppresses(platform::Key::RightSuper)) {
      modifiers.super = false;
    }
    for (const auto& [action, bindings] : bindings_) {
      for (const auto& binding : bindings) {
        if (binding.trigger == Trigger::Down &&
            matchesModifiers(modifiers, binding.mods)) {
          if (binding.use_gamepad_axis && !filter.suppresses(binding.gamepad_axis) &&
              axisActive(window_->gamepadAxis(binding.gamepad_axis),
                         binding.gamepad_axis_threshold,
                         binding.gamepad_axis_positive)) {
            down_this_frame_.insert(action);
          } else if (binding.use_gamepad_button &&
                     !filter.suppresses(binding.gamepad_button) &&
                     window_->isGamepadButtonDown(binding.gamepad_button)) {
            down_this_frame_.insert(action);
          } else if (binding.use_key && !filter.suppresses(binding.key) &&
                     isKeyDown(*window_, binding.key)) {
            down_this_frame_.insert(action);
          } else if (!binding.use_key && !binding.use_gamepad_button &&
                     !binding.use_gamepad_axis &&
                     !filter.suppresses(binding.mouse) &&
                     isMouseDown(*window_, binding.mouse)) {
            down_this_frame_.insert(action);
          }
        }
      }
    }
  }

  for (const auto& event : events) {
    if (event.type == platform::EventType::WindowFocus && !event.focused) {
      has_mouse_pos_ = false;
      previous_gamepad_axes_.clear();
    }
    if (event.type == platform::EventType::MouseMove ||
        event.type == platform::EventType::MouseButtonDown ||
        event.type == platform::EventType::MouseButtonUp) {
      if (has_mouse_pos_) {
        if (event.type == platform::EventType::MouseMove &&
            !filter.pointer && !filter.mouse_motion) {
          mouse_delta_x_ += static_cast<float>(event.x - last_mouse_x_);
          mouse_delta_y_ += static_cast<float>(event.y - last_mouse_y_);
        }
      }
      last_mouse_x_ = event.x;
      last_mouse_y_ = event.y;
      has_mouse_pos_ = true;
    }
    const bool key_suppressed =
        (event.type == platform::EventType::KeyDown ||
         event.type == platform::EventType::KeyUp ||
         event.type == platform::EventType::TextInput) &&
        (filter.keyboard || filter.keys.contains(event.key));
    const bool mouse_suppressed =
        (event.type == platform::EventType::MouseButtonDown ||
         event.type == platform::EventType::MouseButtonUp ||
         event.type == platform::EventType::MouseMove ||
         event.type == platform::EventType::MouseScroll) &&
        (filter.pointer || filter.mouse_buttons.contains(event.mouseButton));
    const bool gamepad_button_suppressed =
        (event.type == platform::EventType::GamepadButtonDown ||
         event.type == platform::EventType::GamepadButtonUp) &&
        filter.suppresses(event.gamepadButton);
    const bool gamepad_axis_suppressed =
        event.type == platform::EventType::GamepadAxisMotion &&
        filter.suppresses(event.gamepadAxis);
    platform::Modifiers event_modifiers = event.mods;
    if (filter.suppresses(platform::Key::LeftShift) ||
        filter.suppresses(platform::Key::RightShift)) {
      event_modifiers.shift = false;
    }
    if (filter.suppresses(platform::Key::LeftControl) ||
        filter.suppresses(platform::Key::RightControl)) {
      event_modifiers.control = false;
    }
    if (filter.suppresses(platform::Key::LeftAlt) ||
        filter.suppresses(platform::Key::RightAlt)) {
      event_modifiers.alt = false;
    }
    if (filter.suppresses(platform::Key::LeftSuper) ||
        filter.suppresses(platform::Key::RightSuper)) {
      event_modifiers.super = false;
    }

    if (!focus_lost) {
      const float previous_axis_value =
          event.type == platform::EventType::GamepadAxisMotion
              ? previous_gamepad_axes_[event.gamepad][event.gamepadAxis]
              : 0.0f;
      for (const auto& [action, bindings] : bindings_) {
        for (const auto& binding : bindings) {
          if (binding.trigger != Trigger::Pressed || event.repeat) {
            continue;
          }
          if (!matchesModifiers(event_modifiers, binding.mods)) {
            continue;
          }
          if (binding.use_gamepad_axis && !gamepad_axis_suppressed &&
              event.type == platform::EventType::GamepadAxisMotion &&
              event.gamepadAxis == binding.gamepad_axis &&
              axisActive(event.gamepadValue,
                         binding.gamepad_axis_threshold,
                         binding.gamepad_axis_positive) &&
              !axisActive(previous_axis_value,
                          binding.gamepad_axis_threshold,
                          binding.gamepad_axis_positive)) {
            pressed_this_frame_.insert(action);
          } else if (binding.use_gamepad_button && !gamepad_button_suppressed &&
                     isGamepadButtonEvent(
                         event, binding.gamepad_button,
                         platform::EventType::GamepadButtonDown)) {
            pressed_this_frame_.insert(action);
          } else if (binding.use_key && !key_suppressed &&
                     isKeyEvent(event, binding.key,
                                platform::EventType::KeyDown)) {
            pressed_this_frame_.insert(action);
          }
          if (!binding.use_key && !binding.use_gamepad_button &&
              !binding.use_gamepad_axis && !mouse_suppressed &&
              isMouseEvent(event, binding.mouse,
                           platform::EventType::MouseButtonDown)) {
            pressed_this_frame_.insert(action);
          }
        }
      }
    }
    if (!focus_lost && event.type == platform::EventType::GamepadAxisMotion) {
      previous_gamepad_axes_[event.gamepad][event.gamepadAxis] = event.gamepadValue;
    } else if (event.type == platform::EventType::GamepadDisconnected) {
      previous_gamepad_axes_.erase(event.gamepad);
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

}  // namespace karma::app
