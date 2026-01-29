#include "karma/renderer/backends/diligent/backend.hpp"

#include "backend_internal.h"

#if KARMA_WITH_IMGUI

#include <imgui.h>

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
#include <cstring>

namespace karma::renderer_backend {

namespace {
ImGuiKey toImGuiKey(platform::Key key) {
  switch (key) {
    case platform::Key::Tab: return ImGuiKey_Tab;
    case platform::Key::Left: return ImGuiKey_LeftArrow;
    case platform::Key::Right: return ImGuiKey_RightArrow;
    case platform::Key::Up: return ImGuiKey_UpArrow;
    case platform::Key::Down: return ImGuiKey_DownArrow;
    case platform::Key::PageUp: return ImGuiKey_PageUp;
    case platform::Key::PageDown: return ImGuiKey_PageDown;
    case platform::Key::Home: return ImGuiKey_Home;
    case platform::Key::End: return ImGuiKey_End;
    case platform::Key::Insert: return ImGuiKey_Insert;
    case platform::Key::Delete: return ImGuiKey_Delete;
    case platform::Key::Backspace: return ImGuiKey_Backspace;
    case platform::Key::Space: return ImGuiKey_Space;
    case platform::Key::Enter: return ImGuiKey_Enter;
    case platform::Key::Escape: return ImGuiKey_Escape;
    case platform::Key::Apostrophe: return ImGuiKey_Apostrophe;
    case platform::Key::Minus: return ImGuiKey_Minus;
    case platform::Key::Equal: return ImGuiKey_Equal;
    case platform::Key::LeftBracket: return ImGuiKey_LeftBracket;
    case platform::Key::RightBracket: return ImGuiKey_RightBracket;
    case platform::Key::GraveAccent: return ImGuiKey_GraveAccent;
    case platform::Key::CapsLock: return ImGuiKey_CapsLock;
    case platform::Key::ScrollLock: return ImGuiKey_ScrollLock;
    case platform::Key::NumLock: return ImGuiKey_NumLock;
    case platform::Key::LeftShift: return ImGuiKey_LeftShift;
    case platform::Key::LeftControl: return ImGuiKey_LeftCtrl;
    case platform::Key::LeftAlt: return ImGuiKey_LeftAlt;
    case platform::Key::LeftSuper: return ImGuiKey_LeftSuper;
    case platform::Key::RightShift: return ImGuiKey_RightShift;
    case platform::Key::RightControl: return ImGuiKey_RightCtrl;
    case platform::Key::RightAlt: return ImGuiKey_RightAlt;
    case platform::Key::RightSuper: return ImGuiKey_RightSuper;
    case platform::Key::Menu: return ImGuiKey_Menu;
    case platform::Key::Num0: return ImGuiKey_0;
    case platform::Key::Num1: return ImGuiKey_1;
    case platform::Key::Num2: return ImGuiKey_2;
    case platform::Key::Num3: return ImGuiKey_3;
    case platform::Key::Num4: return ImGuiKey_4;
    case platform::Key::Num5: return ImGuiKey_5;
    case platform::Key::Num6: return ImGuiKey_6;
    case platform::Key::Num7: return ImGuiKey_7;
    case platform::Key::Num8: return ImGuiKey_8;
    case platform::Key::Num9: return ImGuiKey_9;
    case platform::Key::A: return ImGuiKey_A;
    case platform::Key::B: return ImGuiKey_B;
    case platform::Key::C: return ImGuiKey_C;
    case platform::Key::D: return ImGuiKey_D;
    case platform::Key::E: return ImGuiKey_E;
    case platform::Key::F: return ImGuiKey_F;
    case platform::Key::G: return ImGuiKey_G;
    case platform::Key::H: return ImGuiKey_H;
    case platform::Key::I: return ImGuiKey_I;
    case platform::Key::J: return ImGuiKey_J;
    case platform::Key::K: return ImGuiKey_K;
    case platform::Key::L: return ImGuiKey_L;
    case platform::Key::M: return ImGuiKey_M;
    case platform::Key::N: return ImGuiKey_N;
    case platform::Key::O: return ImGuiKey_O;
    case platform::Key::P: return ImGuiKey_P;
    case platform::Key::Q: return ImGuiKey_Q;
    case platform::Key::R: return ImGuiKey_R;
    case platform::Key::S: return ImGuiKey_S;
    case platform::Key::T: return ImGuiKey_T;
    case platform::Key::U: return ImGuiKey_U;
    case platform::Key::V: return ImGuiKey_V;
    case platform::Key::W: return ImGuiKey_W;
    case platform::Key::X: return ImGuiKey_X;
    case platform::Key::Y: return ImGuiKey_Y;
    case platform::Key::Z: return ImGuiKey_Z;
    case platform::Key::F1: return ImGuiKey_F1;
    case platform::Key::F2: return ImGuiKey_F2;
    case platform::Key::F3: return ImGuiKey_F3;
    case platform::Key::F4: return ImGuiKey_F4;
    case platform::Key::F5: return ImGuiKey_F5;
    case platform::Key::F6: return ImGuiKey_F6;
    case platform::Key::F7: return ImGuiKey_F7;
    case platform::Key::F8: return ImGuiKey_F8;
    case platform::Key::F9: return ImGuiKey_F9;
    case platform::Key::F10: return ImGuiKey_F10;
    case platform::Key::F11: return ImGuiKey_F11;
    case platform::Key::F12: return ImGuiKey_F12;
    default: return ImGuiKey_None;
  }
}

int toImGuiMouseButton(platform::MouseButton button) {
  switch (button) {
    case platform::MouseButton::Left: return 0;
    case platform::MouseButton::Right: return 1;
    case platform::MouseButton::Middle: return 2;
    case platform::MouseButton::Button4: return 3;
    case platform::MouseButton::Button5: return 4;
    default: return -1;
  }
}

void applyModifierState(ImGuiIO& io, const platform::Modifiers& mods) {
  io.AddKeyEvent(ImGuiKey_LeftShift, mods.shift);
  io.AddKeyEvent(ImGuiKey_LeftCtrl, mods.control);
  io.AddKeyEvent(ImGuiKey_LeftAlt, mods.alt);
  io.AddKeyEvent(ImGuiKey_LeftSuper, mods.super);
}

}  // namespace

void DiligentBackend::setOverlayCallback(std::function<void()> callback) {
  overlay_callback_ = std::move(callback);
  if (overlay_callback_ && !imgui_initialized_) {
    initImGui();
  }
}

void DiligentBackend::handleOverlayEvent(const platform::Event& event) {
  if (!imgui_initialized_) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  applyModifierState(io, event.mods);

  switch (event.type) {
    case platform::EventType::KeyDown:
    case platform::EventType::KeyUp: {
      const ImGuiKey key = toImGuiKey(event.key);
      if (key != ImGuiKey_None) {
        io.AddKeyEvent(key, event.type == platform::EventType::KeyDown);
      }
      break;
    }
    case platform::EventType::TextInput:
      if (event.codepoint != 0) {
        io.AddInputCharacter(static_cast<unsigned int>(event.codepoint));
      }
      break;
    case platform::EventType::MouseButtonDown:
    case platform::EventType::MouseButtonUp: {
      const int button = toImGuiMouseButton(event.mouseButton);
      if (button >= 0) {
        io.AddMouseButtonEvent(button, event.type == platform::EventType::MouseButtonDown);
      }
      break;
    }
    case platform::EventType::MouseMove:
      io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
      break;
    case platform::EventType::MouseScroll:
      io.AddMouseWheelEvent(static_cast<float>(event.scrollX), static_cast<float>(event.scrollY));
      break;
    case platform::EventType::WindowFocus:
      io.AddFocusEvent(event.focused);
      break;
    default:
      break;
  }
}

void DiligentBackend::renderOverlay() {
  if (!overlay_callback_ || !imgui_initialized_) {
    return;
  }
  if (!context_ || !swap_chain_) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(current_width_), static_cast<float>(current_height_));
  io.DeltaTime = imgui_last_dt_ > 0.0f ? imgui_last_dt_ : (1.0f / 60.0f);

  ImGui::NewFrame();
  overlay_callback_();
  ImGui::Render();
  renderImGuiDrawData(ImGui::GetDrawData());
}

void DiligentBackend::initImGui() {
  if (imgui_initialized_) {
    return;
  }
  if (!device_) {
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.BackendPlatformName = "karma";
  io.BackendRendererName = "karma_diligent";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  ImGui::StyleColorsDark();

  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  Diligent::TextureDesc font_desc{};
  font_desc.Name = "ImGui Font";
  font_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  font_desc.Width = static_cast<Diligent::Uint32>(width);
  font_desc.Height = static_cast<Diligent::Uint32>(height);
  font_desc.MipLevels = 1;
  font_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  font_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;

  Diligent::TextureSubResData subres{};
  subres.pData = pixels;
  subres.Stride = static_cast<Diligent::Uint32>(width * 4);
  Diligent::TextureData init_data{};
  init_data.pSubResources = &subres;
  init_data.NumSubresources = 1;

  device_->CreateTexture(font_desc, &init_data, &imgui_font_tex_);
  if (imgui_font_tex_) {
    imgui_font_srv_ = imgui_font_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(imgui_font_srv_.RawPtr()));
  } else {
    spdlog::error("Karma: Failed to create ImGui font texture.");
  }

static constexpr const char* kImGuiVS = R"(
cbuffer Constants
{
    row_major float4x4 g_Proj;
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
    output.pos = mul(float4(input.pos.xy, 0.0f, 1.0f), g_Proj);
    output.uv = input.uv;
    output.col = input.col;
    return output;
}
)";

  static constexpr const char* kImGuiPS = R"(
Texture2D g_Texture;
SamplerState g_Sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col * g_Texture.Sample(g_Sampler, input.uv);
}
)";

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "ImGui VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kImGuiVS;
  device_->CreateShader(shader_ci, &vs);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "ImGui PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kImGuiPS;
  device_->CreateShader(shader_ci, &ps);

  if (!vs || !ps) {
    spdlog::error("Karma: Failed to create ImGui shaders.");
    return;
  }

  Diligent::GraphicsPipelineStateCreateInfo pso{};
  pso.PSODesc.Name = "ImGui Pipeline";
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
  graphics.RasterizerDesc.ScissorEnable = true;
  graphics.DepthStencilDesc.DepthEnable = false;
  graphics.DepthStencilDesc.DepthWriteEnable = false;

  auto& blend = graphics.BlendDesc.RenderTargets[0];
  blend.BlendEnable = true;
  blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
  blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
  blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
  blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
  blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
  blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
  blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_UINT8, true}
  };
  graphics.InputLayout.LayoutElements = layout;
  graphics.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_Sampler", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
  };
  pso.PSODesc.ResourceLayout.Variables = vars;
  pso.PSODesc.ResourceLayout.NumVariables = static_cast<Diligent::Uint32>(std::size(vars));

  Diligent::SamplerDesc sampler{};
  sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
  device_->CreateSampler(sampler, &imgui_sampler_);

  device_->CreateGraphicsPipelineState(pso, &imgui_pipeline_state_);
  if (!imgui_pipeline_state_) {
    spdlog::error("Karma: Failed to create ImGui pipeline state.");
    return;
  }

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "ImGui Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(float) * 16;
  device_->CreateBuffer(cb_desc, nullptr, &imgui_cb_);

  if (auto* var = imgui_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
    var->Set(imgui_cb_);
  }
  if (imgui_sampler_) {
    if (auto* var = imgui_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Sampler")) {
      var->Set(imgui_sampler_);
    }
  }

  imgui_pipeline_state_->CreateShaderResourceBinding(&imgui_srb_, true);
  if (imgui_srb_ && imgui_font_srv_) {
    if (auto* var = imgui_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture")) {
      var->Set(imgui_font_srv_);
    }
  }

  imgui_initialized_ = true;
}

void DiligentBackend::renderImGuiDrawData(ImDrawData* draw_data) {
  if (!draw_data || draw_data->TotalVtxCount == 0) {
    return;
  }
  if (!imgui_pipeline_state_ || !imgui_srb_) {
    return;
  }

  const int fb_width = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  const int fb_height = static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0) {
    return;
  }

  if (imgui_vb_size_ < static_cast<size_t>(draw_data->TotalVtxCount)) {
    imgui_vb_size_ = static_cast<size_t>(draw_data->TotalVtxCount + 5000);
    Diligent::BufferDesc desc{};
    desc.Name = "ImGui VB";
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = static_cast<Diligent::Uint32>(imgui_vb_size_ * sizeof(ImDrawVert));
    device_->CreateBuffer(desc, nullptr, &imgui_vb_);
  }

  if (imgui_ib_size_ < static_cast<size_t>(draw_data->TotalIdxCount)) {
    imgui_ib_size_ = static_cast<size_t>(draw_data->TotalIdxCount + 10000);
    Diligent::BufferDesc desc{};
    desc.Name = "ImGui IB";
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = static_cast<Diligent::Uint32>(imgui_ib_size_ * sizeof(ImDrawIdx));
    device_->CreateBuffer(desc, nullptr, &imgui_ib_);
  }

  if (!imgui_vb_ || !imgui_ib_) {
    return;
  }

  {
    Diligent::MapHelper<ImDrawVert> vb_map(context_, imgui_vb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    Diligent::MapHelper<ImDrawIdx> ib_map(context_, imgui_ib_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    ImDrawVert* vtx_dst = vb_map;
    ImDrawIdx* idx_dst = ib_map;
    int global_vtx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
      const ImDrawList* cmd_list = draw_data->CmdLists[n];
      std::memcpy(vtx_dst, cmd_list->VtxBuffer.Data,
                  static_cast<size_t>(cmd_list->VtxBuffer.Size) * sizeof(ImDrawVert));
      for (int i = 0; i < cmd_list->IdxBuffer.Size; ++i) {
        idx_dst[i] = static_cast<ImDrawIdx>(cmd_list->IdxBuffer[i] + global_vtx_offset);
      }
      vtx_dst += cmd_list->VtxBuffer.Size;
      idx_dst += cmd_list->IdxBuffer.Size;
      global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
  }

  const float L = draw_data->DisplayPos.x;
  const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
  const float T = draw_data->DisplayPos.y;
  const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
  const std::array<float, 16> proj = {
      2.0f / (R - L), 0.0f, 0.0f, 0.0f,
      0.0f, 2.0f / (T - B), 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f
  };

  if (imgui_cb_) {
    Diligent::MapHelper<float> cb_map(context_, imgui_cb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    std::memcpy(cb_map, proj.data(), sizeof(float) * proj.size());
  }

  Diligent::ITextureView* rtv = swap_chain_->GetCurrentBackBufferRTV();
  Diligent::ITextureView* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context_->SetPipelineState(imgui_pipeline_state_);

  Diligent::Viewport vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(fb_width);
  vp.Height = static_cast<float>(fb_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context_->SetViewports(1, &vp, static_cast<Diligent::Uint32>(fb_width),
                         static_cast<Diligent::Uint32>(fb_height));

  Diligent::IBuffer* vbs[] = {imgui_vb_};
  Diligent::Uint64 offsets[] = {0};
  context_->SetVertexBuffers(0, 1, vbs, offsets,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
  context_->SetIndexBuffer(imgui_ib_, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  int global_idx_offset = 0;
  ImTextureID last_texture = 0;
  for (int n = 0; n < draw_data->CmdListsCount; ++n) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
      const ImDrawCmd& cmd = cmd_list->CmdBuffer[cmd_i];
      if (cmd.UserCallback) {
        cmd.UserCallback(cmd_list, &cmd);
        global_idx_offset += static_cast<int>(cmd.ElemCount);
        continue;
      }
      ImVec4 clip = cmd.ClipRect;
      clip.x = (clip.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
      clip.y = (clip.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
      clip.z = (clip.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
      clip.w = (clip.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
      if (clip.x >= fb_width || clip.y >= fb_height || clip.z < 0.0f || clip.w < 0.0f) {
        global_idx_offset += static_cast<int>(cmd.ElemCount);
        continue;
      }
      Diligent::Rect scissor{};
      scissor.left = static_cast<Diligent::Int32>(std::max(0.0f, clip.x));
      scissor.top = static_cast<Diligent::Int32>(std::max(0.0f, clip.y));
      scissor.right = static_cast<Diligent::Int32>(std::min(static_cast<float>(fb_width), clip.z));
      scissor.bottom = static_cast<Diligent::Int32>(std::min(static_cast<float>(fb_height), clip.w));
      context_->SetScissorRects(1, &scissor, static_cast<Diligent::Uint32>(fb_width),
                                static_cast<Diligent::Uint32>(fb_height));

      ImTextureID tex_id = cmd.GetTexID();
      if (tex_id != last_texture) {
        last_texture = tex_id;
        auto* texture_srv = reinterpret_cast<Diligent::ITextureView*>(tex_id);
        if (!texture_srv) {
          texture_srv = imgui_font_srv_;
        }
        if (imgui_srb_) {
          if (auto* var = imgui_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture")) {
            var->Set(texture_srv);
          }
        }
      }

      if (imgui_srb_) {
        context_->CommitShaderResources(imgui_srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      }

      Diligent::DrawIndexedAttribs draw{};
      draw.IndexType = sizeof(ImDrawIdx) == 2 ? Diligent::VT_UINT16 : Diligent::VT_UINT32;
      draw.NumIndices = cmd.ElemCount;
      draw.FirstIndexLocation = static_cast<Diligent::Uint32>(global_idx_offset);
      draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
      context_->DrawIndexed(draw);
      global_idx_offset += static_cast<int>(cmd.ElemCount);
    }
  }
}

}  // namespace karma::renderer_backend

#else

namespace karma::renderer_backend {

void DiligentBackend::setOverlayCallback(std::function<void()>) {}

void DiligentBackend::handleOverlayEvent(const platform::Event&) {}

void DiligentBackend::renderOverlay() {}

void DiligentBackend::initImGui() {}

void DiligentBackend::renderImGuiDrawData(ImDrawData*) {}

}  // namespace karma::renderer_backend

#endif
