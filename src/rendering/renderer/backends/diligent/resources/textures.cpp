#include "../backend.hpp"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

#include <algorithm>

namespace karma::renderer_backend {

namespace {

bool isBc7(renderer::TextureFormat format) {
  return format == renderer::TextureFormat::BC7_RGBA_UNORM ||
         format == renderer::TextureFormat::BC7_RGBA_UNORM_SRGB;
}

Diligent::TEXTURE_FORMAT toDiligentTextureFormat(const renderer::TextureDesc& desc) {
  switch (desc.format) {
    case renderer::TextureFormat::BC7_RGBA_UNORM:
      return Diligent::TEX_FORMAT_BC7_UNORM;
    case renderer::TextureFormat::BC7_RGBA_UNORM_SRGB:
      return Diligent::TEX_FORMAT_BC7_UNORM_SRGB;
    case renderer::TextureFormat::RGB8:
      return desc.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                       : Diligent::TEX_FORMAT_RGBA8_UNORM;
    case renderer::TextureFormat::R8:
      return Diligent::TEX_FORMAT_R8_UNORM;
    case renderer::TextureFormat::RGBA8:
    default:
      return desc.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                       : Diligent::TEX_FORMAT_RGBA8_UNORM;
  }
}

Diligent::TEXTURE_FORMAT toDiligentTextureFormat(renderer::TextureFormat format) {
  renderer::TextureDesc desc{};
  desc.format = format;
  desc.srgb = format == renderer::TextureFormat::BC7_RGBA_UNORM_SRGB;
  return toDiligentTextureFormat(desc);
}

std::size_t defaultRowStride(renderer::TextureFormat format, int width) {
  if (isBc7(format)) {
    const std::size_t blocks_x =
        (static_cast<std::size_t>(std::max(width, 1)) + 3u) / 4u;
    return blocks_x * 16u;
  }
  return static_cast<std::size_t>(std::max(width, 0)) * 4u;
}

}  // namespace

renderer::TextureId DiligentBackend::createTexture(const renderer::TextureDesc& desc) {
  const renderer::TextureId id = nextTextureId_++;
  TextureRecord record{};
  record.desc = desc;
  if (device_ && desc.width > 0 && desc.height > 0) {
    const bool compressed = isBc7(desc.format);
    const bool generate_mips = desc.generate_mips && !compressed;
    Diligent::TextureDesc tex_desc{};
    tex_desc.Name = "Karma Texture";
    tex_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    tex_desc.Width = static_cast<Diligent::Uint32>(desc.width);
    tex_desc.Height = static_cast<Diligent::Uint32>(desc.height);
    tex_desc.MipLevels = generate_mips ? 0 : std::max<Diligent::Uint32>(1u, desc.mip_levels);
    tex_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE |
                         (generate_mips ? Diligent::BIND_RENDER_TARGET : Diligent::BIND_NONE);
    tex_desc.MiscFlags = generate_mips ? Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS
                                       : Diligent::MISC_TEXTURE_FLAG_NONE;
    tex_desc.Format = toDiligentTextureFormat(desc);
    device_->CreateTexture(tex_desc, nullptr, &record.texture);
    if (record.texture) {
      auto* raw_view = record.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      if (raw_view) {
        Diligent::RefCntAutoPtr<Diligent::ITextureView> view;
        view = raw_view;
        record.srv = view;
      }
    }
  }
  textures_[id] = std::move(record);
  return id;
}

bool DiligentBackend::supportsTextureFormat(renderer::TextureFormat format) const {
  if (format == renderer::TextureFormat::KTX2_BASIS_UASTC || device_ == nullptr) {
    return false;
  }
  const Diligent::TEXTURE_FORMAT diligent_format = toDiligentTextureFormat(format);
  const Diligent::TextureFormatInfo& info =
      device_->GetTextureFormatInfo(diligent_format);
  return info.Supported;
}

bool DiligentBackend::uploadTexture(renderer::TextureId texture,
                                    const renderer::TextureUploadData& upload) {
  if (!device_ || !context_ || texture == renderer::kInvalidTexture ||
      upload.bytes.empty() || upload.subresources.empty()) {
    return false;
  }
  if (upload.format != renderer::TextureFormat::RGBA8 && !isBc7(upload.format)) {
    return false;
  }

  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.texture) {
    return false;
  }
  if (it->second.desc.format != upload.format) {
    return false;
  }

  for (const renderer::TextureUploadSubresource& subresource : upload.subresources) {
    if (subresource.width <= 0 || subresource.height <= 0 ||
        subresource.offset > upload.bytes.size() ||
        subresource.size > upload.bytes.size() - subresource.offset) {
      return false;
    }
    Diligent::TextureSubResData subres{};
    subres.pData = upload.bytes.data() + subresource.offset;
    subres.Stride = static_cast<Diligent::Uint64>(
        subresource.row_stride != 0u
            ? subresource.row_stride
            : defaultRowStride(upload.format, subresource.width));

    Diligent::Box box{};
    box.MinX = 0;
    box.MaxX = static_cast<Diligent::Uint32>(subresource.width);
    box.MinY = 0;
    box.MaxY = static_cast<Diligent::Uint32>(subresource.height);
    box.MinZ = 0;
    box.MaxZ = 1;

    const Diligent::Uint32 mip = static_cast<Diligent::Uint32>(subresource.mip_level);
    const Diligent::Uint32 slice = static_cast<Diligent::Uint32>(subresource.array_layer);
    context_->UpdateTexture(it->second.texture,
                            mip,
                            slice,
                            box,
                            subres,
                            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }

  if (it->second.srv && it->second.desc.generate_mips) {
    context_->GenerateMips(it->second.srv);
  }
  return true;
}

void DiligentBackend::destroyTexture(renderer::TextureId texture) {
  textures_.erase(texture);
}

void DiligentBackend::updateTextureRGBA8(renderer::TextureId texture,
                                         int w,
                                         int h,
                                         const void* pixels) {
  if (!device_ || !context_ || texture == renderer::kInvalidTexture || !pixels || w <= 0 || h <= 0) {
    return;
  }

  auto it = textures_.find(texture);
  if (it == textures_.end()) {
    return;
  }

  auto& record = it->second;
  const bool size_changed = record.desc.width != w || record.desc.height != h;
  const bool format_changed = record.desc.format != renderer::TextureFormat::RGBA8;
  if (!record.texture || size_changed || format_changed) {
    record.desc.width = w;
    record.desc.height = h;
    record.desc.format = renderer::TextureFormat::RGBA8;
    record.desc.srgb = false;
    record.desc.generate_mips = false;

    Diligent::TextureDesc tex_desc{};
    tex_desc.Name = "Karma UI Texture";
    tex_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    tex_desc.Width = static_cast<Diligent::Uint32>(w);
    tex_desc.Height = static_cast<Diligent::Uint32>(h);
    tex_desc.MipLevels = 1;
    tex_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    tex_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    tex_desc.Usage = Diligent::USAGE_DEFAULT;

    record.texture.Release();
    record.srv.Release();
    Diligent::TextureSubResData subres{};
    subres.pData = pixels;
    subres.Stride = static_cast<Diligent::Uint32>(w * 4);
    Diligent::TextureData init_data{};
    init_data.pSubResources = &subres;
    init_data.NumSubresources = 1;
    device_->CreateTexture(tex_desc, &init_data, &record.texture);
    if (record.texture) {
      auto* raw_view = record.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      if (raw_view) {
        Diligent::RefCntAutoPtr<Diligent::ITextureView> view;
        view = raw_view;
        record.srv = view;
      }
    }
    return;
  }

  if (!record.texture) {
    return;
  }

  Diligent::TextureSubResData subres{};
  subres.pData = pixels;
  subres.Stride = static_cast<Diligent::Uint32>(w * 4);

  Diligent::Box box{};
  box.MinX = 0;
  box.MaxX = static_cast<Diligent::Uint32>(w);
  box.MinY = 0;
  box.MaxY = static_cast<Diligent::Uint32>(h);
  box.MinZ = 0;
  box.MaxZ = 1;

  context_->UpdateTexture(record.texture, 0, 0, box, subres,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  if (record.desc.generate_mips && record.srv) {
    context_->GenerateMips(record.srv);
  }
}

}  // namespace karma::renderer_backend
