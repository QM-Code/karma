#include "karma/renderer/backends/diligent/backend.hpp"

#include "backend_internal.h"

#include <assimp/scene.h>
#include <spdlog/spdlog.h>
#include <cstring>

#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>

namespace karma::renderer_backend {

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::createTextureSRV(
    const unsigned char* data,
    int width,
    int height,
    bool srgb,
    bool generate_mips,
    const char* name,
    Diligent::RefCntAutoPtr<Diligent::ITexture>& out_texture) {
  if (!device_ || !data || width <= 0 || height <= 0) {
    return {};
  }

  const unsigned char* upload_data = data;
  int upload_width = width;
  int upload_height = height;

  const bool should_gen_mips = generate_mips;

  Diligent::TextureDesc desc{};
  desc.Name = name;
  desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width = static_cast<Diligent::Uint32>(upload_width);
  desc.Height = static_cast<Diligent::Uint32>(upload_height);
  desc.MipLevels = should_gen_mips ? 0 : 1;
  desc.Format = srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB : Diligent::TEX_FORMAT_RGBA8_UNORM;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE |
                   (should_gen_mips ? Diligent::BIND_RENDER_TARGET : Diligent::BIND_NONE);
  desc.MiscFlags = should_gen_mips ? Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS
                                   : Diligent::MISC_TEXTURE_FLAG_NONE;

  Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
  if (should_gen_mips) {
    device_->CreateTexture(desc, nullptr, &texture);
    if (texture && context_) {
      Diligent::TextureSubResData subres{};
      subres.pData = upload_data;
      subres.Stride = static_cast<Diligent::Uint64>(upload_width * 4);
      Diligent::Box update_box{};
      update_box.MinX = 0;
      update_box.MinY = 0;
      update_box.MinZ = 0;
      update_box.MaxX = static_cast<Diligent::Uint32>(upload_width);
      update_box.MaxY = static_cast<Diligent::Uint32>(upload_height);
      update_box.MaxZ = 1;
      context_->UpdateTexture(texture,
                              0,
                              0,
                              update_box,
                              subres,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
  } else {
    Diligent::TextureData init_data{};
    Diligent::TextureSubResData subres{};
    subres.pData = upload_data;
    subres.Stride = static_cast<Diligent::Uint64>(upload_width * 4);
    init_data.pSubResources = &subres;
    init_data.NumSubresources = 1;
    device_->CreateTexture(desc, &init_data, &texture);
  }
  if (!texture) {
    return {};
  }
  out_texture = texture;
  auto* raw_view = texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  if (!raw_view) {
    spdlog::warn("Karma: Failed to get texture SRV for {}", name ? name : "texture");
    return {};
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  srv = raw_view;
  if (should_gen_mips && context_) {
    context_->GenerateMips(srv);
  }
  return srv;
}

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::createSolidTextureSRV(
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned char a,
    bool srgb,
    const char* name,
    Diligent::RefCntAutoPtr<Diligent::ITexture>& out_texture) {
  unsigned char pixel[4] = {r, g, b, a};
  return createTextureSRV(pixel, 1, 1, srgb, false, name, out_texture);
}

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::loadTextureFromAssimp(
    const aiScene& scene,
    const std::string& model_key,
    const std::filesystem::path& base_dir,
    const aiString& tex_path,
    bool srgb,
    const char* label) {
  if (tex_path.length == 0) {
    return {};
  }

  const std::string raw_key = tex_path.C_Str();
  const bool is_embedded = !raw_key.empty() && raw_key[0] == '*';
  const std::filesystem::path resolved_path = is_embedded ? std::filesystem::path{} : (base_dir / raw_key);
  std::string key;
  if (is_embedded) {
    key = model_key + ":" + raw_key;
  } else {
    key = resolved_path.string();
  }
  auto cache_it = texture_cache_.find(key);
  if (cache_it != texture_cache_.end()) {
    spdlog::warn("Karma: Texture cache hit {} '{}' key='{}'", label, raw_key, key);
    auto tex_it = textures_.find(cache_it->second);
    if (tex_it != textures_.end()) {
      return tex_it->second.srv;
    }
  }

  LoadedImage image{};
  if (is_embedded) {
    const int index = std::atoi(raw_key.c_str() + 1);
    if (index >= 0 && index < static_cast<int>(scene.mNumTextures)) {
      const aiTexture* embedded = scene.mTextures[index];
      if (embedded) {
        if (embedded->mHeight == 0) {
          image = loadImageFromMemory(reinterpret_cast<const unsigned char*>(embedded->pcData),
                                      embedded->mWidth);
        } else {
          image.width = static_cast<int>(embedded->mWidth);
          image.height = static_cast<int>(embedded->mHeight);
          image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
          std::memcpy(image.pixels.data(),
                      embedded->pcData,
                      image.pixels.size());
        }
      }
    }
  } else {
    image = loadImageFromFile(resolved_path);
  }

  if (image.pixels.empty()) {
    spdlog::warn("Karma: Missing {} texture '{}'", label, raw_key);
    return {};
  }
  spdlog::warn("Karma: Loaded {} texture '{}' ({}x{}) srgb={} embedded={} key='{}'",
               label, raw_key, image.width, image.height, srgb, is_embedded, key);

  const renderer::TextureId id = nextTextureId_++;
  TextureRecord record{};
  record.srv = createTextureSRV(image.pixels.data(),
                                image.width,
                                image.height,
                                srgb,
                                generate_mips_enabled_,
                                label,
                                record.texture);
  textures_[id] = record;
  texture_cache_[key] = id;
  return record.srv;
}

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::loadTextureFromFile(
    const std::filesystem::path& path,
    bool srgb,
    const char* label) {
  if (path.empty()) {
    return {};
  }

  const std::string key = path.string();
  auto cache_it = texture_cache_.find(key);
  if (cache_it != texture_cache_.end()) {
    auto tex_it = textures_.find(cache_it->second);
    if (tex_it != textures_.end()) {
      return tex_it->second.srv;
    }
  }

  LoadedImage image = loadImageFromFile(path);
  if (image.pixels.empty()) {
    spdlog::warn("Karma: Missing {} texture '{}'", label, key);
    return {};
  }

  const renderer::TextureId id = nextTextureId_++;
  TextureRecord record{};
  record.srv = createTextureSRV(image.pixels.data(),
                                image.width,
                                image.height,
                                srgb,
                                generate_mips_enabled_,
                                label,
                                record.texture);
  textures_[id] = record;
  texture_cache_[key] = id;
  return record.srv;
}

}  // namespace karma::renderer_backend
