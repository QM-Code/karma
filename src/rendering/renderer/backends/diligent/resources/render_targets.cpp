#include "../backend.hpp"

#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

namespace karma::rendering::backend {

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

bool hasStencilComponent(Diligent::TEXTURE_FORMAT format) {
  return format == Diligent::TEX_FORMAT_D24_UNORM_S8_UINT ||
         format == Diligent::TEX_FORMAT_D32_FLOAT_S8X24_UINT;
}
}  // namespace

rendering::RenderTargetId DiligentBackend::allocateRenderTargetId() noexcept {
  if (nextTargetId_ == rendering::kDefaultRenderTarget ||
      nextTargetId_ >= kRenderTargetTextureHandleBit) {
    return rendering::kDefaultRenderTarget;
  }
  return nextTargetId_++;
}

rendering::RenderTargetId DiligentBackend::createRenderTarget(const rendering::RenderTargetDesc& desc) {
  if (!device_ || !desc.valid()) {
    return rendering::kDefaultRenderTarget;
  }
  const Diligent::TEXTURE_FORMAT depth_format =
      swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                  : Diligent::TEX_FORMAT_D32_FLOAT;
  if (desc.stencil && !hasStencilComponent(depth_format)) {
    return rendering::kDefaultRenderTarget;
  }

  RenderTargetRecord record{};
  record.desc = desc;
  const int target_width = desc.width > 0 ? desc.width : current_width_;
  const int target_height = desc.height > 0 ? desc.height : current_height_;
  if (!recreateRenderTargetResources(record, target_width, target_height)) {
    return rendering::kDefaultRenderTarget;
  }

  const rendering::RenderTargetId id = allocateRenderTargetId();
  if (id == rendering::kDefaultRenderTarget) {
    return rendering::kDefaultRenderTarget;
  }
  targets_[id] = std::move(record);
  return id;
}

void DiligentBackend::destroyRenderTarget(rendering::RenderTargetId target) {
  targets_.erase(target);
}

bool DiligentBackend::recreateRenderTargetResources(RenderTargetRecord& record,
                                                     int width,
                                                     int height) {
  if (!device_ || width <= 0 || height <= 0) {
    return false;
  }

  RenderTargetRecord replacement{};
  replacement.desc = record.desc;
  Diligent::TextureDesc color_desc{};
  color_desc.Name = "Karma Render Target Color";
  color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  color_desc.Width = static_cast<Diligent::Uint32>(width);
  color_desc.Height = static_cast<Diligent::Uint32>(height);
  color_desc.MipLevels = 1;
  color_desc.Format = sceneColorFormat();
  color_desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  device_->CreateTexture(color_desc, nullptr, &replacement.color_texture);
  if (!replacement.color_texture) {
    return false;
  }

  replacement.color_srv =
      replacement.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  replacement.color_rtv =
      replacement.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!replacement.color_srv || !replacement.color_rtv) {
    return false;
  }

  if (record.desc.depth) {
    Diligent::TextureDesc depth_desc{};
    depth_desc.Name = "Karma Render Target Depth";
    depth_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    depth_desc.Width = static_cast<Diligent::Uint32>(width);
    depth_desc.Height = static_cast<Diligent::Uint32>(height);
    depth_desc.MipLevels = 1;
    depth_desc.Format = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                    : Diligent::TEX_FORMAT_D32_FLOAT;
    if (depth_desc.Format == Diligent::TEX_FORMAT_UNKNOWN ||
        (record.desc.stencil && !hasStencilComponent(depth_desc.Format))) {
      return false;
    }
    depth_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(depth_desc, nullptr, &replacement.depth_texture);
    if (!replacement.depth_texture) {
      return false;
    }
    Diligent::TextureViewDesc depth_srv_desc{};
    depth_srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
    depth_srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
    depth_srv_desc.Format = resolveDepthSrvFormat(depth_desc.Format);
    if (depth_srv_desc.Format != Diligent::TEX_FORMAT_UNKNOWN) {
      replacement.depth_texture->CreateView(depth_srv_desc, &replacement.depth_srv);
    }
    if (!replacement.depth_srv) {
      replacement.depth_srv = replacement.depth_texture->GetDefaultView(
          Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
    replacement.depth_dsv = replacement.depth_texture->GetDefaultView(
        Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    if (!replacement.depth_srv || !replacement.depth_dsv) {
      return false;
    }
    Diligent::TextureViewDesc read_only_dsv_desc{};
    read_only_dsv_desc.ViewType = Diligent::TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
    read_only_dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
    replacement.depth_texture->CreateView(read_only_dsv_desc,
                                          &replacement.depth_read_only_dsv);
  }

  replacement.width = width;
  replacement.height = height;
  record = std::move(replacement);
  return true;
}

}  // namespace karma::rendering::backend
