#include "karma/rendering/renderer/backends/diligent_access.hpp"

#include "backend.hpp"

namespace karma::renderer_backend {

Diligent::IRenderDevice* diligentRenderDevice(Backend* backend) {
  auto* diligent = dynamic_cast<DiligentBackend*>(backend);
  return diligent != nullptr ? diligent->getDevice() : nullptr;
}

Diligent::IDeviceContext* diligentDeviceContext(Backend* backend) {
  auto* diligent = dynamic_cast<DiligentBackend*>(backend);
  return diligent != nullptr ? diligent->getContext() : nullptr;
}

Diligent::ISwapChain* diligentSwapChain(Backend* backend) {
  auto* diligent = dynamic_cast<DiligentBackend*>(backend);
  return diligent != nullptr ? diligent->getSwapChain() : nullptr;
}

}  // namespace karma::renderer_backend
