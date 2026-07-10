#include "karma/app.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "karma/rendering.h"

#include "../../../third_party/stb_image.h"

namespace karma::app {

namespace {

constexpr std::size_t kMaxUITextureBytes = 256u * 1024u * 1024u;

bool validTextureDimensions(int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const std::size_t unsigned_width = static_cast<std::size_t>(width);
  const std::size_t unsigned_height = static_cast<std::size_t>(height);
  return unsigned_height <= kMaxUITextureBytes / 4u / unsigned_width;
}

void premultiplyRgba(unsigned char* pixels, int width, int height) {
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  for (std::size_t i = 0; i < pixel_count; ++i) {
    unsigned char* rgba = pixels + i * 4u;
    const std::uint16_t alpha = rgba[3];
    for (std::size_t channel = 0; channel < 3u; ++channel) {
      rgba[channel] = static_cast<unsigned char>(
          (static_cast<std::uint16_t>(rgba[channel]) * alpha + 127u) / 255u);
    }
  }
}

}  // namespace

UIContext::~UIContext() {
  destroyOwnedTextures();
}

UITextureHandle UIContext::createTextureRGBA8(int width,
                                              int height,
                                              const void* pixels) {
  if (!device_ || !validTextureDimensions(width, height)) {
    return 0;
  }
  const karma::rendering::TextureId id =
      device_->createTextureRGBA8(width, height, pixels);
  const UITextureHandle handle = static_cast<UITextureHandle>(id);
  if (handle != 0) {
    owned_textures_.insert(handle);
  }
  return handle;
}

UITexture UIContext::loadTextureRGBA8FromPng(const std::filesystem::path& path,
                                             bool premultiply_alpha) {
  if (!device_ || path.empty()) {
    return {};
  }

  const std::string path_string = path.string();
  int width = 0;
  int height = 0;
  int comp = 0;
  if (stbi_info(path_string.c_str(), &width, &height, &comp) == 0 ||
      !validTextureDimensions(width, height)) {
    return {};
  }

  stbi_set_flip_vertically_on_load_thread(0);
  unsigned char* pixels = stbi_load(path_string.c_str(), &width, &height, &comp, 4);
  if (!pixels || !validTextureDimensions(width, height)) {
    if (pixels) {
      stbi_image_free(pixels);
    }
    return {};
  }
  if (premultiply_alpha) {
    premultiplyRgba(pixels, width, height);
  }

  const UITextureHandle handle = createTextureRGBA8(width, height, pixels);
  stbi_image_free(pixels);
  if (handle == 0) {
    return {};
  }

  return UITexture{.handle = handle, .width = width, .height = height};
}

void UIContext::updateTextureRGBA8(UITextureHandle texture,
                                   int width,
                                   int height,
                                   const void* pixels) {
  if (!device_ || !owned_textures_.contains(texture) ||
      !validTextureDimensions(width, height)) {
    return;
  }
  device_->updateTextureRGBA8(static_cast<karma::rendering::TextureId>(texture),
                              width,
                              height,
                              pixels);
}

void UIContext::destroyTexture(UITextureHandle texture) {
  if (texture == 0 || owned_textures_.erase(texture) == 0u) {
    return;
  }
  if (!device_) {
    return;
  }
  device_->destroyTexture(static_cast<karma::rendering::TextureId>(texture));
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
  if (input_ == nullptr) {
    throw std::logic_error("UIContext is not attached to an active UI frame.");
  }
  return *input_;
}

}  // namespace karma::app
