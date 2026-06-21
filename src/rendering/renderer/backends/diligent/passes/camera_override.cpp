#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace karma::rendering::backend {

namespace {
template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}
}  // namespace

bool DiligentBackend::ensureCameraOverridePipeline(const rendering::CameraData& camera) {
  if (!device_ || !swap_chain_ || !constants_) {
    return false;
  }
  if (camera.shader_override_vertex_path.empty() ||
      camera.shader_override_fragment_path.empty()) {
    return false;
  }
  if (camera_override_pipeline_state_ &&
      camera.shader_override_vertex_path == camera_override_vertex_path_ &&
      camera.shader_override_fragment_path == camera_override_fragment_path_) {
    return true;
  }

  camera_override_pipeline_state_.Release();
  camera_override_srb_.Release();
  camera_override_vertex_path_.clear();
  camera_override_fragment_path_.clear();

  const std::vector<unsigned char> vs_bytes = readFileBytes(camera.shader_override_vertex_path);
  const std::vector<unsigned char> ps_bytes = readFileBytes(camera.shader_override_fragment_path);
  if (vs_bytes.empty() || ps_bytes.empty()) {
    return false;
  }
  std::string vs_source(vs_bytes.begin(), vs_bytes.end());
  std::string ps_source(ps_bytes.begin(), ps_bytes.end());

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Camera Override VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = vs_source.c_str();
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Camera Override PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = ps_source.c_str();
  ps = device_with_cache_.CreateShader(shader_ci);
  if (!vs || !ps) {
    return false;
  }

  Diligent::GraphicsPipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.Name = "Karma Camera Override Pipeline";
  pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso_ci.pVS = vs;
  pso_ci.pPS = ps;

  auto& graphics = pso_ci.GraphicsPipeline;
  graphics.NumRenderTargets = 1;
  graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                       : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
  graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                   : Diligent::TEX_FORMAT_D32_FLOAT;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
  graphics.RasterizerDesc.FrontCounterClockwise = true;
  graphics.DepthStencilDesc.DepthEnable = true;
  graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;

  constexpr Diligent::Uint32 kInstanceStride = static_cast<Diligent::Uint32>(sizeof(float) * 20);
  Diligent::LayoutElement layout_elems[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{3, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false,
                              0u,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(sizeof(float) * 4),
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(sizeof(float) * 8),
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(sizeof(float) * 12),
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}};
  graphics.InputLayout.LayoutElements = layout_elems;
  graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "CameraOverrideUser",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}};
  pso_ci.PSODesc.ResourceLayout.Variables = vars;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
  pso_ci.PSODesc.ResourceLayout.ImmutableSamplers = nullptr;
  pso_ci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;

  const auto pso_start = core::SteadyClock::now();
  camera_override_pipeline_state_ = device_with_cache_.CreateGraphicsPipelineState(pso_ci);
  recordPipelineCreation("camera_override",
                         "Karma Camera Override Pipeline",
                         pso_start,
                         core::SteadyClock::now());
  if (!camera_override_pipeline_state_) {
    return false;
  }

  bool constants_bound = false;
  if (auto* variable =
          camera_override_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX,
                                                                   "Constants")) {
    variable->Set(constants_);
    constants_bound = true;
  }
  if (auto* variable =
          camera_override_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                                   "Constants")) {
    variable->Set(constants_);
    constants_bound = true;
  }
  if (!constants_bound) {
    camera_override_pipeline_state_.Release();
    return false;
  }
  if (camera_override_user_constants_) {
    if (auto* variable =
            camera_override_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                                     "CameraOverrideUser")) {
      variable->Set(camera_override_user_constants_);
    }
  }
  const auto srb_start = core::SteadyClock::now();
  camera_override_pipeline_state_->CreateShaderResourceBinding(&camera_override_srb_, true);
  recordResourceCreation("camera_override",
                         "camera override SRB",
                         srb_start,
                         core::SteadyClock::now());
  camera_override_vertex_path_ = camera.shader_override_vertex_path;
  camera_override_fragment_path_ = camera.shader_override_fragment_path;
  return true;
}

void DiligentBackend::updateCameraOverrideUserConstants(const rendering::CameraData& camera) {
  if (!context_ || !camera_override_user_constants_) {
    return;
  }
  CameraOverrideUserConstants constants{};
  const uint32_t count =
      std::min(camera.shader_user_param_count, rendering::kCameraShaderUserParamCapacity);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& p = camera.shader_user_params[i];
    constants.user_key_hashes[i][0] = p.key_hash;
    constants.user_key_hashes[i][1] = 0u;
    constants.user_key_hashes[i][2] = 0u;
    constants.user_key_hashes[i][3] = 0u;
    constants.user_values[i][0] = p.value.r;
    constants.user_values[i][1] = p.value.g;
    constants.user_values[i][2] = p.value.b;
    constants.user_values[i][3] = p.value.a;
  }
  constants.user_meta[0] = static_cast<float>(count);
  constants.user_meta[1] = 0.0f;
  constants.user_meta[2] = 0.0f;
  constants.user_meta[3] = 0.0f;
  Diligent::MapHelper<CameraOverrideUserConstants> mapped(context_,
                                                          camera_override_user_constants_,
                                                          Diligent::MAP_WRITE,
                                                          Diligent::MAP_FLAG_DISCARD);
  auto* mapped_constants = getMappedData(mapped);
  if (mapped_constants == nullptr) {
    return;
  }
  *mapped_constants = constants;
}

}  // namespace karma::rendering::backend
