#include "../../backend.hpp"

#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace karma::rendering::backend {

void DiligentBackend::ensurePostProcessResources(int width,
                                                 int height,
                                                 Diligent::TEXTURE_FORMAT format) {
  if (!device_ || width <= 0 || height <= 0 || format == Diligent::TEX_FORMAT_UNKNOWN) {
    return;
  }

  if (post_process_ping_tex_ &&
      post_process_ping_srv_ &&
      post_process_ping_rtv_ &&
      post_process_pong_tex_ &&
      post_process_pong_srv_ &&
      post_process_pong_rtv_ &&
      post_process_history_tex_[0] &&
      post_process_history_srv_[0] &&
      post_process_history_tex_[1] &&
      post_process_history_srv_[1] &&
      !post_process_bloom_mips_.empty() &&
      !post_process_bloom_scratch_mips_.empty() &&
      post_process_width_ == width &&
      post_process_height_ == height &&
      post_process_format_ == format) {
    return;
  }

  post_process_ping_tex_.Release();
  post_process_ping_srv_.Release();
  post_process_ping_rtv_.Release();
  post_process_pong_tex_.Release();
  post_process_pong_srv_.Release();
  post_process_pong_rtv_.Release();
  for (int i = 0; i < 2; ++i) {
    post_process_history_tex_[i].Release();
    post_process_history_srv_[i].Release();
    post_process_history_rtv_[i].Release();
  }
  post_process_bloom_mips_.clear();
  post_process_bloom_scratch_mips_.clear();
  post_process_width_ = 0;
  post_process_height_ = 0;
  post_process_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  post_process_history_valid_ = false;
  post_process_history_index_ = 0;

  auto create_color_texture = [&](const char* name,
                                  int texture_width,
                                  int texture_height,
                                  Diligent::RefCntAutoPtr<Diligent::ITexture>& texture,
                                  Diligent::RefCntAutoPtr<Diligent::ITextureView>& srv,
                                  Diligent::RefCntAutoPtr<Diligent::ITextureView>& rtv) {
    Diligent::TextureDesc desc{};
    desc.Name = name;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Diligent::Uint32>(std::max(texture_width, 1));
    desc.Height = static_cast<Diligent::Uint32>(std::max(texture_height, 1));
    desc.MipLevels = 1;
    desc.Format = format;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    const auto texture_start = core::SteadyClock::now();
    device_->CreateTexture(desc, nullptr, &texture);
    recordResourceCreation("post_process", name, texture_start, core::SteadyClock::now());
    if (!texture) {
      return false;
    }
    srv = texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    rtv = texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    if (!srv || !rtv) {
      texture.Release();
      srv.Release();
      rtv.Release();
      return false;
    }
    return true;
  };

  auto create_post_texture = [&](const char* name,
                                 int texture_width,
                                 int texture_height,
                                 PostProcessTexture& out) {
    if (!create_color_texture(name,
                              texture_width,
                              texture_height,
                              out.texture,
                              out.srv,
                              out.rtv)) {
      out = {};
      return false;
    }
    out.width = std::max(texture_width, 1);
    out.height = std::max(texture_height, 1);
    return true;
  };

  if (!create_color_texture("Karma Post Process Ping",
                            width,
                            height,
                            post_process_ping_tex_,
                            post_process_ping_srv_,
                            post_process_ping_rtv_) ||
      !create_color_texture("Karma Post Process Pong",
                            width,
                            height,
                            post_process_pong_tex_,
                            post_process_pong_srv_,
                            post_process_pong_rtv_) ||
      !create_color_texture("Karma Post Process History A",
                            width,
                            height,
                            post_process_history_tex_[0],
                            post_process_history_srv_[0],
                            post_process_history_rtv_[0]) ||
      !create_color_texture("Karma Post Process History B",
                            width,
                            height,
                            post_process_history_tex_[1],
                            post_process_history_srv_[1],
                            post_process_history_rtv_[1])) {
    post_process_ping_tex_.Release();
    post_process_ping_srv_.Release();
    post_process_ping_rtv_.Release();
    post_process_pong_tex_.Release();
    post_process_pong_srv_.Release();
    post_process_pong_rtv_.Release();
    for (int i = 0; i < 2; ++i) {
      post_process_history_tex_[i].Release();
      post_process_history_srv_[i].Release();
      post_process_history_rtv_[i].Release();
    }
    return;
  }

  constexpr int kMaxBloomMips = 6;
  std::vector<std::pair<int, int>> bloom_sizes;
  int mip_width = std::max(width / 2, 1);
  int mip_height = std::max(height / 2, 1);
  for (int i = 0; i < kMaxBloomMips; ++i) {
    bloom_sizes.emplace_back(mip_width, mip_height);
    if (mip_width == 1 && mip_height == 1) {
      break;
    }
    mip_width = std::max(mip_width / 2, 1);
    mip_height = std::max(mip_height / 2, 1);
  }
  post_process_bloom_mips_.resize(bloom_sizes.size());
  post_process_bloom_scratch_mips_.resize(bloom_sizes.size());
  for (size_t i = 0; i < bloom_sizes.size(); ++i) {
    const auto [level_width, level_height] = bloom_sizes[i];
    const std::string mip_name = "Karma Bloom Mip " + std::to_string(i);
    const std::string scratch_name = "Karma Bloom Scratch Mip " + std::to_string(i);
    if (!create_post_texture(mip_name.c_str(),
                             level_width,
                             level_height,
                             post_process_bloom_mips_[i]) ||
        !create_post_texture(scratch_name.c_str(),
                             level_width,
                             level_height,
                             post_process_bloom_scratch_mips_[i])) {
      post_process_bloom_mips_.clear();
      post_process_bloom_scratch_mips_.clear();
      return;
    }
  }

  post_process_width_ = width;
  post_process_height_ = height;
  post_process_format_ = format;
}

}  // namespace karma::rendering::backend
