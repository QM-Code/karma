#include "karma/app/ui_context.h"

#include "karma/input/input_system.h"
#include "karma/renderer/device.h"

namespace karma::app {

UITextureHandle UIContext::createTextureRGBA8(int w, int h, const void* pixels) {
  if (!device_ || w <= 0 || h <= 0) {
    return 0;
  }
  const karma::renderer::TextureId id = device_->createTextureRGBA8(w, h, pixels);
  return static_cast<UITextureHandle>(id);
}

void UIContext::updateTextureRGBA8(UITextureHandle tex, int w, int h, const void* pixels) {
  if (!device_ || tex == 0 || w <= 0 || h <= 0) {
    return;
  }
  device_->updateTextureRGBA8(static_cast<karma::renderer::TextureId>(tex), w, h, pixels);
}

void UIContext::destroyTexture(UITextureHandle tex) {
  if (!device_ || tex == 0) {
    return;
  }
  device_->destroyTexture(static_cast<karma::renderer::TextureId>(tex));
}

karma::input::InputSystem& UIContext::input() {
  return *input_;
}

}  // namespace karma::app
