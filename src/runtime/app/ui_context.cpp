#include "karma/app.h"

#include <algorithm>

#include "karma/app.h"
#include "karma/rendering.h"

#include "../../../third_party/stb_image.h"

namespace karma::app {

UIContext::~UIContext() {
  destroyOwnedTextures();
}

UITextureHandle UIContext::createTextureRGBA8(int w, int h, const void* pixels) {
  if (!device_ || w <= 0 || h <= 0) {
    return 0;
  }
  const karma::rendering::TextureId id = device_->createTextureRGBA8(w, h, pixels);
  return static_cast<UITextureHandle>(id);
}

UITexture UIContext::loadTextureRGBA8FromPng(const std::filesystem::path& path) {
  if (!device_ || path.empty()) {
    return {};
  }

  int width = 0;
  int height = 0;
  int comp = 0;
  unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &comp, 4);
  if (!pixels || width <= 0 || height <= 0) {
    if (pixels) {
      stbi_image_free(pixels);
    }
    return {};
  }

  const UITextureHandle handle = createTextureRGBA8(width, height, pixels);
  stbi_image_free(pixels);
  if (handle == 0) {
    return {};
  }

  owned_textures_.push_back(handle);
  return UITexture{.handle = handle, .width = width, .height = height};
}

void UIContext::updateTextureRGBA8(UITextureHandle tex, int w, int h, const void* pixels) {
  if (!device_ || tex == 0 || w <= 0 || h <= 0) {
    return;
  }
  device_->updateTextureRGBA8(static_cast<karma::rendering::TextureId>(tex), w, h, pixels);
}

void UIContext::destroyTexture(UITextureHandle tex) {
  if (tex == 0) {
    return;
  }
  const auto it = std::remove(owned_textures_.begin(), owned_textures_.end(), tex);
  owned_textures_.erase(it, owned_textures_.end());
  if (!device_) {
    return;
  }
  device_->destroyTexture(static_cast<karma::rendering::TextureId>(tex));
}

void UIContext::destroyOwnedTextures() {
  if (device_) {
    for (const UITextureHandle tex : owned_textures_) {
      if (tex != 0) {
        device_->destroyTexture(static_cast<karma::rendering::TextureId>(tex));
      }
    }
  }
  owned_textures_.clear();
}

void UIContext::reset() {
  destroyOwnedTextures();
  frame_ = {};
  draw_data_.clear();
  input_ = nullptr;
  device_ = nullptr;
}

karma::app::InputSystem& UIContext::input() {
  return *input_;
}

}  // namespace karma::app
