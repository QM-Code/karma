#pragma once

#include <cstdint>

namespace karma::platform {

enum class Key {
  Unknown,
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,
  Space,
  Escape,
  Enter,
  Tab,
  Backspace,
  Left,
  Right,
  Up,
  Down,
  LeftBracket,
  RightBracket,
  Minus,
  Equal,
  Apostrophe,
  GraveAccent,
  LeftShift,
  RightShift,
  LeftControl,
  RightControl,
  LeftAlt,
  RightAlt,
  LeftSuper,
  RightSuper,
  Menu,
  Home,
  End,
  PageUp,
  PageDown,
  Insert,
  Delete,
  CapsLock,
  NumLock,
  ScrollLock,
  World1,
  World2
};

enum class MouseButton {
  Left,
  Right,
  Middle,
  Button4,
  Button5,
  Button6,
  Button7,
  Button8
};

struct Modifiers {
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool super = false;
};

enum class EventType {
  KeyDown,
  KeyUp,
  TextInput,
  MouseButtonDown,
  MouseButtonUp,
  MouseMove,
  MouseScroll,
  WindowResize,
  WindowFocus,
  WindowClose
};

struct Event {
  EventType type = EventType::KeyDown;
  Key key = Key::Unknown;
  MouseButton mouseButton = MouseButton::Left;
  Modifiers mods{};
  uint32_t codepoint = 0;
  double x = 0.0;
  double y = 0.0;
  double scrollX = 0.0;
  double scrollY = 0.0;
  int width = 0;
  int height = 0;
  bool focused = true;
};

}  // namespace karma::platform
