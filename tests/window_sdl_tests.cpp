#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "karma/platform.h"

namespace {

bool hasEvent(const karma::platform::Window& window,
              karma::platform::EventType type,
              karma::platform::Key key = karma::platform::Key::Unknown) {
  return std::ranges::any_of(window.events(), [&](const karma::platform::Event& event) {
    return event.type == type && (key == karma::platform::Key::Unknown || event.key == key);
  });
}

}  // namespace

int main() {
  if (std::getenv("DISPLAY") == nullptr) {
    return 77;
  }

  karma::platform::WindowConfig config{};
  config.title = "Karma SDL routing A";
  config.width = 320;
  config.height = 200;
  auto first = karma::platform::createWindow(config);
  config.title = "Karma SDL routing B";
  auto second = karma::platform::createWindow(config);
  if (!first || !second) {
    return 77;
  }

  auto* first_native = static_cast<SDL_Window*>(first->nativeHandle());
  auto* second_native = static_cast<SDL_Window*>(second->nativeHandle());
  const SDL_WindowID first_id = SDL_GetWindowID(first_native);
  const SDL_WindowID second_id = SDL_GetWindowID(second_native);
  assert(first_id != 0 && second_id != 0 && first_id != second_id);

  first->pollEvents();
  first->clearEvents();
  second->pollEvents();
  second->clearEvents();

  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.windowID = second_id;
  key_event.key.scancode = SDL_SCANCODE_A;
  key_event.key.down = true;
  assert(SDL_PushEvent(&key_event));

  first->pollEvents();
  assert(!hasEvent(*first, karma::platform::EventType::KeyDown,
                   karma::platform::Key::A));
  second->pollEvents();
  assert(hasEvent(*second, karma::platform::EventType::KeyDown,
                  karma::platform::Key::A));

  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  close_event.window.windowID = first_id;
  assert(SDL_PushEvent(&close_event));
  second->pollEvents();
  assert(!second->shouldClose());
  first->pollEvents();
  assert(first->shouldClose());
  assert(hasEvent(*first, karma::platform::EventType::WindowClose));

  return 0;
}
