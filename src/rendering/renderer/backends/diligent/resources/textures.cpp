#include "../backend.hpp"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

#include <algorithm>

namespace karma::rendering::backend {

namespace {

Diligent::TEXTURE_FORMAT toDiligentTextureFormat(const rendering::TextureDesc& desc) {
  switch (desc.format) {
    case rendering::TextureFormat::BC7_RGBA_UNORM:
      return Diligent::TEX_FORMAT_BC7_UNORM;
    case rendering::TextureFormat::BC7_RGBA_UNORM_SRGB:
      return Diligent::TEX_FORMAT_BC7_UNORM_SRGB;
    case rendering::TextureFormat::RGB8:
      return desc.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                       : Diligent::TEX_FORMAT_RGBA8_UNORM;
    case rendering::TextureFormat::R8:
      return Diligent::TEX_FORMAT_R8_UNORM;
    case rendering::TextureFormat::RGBA16F:
      return Diligent::TEX_FORMAT_RGBA16_FLOAT;
    case rendering::TextureFormat::RGBA8:
    default:
      return desc.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                       : Diligent::TEX_FORMAT_RGBA8_UNORM;
  }
}

Diligent::TEXTURE_FORMAT toDiligentTextureFormat(rendering::TextureFormat format) {
  rendering::TextureDesc desc{};
  desc.format = format;
  desc.srgb = format == rendering::TextureFormat::BC7_RGBA_UNORM_SRGB;
  return toDiligentTextureFormat(desc);
}

}  // namespace

rendering::TextureId DiligentBackend::allocateTextureId() noexcept {
  if (nextTextureId_ == rendering::kInvalidTexture ||
      nextTextureId_ >= kRenderTargetTextureHandleBit) {
    return rendering::kInvalidTexture;
  }
  return nextTextureId_++;
}

rendering::TextureId DiligentBackend::createTexture(const rendering::TextureDesc& desc) {
  if (!device_ || !desc.valid() ||
      desc.format == rendering::TextureFormat::KTX2_BASIS_UASTC ||
      !supportsTextureFormat(desc.format)) {
    return rendering::kInvalidTexture;
  }

  TextureRecord record{};
  record.desc = desc;
  const bool generate_mips = desc.generate_mips;
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
  if (!record.texture || !record.srv) {
    return rendering::kInvalidTexture;
  }

  const rendering::TextureId id = allocateTextureId();
  if (id == rendering::kInvalidTexture) {
    return rendering::kInvalidTexture;
  }
  textures_[id] = std::move(record);
  return id;
}

bool DiligentBackend::supportsTextureFormat(rendering::TextureFormat format) const {
  if (format == rendering::TextureFormat::KTX2_BASIS_UASTC || device_ == nullptr) {
    return false;
  }
  const Diligent::TEXTURE_FORMAT diligent_format = toDiligentTextureFormat(format);
  const Diligent::TextureFormatInfo& info =
      device_->GetTextureFormatInfo(diligent_format);
  return info.Supported;
}

bool DiligentBackend::uploadTexture(rendering::TextureId texture,
                                    const rendering::TextureUploadData& upload) {
  if (!device_ || !context_ || texture == rendering::kInvalidTexture) {
    return false;
  }

  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.texture) {
    return false;
  }
  if (!rendering::validateTextureUpload(it->second.desc, upload)) {
    return false;
  }

  for (const rendering::TextureUploadSubresource& subresource : upload.subresources) {
    std::vector<std::uint8_t> expanded_rgba;
    Diligent::TextureSubResData subres{};
    if (upload.format == rendering::TextureFormat::RGB8) {
      const std::size_t source_min_stride =
          rendering::textureUploadMinimumRowStride(upload.format, subresource.width);
      const std::size_t source_stride =
          subresource.row_stride == 0u ? source_min_stride : subresource.row_stride;
      const std::size_t output_stride =
          static_cast<std::size_t>(subresource.width) * 4u;
      std::size_t output_size = 0u;
      if (!rendering::tryTextureDataSize(subresource.width,
                                         subresource.height,
                                         4u,
                                         output_size)) {
        return false;
      }
      expanded_rgba.resize(output_size);
      const std::uint8_t* source = upload.bytes.data() + subresource.offset;
      for (int row = 0; row < subresource.height; ++row) {
        const std::uint8_t* source_row = source + static_cast<std::size_t>(row) * source_stride;
        std::uint8_t* output_row =
            expanded_rgba.data() + static_cast<std::size_t>(row) * output_stride;
        for (int column = 0; column < subresource.width; ++column) {
          const std::size_t source_index = static_cast<std::size_t>(column) * 3u;
          const std::size_t output_index = static_cast<std::size_t>(column) * 4u;
          output_row[output_index + 0u] = source_row[source_index + 0u];
          output_row[output_index + 1u] = source_row[source_index + 1u];
          output_row[output_index + 2u] = source_row[source_index + 2u];
          output_row[output_index + 3u] = 255u;
        }
      }
      subres.pData = expanded_rgba.data();
      subres.Stride = static_cast<Diligent::Uint64>(output_stride);
    } else {
      subres.pData = upload.bytes.data() + subresource.offset;
      subres.Stride = static_cast<Diligent::Uint64>(
          subresource.row_stride != 0u
              ? subresource.row_stride
              : rendering::textureUploadMinimumRowStride(upload.format,
                                                          subresource.width));
    }

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
  directional_shadow_scene_dirty_ = true;
  point_shadow_scene_dirty_ = true;
  return true;
}

void DiligentBackend::destroyTexture(rendering::TextureId texture) {
  auto texture_it = textures_.find(texture);
  if (texture_it == textures_.end()) {
    return;
  }

  Diligent::RefCntAutoPtr<Diligent::ITextureView> old_srv = texture_it->second.srv;
  for (auto cache_it = texture_cache_.begin(); cache_it != texture_cache_.end();) {
    if (cache_it->second == texture) {
      cache_it = texture_cache_.erase(cache_it);
    } else {
      ++cache_it;
    }
  }
  replaceMaterialTextureView(old_srv, nullptr);
  textures_.erase(texture_it);
  directional_shadow_scene_dirty_ = true;
  point_shadow_scene_dirty_ = true;
}

void DiligentBackend::updateTextureRGBA8(rendering::TextureId texture,
                                         int w,
                                         int h,
                                         const void* pixels) {
  if (!device_ || !context_ || texture == rendering::kInvalidTexture || !pixels || w <= 0 || h <= 0) {
    return;
  }

  auto it = textures_.find(texture);
  if (it == textures_.end()) {
    return;
  }

  auto& record = it->second;
  const bool size_changed = record.desc.width != w || record.desc.height != h;
  const bool format_changed = record.desc.format != rendering::TextureFormat::RGBA8;
  if (!record.texture || size_changed || format_changed) {
    Diligent::TextureDesc tex_desc{};
    tex_desc.Name = "Karma UI Texture";
    tex_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    tex_desc.Width = static_cast<Diligent::Uint32>(w);
    tex_desc.Height = static_cast<Diligent::Uint32>(h);
    tex_desc.MipLevels = 1;
    tex_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    tex_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    tex_desc.Usage = Diligent::USAGE_DEFAULT;

    Diligent::TextureSubResData subres{};
    subres.pData = pixels;
    subres.Stride = static_cast<Diligent::Uint64>(static_cast<std::size_t>(w) * 4u);
    Diligent::TextureData init_data{};
    init_data.pSubResources = &subres;
    init_data.NumSubresources = 1;
    Diligent::RefCntAutoPtr<Diligent::ITexture> replacement_texture;
    device_->CreateTexture(tex_desc, &init_data, &replacement_texture);
    if (!replacement_texture) {
      return;
    }
    Diligent::RefCntAutoPtr<Diligent::ITextureView> replacement_srv;
    replacement_srv =
        replacement_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    if (!replacement_srv) {
      return;
    }

    Diligent::RefCntAutoPtr<Diligent::ITextureView> old_srv = record.srv;
    record.desc = rendering::TextureDesc{
        .width = w,
        .height = h,
        .format = rendering::TextureFormat::RGBA8,
        .srgb = false,
        .generate_mips = false,
        .mip_levels = 1u,
    };
    record.texture = std::move(replacement_texture);
    record.srv = std::move(replacement_srv);
    replaceMaterialTextureView(old_srv, record.srv);
    directional_shadow_scene_dirty_ = true;
    point_shadow_scene_dirty_ = true;
    return;
  }

  if (!record.texture) {
    return;
  }

  Diligent::TextureSubResData subres{};
  subres.pData = pixels;
  subres.Stride = static_cast<Diligent::Uint64>(static_cast<std::size_t>(w) * 4u);

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
  directional_shadow_scene_dirty_ = true;
  point_shadow_scene_dirty_ = true;
}

}  // namespace karma::rendering::backend
