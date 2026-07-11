#pragma once

#include "karma/platform.h"

namespace karma::app::detail {

/// Stateful UI layers must all observe releases, even when a higher layer
/// consumes the event, so their internal key/pointer/gamepad state cannot stick.
inline bool isUiStateReleaseEvent(const platform::Event& event) {
  switch (event.type) {
    case platform::EventType::KeyUp:
    case platform::EventType::MouseButtonUp:
    case platform::EventType::GamepadButtonUp:
    case platform::EventType::GamepadDisconnected:
      return true;
    case platform::EventType::GamepadAxisMotion:
      return event.gamepadValue >= -0.15f && event.gamepadValue <= 0.15f;
    default:
      return false;
  }
}

inline bool shouldRouteToLowerUiLayer(bool higher_layer_consumed,
                                      const platform::Event& event) {
  return !higher_layer_consumed || isUiStateReleaseEvent(event);
}

}  // namespace karma::app::detail
