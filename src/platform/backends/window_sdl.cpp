#include "karma/platform/window.h"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <string>

namespace karma::platform {
namespace {

Key toKey(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_A: return Key::A;
        case SDL_SCANCODE_B: return Key::B;
        case SDL_SCANCODE_C: return Key::C;
        case SDL_SCANCODE_D: return Key::D;
        case SDL_SCANCODE_E: return Key::E;
        case SDL_SCANCODE_F: return Key::F;
        case SDL_SCANCODE_G: return Key::G;
        case SDL_SCANCODE_H: return Key::H;
        case SDL_SCANCODE_I: return Key::I;
        case SDL_SCANCODE_J: return Key::J;
        case SDL_SCANCODE_K: return Key::K;
        case SDL_SCANCODE_L: return Key::L;
        case SDL_SCANCODE_M: return Key::M;
        case SDL_SCANCODE_N: return Key::N;
        case SDL_SCANCODE_O: return Key::O;
        case SDL_SCANCODE_P: return Key::P;
        case SDL_SCANCODE_Q: return Key::Q;
        case SDL_SCANCODE_R: return Key::R;
        case SDL_SCANCODE_S: return Key::S;
        case SDL_SCANCODE_T: return Key::T;
        case SDL_SCANCODE_U: return Key::U;
        case SDL_SCANCODE_V: return Key::V;
        case SDL_SCANCODE_W: return Key::W;
        case SDL_SCANCODE_X: return Key::X;
        case SDL_SCANCODE_Y: return Key::Y;
        case SDL_SCANCODE_Z: return Key::Z;
        case SDL_SCANCODE_0: return Key::Num0;
        case SDL_SCANCODE_1: return Key::Num1;
        case SDL_SCANCODE_2: return Key::Num2;
        case SDL_SCANCODE_3: return Key::Num3;
        case SDL_SCANCODE_4: return Key::Num4;
        case SDL_SCANCODE_5: return Key::Num5;
        case SDL_SCANCODE_6: return Key::Num6;
        case SDL_SCANCODE_7: return Key::Num7;
        case SDL_SCANCODE_8: return Key::Num8;
        case SDL_SCANCODE_9: return Key::Num9;
        case SDL_SCANCODE_F1: return Key::F1;
        case SDL_SCANCODE_F2: return Key::F2;
        case SDL_SCANCODE_F3: return Key::F3;
        case SDL_SCANCODE_F4: return Key::F4;
        case SDL_SCANCODE_F5: return Key::F5;
        case SDL_SCANCODE_F6: return Key::F6;
        case SDL_SCANCODE_F7: return Key::F7;
        case SDL_SCANCODE_F8: return Key::F8;
        case SDL_SCANCODE_F9: return Key::F9;
        case SDL_SCANCODE_F10: return Key::F10;
        case SDL_SCANCODE_F11: return Key::F11;
        case SDL_SCANCODE_F12: return Key::F12;
        case SDL_SCANCODE_F13: return Key::F13;
        case SDL_SCANCODE_F14: return Key::F14;
        case SDL_SCANCODE_F15: return Key::F15;
        case SDL_SCANCODE_F16: return Key::F16;
        case SDL_SCANCODE_F17: return Key::F17;
        case SDL_SCANCODE_F18: return Key::F18;
        case SDL_SCANCODE_F19: return Key::F19;
        case SDL_SCANCODE_F20: return Key::F20;
        case SDL_SCANCODE_F21: return Key::F21;
        case SDL_SCANCODE_F22: return Key::F22;
        case SDL_SCANCODE_F23: return Key::F23;
        case SDL_SCANCODE_F24: return Key::F24;
        case SDL_SCANCODE_SPACE: return Key::Space;
        case SDL_SCANCODE_ESCAPE: return Key::Escape;
        case SDL_SCANCODE_RETURN: return Key::Enter;
        case SDL_SCANCODE_TAB: return Key::Tab;
        case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
        case SDL_SCANCODE_LEFT: return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;
        case SDL_SCANCODE_UP: return Key::Up;
        case SDL_SCANCODE_DOWN: return Key::Down;
        case SDL_SCANCODE_LEFTBRACKET: return Key::LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return Key::RightBracket;
        case SDL_SCANCODE_MINUS: return Key::Minus;
        case SDL_SCANCODE_EQUALS: return Key::Equal;
        case SDL_SCANCODE_APOSTROPHE: return Key::Apostrophe;
        case SDL_SCANCODE_GRAVE: return Key::GraveAccent;
        case SDL_SCANCODE_LSHIFT: return Key::LeftShift;
        case SDL_SCANCODE_RSHIFT: return Key::RightShift;
        case SDL_SCANCODE_LCTRL: return Key::LeftControl;
        case SDL_SCANCODE_RCTRL: return Key::RightControl;
        case SDL_SCANCODE_LALT: return Key::LeftAlt;
        case SDL_SCANCODE_RALT: return Key::RightAlt;
        case SDL_SCANCODE_LGUI: return Key::LeftSuper;
        case SDL_SCANCODE_RGUI: return Key::RightSuper;
        case SDL_SCANCODE_MENU: return Key::Menu;
        case SDL_SCANCODE_HOME: return Key::Home;
        case SDL_SCANCODE_END: return Key::End;
        case SDL_SCANCODE_PAGEUP: return Key::PageUp;
        case SDL_SCANCODE_PAGEDOWN: return Key::PageDown;
        case SDL_SCANCODE_INSERT: return Key::Insert;
        case SDL_SCANCODE_DELETE: return Key::Delete;
        case SDL_SCANCODE_CAPSLOCK: return Key::CapsLock;
        case SDL_SCANCODE_NUMLOCKCLEAR: return Key::NumLock;
        case SDL_SCANCODE_SCROLLLOCK: return Key::ScrollLock;
        default: return Key::Unknown;
    }
}

SDL_Scancode toScancode(Key key) {
    switch (key) {
        case Key::A: return SDL_SCANCODE_A;
        case Key::B: return SDL_SCANCODE_B;
        case Key::C: return SDL_SCANCODE_C;
        case Key::D: return SDL_SCANCODE_D;
        case Key::E: return SDL_SCANCODE_E;
        case Key::F: return SDL_SCANCODE_F;
        case Key::G: return SDL_SCANCODE_G;
        case Key::H: return SDL_SCANCODE_H;
        case Key::I: return SDL_SCANCODE_I;
        case Key::J: return SDL_SCANCODE_J;
        case Key::K: return SDL_SCANCODE_K;
        case Key::L: return SDL_SCANCODE_L;
        case Key::M: return SDL_SCANCODE_M;
        case Key::N: return SDL_SCANCODE_N;
        case Key::O: return SDL_SCANCODE_O;
        case Key::P: return SDL_SCANCODE_P;
        case Key::Q: return SDL_SCANCODE_Q;
        case Key::R: return SDL_SCANCODE_R;
        case Key::S: return SDL_SCANCODE_S;
        case Key::T: return SDL_SCANCODE_T;
        case Key::U: return SDL_SCANCODE_U;
        case Key::V: return SDL_SCANCODE_V;
        case Key::W: return SDL_SCANCODE_W;
        case Key::X: return SDL_SCANCODE_X;
        case Key::Y: return SDL_SCANCODE_Y;
        case Key::Z: return SDL_SCANCODE_Z;
        case Key::Num0: return SDL_SCANCODE_0;
        case Key::Num1: return SDL_SCANCODE_1;
        case Key::Num2: return SDL_SCANCODE_2;
        case Key::Num3: return SDL_SCANCODE_3;
        case Key::Num4: return SDL_SCANCODE_4;
        case Key::Num5: return SDL_SCANCODE_5;
        case Key::Num6: return SDL_SCANCODE_6;
        case Key::Num7: return SDL_SCANCODE_7;
        case Key::Num8: return SDL_SCANCODE_8;
        case Key::Num9: return SDL_SCANCODE_9;
        case Key::F1: return SDL_SCANCODE_F1;
        case Key::F2: return SDL_SCANCODE_F2;
        case Key::F3: return SDL_SCANCODE_F3;
        case Key::F4: return SDL_SCANCODE_F4;
        case Key::F5: return SDL_SCANCODE_F5;
        case Key::F6: return SDL_SCANCODE_F6;
        case Key::F7: return SDL_SCANCODE_F7;
        case Key::F8: return SDL_SCANCODE_F8;
        case Key::F9: return SDL_SCANCODE_F9;
        case Key::F10: return SDL_SCANCODE_F10;
        case Key::F11: return SDL_SCANCODE_F11;
        case Key::F12: return SDL_SCANCODE_F12;
        case Key::F13: return SDL_SCANCODE_F13;
        case Key::F14: return SDL_SCANCODE_F14;
        case Key::F15: return SDL_SCANCODE_F15;
        case Key::F16: return SDL_SCANCODE_F16;
        case Key::F17: return SDL_SCANCODE_F17;
        case Key::F18: return SDL_SCANCODE_F18;
        case Key::F19: return SDL_SCANCODE_F19;
        case Key::F20: return SDL_SCANCODE_F20;
        case Key::F21: return SDL_SCANCODE_F21;
        case Key::F22: return SDL_SCANCODE_F22;
        case Key::F23: return SDL_SCANCODE_F23;
        case Key::F24: return SDL_SCANCODE_F24;
        case Key::Space: return SDL_SCANCODE_SPACE;
        case Key::Escape: return SDL_SCANCODE_ESCAPE;
        case Key::Enter: return SDL_SCANCODE_RETURN;
        case Key::Tab: return SDL_SCANCODE_TAB;
        case Key::Backspace: return SDL_SCANCODE_BACKSPACE;
        case Key::Left: return SDL_SCANCODE_LEFT;
        case Key::Right: return SDL_SCANCODE_RIGHT;
        case Key::Up: return SDL_SCANCODE_UP;
        case Key::Down: return SDL_SCANCODE_DOWN;
        case Key::LeftBracket: return SDL_SCANCODE_LEFTBRACKET;
        case Key::RightBracket: return SDL_SCANCODE_RIGHTBRACKET;
        case Key::Minus: return SDL_SCANCODE_MINUS;
        case Key::Equal: return SDL_SCANCODE_EQUALS;
        case Key::Apostrophe: return SDL_SCANCODE_APOSTROPHE;
        case Key::GraveAccent: return SDL_SCANCODE_GRAVE;
        case Key::LeftShift: return SDL_SCANCODE_LSHIFT;
        case Key::RightShift: return SDL_SCANCODE_RSHIFT;
        case Key::LeftControl: return SDL_SCANCODE_LCTRL;
        case Key::RightControl: return SDL_SCANCODE_RCTRL;
        case Key::LeftAlt: return SDL_SCANCODE_LALT;
        case Key::RightAlt: return SDL_SCANCODE_RALT;
        case Key::LeftSuper: return SDL_SCANCODE_LGUI;
        case Key::RightSuper: return SDL_SCANCODE_RGUI;
        case Key::Menu: return SDL_SCANCODE_MENU;
        case Key::Home: return SDL_SCANCODE_HOME;
        case Key::End: return SDL_SCANCODE_END;
        case Key::PageUp: return SDL_SCANCODE_PAGEUP;
        case Key::PageDown: return SDL_SCANCODE_PAGEDOWN;
        case Key::Insert: return SDL_SCANCODE_INSERT;
        case Key::Delete: return SDL_SCANCODE_DELETE;
        case Key::CapsLock: return SDL_SCANCODE_CAPSLOCK;
        case Key::NumLock: return SDL_SCANCODE_NUMLOCKCLEAR;
        case Key::ScrollLock: return SDL_SCANCODE_SCROLLLOCK;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

MouseButton toMouseButton(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return MouseButton::Left;
        case SDL_BUTTON_RIGHT: return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1: return MouseButton::Button4;
        case SDL_BUTTON_X2: return MouseButton::Button5;
        default: return MouseButton::Left;
    }
}

uint8_t toSdlButton(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return SDL_BUTTON_LEFT;
        case MouseButton::Right: return SDL_BUTTON_RIGHT;
        case MouseButton::Middle: return SDL_BUTTON_MIDDLE;
        case MouseButton::Button4: return SDL_BUTTON_X1;
        case MouseButton::Button5: return SDL_BUTTON_X2;
        case MouseButton::Button6: return SDL_BUTTON_X2 + 1;
        case MouseButton::Button7: return SDL_BUTTON_X2 + 2;
        case MouseButton::Button8: return SDL_BUTTON_X2 + 3;
        default: return SDL_BUTTON_LEFT;
    }
}

Modifiers toModifiers(SDL_Keymod mods) {
    Modifiers out;
    out.shift = (mods & SDL_KMOD_SHIFT) != 0;
    out.control = (mods & SDL_KMOD_CTRL) != 0;
    out.alt = (mods & SDL_KMOD_ALT) != 0;
    out.super = (mods & SDL_KMOD_GUI) != 0;
    return out;
}

uint32_t DecodeUtf8(const char *&p) {
    const unsigned char c = static_cast<unsigned char>(*p++);
    if (c < 0x80) {
        return c;
    }
    if ((c >> 5) == 0x6) {
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        return ((c & 0x1f) << 6) | (c2 & 0x3f);
    }
    if ((c >> 4) == 0xe) {
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        const unsigned char c3 = static_cast<unsigned char>(*p++);
        return ((c & 0x0f) << 12) | ((c2 & 0x3f) << 6) | (c3 & 0x3f);
    }
    if ((c >> 3) == 0x1e) {
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        const unsigned char c3 = static_cast<unsigned char>(*p++);
        const unsigned char c4 = static_cast<unsigned char>(*p++);
        return ((c & 0x07) << 18) | ((c2 & 0x3f) << 12) | ((c3 & 0x3f) << 6) | (c4 & 0x3f);
    }
    return 0;
}

void AppendTextInputEvents(std::vector<Event> &buffer, const char *text) {
    if (!text) {
        return;
    }
    const char *p = text;
    while (*p) {
        const char *start = p;
        uint32_t codepoint = DecodeUtf8(p);
        if (codepoint == 0 && *start == '\0') {
            break;
        }
        Event ev;
        ev.type = EventType::TextInput;
        ev.codepoint = codepoint;
        buffer.push_back(ev);
    }
}

} // namespace

class WindowSdl final : public Window {
public:
    explicit WindowSdl(const WindowConfig &config) {
        const bool sdlInitOk = SDL_Init(SDL_INIT_VIDEO);
        spdlog::info("SDL_Init(SDL_INIT_VIDEO) returned {}", sdlInitOk ? 1 : 0);
        if (!sdlInitOk) {
            spdlog::error("SDL failed to initialize: {}", SDL_GetError());
            spdlog::error("SDL video driver: {}", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");
            return;
        }
        spdlog::info("SDL video driver: {}", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, config.glMajor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, config.glMinor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, config.glCoreProfile ? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, config.samples > 0 ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config.samples);

        window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!window) {
            spdlog::error("SDL window failed to create: {}", SDL_GetError());
            SDL_Quit();
            return;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            spdlog::error("SDL GL context failed to create: {}", SDL_GetError());
            SDL_DestroyWindow(window);
            window = nullptr;
            SDL_Quit();
            return;
        }

        SDL_GL_MakeCurrent(window, glContext);
        SDL_StartTextInput(window);
    }

    ~WindowSdl() override {
        if (window) {
            SDL_StopTextInput(window);
        }
        if (glContext) {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }

    void pollEvents() override {
        eventsBuffer.clear();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: {
                    closeRequested = true;
                    Event ev;
                    ev.type = EventType::WindowClose;
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    closeRequested = true;
                    Event ev;
                    ev.type = EventType::WindowClose;
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST: {
                    Event ev;
                    ev.type = EventType::WindowFocus;
                    ev.focused = (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                    Event ev;
                    ev.type = EventType::WindowResize;
                    int fbW = 0;
                    int fbH = 0;
                    SDL_GetWindowSizeInPixels(window, &fbW, &fbH);
                    ev.width = fbW;
                    ev.height = fbH;
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    Event ev;
                    ev.type = (event.type == SDL_EVENT_KEY_DOWN) ? EventType::KeyDown : EventType::KeyUp;
                    ev.key = toKey(event.key.scancode);
                    ev.mods = toModifiers(event.key.mod);
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    AppendTextInputEvents(eventsBuffer, event.text.text);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    Event ev;
                    ev.type = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? EventType::MouseButtonDown : EventType::MouseButtonUp;
                    ev.mouseButton = toMouseButton(event.button.button);
                    ev.mods = toModifiers(SDL_GetModState());
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION: {
                    Event ev;
                    ev.type = EventType::MouseMove;
                    ev.mods = toModifiers(SDL_GetModState());
                    int winW = 0;
                    int winH = 0;
                    int fbW = 0;
                    int fbH = 0;
                    SDL_GetWindowSize(window, &winW, &winH);
                    SDL_GetWindowSizeInPixels(window, &fbW, &fbH);
                    const double scaleX = (winW > 0) ? static_cast<double>(fbW) / static_cast<double>(winW) : 1.0;
                    const double scaleY = (winH > 0) ? static_cast<double>(fbH) / static_cast<double>(winH) : 1.0;
                    ev.x = event.motion.x * scaleX;
                    ev.y = event.motion.y * scaleY;
                    eventsBuffer.push_back(ev);
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    Event ev;
                    ev.type = EventType::MouseScroll;
                    ev.mods = toModifiers(SDL_GetModState());
                    ev.scrollX = event.wheel.x;
                    ev.scrollY = event.wheel.y;
                    eventsBuffer.push_back(ev);
                    break;
                }
                default:
                    break;
            }
        }
    }

    const std::vector<Event>& events() const override {
        return eventsBuffer;
    }

    void clearEvents() override {
        eventsBuffer.clear();
    }

    bool shouldClose() const override {
        return closeRequested || window == nullptr;
    }

    void requestClose() override {
        closeRequested = true;
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    }

    void swapBuffers() override {
        if (window) {
            SDL_GL_SwapWindow(window);
        }
    }

    void setVsync(bool enabled) override {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }

    void setFullscreen(bool enabled) override {
        if (!window) {
            return;
        }
        if (enabled == fullscreen) {
            return;
        }
        if (enabled) {
            SDL_GetWindowPosition(window, &windowedX, &windowedY);
            SDL_GetWindowSize(window, &windowedW, &windowedH);
            SDL_SetWindowFullscreen(window, true);
            fullscreen = true;
        } else {
            SDL_SetWindowFullscreen(window, false);
            SDL_SetWindowPosition(window, windowedX, windowedY);
            SDL_SetWindowSize(window, windowedW, windowedH);
            fullscreen = false;
        }
    }

    bool isFullscreen() const override {
        return fullscreen;
    }

    void getFramebufferSize(int &width, int &height) const override {
        if (!window) {
            width = 0;
            height = 0;
            return;
        }
        SDL_GetWindowSizeInPixels(window, &width, &height);
    }

    float getContentScale() const override {
        if (!window) {
            return 1.0f;
        }
        int winW = 0;
        int winH = 0;
        int fbW = 0;
        int fbH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GetWindowSizeInPixels(window, &fbW, &fbH);
        if (winW <= 0) {
            return 1.0f;
        }
        return static_cast<float>(fbW) / static_cast<float>(winW);
    }

    bool isKeyDown(Key key) const override {
        if (!window) {
            return false;
        }
        const SDL_Scancode sc = toScancode(key);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            return false;
        }
        int count = 0;
        const bool *state = SDL_GetKeyboardState(&count);
        if (!state || sc >= count) {
            return false;
        }
        return state[sc];
    }

    bool isMouseDown(MouseButton button) const override {
        if (!window) {
            return false;
        }
        float x = 0.0f;
        float y = 0.0f;
        const uint32_t mask = SDL_GetMouseState(&x, &y);
        return (mask & SDL_BUTTON_MASK(toSdlButton(button))) != 0;
    }

    void setCursorVisible(bool visible) override {
        if (visible) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }

    void setIcon(const std::string& path) override {
        if (path.empty()) {
            return;
        }
        spdlog::warn("Karma: SDL window icon not implemented (requested '{}')", path);
    }

    void setClipboardText(std::string_view text) override {
        SDL_SetClipboardText(std::string(text).c_str());
    }

    std::string getClipboardText() const override {
        char *text = SDL_GetClipboardText();
        if (!text) {
            return {};
        }
        std::string out(text);
        SDL_free(text);
        return out;
    }

    void* nativeHandle() const override {
        return window;
    }

private:
    SDL_Window *window = nullptr;
    SDL_GLContext glContext = nullptr;
    std::vector<Event> eventsBuffer;
    bool fullscreen = false;
    bool closeRequested = false;
    int windowedX = 0;
    int windowedY = 0;
    int windowedW = 1280;
    int windowedH = 720;
};

} // namespace

std::unique_ptr<Window> CreateSdlWindow(const WindowConfig &config) {
    return std::make_unique<WindowSdl>(config);
}

} // namespace karma::platform
