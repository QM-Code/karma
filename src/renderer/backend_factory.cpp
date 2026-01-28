#include "karma/renderer/backend.hpp"

#if defined(BZ3_RENDER_BACKEND_DILIGENT)
#include "karma/renderer/backends/diligent/backend.hpp"
#else
#error "Karma render backend not set. Define BZ3_RENDER_BACKEND_DILIGENT."
#endif

namespace karma::renderer_backend {

std::unique_ptr<Backend> CreateGraphicsBackend(karma::platform::Window& window) {
#if defined(BZ3_RENDER_BACKEND_DILIGENT)
  return std::make_unique<DiligentBackend>(window);
#else
  (void)window;
  return nullptr;
#endif
}

}  // namespace karma::renderer_backend
