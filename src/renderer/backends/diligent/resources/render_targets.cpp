#include "karma/renderer/backends/diligent/backend.hpp"

#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

namespace karma::renderer_backend {

namespace {
Diligent::TEXTURE_FORMAT resolveDepthSrvFormat(Diligent::TEXTURE_FORMAT depth_format) {
  switch (depth_format) {
    case Diligent::TEX_FORMAT_D32_FLOAT:
      return Diligent::TEX_FORMAT_R32_FLOAT;
    case Diligent::TEX_FORMAT_D24_UNORM_S8_UINT:
      return Diligent::TEX_FORMAT_R24_UNORM_X8_TYPELESS;
    case Diligent::TEX_FORMAT_D32_FLOAT_S8X24_UINT:
      return Diligent::TEX_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
      return Diligent::TEX_FORMAT_UNKNOWN;
  }
}
}  // namespace

renderer::RenderTargetId DiligentBackend::createRenderTarget(const renderer::RenderTargetDesc& desc) {
  RenderTargetRecord record{};
  record.desc = desc;
  const int target_width = desc.width > 0 ? desc.width : current_width_;
  const int target_height = desc.height > 0 ? desc.height : current_height_;
  recreateRenderTargetResources(record, target_width, target_height);

  const renderer::RenderTargetId id = nextTargetId_++;
  targets_[id] = std::move(record);
  return id;
}

void DiligentBackend::destroyRenderTarget(renderer::RenderTargetId target) {
  targets_.erase(target);
}

void DiligentBackend::recreateRenderTargetResources(RenderTargetRecord& record,
                                                    int width,
                                                    int height) {
  record.color_texture.Release();
  record.color_srv.Release();
  record.color_rtv.Release();
  record.depth_texture.Release();
  record.depth_srv.Release();
  record.depth_dsv.Release();
  record.depth_read_only_dsv.Release();
  record.width = 0;
  record.height = 0;

  if (!device_ || width <= 0 || height <= 0) {
    return;
  }

  Diligent::TextureDesc color_desc{};
  color_desc.Name = "Karma Render Target Color";
  color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  color_desc.Width = static_cast<Diligent::Uint32>(width);
  color_desc.Height = static_cast<Diligent::Uint32>(height);
  color_desc.MipLevels = 1;
  color_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  color_desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  device_->CreateTexture(color_desc, nullptr, &record.color_texture);
  if (!record.color_texture) {
    return;
  }

  record.color_srv = record.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  record.color_rtv = record.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!record.color_rtv) {
    record.color_texture.Release();
    record.color_srv.Release();
    return;
  }

  if (record.desc.depth) {
    Diligent::TextureDesc depth_desc{};
    depth_desc.Name = "Karma Render Target Depth";
    depth_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    depth_desc.Width = static_cast<Diligent::Uint32>(width);
    depth_desc.Height = static_cast<Diligent::Uint32>(height);
    depth_desc.MipLevels = 1;
    depth_desc.Format = record.desc.stencil ? Diligent::TEX_FORMAT_D24_UNORM_S8_UINT
                                            : Diligent::TEX_FORMAT_D32_FLOAT;
    depth_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(depth_desc, nullptr, &record.depth_texture);
    if (record.depth_texture) {
      Diligent::TextureViewDesc depth_srv_desc{};
      depth_srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
      depth_srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
      depth_srv_desc.Format = resolveDepthSrvFormat(depth_desc.Format);
      if (depth_srv_desc.Format != Diligent::TEX_FORMAT_UNKNOWN) {
        record.depth_texture->CreateView(depth_srv_desc, &record.depth_srv);
      }
      if (!record.depth_srv) {
        record.depth_srv = record.depth_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      }
      record.depth_dsv = record.depth_texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
      Diligent::TextureViewDesc read_only_dsv_desc{};
      read_only_dsv_desc.ViewType = Diligent::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
      read_only_dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
      record.depth_texture->CreateView(read_only_dsv_desc, &record.depth_read_only_dsv);
    }
  }

  record.width = width;
  record.height = height;
}

}  // namespace karma::renderer_backend
