#include "backend.hpp"

#include "backend_internal.h"

#include <assimp/scene.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>

#include <array>

namespace karma::rendering::backend {

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::createTextureSRV(
    const unsigned char* data,
    int width,
    int height,
    bool srgb,
    bool generate_mips,
    const char* name,
    Diligent::RefCntAutoPtr<Diligent::ITexture>& out_texture) {
  const auto total_start = core::SteadyClock::now();
  if (!device_ || !data || width <= 0 || height <= 0) {
    logRenderResourceDiag("texture_upload", name ? name : "unnamed invalid", total_start, core::SteadyClock::now());
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
    auto stage_start = core::SteadyClock::now();
    device_->CreateTexture(desc, nullptr, &texture);
    logRenderResourceDiag("texture_upload", "create empty texture", stage_start, core::SteadyClock::now());
    if (texture && context_) {
      stage_start = core::SteadyClock::now();
      Diligent::TextureSubResData subres{};
      subres.pData = upload_data;
      subres.Stride = static_cast<Diligent::Uint64>(
          static_cast<std::size_t>(upload_width) * 4u);
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
      logRenderResourceDiag("texture_upload", "update base mip", stage_start, core::SteadyClock::now());
    }
  } else {
    const auto stage_start = core::SteadyClock::now();
    Diligent::TextureData init_data{};
    Diligent::TextureSubResData subres{};
    subres.pData = upload_data;
    subres.Stride = static_cast<Diligent::Uint64>(
        static_cast<std::size_t>(upload_width) * 4u);
    init_data.pSubResources = &subres;
    init_data.NumSubresources = 1;
    device_->CreateTexture(desc, &init_data, &texture);
    logRenderResourceDiag("texture_upload", "create initialized texture", stage_start, core::SteadyClock::now());
  }
  if (!texture) {
    logRenderResourceDiag("texture_upload", name ? name : "unnamed failed", total_start, core::SteadyClock::now());
    return {};
  }
  out_texture = texture;
  auto* raw_view = texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  if (!raw_view) {
    return {};
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  srv = raw_view;
  if (should_gen_mips && context_) {
    const auto stage_start = core::SteadyClock::now();
    context_->GenerateMips(srv);
    logRenderResourceDiag("texture_upload", "generate mips", stage_start, core::SteadyClock::now());
  }
  logRenderResourceDiag("texture_upload", name ? name : "unnamed total", total_start, core::SteadyClock::now());
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

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::createSolidCubeTextureSRV(
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned char a,
    bool srgb,
    const char* name,
    Diligent::RefCntAutoPtr<Diligent::ITexture>& out_texture) {
  if (!device_) {
    return {};
  }

  std::array<unsigned char, 4> pixel{r, g, b, a};
  std::array<Diligent::TextureSubResData, 6> subresources{};
  for (auto& subresource : subresources) {
    subresource.pData = pixel.data();
    subresource.Stride = 4;
  }

  Diligent::TextureDesc desc{};
  desc.Name = name;
  desc.Type = Diligent::RESOURCE_DIM_TEX_CUBE;
  desc.Width = 1;
  desc.Height = 1;
  desc.ArraySize = 6;
  desc.MipLevels = 1;
  desc.Format = srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                     : Diligent::TEX_FORMAT_RGBA8_UNORM;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;

  Diligent::TextureData init{};
  init.pSubResources = subresources.data();
  init.NumSubresources = static_cast<Diligent::Uint32>(subresources.size());
  device_->CreateTexture(desc, &init, &out_texture);
  if (!out_texture) {
    return {};
  }

  Diligent::ITextureView* raw_view =
      out_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  if (!raw_view) {
    return {};
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  srv = raw_view;
  return srv;
}

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::loadTextureFromAssimp(
    const aiScene& scene,
    const std::string& model_key,
    const std::filesystem::path& base_dir,
    const aiString& tex_path,
    bool srgb,
    const char* label) {
  const auto total_start = core::SteadyClock::now();
  if (tex_path.length == 0) {
    return {};
  }

  const std::string raw_key = tex_path.C_Str();
  const bool is_embedded = !raw_key.empty() && raw_key[0] == '*';
  const std::filesystem::path resolved_path = is_embedded ? std::filesystem::path{} : (base_dir / raw_key);
  const std::string source_key =
      is_embedded ? model_key + ":" + raw_key : resolved_path.string();
  const std::string key = makeMaterialTextureCacheKey(
      source_key, srgb, generate_mips_enabled_);
  auto cache_it = texture_cache_.find(key);
  if (cache_it != texture_cache_.end()) {
    auto tex_it = textures_.find(cache_it->second);
    if (tex_it != textures_.end()) {
      logRenderResourceDiag("assimp_texture", "cache hit", total_start, core::SteadyClock::now());
      return tex_it->second.srv;
    }
  }

  LoadedImage image{};
  auto stage_start = core::SteadyClock::now();
  if (is_embedded) {
    if (const aiTexture* embedded = scene.GetEmbeddedTexture(raw_key.c_str())) {
      image = decodeEmbeddedAssimpTexture(*embedded);
    }
    logRenderResourceDiag("assimp_texture", "embedded decode", stage_start, core::SteadyClock::now());
  } else {
    image = loadImageFromFile(resolved_path);
    logRenderResourceDiag("assimp_texture", "file decode", stage_start, core::SteadyClock::now());
  }

  if (image.pixels.empty()) {
    logRenderResourceDiag("assimp_texture", "total", total_start, core::SteadyClock::now());
    return {};
  }

  stage_start = core::SteadyClock::now();
  TextureRecord record{};
  record.srv = createTextureSRV(image.pixels.data(),
                                image.width,
                                image.height,
                                srgb,
                                generate_mips_enabled_,
                                label,
                                record.texture);
  logRenderResourceDiag("assimp_texture", "gpu upload", stage_start, core::SteadyClock::now());
  if (!record.srv) {
    logRenderResourceDiag("assimp_texture", "total", total_start, core::SteadyClock::now());
    return {};
  }
  record.desc = rendering::TextureDesc{
      .width = image.width,
      .height = image.height,
      .format = rendering::TextureFormat::RGBA8,
      .srgb = srgb,
      .generate_mips = generate_mips_enabled_,
      .mip_levels = 1u,
  };
  const rendering::TextureId id = allocateTextureId();
  if (id == rendering::kInvalidTexture) {
    return {};
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv = record.srv;
  textures_[id] = std::move(record);
  texture_cache_[key] = id;
  logRenderResourceDiag("assimp_texture", "total", total_start, core::SteadyClock::now());
  return srv;
}

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::loadTextureFromFile(
    const std::filesystem::path& path,
    bool srgb,
    const char* label) {
  if (path.empty()) {
    return {};
  }

  const std::string key = makeMaterialTextureCacheKey(
      path.string(), srgb, generate_mips_enabled_);
  auto cache_it = texture_cache_.find(key);
  if (cache_it != texture_cache_.end()) {
    auto tex_it = textures_.find(cache_it->second);
    if (tex_it != textures_.end()) {
      return tex_it->second.srv;
    }
  }

  LoadedImage image = loadImageFromFile(path);
  if (image.pixels.empty()) {
    return {};
  }

  TextureRecord record{};
  record.srv = createTextureSRV(image.pixels.data(),
                                image.width,
                                image.height,
                                srgb,
                                generate_mips_enabled_,
                                label,
                                record.texture);
  if (!record.srv) {
    return {};
  }
  record.desc = rendering::TextureDesc{
      .width = image.width,
      .height = image.height,
      .format = rendering::TextureFormat::RGBA8,
      .srgb = srgb,
      .generate_mips = generate_mips_enabled_,
      .mip_levels = 1u,
  };
  const rendering::TextureId id = allocateTextureId();
  if (id == rendering::kInvalidTexture) {
    return {};
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv = record.srv;
  textures_[id] = std::move(record);
  texture_cache_[key] = id;
  return srv;
}

}  // namespace karma::rendering::backend
