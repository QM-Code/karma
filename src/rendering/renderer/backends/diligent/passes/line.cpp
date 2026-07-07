#include "../backend.hpp"

#include "../backend_internal.h"
#include "pass_shared.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>

#include <cstddef>

namespace karma::rendering::backend {

namespace {
static constexpr const char* kLineVS = R"(
cbuffer Constants
{
    row_major float4x4 g_ViewProj;
};

struct VSInput
{
    float4 pos : ATTRIB0;
    float4 col : ATTRIB1;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.pos = mul(input.pos, g_ViewProj);
    output.col = input.col;
    return output;
}
)";

static constexpr const char* kLinePS = R"(
struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col;
}
)";
}  // namespace

void DiligentBackend::ensureLineResources() {
  if (line_pipeline_state_depth_ && line_pipeline_state_no_depth_) {
    return;
  }
  if (!device_) {
    return;
  }

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Line VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kLineVS;
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Line PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kLinePS;
  ps = device_with_cache_.CreateShader(shader_ci);

  if (!vs || !ps) {
    return;
  }

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(LineVertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(LineVertex))},
      Diligent::LayoutElement{1, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(LineVertex, color)),
                              static_cast<Diligent::Uint32>(sizeof(LineVertex))}};

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}};

  auto create_pipeline = [&](const char* name, bool depth_test,
                             Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso,
                             Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb) {
    Diligent::GraphicsPipelineStateCreateInfo pso{};
    pso.PSODesc.Name = name;
    pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pso.pVS = vs;
    pso.pPS = ps;

    auto& graphics = pso.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.SmplDesc.Count = static_cast<Diligent::Uint8>(activeRasterSampleCount());
    graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                         : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                     : Diligent::TEX_FORMAT_D32_FLOAT;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_LINE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    if (depth_test) {
      graphics.RasterizerDesc.DepthBias = -1;
    }
    graphics.DepthStencilDesc.DepthEnable = depth_test;
    graphics.DepthStencilDesc.DepthWriteEnable = false;
    graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements =
        static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));

    pso.PSODesc.ResourceLayout.Variables = vars;
    pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

    const auto pso_start = core::SteadyClock::now();
    out_pso = createGraphicsPipelineState(pso);
    recordPipelineCreation("line", name, pso_start, core::SteadyClock::now());
    if (!out_pso) {
      return false;
    }
    if (auto* var =
            out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      var->Set(line_cb_);
    }
    const auto srb_start = core::SteadyClock::now();
    out_pso->CreateShaderResourceBinding(&out_srb, true);
    recordResourceCreation("line", name, srb_start, core::SteadyClock::now());
    return true;
  };

  if (!line_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Line Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(LineConstants);
    const auto cb_start = core::SteadyClock::now();
    device_->CreateBuffer(cb_desc, nullptr, &line_cb_);
    recordResourceCreation("line", "constants buffer", cb_start, core::SteadyClock::now());
  }

  if (!line_cb_) {
    return;
  }

  create_pipeline("Karma Line Pipeline (Depth)", true, line_pipeline_state_depth_, line_srb_depth_);
  create_pipeline("Karma Line Pipeline (NoDepth)", false, line_pipeline_state_no_depth_,
                  line_srb_no_depth_);

  if (!line_vb_) {
    line_vb_size_ = 1024;
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma Line VB";
    vb_desc.Usage = Diligent::USAGE_DYNAMIC;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    vb_desc.Size = static_cast<Diligent::Uint32>(line_vb_size_ * sizeof(LineVertex));
    const auto vb_start = core::SteadyClock::now();
    device_->CreateBuffer(vb_desc, nullptr, &line_vb_);
    recordResourceCreation("line", "vertex buffer", vb_start, core::SteadyClock::now());
    if (!line_vb_) {
      line_vb_size_ = 0;
    }
  }
}

}  // namespace karma::rendering::backend
