#include "karma/rendering/renderer/backend.hpp"

#if defined(KARMA_RENDER_BACKEND_DILIGENT)
#include "backends/diligent/backend.hpp"
#endif

namespace karma::renderer_backend {

std::unique_ptr<Backend> CreateGraphicsBackend(karma::platform::Window& window) {
#if defined(KARMA_RENDER_BACKEND_DILIGENT)
  return std::make_unique<DiligentBackend>(window);
#else
  (void)window;
  return nullptr;
#endif
}

}  // namespace karma::renderer_backend
