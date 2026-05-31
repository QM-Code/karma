#pragma once

namespace Diligent {
class IRenderDevice;
class IDeviceContext;
class ISwapChain;
}  // namespace Diligent

namespace karma::renderer_backend {
class Backend;

Diligent::IRenderDevice* diligentRenderDevice(Backend* backend);
Diligent::IDeviceContext* diligentDeviceContext(Backend* backend);
Diligent::ISwapChain* diligentSwapChain(Backend* backend);

}  // namespace karma::renderer_backend
