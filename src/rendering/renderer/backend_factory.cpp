#include "private/rendering/backend.hpp"

#if defined(KARMA_RENDER_BACKEND_DILIGENT)
#include "backends/diligent/backend.hpp"
#endif

namespace karma::rendering::backend {

std::unique_ptr<Backend> CreateGraphicsBackend(
    karma::platform::Window& window,
    const rendering::GraphicsDeviceCreateInfo& create_info) {
#if defined(KARMA_RENDER_BACKEND_DILIGENT)
  return std::make_unique<DiligentBackend>(window, create_info);
#else
  (void)window;
  (void)create_info;
  return nullptr;
#endif
}

}  // namespace karma::rendering::backend
