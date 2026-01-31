#include "karma/renderer/backends/diligent/backend.hpp"

#include <Common/interface/BasicMath.hpp>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace karma::renderer_backend {

namespace {
struct alignas(16) UiConstants {
  Diligent::float4x4 transform;
  Diligent::float4 translate;
};

static constexpr const char* kUiVS = R"(
cbuffer Constants
{
    row_major float4x4 g_Transform;
    float4 g_Translate;
};

struct VSInput
{
    float2 pos : ATTRIB0;
    float2 uv  : ATTRIB1;
    float4 col : ATTRIB2;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

PSInput main(VSInput input)
{
    PSInput output;
    float2 pos = input.pos.xy + g_Translate.xy;
    output.pos = mul(float4(pos, 0.0f, 1.0f), g_Transform);
    output.uv = input.uv;
    output.col = input.col;
    return output;
}
)";

static constexpr const char* kUiColorPS = R"(
struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col;
}
)";

static constexpr const char* kUiTexturePS = R"(
Texture2D g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col * g_Texture.Sample(g_Texture_sampler, input.uv);
}
)";
}  // namespace

void DiligentBackend::ensureUiResources() {
  static bool logged_once = false;
  if (ui_pso_color_ && ui_pso_color_scissor_ && ui_pso_texture_ && ui_pso_texture_scissor_) {
    return;
  }
  if (!device_) {
    return;
  }

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma UI VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kUiVS;
  device_->CreateShader(shader_ci, &vs);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps_color;
  shader_ci.Desc.Name = "Karma UI Color PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kUiColorPS;
  device_->CreateShader(shader_ci, &ps_color);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps_texture;
  shader_ci.Desc.Name = "Karma UI Texture PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kUiTexturePS;
  device_->CreateShader(shader_ci, &ps_texture);

  if (!vs || !ps_color || !ps_texture) {
    spdlog::error("Karma: Failed to create UI shaders.");
    return;
  }

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(karma::app::UIVertex, x)),
                              static_cast<Diligent::Uint32>(sizeof(karma::app::UIVertex))},
      Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(karma::app::UIVertex, u)),
                              static_cast<Diligent::Uint32>(sizeof(karma::app::UIVertex))},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_UINT8, true,
                              static_cast<Diligent::Uint32>(offsetof(karma::app::UIVertex, rgba)),
                              static_cast<Diligent::Uint32>(sizeof(karma::app::UIVertex))}
  };

  if (!ui_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma UI Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(UiConstants);
    device_->CreateBuffer(cb_desc, nullptr, &ui_cb_);
  }

  static const Diligent::ShaderResourceVariableDesc kUiTextureVars[] = {
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
  };
  static const Diligent::SamplerDesc kUiTextureSamplerDesc{
      Diligent::FILTER_TYPE_LINEAR, Diligent::FILTER_TYPE_LINEAR, Diligent::FILTER_TYPE_LINEAR,
      Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP
  };
  static const Diligent::ImmutableSamplerDesc kUiTextureSamplers[] = {
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler", kUiTextureSamplerDesc}
  };

  auto create_pipeline = [&](Diligent::IShader* ps,
                             bool scissor,
                             bool textured,
                             const char* name,
                             Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso,
                             Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb) {
    if (out_pso) {
      return;
    }
    Diligent::GraphicsPipelineStateCreateInfo pso{};
    pso.PSODesc.Name = name;
    pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pso.pVS = vs;
    pso.pPS = ps;

    auto& graphics = pso.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                        : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                     : Diligent::TEX_FORMAT_D32_FLOAT;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    graphics.RasterizerDesc.ScissorEnable = scissor;
    graphics.DepthStencilDesc.DepthEnable = false;
    graphics.DepthStencilDesc.DepthWriteEnable = false;

    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));

    if (textured) {
      pso.PSODesc.ResourceLayout.Variables = kUiTextureVars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(std::size(kUiTextureVars));
      pso.PSODesc.ResourceLayout.ImmutableSamplers = kUiTextureSamplers;
      pso.PSODesc.ResourceLayout.NumImmutableSamplers =
          static_cast<Diligent::Uint32>(std::size(kUiTextureSamplers));
    }

    device_->CreateGraphicsPipelineState(pso, &out_pso);
    if (!out_pso) {
      spdlog::error("Karma: Failed to create UI pipeline state.");
      return;
    }

    if (ui_cb_) {
      if (auto* var = out_pso->GetStaticVariableByName(
              Diligent::SHADER_TYPE_VERTEX, "Constants")) {
        var->Set(ui_cb_);
      }
    }

    out_pso->CreateShaderResourceBinding(&out_srb, true);
  };

  create_pipeline(ps_color, false, false, "Karma UI Color PSO",
                  ui_pso_color_, ui_srb_color_);
  create_pipeline(ps_color, true, false, "Karma UI Color PSO Scissor",
                  ui_pso_color_scissor_, ui_srb_color_scissor_);
  create_pipeline(ps_texture, false, true, "Karma UI Texture PSO",
                  ui_pso_texture_, ui_srb_texture_);
  create_pipeline(ps_texture, true, true, "Karma UI Texture PSO Scissor",
                  ui_pso_texture_scissor_, ui_srb_texture_scissor_);

  if (!logged_once &&
      ui_pso_color_ && ui_pso_color_scissor_ && ui_pso_texture_ && ui_pso_texture_scissor_) {
    spdlog::info("Karma UI: pipeline created.");
    logged_once = true;
  }
}

void DiligentBackend::renderUi(const karma::app::UIDrawData& draw_data) {
  if (!context_ || !swap_chain_) {
    return;
  }
  if (current_width_ <= 0 || current_height_ <= 0) {
    return;
  }
  if (draw_data.vertices.empty() || draw_data.indices.empty() || draw_data.commands.empty()) {
    return;
  }

  ensureUiResources();
  if (!ui_cb_) {
    return;
  }

  if (ui_vb_size_ < draw_data.vertices.size()) {
    ui_vb_size_ = draw_data.vertices.size() + 2048;
    Diligent::BufferDesc desc{};
    desc.Name = "Karma UI VB";
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = static_cast<Diligent::Uint32>(ui_vb_size_ * sizeof(karma::app::UIVertex));
    device_->CreateBuffer(desc, nullptr, &ui_vb_);
  }

  if (ui_ib_size_ < draw_data.indices.size()) {
    ui_ib_size_ = draw_data.indices.size() + 4096;
    Diligent::BufferDesc desc{};
    desc.Name = "Karma UI IB";
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = static_cast<Diligent::Uint32>(ui_ib_size_ * sizeof(uint32_t));
    device_->CreateBuffer(desc, nullptr, &ui_ib_);
  }

  if (!ui_vb_ || !ui_ib_) {
    return;
  }

  {
    Diligent::MapHelper<karma::app::UIVertex> vb_map(context_, ui_vb_, Diligent::MAP_WRITE,
                                                     Diligent::MAP_FLAG_DISCARD);
    std::memcpy(vb_map, draw_data.vertices.data(),
                draw_data.vertices.size() * sizeof(karma::app::UIVertex));
  }
  {
    Diligent::MapHelper<uint32_t> ib_map(context_, ui_ib_, Diligent::MAP_WRITE,
                                         Diligent::MAP_FLAG_DISCARD);
    std::memcpy(ib_map, draw_data.indices.data(),
                draw_data.indices.size() * sizeof(uint32_t));
  }

  const float L = 0.0f;
  const float R = static_cast<float>(current_width_);
  const float T = 0.0f;
  const float B = static_cast<float>(current_height_);
  const std::array<float, 16> proj = {
      2.0f / (R - L), 0.0f, 0.0f, 0.0f,
      0.0f, 2.0f / (T - B), 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f
  };
  {
    Diligent::MapHelper<UiConstants> cb_map(context_, ui_cb_, Diligent::MAP_WRITE,
                                            Diligent::MAP_FLAG_DISCARD);
    cb_map->transform = Diligent::float4x4::MakeMatrix(proj.data());
    cb_map->translate = Diligent::float4{0.0f, 0.0f, 0.0f, 0.0f};
  }

  Diligent::ITextureView* rtv = swap_chain_->GetCurrentBackBufferRTV();
  Diligent::ITextureView* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(current_width_);
  vp.Height = static_cast<float>(current_height_);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context_->SetViewports(1, &vp, static_cast<Diligent::Uint32>(current_width_),
                         static_cast<Diligent::Uint32>(current_height_));

  Diligent::IBuffer* vbs[] = {ui_vb_};
  Diligent::Uint64 offsets[] = {0};
  context_->SetVertexBuffers(0, 1, vbs, offsets,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
  context_->SetIndexBuffer(ui_ib_, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::ITextureView* current_texture = nullptr;
  for (const auto& cmd : draw_data.commands) {
    const bool use_texture = cmd.texture != 0;
    auto* pipeline = use_texture
        ? (cmd.scissor_enabled ? ui_pso_texture_scissor_.RawPtr()
                               : ui_pso_texture_.RawPtr())
        : (cmd.scissor_enabled ? ui_pso_color_scissor_.RawPtr()
                               : ui_pso_color_.RawPtr());
    auto* srb = use_texture
        ? (cmd.scissor_enabled ? ui_srb_texture_scissor_.RawPtr()
                               : ui_srb_texture_.RawPtr())
        : (cmd.scissor_enabled ? ui_srb_color_scissor_.RawPtr()
                               : ui_srb_color_.RawPtr());
    if (!pipeline || !srb) {
      continue;
    }
    context_->SetPipelineState(pipeline);

    const bool scissor = cmd.scissor_enabled;
    Diligent::Rect rect{};
    if (scissor) {
      const int right = std::max(0, cmd.scissor_x + cmd.scissor_w);
      const int bottom = std::max(0, cmd.scissor_y + cmd.scissor_h);
      rect.left = std::max(0, cmd.scissor_x);
      rect.top = std::max(0, cmd.scissor_y);
      rect.right = std::min(current_width_, right);
      rect.bottom = std::min(current_height_, bottom);
    } else {
      rect.left = 0;
      rect.top = 0;
      rect.right = current_width_;
      rect.bottom = current_height_;
    }
    context_->SetScissorRects(1, &rect, static_cast<Diligent::Uint32>(current_width_),
                              static_cast<Diligent::Uint32>(current_height_));

    if (use_texture) {
      Diligent::ITextureView* desired = default_base_color_;
      auto it = textures_.find(cmd.texture);
      if (it != textures_.end() && it->second.srv) {
        desired = it->second.srv;
      }
      if (desired && desired != current_texture) {
        current_texture = desired;
        if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture")) {
          var->Set(current_texture);
        }
      }
    }

    context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawIndexedAttribs draw{};
    draw.IndexType = Diligent::VT_UINT32;
    draw.NumIndices = cmd.index_count;
    draw.FirstIndexLocation = cmd.index_offset;
    draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
    context_->DrawIndexed(draw);
  }
}

}  // namespace karma::renderer_backend
