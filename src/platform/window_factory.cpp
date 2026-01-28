#include "karma/platform/window.h"

namespace karma::platform {

std::unique_ptr<Window> CreateWindow(const WindowConfig &config) {
#if defined(BZ3_WINDOW_BACKEND_SDL)
    return CreateSdlWindow(config);
#else
    return CreateGlfwWindow(config);
#endif
}

} // namespace karma::platform
