#include "karma/rendering/renderer/backend.hpp"

#if defined(KARMA_RENDER_BACKEND_DILIGENT)
#include "backends/diligent/backend.hpp"
#endif

namespace karma::renderer_backend {

std::unique_ptr<Backend> CreateGraphicsBackend(
    karma::platform::Window& window,
    const renderer::GraphicsDeviceCreateInfo& create_info) {
#if defined(KARMA_RENDER_BACKEND_DILIGENT)
  return std::make_unique<DiligentBackend>(window, create_info);
#else
  (void)window;
  (void)create_info;
  return nullptr;
#endif
}

}  // namespace karma::renderer_backend
