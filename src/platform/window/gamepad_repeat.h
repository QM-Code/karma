#pragma once

#include "karma/platform.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace karma::platform::detail {

/// Produces deterministic UI-navigation repeats from normalized gamepad state.
/// Backends feed it state transitions and append due events once per poll.
class GamepadRepeatScheduler {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  static constexpr auto initial_delay = std::chrono::milliseconds(450);
  static constexpr auto repeat_interval = std::chrono::milliseconds(90);
  static constexpr float axis_threshold = 0.55f;

  static constexpr bool repeats(GamepadButton button) {
    switch (button) {
      case GamepadButton::LeftShoulder:
      case GamepadButton::RightShoulder:
      case GamepadButton::DpadUp:
      case GamepadButton::DpadRight:
      case GamepadButton::DpadDown:
      case GamepadButton::DpadLeft:
        return true;
      default:
        return false;
    }
  }

  static constexpr bool repeats(GamepadAxis axis) {
    return axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY;
  }

  void buttonChanged(int gamepad,
                     GamepadButton button,
                     bool pressed,
                     TimePoint now = Clock::now()) {
    if (!repeats(button)) {
      return;
    }
    auto& slot = states_[gamepad].buttons[index(button)];
    if (!pressed) {
      slot = {};
    } else if (!slot.held) {
      slot.held = true;
      slot.next = now + initial_delay;
    }
  }

  void axisChanged(int gamepad,
                   GamepadAxis axis,
                   float value,
                   TimePoint now = Clock::now()) {
    if (!repeats(axis)) {
      return;
    }
    auto& slot = states_[gamepad].axes[index(axis)];
    const int direction = value >= axis_threshold
                              ? 1
                              : (value <= -axis_threshold ? -1 : 0);
    if (direction == 0) {
      slot = {};
      return;
    }
    if (!slot.held || slot.direction != direction) {
      slot.held = true;
      slot.direction = direction;
      slot.next = now + initial_delay;
    }
    slot.value = value;
  }

  void resetGamepad(int gamepad) { states_.erase(gamepad); }

  void appendDue(std::vector<Event>& events,
                 TimePoint now = Clock::now()) {
    for (auto& [gamepad, state] : states_) {
      for (std::size_t i = 0; i < state.buttons.size(); ++i) {
        RepeatSlot& slot = state.buttons[i];
        if (!slot.held || now < slot.next) {
          continue;
        }
        Event event;
        event.type = EventType::GamepadButtonDown;
        event.repeat = true;
        event.gamepad = gamepad;
        event.gamepadButton = static_cast<GamepadButton>(i);
        events.push_back(event);
        advance(slot, now);
      }
      for (std::size_t i = 0; i < state.axes.size(); ++i) {
        RepeatSlot& slot = state.axes[i];
        if (!slot.held || now < slot.next) {
          continue;
        }
        Event event;
        event.type = EventType::GamepadAxisMotion;
        event.repeat = true;
        event.gamepad = gamepad;
        event.gamepadAxis = static_cast<GamepadAxis>(i);
        event.gamepadValue = slot.value;
        events.push_back(event);
        advance(slot, now);
      }
    }
  }

private:
  struct RepeatSlot {
    bool held = false;
    int direction = 0;
    float value = 0.0f;
    TimePoint next{};
  };

  static constexpr std::size_t button_count =
      static_cast<std::size_t>(GamepadButton::DpadLeft) + 1u;
  static constexpr std::size_t axis_count =
      static_cast<std::size_t>(GamepadAxis::RightTrigger) + 1u;

  struct GamepadState {
    std::array<RepeatSlot, button_count> buttons{};
    std::array<RepeatSlot, axis_count> axes{};
  };

  template <typename Enum>
  static constexpr std::size_t index(Enum value) {
    return static_cast<std::size_t>(value);
  }

  static void advance(RepeatSlot& slot, TimePoint now) {
    do {
      slot.next += repeat_interval;
    } while (slot.next <= now);
  }

  std::unordered_map<int, GamepadState> states_;
};

}  // namespace karma::platform::detail
