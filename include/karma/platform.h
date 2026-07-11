#pragma once





#include <cstdint>

namespace karma::platform {

/// \ingroup karma_platform
/// Platform-independent key enum used by events and input bindings.
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
  World2,
  Comma,
  Period,
  Slash,
  Semicolon,
  Backslash,
  PrintScreen,
  Pause,
  Keypad0,
  Keypad1,
  Keypad2,
  Keypad3,
  Keypad4,
  Keypad5,
  Keypad6,
  Keypad7,
  Keypad8,
  Keypad9,
  KeypadDecimal,
  KeypadDivide,
  KeypadMultiply,
  KeypadSubtract,
  KeypadAdd,
  KeypadEnter,
  KeypadEqual
};

/// \ingroup karma_platform
/// Platform-independent mouse button enum.
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

/// \ingroup karma_platform
/// Normalized gamepad button names shared by window backends.
enum class GamepadButton {
  Unknown,
  A,
  B,
  X,
  Y,
  Back,
  Guide,
  Start,
  LeftStick,
  RightStick,
  LeftShoulder,
  RightShoulder,
  DpadUp,
  DpadRight,
  DpadDown,
  DpadLeft,
};

/// \ingroup karma_platform
/// Normalized gamepad axes. Stick axes use -1..1 and triggers use 0..1.
enum class GamepadAxis {
  Unknown,
  LeftX,
  LeftY,
  RightX,
  RightY,
  LeftTrigger,
  RightTrigger,
};

/// \ingroup karma_platform
/// Cursor shapes available to UI implementations.
enum class CursorShape {
  Default,
  Pointer,
  Text,
  Crosshair,
  Move,
  ResizeHorizontal,
  ResizeVertical,
  ResizeDiagonalNwSe,
  ResizeDiagonalNeSw,
  NotAllowed,
};

/// Insets, in window-logical units, where game UI should avoid platform
/// cutouts, rounded corners, or system-reserved overlays.
struct SafeAreaInsets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  friend bool operator==(const SafeAreaInsets&, const SafeAreaInsets&) = default;
};

/// \ingroup karma_platform
/// Keyboard modifier state.
struct Modifiers {
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool super = false;
};

/// \ingroup karma_platform
/// Window/input event kind.
enum class EventType {
  KeyDown,
  KeyUp,
  TextInput,
  MouseButtonDown,
  MouseButtonUp,
  MouseMove,
  MouseScroll,
  GamepadConnected,
  GamepadDisconnected,
  GamepadButtonDown,
  GamepadButtonUp,
  GamepadAxisMotion,
  WindowResize,
  WindowFocus,
  WindowClose
};

/// \ingroup karma_platform
/// Platform-independent window/input event.
struct Event {
  EventType type = EventType::KeyDown;
  Key key = Key::Unknown;
  /// True for a repeated key-down or a synthesized held-navigation gamepad
  /// button/axis event. Initial presses and physical axis changes are false.
  bool repeat = false;
  MouseButton mouseButton = MouseButton::Left;
  Modifiers mods{};
  uint32_t codepoint = 0;
  double x = 0.0;
  double y = 0.0;
  double scrollX = 0.0;
  double scrollY = 0.0;
  int gamepad = -1;
  GamepadButton gamepadButton = GamepadButton::Unknown;
  GamepadAxis gamepadAxis = GamepadAxis::Unknown;
  float gamepadValue = 0.0f;
  /// Compatibility framebuffer dimensions for resize events.
  int width = 0;
  int height = 0;
  int logicalWidth = 0;
  int logicalHeight = 0;
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  bool focused = true;
};

}  // namespace karma::platform


#include <memory>
#include <string>
#include <string_view>
#include <vector>


namespace karma::platform {

/// \ingroup karma_platform
/// Platform window creation settings.
struct WindowConfig {
  int width = 1280;
  int height = 720;
  std::string title = "Karma";
  std::string icon_path;
  int gl_major = 3;
  int gl_minor = 3;
  bool gl_core_profile = true;
  int samples = 4;
};

/// \ingroup karma_platform
/// Platform window abstraction used by runtime and renderer backend creation.
class Window {
 public:
  virtual ~Window() = default;

  virtual void pollEvents() = 0;
  virtual const std::vector<Event>& events() const = 0;
  virtual void clearEvents() = 0;

  virtual bool shouldClose() const = 0;
  virtual void requestClose() = 0;

  virtual void swapBuffers() = 0;
  virtual void setVsync(bool enabled) = 0;
  virtual void setFullscreen(bool enabled) = 0;
  virtual bool isFullscreen() const = 0;
  virtual void setIcon(const std::string& path) = 0;

  virtual void getFramebufferSize(int& width, int& height) const = 0;
  /// Returns the logical window size used by pointer input and UI layout.
  virtual void getLogicalSize(int& width, int& height) const {
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    getFramebufferSize(framebuffer_width, framebuffer_height);
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    getContentScale(scale_x, scale_y);
    width = scale_x > 0.0f
                ? static_cast<int>(static_cast<float>(framebuffer_width) / scale_x + 0.5f)
                : framebuffer_width;
    height = scale_y > 0.0f
                 ? static_cast<int>(static_cast<float>(framebuffer_height) / scale_y + 0.5f)
                 : framebuffer_height;
  }
  /// Compatibility scalar content scale. Prefer the independent-axis overload.
  virtual float getContentScale() const = 0;
  /// Returns the framebuffer-to-logical scale independently for each axis.
  virtual void getContentScale(float& scale_x, float& scale_y) const {
    scale_x = getContentScale();
    scale_y = scale_x;
  }
  /// Desktop backends default to a zero-inset safe area. Console/mobile
  /// backends can override this without changing the UI API.
  virtual SafeAreaInsets getSafeAreaInsets() const { return {}; }

  virtual bool isKeyDown(Key key) const = 0;
  virtual bool isMouseDown(MouseButton button) const = 0;
  virtual bool isGamepadButtonDown(GamepadButton button, int gamepad = -1) const {
    (void)button;
    (void)gamepad;
    return false;
  }
  virtual float gamepadAxis(GamepadAxis axis, int gamepad = -1) const {
    (void)axis;
    (void)gamepad;
    return 0.0f;
  }

  virtual void setCursorVisible(bool visible) = 0;
  virtual void setCursorShape(CursorShape shape) { (void)shape; }
  virtual void setClipboardText(std::string_view text) = 0;
  virtual std::string getClipboardText() const = 0;

  virtual void* nativeHandle() const = 0;
};

/// Creates the configured default window backend.
std::unique_ptr<Window> createWindow(const WindowConfig& config);
/// Creates a GLFW window backend.
std::unique_ptr<Window> createGlfwWindow(const WindowConfig& config);
/// Creates an SDL window backend.
std::unique_ptr<Window> createSdlWindow(const WindowConfig& config);

}  // namespace karma::platform
