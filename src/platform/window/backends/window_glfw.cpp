#include "karma/platform.h"

#include "../gamepad_repeat.h"
#include "../standard_cursor_cache.h"

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <unordered_map>

namespace karma::platform {
namespace {

struct GlfwRuntime {
    std::mutex mutex;
    std::size_t users = 0u;
};

GlfwRuntime& glfwRuntime() {
    static GlfwRuntime runtime;
    return runtime;
}

bool acquireGlfw() {
    auto& runtime = glfwRuntime();
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (runtime.users == 0u && glfwInit() != GLFW_TRUE) {
        spdlog::error("GLFW failed to initialize");
        return false;
    }
    ++runtime.users;
    return true;
}

void releaseGlfw() {
    auto& runtime = glfwRuntime();
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (runtime.users == 0u) {
        return;
    }
    --runtime.users;
    if (runtime.users == 0u) {
        glfwTerminate();
    }
}

Key toKey(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_A: return Key::A;
        case GLFW_KEY_B: return Key::B;
        case GLFW_KEY_C: return Key::C;
        case GLFW_KEY_D: return Key::D;
        case GLFW_KEY_E: return Key::E;
        case GLFW_KEY_F: return Key::F;
        case GLFW_KEY_G: return Key::G;
        case GLFW_KEY_H: return Key::H;
        case GLFW_KEY_I: return Key::I;
        case GLFW_KEY_J: return Key::J;
        case GLFW_KEY_K: return Key::K;
        case GLFW_KEY_L: return Key::L;
        case GLFW_KEY_M: return Key::M;
        case GLFW_KEY_N: return Key::N;
        case GLFW_KEY_O: return Key::O;
        case GLFW_KEY_P: return Key::P;
        case GLFW_KEY_Q: return Key::Q;
        case GLFW_KEY_R: return Key::R;
        case GLFW_KEY_S: return Key::S;
        case GLFW_KEY_T: return Key::T;
        case GLFW_KEY_U: return Key::U;
        case GLFW_KEY_V: return Key::V;
        case GLFW_KEY_W: return Key::W;
        case GLFW_KEY_X: return Key::X;
        case GLFW_KEY_Y: return Key::Y;
        case GLFW_KEY_Z: return Key::Z;
        case GLFW_KEY_0: return Key::Num0;
        case GLFW_KEY_1: return Key::Num1;
        case GLFW_KEY_2: return Key::Num2;
        case GLFW_KEY_3: return Key::Num3;
        case GLFW_KEY_4: return Key::Num4;
        case GLFW_KEY_5: return Key::Num5;
        case GLFW_KEY_6: return Key::Num6;
        case GLFW_KEY_7: return Key::Num7;
        case GLFW_KEY_8: return Key::Num8;
        case GLFW_KEY_9: return Key::Num9;
        case GLFW_KEY_F1: return Key::F1;
        case GLFW_KEY_F2: return Key::F2;
        case GLFW_KEY_F3: return Key::F3;
        case GLFW_KEY_F4: return Key::F4;
        case GLFW_KEY_F5: return Key::F5;
        case GLFW_KEY_F6: return Key::F6;
        case GLFW_KEY_F7: return Key::F7;
        case GLFW_KEY_F8: return Key::F8;
        case GLFW_KEY_F9: return Key::F9;
        case GLFW_KEY_F10: return Key::F10;
        case GLFW_KEY_F11: return Key::F11;
        case GLFW_KEY_F12: return Key::F12;
        case GLFW_KEY_F13: return Key::F13;
        case GLFW_KEY_F14: return Key::F14;
        case GLFW_KEY_F15: return Key::F15;
        case GLFW_KEY_F16: return Key::F16;
        case GLFW_KEY_F17: return Key::F17;
        case GLFW_KEY_F18: return Key::F18;
        case GLFW_KEY_F19: return Key::F19;
        case GLFW_KEY_F20: return Key::F20;
        case GLFW_KEY_F21: return Key::F21;
        case GLFW_KEY_F22: return Key::F22;
        case GLFW_KEY_F23: return Key::F23;
        case GLFW_KEY_F24: return Key::F24;
        case GLFW_KEY_F25: return Key::F25;
        case GLFW_KEY_SPACE: return Key::Space;
        case GLFW_KEY_ESCAPE: return Key::Escape;
        case GLFW_KEY_ENTER: return Key::Enter;
        case GLFW_KEY_TAB: return Key::Tab;
        case GLFW_KEY_BACKSPACE: return Key::Backspace;
        case GLFW_KEY_LEFT: return Key::Left;
        case GLFW_KEY_RIGHT: return Key::Right;
        case GLFW_KEY_UP: return Key::Up;
        case GLFW_KEY_DOWN: return Key::Down;
        case GLFW_KEY_LEFT_BRACKET: return Key::LeftBracket;
        case GLFW_KEY_RIGHT_BRACKET: return Key::RightBracket;
        case GLFW_KEY_MINUS: return Key::Minus;
        case GLFW_KEY_EQUAL: return Key::Equal;
        case GLFW_KEY_APOSTROPHE: return Key::Apostrophe;
        case GLFW_KEY_GRAVE_ACCENT: return Key::GraveAccent;
        case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
        case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
        case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
        case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
        case GLFW_KEY_LEFT_SUPER: return Key::LeftSuper;
        case GLFW_KEY_RIGHT_SUPER: return Key::RightSuper;
        case GLFW_KEY_MENU: return Key::Menu;
        case GLFW_KEY_HOME: return Key::Home;
        case GLFW_KEY_END: return Key::End;
        case GLFW_KEY_PAGE_UP: return Key::PageUp;
        case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
        case GLFW_KEY_INSERT: return Key::Insert;
        case GLFW_KEY_DELETE: return Key::Delete;
        case GLFW_KEY_CAPS_LOCK: return Key::CapsLock;
        case GLFW_KEY_NUM_LOCK: return Key::NumLock;
        case GLFW_KEY_SCROLL_LOCK: return Key::ScrollLock;
        case GLFW_KEY_WORLD_1: return Key::World1;
        case GLFW_KEY_WORLD_2: return Key::World2;
        default: return Key::Unknown;
    }
}

int toGlfwKey(Key key) {
    switch (key) {
        case Key::A: return GLFW_KEY_A;
        case Key::B: return GLFW_KEY_B;
        case Key::C: return GLFW_KEY_C;
        case Key::D: return GLFW_KEY_D;
        case Key::E: return GLFW_KEY_E;
        case Key::F: return GLFW_KEY_F;
        case Key::G: return GLFW_KEY_G;
        case Key::H: return GLFW_KEY_H;
        case Key::I: return GLFW_KEY_I;
        case Key::J: return GLFW_KEY_J;
        case Key::K: return GLFW_KEY_K;
        case Key::L: return GLFW_KEY_L;
        case Key::M: return GLFW_KEY_M;
        case Key::N: return GLFW_KEY_N;
        case Key::O: return GLFW_KEY_O;
        case Key::P: return GLFW_KEY_P;
        case Key::Q: return GLFW_KEY_Q;
        case Key::R: return GLFW_KEY_R;
        case Key::S: return GLFW_KEY_S;
        case Key::T: return GLFW_KEY_T;
        case Key::U: return GLFW_KEY_U;
        case Key::V: return GLFW_KEY_V;
        case Key::W: return GLFW_KEY_W;
        case Key::X: return GLFW_KEY_X;
        case Key::Y: return GLFW_KEY_Y;
        case Key::Z: return GLFW_KEY_Z;
        case Key::Num0: return GLFW_KEY_0;
        case Key::Num1: return GLFW_KEY_1;
        case Key::Num2: return GLFW_KEY_2;
        case Key::Num3: return GLFW_KEY_3;
        case Key::Num4: return GLFW_KEY_4;
        case Key::Num5: return GLFW_KEY_5;
        case Key::Num6: return GLFW_KEY_6;
        case Key::Num7: return GLFW_KEY_7;
        case Key::Num8: return GLFW_KEY_8;
        case Key::Num9: return GLFW_KEY_9;
        case Key::F1: return GLFW_KEY_F1;
        case Key::F2: return GLFW_KEY_F2;
        case Key::F3: return GLFW_KEY_F3;
        case Key::F4: return GLFW_KEY_F4;
        case Key::F5: return GLFW_KEY_F5;
        case Key::F6: return GLFW_KEY_F6;
        case Key::F7: return GLFW_KEY_F7;
        case Key::F8: return GLFW_KEY_F8;
        case Key::F9: return GLFW_KEY_F9;
        case Key::F10: return GLFW_KEY_F10;
        case Key::F11: return GLFW_KEY_F11;
        case Key::F12: return GLFW_KEY_F12;
        case Key::F13: return GLFW_KEY_F13;
        case Key::F14: return GLFW_KEY_F14;
        case Key::F15: return GLFW_KEY_F15;
        case Key::F16: return GLFW_KEY_F16;
        case Key::F17: return GLFW_KEY_F17;
        case Key::F18: return GLFW_KEY_F18;
        case Key::F19: return GLFW_KEY_F19;
        case Key::F20: return GLFW_KEY_F20;
        case Key::F21: return GLFW_KEY_F21;
        case Key::F22: return GLFW_KEY_F22;
        case Key::F23: return GLFW_KEY_F23;
        case Key::F24: return GLFW_KEY_F24;
        case Key::F25: return GLFW_KEY_F25;
        case Key::Space: return GLFW_KEY_SPACE;
        case Key::Escape: return GLFW_KEY_ESCAPE;
        case Key::Enter: return GLFW_KEY_ENTER;
        case Key::Tab: return GLFW_KEY_TAB;
        case Key::Backspace: return GLFW_KEY_BACKSPACE;
        case Key::Left: return GLFW_KEY_LEFT;
        case Key::Right: return GLFW_KEY_RIGHT;
        case Key::Up: return GLFW_KEY_UP;
        case Key::Down: return GLFW_KEY_DOWN;
        case Key::LeftBracket: return GLFW_KEY_LEFT_BRACKET;
        case Key::RightBracket: return GLFW_KEY_RIGHT_BRACKET;
        case Key::Minus: return GLFW_KEY_MINUS;
        case Key::Equal: return GLFW_KEY_EQUAL;
        case Key::Apostrophe: return GLFW_KEY_APOSTROPHE;
        case Key::GraveAccent: return GLFW_KEY_GRAVE_ACCENT;
        case Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case Key::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case Key::RightControl: return GLFW_KEY_RIGHT_CONTROL;
        case Key::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case Key::RightAlt: return GLFW_KEY_RIGHT_ALT;
        case Key::LeftSuper: return GLFW_KEY_LEFT_SUPER;
        case Key::RightSuper: return GLFW_KEY_RIGHT_SUPER;
        case Key::Menu: return GLFW_KEY_MENU;
        case Key::Home: return GLFW_KEY_HOME;
        case Key::End: return GLFW_KEY_END;
        case Key::PageUp: return GLFW_KEY_PAGE_UP;
        case Key::PageDown: return GLFW_KEY_PAGE_DOWN;
        case Key::Insert: return GLFW_KEY_INSERT;
        case Key::Delete: return GLFW_KEY_DELETE;
        case Key::CapsLock: return GLFW_KEY_CAPS_LOCK;
        case Key::NumLock: return GLFW_KEY_NUM_LOCK;
        case Key::ScrollLock: return GLFW_KEY_SCROLL_LOCK;
        case Key::World1: return GLFW_KEY_WORLD_1;
        case Key::World2: return GLFW_KEY_WORLD_2;
        default: return GLFW_KEY_UNKNOWN;
    }
}

MouseButton toMouseButton(int glfwButton) {
    switch (glfwButton) {
        case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
        case GLFW_MOUSE_BUTTON_4: return MouseButton::Button4;
        case GLFW_MOUSE_BUTTON_5: return MouseButton::Button5;
        case GLFW_MOUSE_BUTTON_6: return MouseButton::Button6;
        case GLFW_MOUSE_BUTTON_7: return MouseButton::Button7;
        case GLFW_MOUSE_BUTTON_8: return MouseButton::Button8;
        default: return MouseButton::Left;
    }
}

int toGlfwMouseButton(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
        case MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
        case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        case MouseButton::Button4: return GLFW_MOUSE_BUTTON_4;
        case MouseButton::Button5: return GLFW_MOUSE_BUTTON_5;
        case MouseButton::Button6: return GLFW_MOUSE_BUTTON_6;
        case MouseButton::Button7: return GLFW_MOUSE_BUTTON_7;
        case MouseButton::Button8: return GLFW_MOUSE_BUTTON_8;
        default: return GLFW_MOUSE_BUTTON_LEFT;
    }
}

GamepadButton toGamepadButton(int button) {
    switch (button) {
        case GLFW_GAMEPAD_BUTTON_A: return GamepadButton::A;
        case GLFW_GAMEPAD_BUTTON_B: return GamepadButton::B;
        case GLFW_GAMEPAD_BUTTON_X: return GamepadButton::X;
        case GLFW_GAMEPAD_BUTTON_Y: return GamepadButton::Y;
        case GLFW_GAMEPAD_BUTTON_BACK: return GamepadButton::Back;
        case GLFW_GAMEPAD_BUTTON_GUIDE: return GamepadButton::Guide;
        case GLFW_GAMEPAD_BUTTON_START: return GamepadButton::Start;
        case GLFW_GAMEPAD_BUTTON_LEFT_THUMB: return GamepadButton::LeftStick;
        case GLFW_GAMEPAD_BUTTON_RIGHT_THUMB: return GamepadButton::RightStick;
        case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER: return GamepadButton::LeftShoulder;
        case GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER: return GamepadButton::RightShoulder;
        case GLFW_GAMEPAD_BUTTON_DPAD_UP: return GamepadButton::DpadUp;
        case GLFW_GAMEPAD_BUTTON_DPAD_RIGHT: return GamepadButton::DpadRight;
        case GLFW_GAMEPAD_BUTTON_DPAD_DOWN: return GamepadButton::DpadDown;
        case GLFW_GAMEPAD_BUTTON_DPAD_LEFT: return GamepadButton::DpadLeft;
        default: return GamepadButton::Unknown;
    }
}

int toGlfwGamepadButton(GamepadButton button) {
    switch (button) {
        case GamepadButton::A: return GLFW_GAMEPAD_BUTTON_A;
        case GamepadButton::B: return GLFW_GAMEPAD_BUTTON_B;
        case GamepadButton::X: return GLFW_GAMEPAD_BUTTON_X;
        case GamepadButton::Y: return GLFW_GAMEPAD_BUTTON_Y;
        case GamepadButton::Back: return GLFW_GAMEPAD_BUTTON_BACK;
        case GamepadButton::Guide: return GLFW_GAMEPAD_BUTTON_GUIDE;
        case GamepadButton::Start: return GLFW_GAMEPAD_BUTTON_START;
        case GamepadButton::LeftStick: return GLFW_GAMEPAD_BUTTON_LEFT_THUMB;
        case GamepadButton::RightStick: return GLFW_GAMEPAD_BUTTON_RIGHT_THUMB;
        case GamepadButton::LeftShoulder: return GLFW_GAMEPAD_BUTTON_LEFT_BUMPER;
        case GamepadButton::RightShoulder: return GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER;
        case GamepadButton::DpadUp: return GLFW_GAMEPAD_BUTTON_DPAD_UP;
        case GamepadButton::DpadRight: return GLFW_GAMEPAD_BUTTON_DPAD_RIGHT;
        case GamepadButton::DpadDown: return GLFW_GAMEPAD_BUTTON_DPAD_DOWN;
        case GamepadButton::DpadLeft: return GLFW_GAMEPAD_BUTTON_DPAD_LEFT;
        default: return -1;
    }
}

GamepadAxis toGamepadAxis(int axis) {
    switch (axis) {
        case GLFW_GAMEPAD_AXIS_LEFT_X: return GamepadAxis::LeftX;
        case GLFW_GAMEPAD_AXIS_LEFT_Y: return GamepadAxis::LeftY;
        case GLFW_GAMEPAD_AXIS_RIGHT_X: return GamepadAxis::RightX;
        case GLFW_GAMEPAD_AXIS_RIGHT_Y: return GamepadAxis::RightY;
        case GLFW_GAMEPAD_AXIS_LEFT_TRIGGER: return GamepadAxis::LeftTrigger;
        case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER: return GamepadAxis::RightTrigger;
        default: return GamepadAxis::Unknown;
    }
}

int toGlfwGamepadAxis(GamepadAxis axis) {
    switch (axis) {
        case GamepadAxis::LeftX: return GLFW_GAMEPAD_AXIS_LEFT_X;
        case GamepadAxis::LeftY: return GLFW_GAMEPAD_AXIS_LEFT_Y;
        case GamepadAxis::RightX: return GLFW_GAMEPAD_AXIS_RIGHT_X;
        case GamepadAxis::RightY: return GLFW_GAMEPAD_AXIS_RIGHT_Y;
        case GamepadAxis::LeftTrigger: return GLFW_GAMEPAD_AXIS_LEFT_TRIGGER;
        case GamepadAxis::RightTrigger: return GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
        default: return -1;
    }
}

float normalizeGamepadAxis(int axis, float value) {
    if (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER ||
        axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        const float normalized = std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
        return normalized < 0.05f ? 0.0f : normalized;
    }
    constexpr float dead_zone = 0.18f;
    const float magnitude = std::abs(value);
    if (magnitude <= dead_zone) {
        return 0.0f;
    }
    return std::copysign((magnitude - dead_zone) / (1.0f - dead_zone), value);
}

Modifiers toModifiers(int mods) {
    Modifiers out;
    out.shift = (mods & GLFW_MOD_SHIFT) != 0;
    out.control = (mods & GLFW_MOD_CONTROL) != 0;
    out.alt = (mods & GLFW_MOD_ALT) != 0;
    out.super = (mods & GLFW_MOD_SUPER) != 0;
    return out;
}

class WindowGlfw final : public Window {
public:
    explicit WindowGlfw(const WindowConfig &config) {
        glfwAcquired = acquireGlfw();
        if (!glfwAcquired) {
            return;
        }

#if defined(KARMA_RENDER_BACKEND_DILIGENT)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, config.gl_major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, config.gl_minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, config.gl_core_profile ? GLFW_OPENGL_CORE_PROFILE : GLFW_OPENGL_ANY_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, config.samples);
#endif

        window = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!window) {
            spdlog::error("GLFW window failed to create");
            return;
        }

#if !defined(KARMA_RENDER_BACKEND_DILIGENT)
        glfwMakeContextCurrent(window);
#endif
        glfwSetWindowUserPointer(window, this);
        refreshCachedWindowState();

        setupCallbacks();
    }

    ~WindowGlfw() override {
        cursors.clear([](GLFWcursor* cursor) { glfwDestroyCursor(cursor); });
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        if (glfwAcquired) {
            releaseGlfw();
            glfwAcquired = false;
        }
    }

    void pollEvents() override {
        eventsBuffer.clear();
        glfwPollEvents();
        pollGamepads();
    }

    const std::vector<Event>& events() const override {
        return eventsBuffer;
    }

    void clearEvents() override {
        eventsBuffer.clear();
    }

    bool shouldClose() const override {
        return window && glfwWindowShouldClose(window);
    }

    void requestClose() override {
        if (window) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    void swapBuffers() override {
        if (window) {
#if defined(KARMA_RENDER_BACKEND_DILIGENT)
            return;
#else
            glfwSwapBuffers(window);
#endif
        }
    }

    void setVsync(bool enabled) override {
#if defined(KARMA_RENDER_BACKEND_DILIGENT)
        (void)enabled;
        return;
#else
        glfwSwapInterval(enabled ? 1 : 0);
#endif
    }

    void setFullscreen(bool enabled) override {
        if (!window) {
            return;
        }
        if (enabled == fullscreen) {
            return;
        }
        if (enabled) {
            glfwGetWindowPos(window, &windowedX, &windowedY);
            glfwGetWindowSize(window, &windowedW, &windowedH);

            GLFWmonitor *monitor = glfwGetWindowMonitor(window);
            if (!monitor) {
                monitor = glfwGetPrimaryMonitor();
            }

            const GLFWvidmode *mode = glfwGetVideoMode(monitor);
            if (!mode) {
                return;
            }

            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            fullscreen = true;
        } else {
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedW, windowedH, 0);
            fullscreen = false;
        }
        refreshCachedWindowState();
    }

    bool isFullscreen() const override {
        return fullscreen;
    }

    void getFramebufferSize(int &width, int &height) const override {
        width = framebufferW;
        height = framebufferH;
    }

    void getLogicalSize(int &width, int &height) const override {
        width = windowW;
        height = windowH;
    }

    float getContentScale() const override {
        return static_cast<float>(framebufferScaleX());
    }

    void getContentScale(float& scale_x, float& scale_y) const override {
        scale_x = static_cast<float>(framebufferScaleX());
        scale_y = static_cast<float>(framebufferScaleY());
    }

    bool isKeyDown(Key key) const override {
        if (!window) {
            return false;
        }
        const int glfwKey = toGlfwKey(key);
        if (glfwKey == GLFW_KEY_UNKNOWN) {
            return false;
        }
        return glfwGetKey(window, glfwKey) == GLFW_PRESS;
    }

    bool isMouseDown(MouseButton button) const override {
        if (!window) {
            return false;
        }
        return glfwGetMouseButton(window, toGlfwMouseButton(button)) == GLFW_PRESS;
    }

    bool isGamepadButtonDown(GamepadButton button, int gamepad = -1) const override {
        const int glfw_button = toGlfwGamepadButton(button);
        if (glfw_button < 0) {
            return false;
        }
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (gamepad >= 0 && jid != gamepad) {
                continue;
            }
            const auto& state = gamepads[static_cast<std::size_t>(jid)];
            if (state.connected && state.buttons[static_cast<std::size_t>(glfw_button)] != 0) {
                return true;
            }
        }
        return false;
    }

    float gamepadAxis(GamepadAxis axis, int gamepad = -1) const override {
        const int glfw_axis = toGlfwGamepadAxis(axis);
        if (glfw_axis < 0) {
            return 0.0f;
        }
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (gamepad >= 0 && jid != gamepad) {
                continue;
            }
            const auto& state = gamepads[static_cast<std::size_t>(jid)];
            if (state.connected) {
                return state.axes[static_cast<std::size_t>(glfw_axis)];
            }
        }
        return 0.0f;
    }

    void setCursorVisible(bool visible) override {
        if (!window) {
            return;
        }
        glfwSetInputMode(window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    }

    void setCursorShape(CursorShape shape) override {
        if (!window || shape == cursorShape) {
            return;
        }
        if (shape == CursorShape::Default) {
            glfwSetCursor(window, nullptr);
            cursorShape = shape;
            return;
        }
        int glfw_shape = GLFW_ARROW_CURSOR;
        switch (shape) {
            case CursorShape::Pointer: glfw_shape = GLFW_HAND_CURSOR; break;
            case CursorShape::Text: glfw_shape = GLFW_IBEAM_CURSOR; break;
            case CursorShape::Crosshair: glfw_shape = GLFW_CROSSHAIR_CURSOR; break;
            case CursorShape::ResizeHorizontal: glfw_shape = GLFW_HRESIZE_CURSOR; break;
            case CursorShape::ResizeVertical: glfw_shape = GLFW_VRESIZE_CURSOR; break;
#if defined(GLFW_RESIZE_ALL_CURSOR)
            case CursorShape::Move: glfw_shape = GLFW_RESIZE_ALL_CURSOR; break;
#endif
#if defined(GLFW_RESIZE_NWSE_CURSOR)
            case CursorShape::ResizeDiagonalNwSe: glfw_shape = GLFW_RESIZE_NWSE_CURSOR; break;
#endif
#if defined(GLFW_RESIZE_NESW_CURSOR)
            case CursorShape::ResizeDiagonalNeSw: glfw_shape = GLFW_RESIZE_NESW_CURSOR; break;
#endif
#if defined(GLFW_NOT_ALLOWED_CURSOR)
            case CursorShape::NotAllowed: glfw_shape = GLFW_NOT_ALLOWED_CURSOR; break;
#endif
            default: glfw_shape = GLFW_ARROW_CURSOR; break;
        }
        GLFWcursor* requested = cursors.getOrCreate(
            glfw_shape, [](int native_shape) {
                return glfwCreateStandardCursor(native_shape);
            });
        if (!requested) {
            return;
        }
        glfwSetCursor(window, requested);
        cursorShape = shape;
    }

    void setIcon(const std::string& path) override {
        if (!window || path.empty()) {
            return;
        }
        spdlog::warn("Karma: GLFW window icon not implemented (requested '{}')", path);
    }

    void setClipboardText(std::string_view text) override {
        if (!window) {
            return;
        }
        glfwSetClipboardString(window, std::string(text).c_str());
    }

    std::string getClipboardText() const override {
        if (!window) {
            return {};
        }
        const char *text = glfwGetClipboardString(window);
        return text ? std::string(text) : std::string();
    }

    void* nativeHandle() const override {
        return window;
    }

private:
    struct CachedGamepadState {
        bool connected = false;
        std::array<unsigned char, GLFW_GAMEPAD_BUTTON_LAST + 1> buttons{};
        std::array<float, GLFW_GAMEPAD_AXIS_LAST + 1> axes{};
    };

    GLFWwindow *window = nullptr;
    detail::StandardCursorCache<int, GLFWcursor*> cursors;
    std::vector<Event> eventsBuffer;
    std::array<CachedGamepadState, GLFW_JOYSTICK_LAST + 1> gamepads{};
    detail::GamepadRepeatScheduler gamepadRepeats;
    bool fullscreen = false;
    bool glfwAcquired = false;
    CursorShape cursorShape = CursorShape::Default;
    int windowedX = 0;
    int windowedY = 0;
    int windowedW = 1280;
    int windowedH = 720;
    int windowW = 1280;
    int windowH = 720;
    int framebufferW = 0;
    int framebufferH = 0;
    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
    double cursorX = 0.0;
    double cursorY = 0.0;

    void refreshCachedWindowState() {
        if (!window) {
            windowW = 0;
            windowH = 0;
            framebufferW = 0;
            framebufferH = 0;
            contentScaleX = 1.0f;
            contentScaleY = 1.0f;
            cursorX = 0.0;
            cursorY = 0.0;
            return;
        }
        glfwGetWindowSize(window, &windowW, &windowH);
        glfwGetFramebufferSize(window, &framebufferW, &framebufferH);
        glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
        glfwGetCursorPos(window, &cursorX, &cursorY);
    }

    double framebufferScaleX() const {
        return (windowW > 0) ? static_cast<double>(framebufferW) / static_cast<double>(windowW)
                             : 1.0;
    }

    double framebufferScaleY() const {
        return (windowH > 0) ? static_cast<double>(framebufferH) / static_cast<double>(windowH)
                             : 1.0;
    }

    void appendResizeEvent() {
        Event ev;
        ev.type = EventType::WindowResize;
        ev.width = framebufferW;
        ev.height = framebufferH;
        ev.logicalWidth = windowW;
        ev.logicalHeight = windowH;
        ev.framebufferWidth = framebufferW;
        ev.framebufferHeight = framebufferH;
        ev.scaleX = static_cast<float>(framebufferScaleX());
        ev.scaleY = static_cast<float>(framebufferScaleY());
        eventsBuffer.push_back(ev);
    }

    void pollGamepads() {
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            auto& cached = gamepads[static_cast<std::size_t>(jid)];
            const bool connected = glfwJoystickIsGamepad(jid) == GLFW_TRUE;
            if (connected != cached.connected) {
                Event event;
                event.type = connected ? EventType::GamepadConnected
                                       : EventType::GamepadDisconnected;
                event.gamepad = jid;
                eventsBuffer.push_back(event);
                cached = {};
                cached.connected = connected;
                gamepadRepeats.resetGamepad(jid);
            }
            if (!connected) {
                continue;
            }

            GLFWgamepadstate current{};
            if (glfwGetGamepadState(jid, &current) != GLFW_TRUE) {
                continue;
            }
            for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
                const unsigned char pressed =
                    current.buttons[button] == GLFW_PRESS ? 1u : 0u;
                if (pressed == cached.buttons[static_cast<std::size_t>(button)]) {
                    continue;
                }
                cached.buttons[static_cast<std::size_t>(button)] = pressed;
                const GamepadButton normalized_button = toGamepadButton(button);
                gamepadRepeats.buttonChanged(jid, normalized_button, pressed != 0u);
                Event event;
                event.type = pressed ? EventType::GamepadButtonDown
                                     : EventType::GamepadButtonUp;
                event.gamepad = jid;
                event.gamepadButton = normalized_button;
                eventsBuffer.push_back(event);
            }
            for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
                const float value = normalizeGamepadAxis(axis, current.axes[axis]);
                float& previous = cached.axes[static_cast<std::size_t>(axis)];
                if (std::abs(value - previous) < 0.002f) {
                    continue;
                }
                previous = value;
                const GamepadAxis normalized_axis = toGamepadAxis(axis);
                gamepadRepeats.axisChanged(jid, normalized_axis, value);
                Event event;
                event.type = EventType::GamepadAxisMotion;
                event.gamepad = jid;
                event.gamepadAxis = normalized_axis;
                event.gamepadValue = value;
                eventsBuffer.push_back(event);
            }
        }
        gamepadRepeats.appendDue(eventsBuffer);
    }

    void setupCallbacks() {
        glfwSetKeyCallback(window, [](GLFWwindow *w, int key, int, int action, int mods) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.key = toKey(key);
            ev.mods = toModifiers(mods);
            ev.repeat = action == GLFW_REPEAT;
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                ev.type = EventType::KeyDown;
            } else if (action == GLFW_RELEASE) {
                ev.type = EventType::KeyUp;
            } else {
                return;
            }
            self->eventsBuffer.push_back(ev);
        });

        glfwSetCharCallback(window, [](GLFWwindow *w, unsigned int codepoint) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.type = EventType::TextInput;
            ev.codepoint = codepoint;
            self->eventsBuffer.push_back(ev);
        });

        glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int button, int action, int mods) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.mouseButton = toMouseButton(button);
            ev.mods = toModifiers(mods);
            ev.type = (action == GLFW_PRESS) ? EventType::MouseButtonDown : EventType::MouseButtonUp;
            ev.x = self->cursorX;
            ev.y = self->cursorY;
            self->eventsBuffer.push_back(ev);
        });

        glfwSetCursorPosCallback(window, [](GLFWwindow *w, double xpos, double ypos) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.type = EventType::MouseMove;
            self->cursorX = xpos;
            self->cursorY = ypos;
            ev.x = xpos;
            ev.y = ypos;
            self->eventsBuffer.push_back(ev);
        });

        glfwSetScrollCallback(window, [](GLFWwindow *w, double xoffset, double yoffset) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.type = EventType::MouseScroll;
            ev.x = self->cursorX;
            ev.y = self->cursorY;
            ev.mods.shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                            glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            ev.mods.control = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                              glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            ev.mods.alt = glfwGetKey(w, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                          glfwGetKey(w, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            ev.mods.super = glfwGetKey(w, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                            glfwGetKey(w, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
            ev.scrollX = xoffset;
            ev.scrollY = yoffset;
            self->eventsBuffer.push_back(ev);
        });

        glfwSetWindowFocusCallback(window, [](GLFWwindow *w, int focused) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.type = EventType::WindowFocus;
            ev.focused = (focused == GLFW_TRUE);
            self->eventsBuffer.push_back(ev);
        });

        glfwSetWindowCloseCallback(window, [](GLFWwindow *w) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            Event ev;
            ev.type = EventType::WindowClose;
            self->eventsBuffer.push_back(ev);
        });

        glfwSetWindowSizeCallback(window, [](GLFWwindow *w, int width, int height) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            self->windowW = width;
            self->windowH = height;
            glfwGetFramebufferSize(w, &self->framebufferW, &self->framebufferH);
            self->appendResizeEvent();
        });

        glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int width, int height) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            self->framebufferW = width;
            self->framebufferH = height;
            glfwGetWindowSize(w, &self->windowW, &self->windowH);
            self->appendResizeEvent();
        });

        glfwSetWindowContentScaleCallback(window, [](GLFWwindow *w, float xscale, float yscale) {
            auto *self = static_cast<WindowGlfw *>(glfwGetWindowUserPointer(w));
            if (!self) {
                return;
            }
            self->contentScaleX = xscale;
            self->contentScaleY = yscale;
            self->appendResizeEvent();
        });
    }
};

} // namespace

std::unique_ptr<Window> createGlfwWindow(const WindowConfig& config) {
    auto window = std::make_unique<WindowGlfw>(config);
    if (window->nativeHandle() == nullptr) {
        return nullptr;
    }
    return window;
}

} // namespace karma::platform
