#include "karma/platform.h"

namespace karma::platform {

std::unique_ptr<Window> createWindow(const WindowConfig& config) {
#if defined(KARMA_HEADLESS)
  (void)config;
  return nullptr;
#elif defined(KARMA_WINDOW_BACKEND_SDL)
  return createSdlWindow(config);
#else
  return createGlfwWindow(config);
#endif
}

}  // namespace karma::platform
