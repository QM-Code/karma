#include "common.h"
#include "shader_source.h"

#include "../../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>

#include <string>
#include <vector>

namespace karma::renderer_backend {

bool DiligentBackend::ensurePostProcessPipelines(Diligent::TEXTURE_FORMAT format) {
  using post_process::ShaderFallback;

  if (!device_ || format == Diligent::TEX_FORMAT_UNKNOWN) {
    return false;
  }

  if (post_process::passReady(post_process_bloom_prefilter_pass_) &&
      post_process::passReady(post_process_bloom_downsample_pass_) &&
      post_process::passReady(post_process_bloom_upsample_pass_) &&
      post_process::passReady(post_process_composite_pass_) &&
      post_process::passReady(post_process_temporal_pass_) &&
      post_process_cb_ &&
      post_process_pipeline_format_ == format) {
    return true;
  }

  post_process::releasePass(post_process_bloom_prefilter_pass_);
  post_process::releasePass(post_process_bloom_downsample_pass_);
  post_process::releasePass(post_process_bloom_upsample_pass_);
  post_process::releasePass(post_process_composite_pass_);
  post_process::releasePass(post_process_temporal_pass_);
  post_process_pipeline_format_ = Diligent::TEX_FORMAT_UNKNOWN;

  if (!post_process_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Post Process Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(post_process::PostProcessConstants);
    device_->CreateBuffer(cb_desc, nullptr, &post_process_cb_);
    if (!post_process_cb_) {
      return false;
    }
  }

  const std::string fullscreen_vs_source =
      post_process::loadShader("fullscreen_triangle_vs.hlsl", ShaderFallback::FullscreenTriangle);
  auto create_pass = [&](const char* pipeline_name,
                         const char* pixel_shader_name,
                         ShaderFallback fallback_pixel_source,
                         bool use_depth,
                         bool use_bloom,
                         bool use_history,
                         PostProcessPassResources& out_pass) {
    const std::string pixel_source =
        post_process::loadShader(pixel_shader_name, fallback_pixel_source);

    Diligent::ShaderCreateInfo shader_ci{};
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};
    shader_ci.EntryPoint = "main";

    Diligent::RefCntAutoPtr<Diligent::IShader> vs;
    const std::string vs_name = std::string(pipeline_name) + " VS";
    shader_ci.Desc.Name = vs_name.c_str();
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.Source = fullscreen_vs_source.c_str();
    vs = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps;
    const std::string ps_name = std::string(pipeline_name) + " PS";
    shader_ci.Desc.Name = ps_name.c_str();
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.Source = pixel_source.c_str();
    ps = device_with_cache_.CreateShader(shader_ci);
    if (!vs || !ps) {
      return false;
    }

    std::vector<Diligent::ShaderResourceVariableDesc> vars;
    vars.push_back({Diligent::SHADER_TYPE_PIXEL,
                    "PostConstants",
                    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC});
    vars.push_back({Diligent::SHADER_TYPE_PIXEL,
                    "g_Source",
                    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    if (use_depth) {
      vars.push_back({Diligent::SHADER_TYPE_PIXEL,
                      "g_Depth",
                      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }
    if (use_bloom) {
      vars.push_back({Diligent::SHADER_TYPE_PIXEL,
                      "g_Bloom",
                      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }
    if (use_history) {
      vars.push_back({Diligent::SHADER_TYPE_PIXEL,
                      "g_History",
                      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }
    vars.push_back({Diligent::SHADER_TYPE_PIXEL,
                    "g_Sampler",
                    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});

    Diligent::GraphicsPipelineStateCreateInfo pso{};
    pso.PSODesc.Name = pipeline_name;
    pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pso.pVS = vs;
    pso.pPS = ps;
    pso.PSODesc.ResourceLayout.Variables = vars.data();
    pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(vars.size());

    auto& graphics = pso.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = format;
    graphics.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = false;
    graphics.DepthStencilDesc.DepthWriteEnable = false;
    graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    const auto pso_start = core::SteadyClock::now();
    out_pass.pso = device_with_cache_.CreateGraphicsPipelineState(pso);
    logRenderPipelineDiag("post_process", pipeline_name, pso_start, core::SteadyClock::now());
    if (!out_pass.pso) {
      return false;
    }

    if (auto* var =
            out_pass.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                  "PostConstants")) {
      var->Set(post_process_cb_);
    }

    out_pass.pso->CreateShaderResourceBinding(&out_pass.srb, true);
    if (!out_pass.srb) {
      post_process::releasePass(out_pass);
      return false;
    }

    out_pass.source_var =
        out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Source");
    out_pass.depth_var =
        out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Depth");
    out_pass.bloom_var =
        out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Bloom");
    out_pass.history_var =
        out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_History");
    out_pass.sampler_var =
        out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Sampler");
    if (out_pass.sampler_var && sampler_color_) {
      out_pass.sampler_var->Set(sampler_color_);
    }
    return true;
  };

  if (!create_pass("Karma Bloom Prefilter Pipeline",
                   "bloom_prefilter_ps.hlsl",
                   ShaderFallback::Copy,
                   false,
                   false,
                   false,
                   post_process_bloom_prefilter_pass_) ||
      !create_pass("Karma Bloom Downsample Pipeline",
                   "bloom_downsample_ps.hlsl",
                   ShaderFallback::Copy,
                   false,
                   false,
                   false,
                   post_process_bloom_downsample_pass_) ||
      !create_pass("Karma Bloom Upsample Pipeline",
                   "bloom_upsample_combine_ps.hlsl",
                   ShaderFallback::BloomCombine,
                   false,
                   true,
                   false,
                   post_process_bloom_upsample_pass_) ||
      !create_pass("Karma Post Process Composite Pipeline",
                   "final_composite_ps.hlsl",
                   ShaderFallback::Composite,
                   true,
                   true,
                   false,
                   post_process_composite_pass_) ||
      !create_pass("Karma Post Process Temporal Pipeline",
                   "temporal_resolve_ps.hlsl",
                   ShaderFallback::Temporal,
                   false,
                   false,
                   true,
                   post_process_temporal_pass_)) {
    post_process::releasePass(post_process_bloom_prefilter_pass_);
    post_process::releasePass(post_process_bloom_downsample_pass_);
    post_process::releasePass(post_process_bloom_upsample_pass_);
    post_process::releasePass(post_process_composite_pass_);
    post_process::releasePass(post_process_temporal_pass_);
    return false;
  }

  post_process_pipeline_format_ = format;
  return true;
}

}  // namespace karma::renderer_backend
