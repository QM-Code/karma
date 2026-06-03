#pragma once

namespace Diligent {
class IRenderDevice;
class IDeviceContext;
class ISwapChain;
}  // namespace Diligent

namespace karma::renderer_backend {
class Backend;

/// \ingroup karma_rendering
/// Returns the Diligent render device for a Diligent backend, or null otherwise.
Diligent::IRenderDevice* diligentRenderDevice(Backend* backend);
/// \ingroup karma_rendering
/// Returns the Diligent immediate device context for a Diligent backend.
Diligent::IDeviceContext* diligentDeviceContext(Backend* backend);
/// \ingroup karma_rendering
/// Returns the Diligent swapchain for a Diligent backend.
Diligent::ISwapChain* diligentSwapChain(Backend* backend);

}  // namespace karma::renderer_backend
