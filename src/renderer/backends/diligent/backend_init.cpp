#include "karma/renderer/backends/diligent/backend.hpp"

#include "karma/platform/window.h"

#include "backend_internal.h"


#include <Primitives/interface/BasicTypes.h>
#include <Primitives/interface/DebugOutput.h>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <Platforms/interface/NativeWindow.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(BZ3_WINDOW_BACKEND_SDL)
  #include <GLFW/glfw3.h>
  #include <GLFW/glfw3native.h>
#endif

namespace karma::renderer_backend {

namespace {
bool envFlagEnabled(const char* name) {
  if (const char* value = std::getenv(name)) {
    if (value[0] == '\0') {
      return false;
    }
    return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "FALSE") != 0 && std::strcmp(value, "off") != 0 &&
           std::strcmp(value, "OFF") != 0;
  }
  return false;
}

void DILIGENT_CALL_TYPE IgnoreDiligentMessage(Diligent::DEBUG_MESSAGE_SEVERITY,
                                              const char*,
                                              const char*,
                                              const char*,
                                              int) {}

void DILIGENT_CALL_TYPE LogDiligentMessage(Diligent::DEBUG_MESSAGE_SEVERITY severity,
                                           const char* message,
                                           const char* function,
                                           const char* file,
                                           int line) {
  const char* severity_name = "INFO";
  switch (severity) {
    case Diligent::DEBUG_MESSAGE_SEVERITY_INFO:
      severity_name = "INFO";
      break;
    case Diligent::DEBUG_MESSAGE_SEVERITY_WARNING:
      severity_name = "WARN";
      break;
    case Diligent::DEBUG_MESSAGE_SEVERITY_ERROR:
      severity_name = "ERROR";
      break;
    case Diligent::DEBUG_MESSAGE_SEVERITY_FATAL_ERROR:
      severity_name = "FATAL";
      break;
    default:
      break;
  }

  std::fprintf(stderr, "[Diligent][%s] %s", severity_name, message ? message : "(null)");
  if ((file && file[0] != '\0') || (function && function[0] != '\0')) {
    std::fprintf(stderr,
                 " (%s%s%s%s%d)",
                 file && file[0] != '\0' ? file : "",
                 file && file[0] != '\0' && function && function[0] != '\0' ? ":" : "",
                 function && function[0] != '\0' ? function : "",
                 line > 0 ? ":" : "",
                 line > 0 ? line : 0);
  }
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

static constexpr const char* kShadowVertexShader = R"(
cbuffer Constants
{
    float4x4 g_MVP;
    float4x4 g_Model;
    float4x4 g_LightViewProj;
    float4x4 g_ShadowUVProj;
    float4x4 g_ShadowCascadeUVProj[4];
    float4x4 g_PointShadowUVProj[96];
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_PointShadowParams;
    float4 g_LocalLightParams;
    float4 g_PointShadowTuning;
    float4 g_ShadowBiasParams;
    float4 g_ShadowCascadeSplits;
    float4 g_ShadowCascadeWorldTexel;
    float4 g_ShadowCascadeParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_CameraForward;
    float4 g_ScreenParams;
    float4 g_CameraClipParams;
    float4 g_ForwardPlusParams;
    float4 g_LocalLightPositionRange[64];
    float4 g_LocalLightDirectionType[64];
    float4 g_LocalLightColorIntensity[64];
    float4 g_LocalLightSpotParams[64];
    float4 g_LocalLightMeta;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
    float4 ModelCol0 : ATTRIB4;
    float4 ModelCol1 : ATTRIB5;
    float4 ModelCol2 : ATTRIB6;
    float4 ModelCol3 : ATTRIB7;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = input.ModelCol0 * input.Pos.x +
                       input.ModelCol1 * input.Pos.y +
                       input.ModelCol2 * input.Pos.z +
                       input.ModelCol3;
    output.Pos = mul(g_MVP, world_pos);
    return output;
}
)";
}  // namespace

void DiligentBackend::recreateShadowMap() {
  if (!device_) {
    return;
  }
  const auto& adapter = device_->GetAdapterInfo();
  const int max_dim = static_cast<int>(adapter.Texture.MaxTexture2DDimension);
  if (max_dim > 0 && shadow_map_size_ > max_dim) {
    shadow_map_size_ = max_dim;
  }
  shadow_map_tex_.Release();
  shadow_map_srv_.Release();
  shadow_map_dsv_.Release();
  for (auto& dsv : shadow_map_dsv_cascades_) {
    dsv.Release();
  }

  Diligent::TextureDesc shadow_desc{};
  shadow_desc.Name = "Karma Shadow Map";
  shadow_desc.Type = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
  shadow_desc.Width = static_cast<Diligent::Uint32>(shadow_map_size_);
  shadow_desc.Height = static_cast<Diligent::Uint32>(shadow_map_size_);
  shadow_desc.ArraySize = static_cast<Diligent::Uint32>(kShadowCascadeCount);
  shadow_desc.MipLevels = 1;
  shadow_desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
  shadow_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
  device_->CreateTexture(shadow_desc, nullptr, &shadow_map_tex_);
  if (shadow_map_tex_) {
    Diligent::TextureViewDesc srv_desc{};
    srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
    srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
    srv_desc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    srv_desc.MostDetailedMip = 0;
    srv_desc.NumMipLevels = 1;
    srv_desc.FirstArraySlice = 0;
    srv_desc.NumArraySlices = static_cast<Diligent::Uint32>(kShadowCascadeCount);
    shadow_map_tex_->CreateView(srv_desc, &shadow_map_srv_);
    if (!shadow_map_srv_) {
      if (auto* srv = shadow_map_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)) {
        shadow_map_srv_ = srv;
      }
    }
    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
      Diligent::TextureViewDesc dsv_desc{};
      dsv_desc.ViewType = Diligent::TEXTURE_VIEW_DEPTH_STENCIL;
      dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
      dsv_desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
      dsv_desc.MostDetailedMip = 0;
      dsv_desc.NumMipLevels = 1;
      dsv_desc.FirstArraySlice = static_cast<Diligent::Uint32>(cascade);
      dsv_desc.NumArraySlices = 1;
      shadow_map_tex_->CreateView(dsv_desc, &shadow_map_dsv_cascades_[cascade]);
    }
    shadow_map_dsv_ = shadow_map_dsv_cascades_[0];
  } else {
  }
  if (!shadow_map_srv_ || !shadow_map_dsv_) {
  }
  if (shadow_map_srv_) {
    for (auto* pso : {pipeline_state_.RawPtr(),
                      transparent_pipeline_state_.RawPtr(),
                      transparent_double_sided_pipeline_state_.RawPtr(),
                      additive_pipeline_state_.RawPtr(),
                      additive_double_sided_pipeline_state_.RawPtr()}) {
      if (!pso) {
        continue;
      }
      if (auto* var =
              pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
        var->Set(shadow_map_srv_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (auto* var =
              pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap")) {
        var->Set(point_shadow_map_srv_ ? point_shadow_map_srv_ : shadow_map_srv_,
                 Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    }
  }
  directional_shadow_cache_valid_ = false;
}

void DiligentBackend::recreatePointShadowMap() {
  if (!device_) {
    return;
  }
  const auto& adapter = device_->GetAdapterInfo();
  const int max_dim = static_cast<int>(adapter.Texture.MaxTexture2DDimension);
  if (max_dim > 0 && point_shadow_map_size_ > max_dim) {
    point_shadow_map_size_ = max_dim;
  }

  point_shadow_map_tex_.Release();
  point_shadow_map_srv_.Release();
  for (auto& dsv : point_shadow_map_dsv_faces_) {
    dsv.Release();
  }

  Diligent::TextureDesc shadow_desc{};
  shadow_desc.Name = "Karma Point Shadow Map";
  shadow_desc.Type = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
  shadow_desc.Width = static_cast<Diligent::Uint32>(point_shadow_map_size_);
  shadow_desc.Height = static_cast<Diligent::Uint32>(point_shadow_map_size_);
  const int active_point_shadow_lights = std::clamp(point_shadow_max_lights_, 1, kMaxPointShadowLights);
  const int active_point_shadow_faces = active_point_shadow_lights * kPointShadowFaceCount;
  shadow_desc.ArraySize = static_cast<Diligent::Uint32>(active_point_shadow_faces);
  shadow_desc.MipLevels = 1;
  shadow_desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
  shadow_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
  device_->CreateTexture(shadow_desc, nullptr, &point_shadow_map_tex_);
  if (point_shadow_map_tex_) {
    Diligent::TextureViewDesc srv_desc{};
    srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
    srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
    srv_desc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    srv_desc.MostDetailedMip = 0;
    srv_desc.NumMipLevels = 1;
    srv_desc.FirstArraySlice = 0;
    srv_desc.NumArraySlices = static_cast<Diligent::Uint32>(active_point_shadow_faces);
    point_shadow_map_tex_->CreateView(srv_desc, &point_shadow_map_srv_);
    if (!point_shadow_map_srv_) {
      if (auto* srv = point_shadow_map_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)) {
        point_shadow_map_srv_ = srv;
      }
    }
    for (int face = 0; face < active_point_shadow_faces; ++face) {
      Diligent::TextureViewDesc dsv_desc{};
      dsv_desc.ViewType = Diligent::TEXTURE_VIEW_DEPTH_STENCIL;
      dsv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
      dsv_desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
      dsv_desc.MostDetailedMip = 0;
      dsv_desc.NumMipLevels = 1;
      dsv_desc.FirstArraySlice = static_cast<Diligent::Uint32>(face);
      dsv_desc.NumArraySlices = 1;
      point_shadow_map_tex_->CreateView(dsv_desc, &point_shadow_map_dsv_faces_[face]);
    }
  }

  for (auto* pso : {pipeline_state_.RawPtr(),
                    transparent_pipeline_state_.RawPtr(),
                    transparent_double_sided_pipeline_state_.RawPtr(),
                    additive_pipeline_state_.RawPtr(),
                    additive_double_sided_pipeline_state_.RawPtr()}) {
    if (!pso) {
      continue;
    }
    if (auto* var = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                 "g_PointShadowMap")) {
      var->Set(point_shadow_map_srv_ ? point_shadow_map_srv_ : shadow_map_srv_,
               Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
  point_shadow_cache_initialized_ = false;
  point_shadow_slot_valid_.fill(false);
  point_shadow_slot_source_index_.fill(-1);
  point_shadow_face_dirty_.fill(1u);
  point_shadow_face_cursor_ = 0;
  for (auto& m : cached_point_shadow_uv_proj_) {
    m = glm::mat4(1.0f);
  }
}

void DiligentBackend::recreateShadowPipeline() {
  shadow_pipeline_state_.Release();
  shadow_srb_.Release();
  if (!device_) {
    return;
  }

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.Desc.Name = "Karma Shadow VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kShadowVertexShader;
  Diligent::RefCntAutoPtr<Diligent::IShader> shadow_vs = device_with_cache_.CreateShader(shader_ci);
  if (!shadow_vs) {
    return;
  }

  Diligent::GraphicsPipelineStateCreateInfo shadow_pso{};
  shadow_pso.PSODesc.Name = "Karma Shadow Pipeline";
  shadow_pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  shadow_pso.pVS = shadow_vs;
  shadow_pso.pPS = nullptr;

  auto& shadow_graphics = shadow_pso.GraphicsPipeline;
  shadow_graphics.NumRenderTargets = 0;
  shadow_graphics.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
  shadow_graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  // Mixed imported assets can have inconsistent winding; disable culling in
  // the shadow pass so casters still contribute.
  shadow_graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
  shadow_graphics.RasterizerDesc.FrontCounterClockwise = true;
  shadow_graphics.RasterizerDesc.DepthBias = shadow_raster_depth_bias_;
  shadow_graphics.RasterizerDesc.SlopeScaledDepthBias = shadow_raster_slope_bias_;
  shadow_graphics.DepthStencilDesc.DepthEnable = true;
  shadow_graphics.DepthStencilDesc.DepthWriteEnable = true;
  shadow_graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;

  constexpr Diligent::Uint32 kInstanceStride = static_cast<Diligent::Uint32>(sizeof(float) * 16);
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
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
  };
  shadow_graphics.InputLayout.LayoutElements = layout_elems;
  shadow_graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc shadow_vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
  };
  shadow_pso.PSODesc.ResourceLayout.Variables = shadow_vars;
  shadow_pso.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(shadow_vars) / sizeof(shadow_vars[0]));

  shadow_pipeline_state_ = device_with_cache_.CreateGraphicsPipelineState(shadow_pso);
  if (!shadow_pipeline_state_) {
    return;
  }

  if (constants_) {
    if (auto* variable =
            shadow_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      variable->Set(constants_);
    }
  }
  shadow_pipeline_state_->CreateShaderResourceBinding(&shadow_srb_, true);
}

void DiligentBackend::initializeDevice() {
#if defined(ENGINE_FORCE_VULKAN)
  (void)window_;
#endif
  const bool enable_vk_validation = envFlagEnabled("KARMA_VK_VALIDATION");
  const bool enable_diligent_debug =
      enable_vk_validation || envFlagEnabled("KARMA_DILIGENT_DEBUG");
  auto* message_callback = enable_diligent_debug ? LogDiligentMessage : IgnoreDiligentMessage;

  Diligent::SetDebugMessageCallback(message_callback);
  Diligent::RefCntAutoPtr<Diligent::IEngineFactoryVk> factory;
  Diligent::EngineVkCreateInfo engine_ci{};

#if defined(PLATFORM_LINUX)
  factory = Diligent::GetEngineFactoryVk();
#else
  factory = Diligent::GetEngineFactoryVk();
#endif

  if (!factory) {
    return;
  }

  factory->SetMessageCallback(message_callback);
  if (enable_vk_validation) {
    engine_ci.SetValidationLevel(Diligent::VALIDATION_LEVEL_1);
    engine_ci.EnableValidation = true;
    engine_ci.ValidationFlags |= Diligent::VALIDATION_FLAG_CHECK_SHADER_BUFFER_SIZE;
    std::fprintf(stderr, "[Karma] Vulkan validation enabled via KARMA_VK_VALIDATION=1\n");
    std::fflush(stderr);
  }

  if (window_) {
#if !defined(BZ3_WINDOW_BACKEND_SDL)
    Diligent::NativeWindow native = toNativeWindow(static_cast<GLFWwindow*>(window_->nativeHandle()));
    Diligent::SwapChainDesc sc_desc{};
    sc_desc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    sc_desc.DepthBufferFormat = Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
    sc_desc.Width = static_cast<Diligent::Uint32>(current_width_);
    sc_desc.Height = static_cast<Diligent::Uint32>(current_height_);
    sc_desc.BufferCount = 2;
    sc_desc.Usage = Diligent::SWAP_CHAIN_USAGE_RENDER_TARGET;
    factory->CreateDeviceAndContextsVk(engine_ci, &device_, &context_);
    if (device_) {
      factory->CreateSwapChainVk(device_, context_, sc_desc, native, &swap_chain_);
    }
#else
    factory->CreateDeviceAndContextsVk(engine_ci, &device_, &context_);
#endif
  } else {
    factory->CreateDeviceAndContextsVk(engine_ci, &device_, &context_);
  }

  if (!device_ || !context_) {
  }

  if (!device_) {
    return;
  }

  device_with_cache_ = Diligent::RenderDeviceWithCache<false>{device_};
  if (shader_cache_enabled_) {
    Diligent::RenderStateCacheCreateInfo cache_ci{};
    device_with_cache_.CreateRenderStateCache(cache_ci);
    if (!device_with_cache_.GetCache()) {
    }
    std::error_code ec;
    std::filesystem::create_directories(render_state_cache_path_.parent_path(), ec);
    const bool cache_exists = std::filesystem::exists(render_state_cache_path_);
    if (shader_cache_log_) {
    }
    device_with_cache_.LoadCacheFromFile(render_state_cache_path_.string().c_str(),
                                         true,
                                         shader_cache_version_);
    if (shader_cache_log_) {
      const bool exists_after = std::filesystem::exists(render_state_cache_path_);
      (void)exists_after;
    }
  } else if (shader_cache_log_) {
  }

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

  static constexpr const char* kVertexShader = R"(
cbuffer Constants
{
    float4x4 g_MVP;
    float4x4 g_Model;
    float4x4 g_LightViewProj;
    float4x4 g_ShadowUVProj;
    float4x4 g_ShadowCascadeUVProj[4];
    float4x4 g_PointShadowUVProj[96];
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_PointShadowParams;
    float4 g_LocalLightParams;
    float4 g_PointShadowTuning;
    float4 g_ShadowBiasParams;
    float4 g_ShadowCascadeSplits;
    float4 g_ShadowCascadeWorldTexel;
    float4 g_ShadowCascadeParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_CameraForward;
    float4 g_ScreenParams;
    float4 g_CameraClipParams;
    float4 g_ForwardPlusParams;
    float4 g_LocalLightPositionRange[64];
    float4 g_LocalLightDirectionType[64];
    float4 g_LocalLightColorIntensity[64];
    float4 g_LocalLightSpotParams[64];
    float4 g_LocalLightMeta;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
    float4 ModelCol0 : ATTRIB4;
    float4 ModelCol1 : ATTRIB5;
    float4 ModelCol2 : ATTRIB6;
    float4 ModelCol3 : ATTRIB7;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    float2 UV : TEXCOORD0;
    float4 Tangent : TEXCOORD1;
    float3 WorldPos : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = input.ModelCol0 * input.Pos.x +
                       input.ModelCol1 * input.Pos.y +
                       input.ModelCol2 * input.Pos.z +
                       input.ModelCol3;
    output.Pos = mul(g_MVP, world_pos);
    output.WorldPos = world_pos.xyz;
    output.Normal = normalize(input.ModelCol0.xyz * input.Normal.x +
                              input.ModelCol1.xyz * input.Normal.y +
                              input.ModelCol2.xyz * input.Normal.z);
    output.UV = input.UV;
    output.Tangent = input.Tangent;
    return output;
}
)";

  static constexpr const char* kPixelShader = R"(
cbuffer Constants
{
    float4x4 g_MVP;
    float4x4 g_Model;
    float4x4 g_LightViewProj;
    float4x4 g_ShadowUVProj;
    float4x4 g_ShadowCascadeUVProj[4];
    float4x4 g_PointShadowUVProj[96];
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_PointShadowParams;
    float4 g_LocalLightParams;
    float4 g_PointShadowTuning;
    float4 g_ShadowBiasParams;
    float4 g_ShadowCascadeSplits;
    float4 g_ShadowCascadeWorldTexel;
    float4 g_ShadowCascadeParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_CameraForward;
    float4 g_ScreenParams;
    float4 g_CameraClipParams;
    float4 g_ForwardPlusParams;
    float4 g_LocalLightPositionRange[64];
    float4 g_LocalLightDirectionType[64];
    float4 g_LocalLightColorIntensity[64];
    float4 g_LocalLightSpotParams[64];
    float4 g_LocalLightMeta;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
};

Texture2D g_BaseColorTex;
Texture2D g_NormalTex;
Texture2D g_MetallicRoughnessTex;
Texture2D g_OcclusionTex;
Texture2D g_EmissiveTex;
TextureCube g_IrradianceTex;
TextureCube g_PrefilterTex;
Texture2D g_BRDFLUT;
Texture2D g_SceneColor;
Texture2D<float> g_SceneDepth;
Texture2DArray<float> g_ShadowMap;
Texture2DArray<float> g_PointShadowMap;
SamplerState g_SamplerColor;
SamplerState g_SamplerData;
SamplerComparisonState g_ShadowSampler;

struct ForwardPlusLight
{
    float4 position_range;
    float4 direction_type;
    float4 color_intensity;
    float4 spot_params;
    float4 screen_rect;
};

StructuredBuffer<ForwardPlusLight> g_ForwardPlusLights;
StructuredBuffer<uint> g_ForwardPlusTileLightCounts;
StructuredBuffer<uint> g_ForwardPlusTileLightIndices;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    float2 UV : TEXCOORD0;
    float4 Tangent : TEXCOORD1;
    float3 WorldPos : TEXCOORD2;
    bool FrontFace : SV_IsFrontFace;
};

float SampleCascadeShadow(uint cascade_idx,
                          float3 world_pos,
                          float3 geom_n,
                          float3 l_dir,
                          float slope,
                          float normal_scale,
                          float receiver_scale)
{
    float shadow = 1.0;
    float world_texel = max(g_ShadowCascadeWorldTexel[cascade_idx], 0.0);
    float normal_offset_ws = world_texel * normal_scale * (0.4 + 1.2 * slope);
    float light_offset_ws = world_texel * receiver_scale * (0.03 + 0.07 * slope);
    float3 shadow_world_pos = world_pos + geom_n * normal_offset_ws + l_dir * light_offset_ws;
    float4 shadow_uv_depth = mul(g_ShadowCascadeUVProj[cascade_idx], float4(shadow_world_pos, 1.0));
    shadow_uv_depth.xyz /= max(shadow_uv_depth.w, 1e-7);
    float2 shadow_uv = shadow_uv_depth.xy;
    float shadow_depth = max(shadow_uv_depth.z, 1e-7);
    if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 &&
        shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0 &&
        shadow_depth >= 0.0 && shadow_depth <= 1.0)
    {
        int radius = (int)g_ShadowParams.z;
        radius = clamp(radius, 0, 4);
        float const_bias = max(g_ShadowParams.y, 0.0);
        float receiver_plane_bias = abs(ddx(shadow_depth)) + abs(ddy(shadow_depth));
        float texel_size = max(g_ShadowParams.w, 0.0);
        float slope_texel_bias = texel_size * normal_scale * (0.45 + 0.9 * slope);
        float receiver_bias = receiver_plane_bias * (0.5 * receiver_scale);
        float bias = const_bias + receiver_bias + slope_texel_bias;
        bias = min(bias, 0.01);
        if (radius == 0)
        {
            shadow = g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                    float3(shadow_uv, (float)cascade_idx),
                                                    shadow_depth - bias);
        }
        else
        {
            float2 texel = float2(g_ShadowParams.w, g_ShadowParams.w);
            float sum = 0.0;
            int count = 0;
            [unroll]
            for (int y = -4; y <= 4; ++y)
            {
                [unroll]
                for (int x = -4; x <= 4; ++x)
                {
                    if (abs(x) <= radius && abs(y) <= radius)
                    {
                        float2 offset = float2((float)x, (float)y) * texel;
                        sum += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                              float3(shadow_uv + offset,
                                                                     (float)cascade_idx),
                                                              shadow_depth - bias);
                        count += 1;
                    }
                }
            }
            shadow = (count > 0) ? (sum / count) : 1.0;
        }
    }
    return shadow;
}

uint SelectPointShadowFace(float3 dir_ws)
{
    float3 a = abs(dir_ws);
    if (a.x >= a.y && a.x >= a.z)
    {
        return dir_ws.x >= 0.0 ? 0u : 1u;
    }
    if (a.y >= a.x && a.y >= a.z)
    {
        return dir_ws.y >= 0.0 ? 2u : 3u;
    }
    return dir_ws.z >= 0.0 ? 4u : 5u;
}

uint SelectPointShadowFaceForAxis(uint axis, float signed_component)
{
    if (axis == 0u)
    {
        return signed_component >= 0.0 ? 0u : 1u;
    }
    if (axis == 1u)
    {
        return signed_component >= 0.0 ? 2u : 3u;
    }
    return signed_component >= 0.0 ? 4u : 5u;
}

float SamplePointShadowFace(uint shadow_slot,
                            uint face,
                            float3 shadow_world_pos,
                            float texel_size,
                            float slope,
                            out float valid)
{
    uint matrix_idx = shadow_slot * 6u + face;
    float4 shadow_uv_depth = mul(g_PointShadowUVProj[matrix_idx], float4(shadow_world_pos, 1.0));
    shadow_uv_depth.xyz /= max(shadow_uv_depth.w, 1e-7);
    float2 shadow_uv = shadow_uv_depth.xy;
    float shadow_depth = max(shadow_uv_depth.z, 1e-7);
    if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
        shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||
        shadow_depth < 0.0 || shadow_depth > 1.0)
    {
        valid = 0.0;
        return 1.0;
    }

    float const_bias = max(g_PointShadowTuning.x, 0.0);
    float slope_bias = texel_size * max(g_PointShadowTuning.y, 0.0) * (0.4 + slope);
    float receiver_bias = (abs(ddx(shadow_depth)) + abs(ddy(shadow_depth))) *
                          max(g_PointShadowTuning.w, 0.0);
    float bias = const_bias + slope_bias + receiver_bias;
    bias = min(bias, 0.04);
    float compare_depth = shadow_depth - bias;

    int radius = clamp((int)g_ShadowParams.z, 0, 1);
    float shadow = 1.0;
    if (radius == 0)
    {
        shadow = g_PointShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                     float3(shadow_uv, (float)matrix_idx),
                                                     compare_depth);
    }
    else
    {
        float2 texel = float2(texel_size, texel_size);
        float sum = 0.0;
        int count = 0;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                if (abs(x) <= radius && abs(y) <= radius)
                {
                    float2 offset = float2((float)x, (float)y) * texel;
                    sum += g_PointShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                               float3(shadow_uv + offset,
                                                                      (float)matrix_idx),
                                                               compare_depth);
                    count += 1;
                }
            }
        }
        shadow = (count > 0) ? (sum / count) : 1.0;
    }

    valid = 1.0;
    return shadow;
}

float SamplePointShadow(ForwardPlusLight light,
                        float3 world_pos,
                        float3 geom_n,
                        float3 l_local)
{
    if (g_PointShadowParams.x < 0.5 || light.direction_type.w < 0.5 || light.direction_type.w > 1.5)
    {
        return 1.0;
    }

    float shadow_slot_f = light.spot_params.z;
    if (shadow_slot_f < 0.0)
    {
        return 1.0;
    }
    uint shadow_slot = (uint)(shadow_slot_f + 0.5);
    uint active_slots = (uint)max(g_PointShadowParams.w, 0.0);
    if (shadow_slot >= active_slots || shadow_slot >= 16u)
    {
        return 1.0;
    }

    float texel_size = max(g_PointShadowParams.y, 0.0);
    float slope = 1.0 - saturate(dot(geom_n, l_local));
    float normal_ws = texel_size * max(g_PointShadowTuning.z, 0.0) * (0.5 + slope);
    float3 shadow_world_pos = world_pos + geom_n * normal_ws;
    float3 to_sample = shadow_world_pos - light.position_range.xyz;
    float3 a = abs(to_sample);
    float dominant = max(max(a.x, a.y), a.z);
    if (dominant <= 1e-6)
    {
        return 1.0;
    }

    float seam_start = 0.85;
    float valid = 0.0;
    float shadow_sum = 0.0;
    float weight_sum = 0.0;

    uint main_face = SelectPointShadowFace(to_sample);
    float main_shadow = SamplePointShadowFace(shadow_slot,
                                              main_face,
                                              shadow_world_pos,
                                              texel_size,
                                              slope,
                                              valid);
    if (valid > 0.5)
    {
        shadow_sum += main_shadow;
        weight_sum += 1.0;
    }

    uint seam_axis0 = 3u;
    uint seam_axis1 = 3u;
    float seam_ratio0 = 0.0;
    float seam_ratio1 = 0.0;
    if (a.x >= a.y && a.x >= a.z)
    {
        seam_axis0 = 1u;
        seam_axis1 = 2u;
        seam_ratio0 = a.y / dominant;
        seam_ratio1 = a.z / dominant;
    }
    else if (a.y >= a.x && a.y >= a.z)
    {
        seam_axis0 = 0u;
        seam_axis1 = 2u;
        seam_ratio0 = a.x / dominant;
        seam_ratio1 = a.z / dominant;
    }
    else
    {
        seam_axis0 = 0u;
        seam_axis1 = 1u;
        seam_ratio0 = a.x / dominant;
        seam_ratio1 = a.y / dominant;
    }

    [unroll]
    for (uint seam_idx = 0u; seam_idx < 2u; ++seam_idx)
    {
        uint seam_axis = seam_idx == 0u ? seam_axis0 : seam_axis1;
        float seam_ratio = seam_idx == 0u ? seam_ratio0 : seam_ratio1;
        if (seam_axis > 2u || seam_ratio <= seam_start)
        {
            continue;
        }

        float blend_weight = saturate((seam_ratio - seam_start) / max(1.0 - seam_start, 1e-4));
        if (blend_weight <= 0.0)
        {
            continue;
        }

        float signed_component = seam_axis == 0u ? to_sample.x : (seam_axis == 1u ? to_sample.y : to_sample.z);
        uint seam_face = SelectPointShadowFaceForAxis(seam_axis, signed_component);
        float seam_shadow = SamplePointShadowFace(shadow_slot,
                                                  seam_face,
                                                  shadow_world_pos,
                                                  texel_size,
                                                  slope,
                                                  valid);
        if (valid > 0.5)
        {
            shadow_sum += seam_shadow * blend_weight;
            weight_sum += blend_weight;
        }
    }

    return weight_sum > 0.0 ? (shadow_sum / weight_sum) : 1.0;
}

void AccumulateLocalLight(ForwardPlusLight light,
                          float3 world_pos,
                          float3 geom_n,
                          float3 n,
                          float3 v,
                          float shininess,
                          float3 base_color,
                          float3 spec_color,
                          inout float3 lit,
                          inout float local_shadow_lift_energy)
{
    float3 to_light = light.position_range.xyz - world_pos;
    float dist = length(to_light);
    if (dist <= 1e-4 || dist >= light.position_range.w)
    {
        return;
    }
    float3 l_local = to_light / dist;
    float local_ndotl = max(dot(n, l_local), 0.0);
    if (local_ndotl <= 0.0)
    {
        return;
    }
    float dist_sq = max(dot(to_light, to_light), 1e-4);
    float range_t = saturate(dist / light.position_range.w);
    float range_falloff = saturate(1.0 - range_t);
    range_falloff = range_falloff * range_falloff * (3.0 - 2.0 * range_falloff);
    range_falloff = pow(range_falloff, max(g_LocalLightParams.y, 0.1));
    // Inverse-square attenuation with soft cutoff at the authored light range.
    float softening = max(g_LocalLightParams.x, 0.0);
    float atten = range_falloff / max(dist_sq + softening, 1e-4);
    // Spot lights: direction_type.w == 2, directional/point are handled elsewhere.
    if (light.direction_type.w > 1.5)
    {
        float3 spot_dir = normalize(-light.direction_type.xyz);
        float cone = dot(spot_dir, l_local);
        float inner_cos = light.spot_params.x;
        float outer_cos = light.spot_params.y;
        float denom = max(inner_cos - outer_cos, 1e-4);
        float spot = saturate((cone - outer_cos) / denom);
        atten *= spot;
    }
    if (atten <= 0.0)
    {
        return;
    }
    float3 h_local = normalize(v + l_local);
    float local_spec = pow(max(dot(n, h_local), 0.0), shininess);
    float point_shadow = SamplePointShadow(light, world_pos, geom_n, l_local);
    float3 light_color = light.color_intensity.rgb * light.color_intensity.w * atten * point_shadow;
    lit += base_color * light_color * local_ndotl;
    lit += spec_color * light_color * local_spec;
    float local_luminance = dot(light_color, float3(0.2126, 0.7152, 0.0722));
    local_shadow_lift_energy += local_luminance * local_ndotl;
}

float Hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float Noise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = Hash13(i + float3(0.0, 0.0, 0.0));
    float n100 = Hash13(i + float3(1.0, 0.0, 0.0));
    float n010 = Hash13(i + float3(0.0, 1.0, 0.0));
    float n110 = Hash13(i + float3(1.0, 1.0, 0.0));
    float n001 = Hash13(i + float3(0.0, 0.0, 1.0));
    float n101 = Hash13(i + float3(1.0, 0.0, 1.0));
    float n011 = Hash13(i + float3(0.0, 1.0, 1.0));
    float n111 = Hash13(i + float3(1.0, 1.0, 1.0));

    float n00 = lerp(n000, n100, f.x);
    float n10 = lerp(n010, n110, f.x);
    float n01 = lerp(n001, n101, f.x);
    float n11 = lerp(n011, n111, f.x);
    float n0 = lerp(n00, n10, f.y);
    float n1 = lerp(n01, n11, f.y);
    return lerp(n0, n1, f.z);
}

float LinearizeSceneDepth(float depth)
{
    float near_clip = max(g_CameraClipParams.x, 0.001);
    float far_clip = max(g_CameraClipParams.y, near_clip + 0.001);
    if (g_CameraClipParams.z > 0.5)
    {
        return (near_clip * far_clip) /
               max(far_clip - depth * (far_clip - near_clip), 1.0e-4);
    }
    return near_clip + depth * (far_clip - near_clip);
}

float3 SafeNormalize(float3 v, float3 fallback)
{
    float len_sq = dot(v, v);
    if (len_sq <= 1.0e-8)
    {
        return fallback;
    }
    return v * rsqrt(len_sq);
}

float4 main(PSInput input) : SV_TARGET
{
    const float PI = 3.14159265;
    float3 geom_n = normalize(input.Normal);
    float3 n = geom_n;
    float3 t = normalize(input.Tangent.xyz);
    float3 b = normalize(cross(geom_n, t) * input.Tangent.w);
    float3 normal_tex = g_NormalTex.Sample(g_SamplerData, input.UV).xyz * 2.0 - 1.0;
    normal_tex.xy *= g_PbrParams.w;
    normal_tex = normalize(normal_tex);
    n = normalize(normal_tex.x * t + normal_tex.y * b + normal_tex.z * n);
    float3 l_dir = normalize(-g_LightDir.xyz);
    float ndotl = max(dot(n, l_dir), 0.0);
    float4 base_tex = g_BaseColorTex.Sample(g_SamplerColor, input.UV);
    float3 emissive_tex = g_EmissiveTex.Sample(g_SamplerColor, input.UV).rgb;
    float occlusion = g_OcclusionTex.Sample(g_SamplerData, input.UV).r;
    float2 mr = g_MetallicRoughnessTex.Sample(g_SamplerData, input.UV).bg;
    float metallic = saturate(mr.x * g_PbrParams.x);
    float roughness = saturate(mr.y * g_PbrParams.y);

    float3 base_color = g_BaseColorFactor.rgb * base_tex.rgb;
    float3 emissive = g_EmissiveFactor.rgb * emissive_tex;

    float3 v = normalize(g_CameraPos.xyz - input.WorldPos);
    float3 h = normalize(v + l_dir);
    float ndoth = max(dot(n, h), 0.0);
    float rough = max(roughness, 0.05);
    float shininess = 2.0 / (rough * rough) - 2.0;
    float spec = pow(ndoth, shininess);
    float3 spec_color = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);

    float shadow = 1.0;
    if (g_ShadowParams.x > 0.5)
    {
        float view_depth = dot(input.WorldPos - g_CameraPos.xyz, g_CameraForward.xyz);
        view_depth = max(view_depth, 0.0);
        uint cascade_idx = 0u;
        if (view_depth > g_ShadowCascadeSplits.x) cascade_idx = 1u;
        if (view_depth > g_ShadowCascadeSplits.y) cascade_idx = 2u;
        if (view_depth > g_ShadowCascadeSplits.z) cascade_idx = 3u;

        float ndotl_shadow = saturate(dot(geom_n, l_dir));
        float slope = 1.0 - ndotl_shadow;
        float normal_scale = max(g_ShadowBiasParams.y, 0.0);
        float receiver_scale = max(g_ShadowBiasParams.x, 0.0);

        shadow = SampleCascadeShadow(cascade_idx,
                                     input.WorldPos,
                                     geom_n,
                                     l_dir,
                                     slope,
                                     normal_scale,
                                     receiver_scale);

        if (cascade_idx < 3u)
        {
            float split_depth = g_ShadowCascadeSplits[cascade_idx];
            float transition_fraction = max(g_ShadowCascadeParams.x, 0.0);
            float transition_range = max(split_depth * transition_fraction, 0.25);
            float blend = saturate((view_depth - (split_depth - transition_range)) /
                                   max(transition_range, 1e-4));
            if (blend > 0.0)
            {
                float shadow_next = SampleCascadeShadow(cascade_idx + 1u,
                                                        input.WorldPos,
                                                        geom_n,
                                                        l_dir,
                                                        slope,
                                                        normal_scale,
                                                        receiver_scale);
                shadow = lerp(shadow, shadow_next, blend);
            }
        }
    }
    float3 lit_local = float3(0.0, 0.0, 0.0);
    float local_shadow_lift_energy = 0.0;
    uint cb_local_light_count = (uint)max(g_LocalLightMeta.x, 0.0);
    cb_local_light_count = min(cb_local_light_count, 64u);
    uint total_local_light_count = (uint)max(g_LocalLightMeta.y, 0.0);
    if (cb_local_light_count > 0u)
    {
        [loop]
        for (uint i = 0u; i < cb_local_light_count; ++i)
        {
            ForwardPlusLight light;
            light.position_range = g_LocalLightPositionRange[i];
            light.direction_type = g_LocalLightDirectionType[i];
            light.color_intensity = g_LocalLightColorIntensity[i];
            light.spot_params = g_LocalLightSpotParams[i];
            AccumulateLocalLight(light,
                                 input.WorldPos,
                                 geom_n,
                                 n,
                                 v,
                                 shininess,
                                 base_color,
                                 spec_color,
                                 lit_local,
                                 local_shadow_lift_energy);
        }
    }
    uint tile_size = (uint)max(g_ForwardPlusParams.x, 1.0);
    uint tiles_x = (uint)max(g_ForwardPlusParams.y, 1.0);
    uint tiles_y = (uint)max(g_ForwardPlusParams.z, 1.0);
    uint max_lights_per_tile = (uint)max(g_ForwardPlusParams.w, 0.0);
    if (cb_local_light_count == 0u && max_lights_per_tile > 0u)
    {
        uint safe_tiles_x = max(tiles_x, 1u);
        uint safe_tiles_y = max(tiles_y, 1u);
        uint2 pixel = uint2(input.Pos.xy);
        uint tile_x = min(pixel.x / tile_size, safe_tiles_x - 1u);
        uint tile_y = min(pixel.y / tile_size, safe_tiles_y - 1u);
        uint tile_idx = tile_y * safe_tiles_x + tile_x;
        uint light_count = min(g_ForwardPlusTileLightCounts[tile_idx], max_lights_per_tile);
        light_count = min(light_count, total_local_light_count);
        uint base_idx = tile_idx * max_lights_per_tile;
        [loop]
        for (uint i = 0u; i < light_count; ++i)
        {
            uint light_index = g_ForwardPlusTileLightIndices[base_idx + i];
            if (light_index >= total_local_light_count)
            {
                continue;
            }
            ForwardPlusLight light = g_ForwardPlusLights[light_index];
            AccumulateLocalLight(light,
                                 input.WorldPos,
                                 geom_n,
                                 n,
                                 v,
                                 shininess,
                                 base_color,
                                 spec_color,
                                 lit_local,
                                 local_shadow_lift_energy);
        }
    }
    float shadow_lift_strength = max(g_LocalLightParams.w, 0.0);
    float shadow_lift = 1.0 - exp(-local_shadow_lift_energy * shadow_lift_strength);
    float lifted_shadow = lerp(shadow, 1.0, saturate(shadow_lift));
    float3 lit_directional = base_color * g_LightColor.rgb * (ndotl * lifted_shadow);
    lit_directional += spec_color * spec * g_LightColor.rgb * lifted_shadow;
    occlusion = lerp(1.0, occlusion, g_PbrParams.z);
    float local_ao_factor = lerp(1.0, occlusion, saturate(g_LocalLightParams.z));
    float3 lit = lit_directional * occlusion + lit_local * local_ao_factor;
    float ndotv = max(dot(n, v), 0.0);
    float3 env_diffuse = float3(0.0, 0.0, 0.0);
    float3 env_spec = float3(0.0, 0.0, 0.0);
    const bool env_debug = g_EnvParams.z > 0.5;
    if (g_EnvParams.x > 0.0 || env_debug)
    {
        env_diffuse = g_IrradianceTex.Sample(g_SamplerColor, n).rgb * g_EnvParams.x;
        float3 r = reflect(-v, n);
        float mip = saturate(roughness) * g_EnvParams.y;
        float3 prefiltered = g_PrefilterTex.SampleLevel(g_SamplerColor, r, mip).rgb;
        float2 brdf = g_BRDFLUT.Sample(g_SamplerColor, float2(ndotv, roughness)).rg;
        env_spec = prefiltered * (spec_color * brdf.x + brdf.y);
        lit += env_diffuse * base_color * occlusion;
        lit += env_spec * g_EnvParams.x;
        if (env_debug)
        {
            if (g_EnvParams.z < 1.5)
            {
                return float4(env_diffuse, 1.0);
            }
            if (g_EnvParams.z < 2.5)
            {
                return float4(prefiltered, 1.0);
            }
            if (g_EnvParams.z < 3.5)
            {
                return float4(brdf.x, brdf.y, 0.0, 1.0);
            }
            if (g_EnvParams.z < 4.5)
            {
                return float4(g_EnvParams.xxx, 1.0);
            }
            if (g_EnvParams.z < 5.5)
            {
                return float4(g_IrradianceTex.Sample(g_SamplerColor, float3(0.0, 1.0, 0.0)).rgb, 1.0);
            }
            if (g_EnvParams.z < 6.5)
            {
                return float4(g_PrefilterTex.SampleLevel(g_SamplerColor, float3(0.0, 1.0, 0.0), 0.0).rgb, 1.0);
            }
            if (g_EnvParams.z < 7.5)
            {
                return float4(base_tex.rgb, 1.0);
            }
            if (g_EnvParams.z < 8.5)
            {
                return float4(input.UV, 0.0, 1.0);
            }
            if (g_EnvParams.z < 9.5)
            {
                return float4(normal_tex.xyz * 0.5 + 0.5, 1.0);
            }
        }
    }
    float base_alpha = saturate(g_BaseColorFactor.a * base_tex.a);
    uint shading_mode = (uint)round(g_MaterialParams0.x);
    if (shading_mode == 1u)
    {
        float fresnel_power = max(g_MaterialParams0.y, 0.001);
        float fresnel_strength = max(g_MaterialParams0.z, 0.0);
        float refraction_strength = max(g_MaterialParams0.w, 0.0);
        float interior_strength = max(g_MaterialParams1.x, 0.0);
        float highlight_strength = max(g_MaterialParams1.y, 0.0);
        float alpha_boost = g_MaterialParams1.z;
        float swirl_strength = saturate(g_MaterialParams1.w);
        bool analytic_sphere_normals = g_MaterialParams2.x > 0.5;
        float body_strength = saturate(g_MaterialParams2.y);
        bool shell_unlit = g_MaterialParams2.z > 0.5;
        float time = g_LocalLightMeta.w;

        if (analytic_sphere_normals)
        {
            float3 sphere_center_ws = mul(g_Model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
            float3 sphere_offset_ws = input.WorldPos - sphere_center_ws;
            float sphere_offset_len = max(length(sphere_offset_ws), 1.0e-5);
            float3 sphere_dir = sphere_offset_ws / sphere_offset_len;
            n = sphere_dir;
            geom_n = sphere_dir;
        }

        if (dot(n, v) < 0.0)
        {
            n = -n;
            geom_n = -geom_n;
        }
        ndotv = max(dot(n, v), 0.0);

        float rim = saturate(pow(1.0 - ndotv, fresnel_power) * fresnel_strength);
        float3 refract_dir = refract(-v, n, 1.0 / (1.0 + refraction_strength * 0.35));
        if (dot(refract_dir, refract_dir) <= 1.0e-5)
        {
            refract_dir = -v;
        }
        float3 reflect_dir = reflect(-v, n);
        float refract_mip = saturate(roughness * 0.35 + (1.0 - ndotv) * 0.08) * g_EnvParams.y;
        float reflect_mip = saturate(roughness) * g_EnvParams.y;
        float3 env_refract =
            g_PrefilterTex.SampleLevel(g_SamplerColor, refract_dir, refract_mip).rgb * g_EnvParams.x;
        float3 prefiltered_reflect =
            g_PrefilterTex.SampleLevel(g_SamplerColor, reflect_dir, reflect_mip).rgb;
        float2 brdf_reflect = g_BRDFLUT.Sample(g_SamplerColor, float2(ndotv, roughness)).rg;
        float3 env_reflect = prefiltered_reflect * (spec_color * brdf_reflect.x + brdf_reflect.y);
        float body = pow(ndotv, 1.65) * body_strength;
        float center_glow = body * (0.18 + interior_strength * 0.82);
        float3 shell_tint = base_color * (0.32 + interior_strength * 0.68);
        float light_glint =
            pow(max(dot(reflect(-l_dir, n), v), 0.0), 42.0) * (0.18 + highlight_strength * 1.35);
        float3 sphere_center_ws = mul(g_Model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 sphere_offset_ws = input.WorldPos - sphere_center_ws;
        float sphere_offset_len = max(length(sphere_offset_ws), 1.0e-5);
        float3 sphere_dir = sphere_offset_ws / sphere_offset_len;
        float flow_time = time * 0.32;
        float3 shell_domain = sphere_dir * 5.8;
        float warp_a = Noise3(shell_domain.yzx * 2.45 +
                              float3(flow_time * 0.72, -flow_time * 0.41, flow_time * 0.36));
        float warp_b = Noise3(shell_domain.zxy * 2.05 +
                              float3(-flow_time * 0.38, flow_time * 0.61, flow_time * 0.44));
        float3 island_domain =
            shell_domain * 5.9 +
            (float3(warp_a, warp_b, 0.5 * (warp_a + warp_b)) - 0.5) * 1.30 +
            float3(flow_time * 0.42, -flow_time * 0.36, flow_time * 0.29);
        float islands_base = Noise3(island_domain);
        float islands_detail = Noise3(island_domain * 3.85 + float3(4.2, -7.1, 2.8));
        float islands_field = islands_base * 0.58 + islands_detail * 0.42;
        float threshold =
            0.43 + 0.05 * Noise3(shell_domain * 1.65 + float3(-flow_time * 0.22,
                                                               flow_time * 0.18,
                                                               1.7));
        float islands_mask = smoothstep(threshold - 0.05, threshold + 0.010, islands_field);
        float islands_soft = smoothstep(threshold - 0.10, threshold + 0.04, islands_field);
        float visibility = lerp(1.0, 0.10 + islands_mask * 0.90, swirl_strength);

        const float refract_scale = 0.08 + 0.92 * body_strength;
        const float spec_scale = 0.16 + 0.84 * body_strength;
        const float halo = pow(saturate(rim), 0.72) * (0.28 + highlight_strength * 0.42);

        if (shell_unlit)
        {
            float3 key_dir = normalize(float3(-0.26, 0.64, 0.72));
            float key_gloss =
                pow(saturate(dot(reflect(-key_dir, n), v)), 28.0) * (highlight_strength * 0.72);
            float3 self_tint = lerp(shell_tint, base_color, 0.42);
            lit = self_tint * (0.26 + center_glow * 1.12 + halo * 0.48);
            lit += base_color * (rim * (0.52 + highlight_strength * 1.08) + halo * 0.92);
            lit += emissive * (1.10 + interior_strength * 0.30 + center_glow * 0.42 + halo * 1.12);
            lit += float3(1.0, 1.0, 1.0) * key_gloss;
            base_alpha = saturate(base_alpha * (0.26 + 0.16 * body_strength) +
                                  rim * (0.42 + highlight_strength * 0.30) +
                                  halo * (0.20 + highlight_strength * 0.16) +
                                  center_glow * (0.20 + alpha_boost * 1.40) +
                                  0.04);
        }
        else
        {
            lit = env_refract * (0.42 + interior_strength * 0.34) * refract_scale +
                  env_reflect * ((0.10 + rim * 0.85) * spec_scale + halo * 0.24) +
                  shell_tint * center_glow +
                  base_color * (rim * (0.20 + highlight_strength * 0.95) + halo * 0.85) +
                  g_LightColor.rgb * light_glint;
            lit += emissive * (0.45 + interior_strength * 0.28 + halo * 0.80);
            base_alpha = saturate(base_alpha * 0.24 * (0.10 + 0.90 * body_strength) +
                                  rim * (0.34 + highlight_strength * 0.24) +
                                  halo * (0.12 + highlight_strength * 0.18) +
                                  center_glow * (0.10 + alpha_boost));
        }
        lit *= lerp(1.0, 0.52 + islands_soft * 0.48, swirl_strength);
        base_alpha *= visibility;
    }
    else if (shading_mode == 3u)
    {
        float halo_tightness = max(g_MaterialParams0.y, 0.25);
        float halo_intensity = max(g_MaterialParams0.z, 0.0);
        float inner_radius = saturate(g_MaterialParams1.x);
        float highlight_strength = max(g_MaterialParams1.y, 0.0);
        float alpha_boost = max(g_MaterialParams1.z, 0.0);
        float shimmer_strength = saturate(g_MaterialParams1.w);
        float2 screen_center = g_MaterialParams2.xy;
        float2 screen_radius = max(g_MaterialParams2.zw, float2(1.0e-4, 1.0e-4));
        float use_projected = step(1.0e-4, min(g_MaterialParams2.z, g_MaterialParams2.w));
        float2 projected_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
        float2 sample_uv = lerp(input.UV, projected_uv, use_projected);
        float2 centered_uv = use_projected > 0.5 ? (sample_uv - screen_center) / screen_radius
                                                 : (input.UV * 2.0 - 1.0);
        float radial_sq = dot(centered_uv, centered_uv);
        float radial = sqrt(saturate(radial_sq));
        float inner_feather = max(0.008, (1.0 - inner_radius) * 0.08);
        float inner_mask = smoothstep(inner_radius - inner_feather,
                                      inner_radius + inner_feather,
                                      radial);
        float aura_t = saturate((radial - inner_radius) / max(1.0 - inner_radius, 1.0e-4));
        float aura_t_sq = aura_t * aura_t;

        float glow = exp(-aura_t_sq * (2.4 + halo_tightness * 1.45));
        float core = exp(-aura_t_sq * (8.5 + halo_tightness * 4.2));

        float time = g_LocalLightMeta.w;
        float3 domain = float3(centered_uv * (2.8 + halo_tightness * 0.45), time * 0.22);
        float noise_a = Noise3(domain);
        float noise_b = Noise3(domain.yzx * 2.4 + float3(2.4, -1.2, 4.1));
        float shimmer_wave = 0.5 + 0.5 * sin(time * 2.1 + noise_a * 5.6 + noise_b * 3.1);
        float shimmer = lerp(1.0, 0.84 + shimmer_wave * 0.26, shimmer_strength);

        float edge_fade = 1.0 - smoothstep(0.88, 1.05, aura_t);
        float halo_profile = saturate(inner_mask * glow * edge_fade);
        float core_profile = saturate(inner_mask * core * (0.82 + highlight_strength * 0.42));

        lit = base_color * halo_profile * (0.92 + halo_intensity * 0.58) +
              emissive * (halo_profile * (1.18 + halo_intensity * 0.76) +
                          core_profile * (0.78 + highlight_strength * 0.82));
        lit *= shimmer;

        base_alpha = saturate(base_alpha * halo_profile * (1.18 + alpha_boost * 0.82) +
                              core_profile * (0.34 + alpha_boost * 0.38));
    }
    else if (shading_mode == 4u)
    {
        float tint_strength = max(g_MaterialParams1.x, 0.0);
        float distortion_strength = max(g_MaterialParams1.y, 0.0);
        float edge_strength = max(g_MaterialParams1.z, 0.0);
        float noise_strength = saturate(g_MaterialParams1.w);
        float2 screen_center = g_MaterialParams2.xy;
        float2 screen_radius = max(g_MaterialParams2.zw, float2(1.0e-4, 1.0e-4));
        float time = g_LocalLightMeta.w;

        float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
        float2 centered_uv = (screen_uv - screen_center) / screen_radius;
        float radial = length(centered_uv);
        float full_overlay = step(0.95, min(screen_radius.x, screen_radius.y));
        float3 domain = float3(centered_uv * (3.8 + noise_strength * 1.6), time * 0.35);
        float noise_a = Noise3(domain);
        float noise_b = Noise3(domain.yzx * 2.4 + float3(3.1, -1.7, 4.6));
        float noise_c = Noise3(domain.zxy * 1.8 + float3(-2.2, 1.3, -3.8));
        float shimmer =
            0.5 + 0.5 * sin(time * 1.8 + noise_a * 6.1 + noise_b * 3.7 + noise_c * 2.5);

        if (full_overlay > 0.5)
        {
            float mask = 1.0;
            float2 distort_dir = normalize(float2(noise_a - 0.5, noise_b - 0.5) + 1.0e-4);
            float distort_scale =
                (0.0030 + distortion_strength * 0.0105) * (0.84 + shimmer * 0.34);
            float2 distorted_uv = clamp(screen_uv + distort_dir * distort_scale * mask, 0.001, 0.999);

            float3 scene_color = g_SceneColor.Sample(g_SamplerColor, distorted_uv).rgb;
            float edge = pow(saturate(radial), 1.4);
            float tint_mix = 0.34 + shimmer * 0.10;
            float3 tint = lerp(float3(1.0, 1.0, 1.0),
                               float3(0.92, 0.97, 1.0) + base_color * (0.34 + tint_strength * 0.46),
                               tint_mix + edge_strength * 0.18 + edge * 0.08);
            float tint_amount = saturate(tint_mix + edge_strength * 0.14 + edge * 0.08);
            float overlay_alpha = 0.94;
            lit = lerp(scene_color, scene_color * tint, tint_amount) +
                  emissive * (0.16 + shimmer * 0.18) +
                  tint * (0.026 + edge * 0.034);
            base_alpha = saturate(overlay_alpha);
        }
        else
        {
            float aura_width = 0.62 + edge_strength * 0.48;
            float aura_t = saturate((radial - 1.0) / max(aura_width, 1.0e-4));
            float aura_gate = smoothstep(0.965, 1.0, radial);
            float aura_fade = 1.0 - smoothstep(1.0 + aura_width,
                                               1.0 + aura_width * 1.22,
                                               radial);
            float glow = exp(-aura_t * (0.52 + edge_strength * 0.32));
            float core = exp(-(aura_t * aura_t) * (3.1 + edge_strength * 1.65));
            float halo = aura_gate * aura_fade * glow * (1.95 + tint_strength * 1.05);
            float hot_band = aura_gate * aura_fade * core * (1.25 + tint_strength * 0.72);

            float3 outer_color = base_color * halo * (1.65 + edge_strength * 0.95);
            float3 inner_hot =
                emissive * (halo * (3.45 + tint_strength * 1.20) +
                            hot_band * (4.35 + edge_strength * 1.55));
            lit = (outer_color + inner_hot) * (0.94 + shimmer * 0.22);
            base_alpha = saturate(halo * 0.78 + hot_band * 0.42);
        }
    }
    else if (shading_mode == 6u)
    {
        float3 sphere_center_ws = g_MaterialParams1.xyz;
        float sphere_radius = max(g_MaterialParams1.w, 1.0e-4);
        float density = max(g_MaterialParams2.x, 0.0);
        float distortion_strength = max(g_MaterialParams2.y, 0.0);
        float noise_strength = saturate(g_MaterialParams2.z);
        float time = g_LocalLightMeta.w;

        float3 ro = g_CameraPos.xyz - sphere_center_ws;
        float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
        float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
        float half_b = dot(ro, ray_dir);
        float c = dot(ro, ro) - sphere_radius * sphere_radius;
        float h = half_b * half_b - c;
        if (h <= 0.0)
        {
            discard;
        }
        h = sqrt(h);
        float t0 = -half_b - h;
        float t1 = -half_b + h;
        if (t1 <= 0.0)
        {
            discard;
        }

        float t_enter = max(t0, 0.0);
        float t_exit = max(t1, t_enter + 1.0e-4);
        float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
        float raw_scene_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
        float scene_linear_depth = LinearizeSceneDepth(raw_scene_depth);
        float scene_t = scene_linear_depth / ray_forward;
        if (raw_scene_depth >= 0.9999)
        {
            scene_t = t_exit;
        }

        float t_hit = min(t_exit, max(scene_t, t_enter));
        float path_length = max(t_hit - t_enter, 0.0);
        if (path_length <= 1.0e-4)
        {
            discard;
        }

        float transmittance = exp(-density * path_length);
        float opacity = saturate(1.0 - transmittance);

        float sample_t = lerp(t_enter, t_hit, 0.5);
        float3 sample_local =
            (g_CameraPos.xyz + ray_dir * sample_t - sphere_center_ws) / sphere_radius;
        float3 noise_domain =
            sample_local * (2.4 + noise_strength * 1.6) +
            float3(time * 0.46, -time * 0.34, time * 0.29);
        float noise_a = Noise3(noise_domain * 1.6);
        float noise_b = Noise3(noise_domain.yzx * 2.3 + float3(2.1, -1.4, 4.3));
        float2 distort_dir = normalize(float2(noise_a - 0.5, noise_b - 0.5) + 1.0e-4);
        float distort_scale =
            (0.0012 + distortion_strength * 0.0060) *
            (0.82 + 0.18 * sin(time * 1.9 + noise_a * 5.8 + noise_b * 3.1));
        float2 distorted_uv = clamp(screen_uv + distort_dir * distort_scale, 0.001, 0.999);

        float3 background_color = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;
        float3 scene_color = g_SceneColor.Sample(g_SamplerColor, distorted_uv).rgb;
        float3 medium_color =
            lerp(float3(0.04, 0.04, 0.04),
                 saturate(base_color * 0.92 + float3(0.08, 0.08, 0.08)),
                 0.96);
        float3 fluorescent_glow =
            emissive * (5.5 + opacity * 14.0) +
            base_color * (0.45 + opacity * 0.80);

        float radial = saturate(sqrt(max(sphere_radius * sphere_radius - h * h, 0.0)) /
                                sphere_radius);
        float rim = pow(saturate(radial), 5.0);
        float shimmer = 0.82 + 0.18 * sin(time * 1.9 + noise_a * 5.8 + noise_b * 3.1);
        float3 boundary_glow =
            (emissive * 0.38 + base_color * 0.62) *
            rim * (0.50 + distortion_strength * 0.08 + noise_strength * 0.20) * shimmer;

        float alpha = saturate(opacity + rim * (0.16 + noise_strength * 0.05));
        float safe_alpha = max(alpha, 0.05);
        float3 old_composite = scene_color * transmittance +
                               medium_color * opacity +
                               fluorescent_glow +
                               boundary_glow;
        lit = max((old_composite - background_color * (1.0 - alpha)) / safe_alpha, 0.0);
        base_alpha = alpha;
    }
    else if (shading_mode == 5u)
    {
        float density_power = max(g_MaterialParams0.y, 0.25);
        float edge_boost = max(g_MaterialParams0.z, 0.0);
        float shell_softness = max(g_MaterialParams0.w, 0.0);
        float inner_radius_ratio = saturate(g_MaterialParams1.x);
        float highlight_strength = max(g_MaterialParams1.y, 0.0);
        float alpha_boost = max(g_MaterialParams1.z, 0.0);
        float shimmer_strength = saturate(g_MaterialParams1.w);
        float time = g_LocalLightMeta.w;

        float3 sphere_center_ws = mul(g_Model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 axis_x = mul(g_Model, float4(1.0, 0.0, 0.0, 0.0)).xyz;
        float3 axis_y = mul(g_Model, float4(0.0, 1.0, 0.0, 0.0)).xyz;
        float3 axis_z = mul(g_Model, float4(0.0, 0.0, 1.0, 0.0)).xyz;
        float outer_radius = max(max(length(axis_x), length(axis_y)), length(axis_z));
        outer_radius = max(outer_radius, 1.0e-4);
        float inner_radius = outer_radius * inner_radius_ratio;

        float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
        float3 ro = g_CameraPos.xyz - sphere_center_ws;
        float3 sphere_dir = SafeNormalize(input.WorldPos - sphere_center_ws, n);
        n = sphere_dir;
        geom_n = sphere_dir;
        float shell_ndotv = saturate(dot(n, v));

        float proj = dot(ro, ray_dir);
        float closest_sq = max(dot(ro, ro) - proj * proj, 0.0);
        float radial = sqrt(closest_sq) / outer_radius;
        float aura_inner = inner_radius_ratio;
        float aura_t = saturate((radial - aura_inner) / max(1.0 - aura_inner, 1.0e-4));
        float aura_gate = smoothstep(aura_inner - 0.010, aura_inner + 0.006, radial);
        float aura_fade = 1.0 - smoothstep(0.88, 1.06, aura_t);

        float flow_time = time * 0.34;
        float3 domain = sphere_dir * (3.4 + shell_softness * 1.2) +
                        float3(flow_time * 0.41, -flow_time * 0.28, flow_time * 0.23);
        float noise_a = Noise3(domain * 1.35);
        float noise_b = Noise3(domain.yzx * 2.25 + float3(2.3, -1.2, 4.1));
        float shimmer_wave =
            0.5 + 0.5 * sin(time * 2.0 + noise_a * 6.0 + noise_b * 3.4);
        float shimmer = lerp(1.0, 0.82 + shimmer_wave * 0.34, shimmer_strength);

        float glow = exp(-aura_t * (0.85 + density_power * 0.55));
        float core = exp(-(aura_t * aura_t) * (4.2 + density_power * 1.8));
        float soft_edge = pow(saturate(1.0 - shell_ndotv), 1.8 + edge_boost * 0.3);
        float halo = aura_gate * aura_fade * glow * (1.45 + highlight_strength * 0.75) +
                     soft_edge * 0.10;
        float hot_band = aura_gate * aura_fade * core * (0.72 + highlight_strength * 0.42);

        lit = base_color * halo * (1.45 + edge_boost * 0.80) +
              emissive * (halo * (2.35 + alpha_boost * 0.95) +
                          hot_band * (1.85 + highlight_strength * 0.92));
        lit *= shimmer;
        base_alpha = saturate(halo * (0.62 + alpha_boost * 0.28) +
                              hot_band * (0.16 + alpha_boost * 0.12));
    }
    else if (shading_mode == 2u)
    {
        float fresnel_power = max(g_MaterialParams0.y, 0.001);
        float fresnel_strength = max(g_MaterialParams0.z, 0.0);
        float refraction_strength = max(g_MaterialParams0.w, 0.0);
        float tint_strength = max(g_MaterialParams1.x, 0.0);
        float distortion_strength = max(g_MaterialParams1.y, 0.0);
        float edge_strength = max(g_MaterialParams1.z, 0.0);
        float noise_strength = saturate(g_MaterialParams1.w);
        float time = g_LocalLightMeta.w;

        float3 sphere_center_ws = mul(g_Model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 axis_x = mul(g_Model, float4(1.0, 0.0, 0.0, 0.0)).xyz;
        float3 axis_y = mul(g_Model, float4(0.0, 1.0, 0.0, 0.0)).xyz;
        float3 axis_z = mul(g_Model, float4(0.0, 0.0, 1.0, 0.0)).xyz;
        float sphere_radius = max(max(length(axis_x), length(axis_y)), length(axis_z));
        sphere_radius = max(sphere_radius, 1.0e-4);

        float3 ro = g_CameraPos.xyz - sphere_center_ws;
        bool camera_inside = dot(ro, ro) < sphere_radius * sphere_radius;
        if (!camera_inside && !input.FrontFace)
        {
            discard;
        }

        float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
        float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
        float half_b = dot(ro, ray_dir);
        float c = dot(ro, ro) - sphere_radius * sphere_radius;
        float h = half_b * half_b - c;
        if (h <= 0.0)
        {
            discard;
        }
        h = sqrt(h);
        float t0 = -half_b - h;
        float t1 = -half_b + h;
        if (t1 <= 0.0)
        {
            discard;
        }
        float t_enter = max(t0, 0.0);
        float t_exit = max(t1, t_enter + 1.0e-4);

        float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
        float raw_scene_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
        float scene_linear_depth = LinearizeSceneDepth(raw_scene_depth);
        float scene_t = scene_linear_depth / ray_forward;
        if (raw_scene_depth >= 0.9999)
        {
            scene_t = t_exit;
        }
        float t_hit = min(t_exit, max(scene_t, t_enter));
        float thickness = max(t_hit - t_enter, 0.0);
        float thickness_norm = saturate(thickness / max(sphere_radius * 2.0, 1.0e-4));
        if (thickness <= 1.0e-4)
        {
            discard;
        }

        float sample_t0 = t_enter + thickness * 0.35;
        float sample_t1 = t_enter + thickness * 0.72;
        float3 sample_local0 =
            (g_CameraPos.xyz + ray_dir * sample_t0 - sphere_center_ws) / sphere_radius;
        float3 sample_local1 =
            (g_CameraPos.xyz + ray_dir * sample_t1 - sphere_center_ws) / sphere_radius;

        float3 sphere_normal = SafeNormalize(input.WorldPos - sphere_center_ws, n);
        n = sphere_normal;
        geom_n = sphere_normal;
        float shell_ndotv = saturate(dot(n, v));
        float rim = saturate(pow(1.0 - shell_ndotv, fresnel_power) * fresnel_strength);

        float flow_time = time * 0.38;
        float3 volume_domain0 =
            sample_local0 * (2.4 + noise_strength * 1.8) +
            float3(flow_time * 0.31, -flow_time * 0.24, flow_time * 0.19);
        float3 volume_domain1 =
            sample_local1.yzx * (4.0 + noise_strength * 2.0) +
            float3(-flow_time * 0.28, flow_time * 0.21, flow_time * 0.26);
        float noise_a = Noise3(volume_domain0 * 1.25);
        float noise_b = Noise3(volume_domain0.yzx * 2.15 + float3(2.3, -1.7, 4.6));
        float noise_c = Noise3(volume_domain1.zxy * 1.75 + float3(-3.2, 1.4, -2.5));
        float shimmer =
            0.5 + 0.5 * sin(time * 1.9 + noise_a * 6.0 + noise_b * 3.2 + noise_c * 2.7);

        float path_strength = saturate(thickness_norm * (camera_inside ? 1.45 : 0.78));
        float2 distort_dir = normalize(float2(noise_a - 0.5, noise_b - 0.5) + 1.0e-4);
        float distort_scale =
            (0.0018 + distortion_strength * 0.0075) *
            (0.35 + path_strength * 0.90 + shimmer * 0.18);
        float2 distorted_uv = clamp(screen_uv + distort_dir * distort_scale, 0.001, 0.999);

        float3 scene_color = g_SceneColor.Sample(g_SamplerColor, distorted_uv).rgb;
        float tint_mix = saturate((camera_inside ? 0.54 : 0.22) +
                                  path_strength * (0.70 + tint_strength * 0.24) +
                                  shimmer * 0.08);
        float3 tint_target =
            lerp(float3(1.0, 1.0, 1.0),
                 float3(0.94, 0.98, 1.02) + base_color * (0.28 + tint_strength * 0.42),
                 tint_mix);

        float boundary_glow = saturate(0.20 +
                                       rim * (0.28 + edge_strength * 0.24) +
                                       shimmer * 0.14);
        float3 glow =
            emissive * (0.36 + path_strength * 0.34 + shimmer * 0.14) +
            base_color * (boundary_glow * (0.24 + edge_strength * 0.24) +
                          path_strength * 0.10);

        lit = scene_color * tint_target + glow;

        float base_mix = camera_inside ? 0.76 : 0.34;
        base_alpha = saturate(base_mix +
                              path_strength * (0.46 + tint_strength * 0.18) +
                              rim * (camera_inside ? 0.16 : 0.10));
    }
    else
    {
        float transparency = saturate(1.0 - base_alpha);
        if (transparency > 0.001)
        {
            float fresnel = pow(1.0 - ndotv, 4.0);
            lit += base_color * (0.10 + 0.30 * fresnel) * transparency;
            lit += spec_color * (0.20 + 1.25 * fresnel) * transparency;
            base_alpha = saturate(base_alpha * 0.45 + fresnel * (0.22 + transparency * 0.38));
        }
        lit += emissive;
    }
    float exposure = max(g_EnvParams.w, 0.0);
    float3 mapped = 1.0 - exp(-lit * exposure);
    return float4(mapped, base_alpha);
}
)";

  static constexpr const char* kForwardPlusComputeShader = R"(
cbuffer ForwardPlusConstants
{
    float4x4 g_ViewProj;
    float4 g_ForwardPlusParams;
    float4 g_ScreenParams;
};

struct ForwardPlusLight
{
    float4 position_range;
    float4 direction_type;
    float4 color_intensity;
    float4 spot_params;
    float4 screen_rect;
};

StructuredBuffer<ForwardPlusLight> g_ForwardPlusLights;
RWStructuredBuffer<uint> g_ForwardPlusTileLightCounts;
RWStructuredBuffer<uint> g_ForwardPlusTileLightIndices;

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint tile_size = (uint)max(g_ForwardPlusParams.x, 1.0);
    uint tiles_x = (uint)max(g_ForwardPlusParams.y, 1.0);
    uint tiles_y = (uint)max(g_ForwardPlusParams.z, 1.0);
    uint max_lights_per_tile = (uint)max(g_ForwardPlusParams.w, 1.0);
    uint light_count = (uint)max(g_ScreenParams.z, 0.0);

    uint tile_x = dispatch_id.x;
    uint tile_y = dispatch_id.y;
    if (tile_x >= tiles_x || tile_y >= tiles_y)
    {
        return;
    }

    uint tile_idx = tile_y * tiles_x + tile_x;
    uint out_base = tile_idx * max_lights_per_tile;
    g_ForwardPlusTileLightCounts[tile_idx] = 0u;

    float tile_min_x = (float)(tile_x * tile_size);
    float tile_min_y = (float)(tile_y * tile_size);
    float tile_max_x = tile_min_x + (float)tile_size;
    float tile_max_y = tile_min_y + (float)tile_size;

    [loop]
    for (uint light_idx = 0u; light_idx < light_count; ++light_idx)
    {
        ForwardPlusLight light = g_ForwardPlusLights[light_idx];
        if (light.direction_type.w < 0.5)
        {
            continue;
        }
        if (light.color_intensity.w <= 0.0 || light.position_range.w <= 0.0)
        {
            continue;
        }

        float4 rect = light.screen_rect;
        if (rect.z < rect.x || rect.w < rect.y)
        {
            continue;
        }

        bool overlaps = !(rect.z < tile_min_x ||
                          rect.w < tile_min_y ||
                          rect.x >= tile_max_x ||
                          rect.y >= tile_max_y);
        if (!overlaps)
        {
            continue;
        }

        uint count = g_ForwardPlusTileLightCounts[tile_idx];
        if (count >= max_lights_per_tile)
        {
            continue;
        }
        g_ForwardPlusTileLightIndices[out_base + count] = light_idx;
        g_ForwardPlusTileLightCounts[tile_idx] = count + 1u;
    }
}
)";

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kVertexShader;
  vs = device_with_cache_.CreateShader(shader_ci);
  if (!vs) {
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kPixelShader;
  ps = device_with_cache_.CreateShader(shader_ci);
  if (!ps) {
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> forward_plus_cs;
  shader_ci.Desc.Name = "Karma Forward+ CS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kForwardPlusComputeShader;
  forward_plus_cs = device_with_cache_.CreateShader(shader_ci);

  Diligent::GraphicsPipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.Name = "Karma Pipeline";
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

  constexpr Diligent::Uint32 kInstanceStride = static_cast<Diligent::Uint32>(sizeof(float) * 16);
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
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
  };
  graphics.InputLayout.LayoutElements = layout_elems;
  graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusLights", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightCounts", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightIndices", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerData", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_NormalTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
  };
  pso_ci.PSODesc.ResourceLayout.Variables = vars;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

  Diligent::GraphicsPipelineStateCreateInfo depth_prepass_ci = pso_ci;
  depth_prepass_ci.PSODesc.Name = "Karma Depth Prepass Pipeline";
  depth_prepass_ci.pPS = nullptr;
  auto& depth_graphics = depth_prepass_ci.GraphicsPipeline;
  depth_graphics.NumRenderTargets = 0;
  depth_graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                         : Diligent::TEX_FORMAT_D32_FLOAT;
  depth_graphics.DepthStencilDesc.DepthEnable = true;
  depth_graphics.DepthStencilDesc.DepthWriteEnable = true;
  depth_graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
  Diligent::ShaderResourceVariableDesc depth_prepass_vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
  };
  depth_prepass_ci.PSODesc.ResourceLayout.Variables = depth_prepass_vars;
  depth_prepass_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(depth_prepass_vars) / sizeof(depth_prepass_vars[0]));
  // Depth pre-pass has no pixel shader; avoid inheriting immutable PS samplers
  // from the full forward pipeline layout.
  depth_prepass_ci.PSODesc.ResourceLayout.ImmutableSamplers = nullptr;
  depth_prepass_ci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;

  Diligent::SamplerDesc sampler_color{};
  sampler_color.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_color.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  device_->CreateSampler(sampler_color, &sampler_color_);

  Diligent::SamplerDesc sampler_data{};
  sampler_data.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_data.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_data.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  device_->CreateSampler(sampler_data, &sampler_data_);

  Diligent::SamplerDesc shadow_sampler{};
  shadow_sampler.MinFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
  shadow_sampler.MagFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
  shadow_sampler.MipFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
  // LESS_EQUAL is more robust against precision ties in shadow compares.
  shadow_sampler.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
  shadow_sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  shadow_sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  shadow_sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
  device_->CreateSampler(shadow_sampler, &shadow_sampler_);

  recreateShadowMap();
  recreatePointShadowMap();

  pipeline_state_ = device_with_cache_.CreateGraphicsPipelineState(pso_ci);
  depth_prepass_pipeline_state_ = device_with_cache_.CreateGraphicsPipelineState(depth_prepass_ci);
  auto create_transparent_pipeline =
      [&](const char* name,
          Diligent::CULL_MODE cull_mode,
          bool additive,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso) {
        Diligent::GraphicsPipelineStateCreateInfo transparent_ci = pso_ci;
        transparent_ci.PSODesc.Name = name;
        auto& transparent_graphics = transparent_ci.GraphicsPipeline;
        transparent_graphics.RasterizerDesc.CullMode = cull_mode;
        transparent_graphics.DepthStencilDesc.DepthEnable = true;
        transparent_graphics.DepthStencilDesc.DepthWriteEnable = false;
        auto& blend = transparent_graphics.BlendDesc.RenderTargets[0];
        blend.BlendEnable = true;
        blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
        blend.DestBlend = additive ? Diligent::BLEND_FACTOR_ONE
                                   : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = additive ? Diligent::BLEND_FACTOR_ONE
                                        : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
        out_pso = device_with_cache_.CreateGraphicsPipelineState(transparent_ci);
      };
  create_transparent_pipeline("Karma Transparent Pipeline",
                              Diligent::CULL_MODE_BACK,
                              false,
                              transparent_pipeline_state_);
  create_transparent_pipeline("Karma Transparent Pipeline (DoubleSided)",
                              Diligent::CULL_MODE_NONE,
                              false,
                              transparent_double_sided_pipeline_state_);
  create_transparent_pipeline("Karma Additive Pipeline",
                              Diligent::CULL_MODE_BACK,
                              true,
                              additive_pipeline_state_);
  create_transparent_pipeline("Karma Additive Pipeline (DoubleSided)",
                              Diligent::CULL_MODE_NONE,
                              true,
                              additive_double_sided_pipeline_state_);

  if (!pipeline_state_) {
    return;
  }

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "Karma Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(DrawConstants);
  device_->CreateBuffer(cb_desc, nullptr, &constants_);

  Diligent::BufferDesc camera_override_cb_desc{};
  camera_override_cb_desc.Name = "Karma Camera Override User Constants";
  camera_override_cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  camera_override_cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  camera_override_cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  camera_override_cb_desc.Size = sizeof(CameraOverrideUserConstants);
  device_->CreateBuffer(camera_override_cb_desc, nullptr, &camera_override_user_constants_);

  if (constants_) {
    bool bound = false;
    auto bind_constants_to_pipeline = [&](Diligent::IPipelineState* pso) {
      if (!pso) {
        return false;
      }
      bool pipeline_bound = false;
      if (auto* variable =
              pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
        variable->Set(constants_);
        pipeline_bound = true;
      }
      if (auto* variable =
              pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) {
        variable->Set(constants_);
        pipeline_bound = true;
      }
      return pipeline_bound;
    };
    bound = bind_constants_to_pipeline(pipeline_state_.RawPtr()) || bound;
    bound = bind_constants_to_pipeline(transparent_pipeline_state_.RawPtr()) || bound;
    bound =
        bind_constants_to_pipeline(transparent_double_sided_pipeline_state_.RawPtr()) || bound;
    bound = bind_constants_to_pipeline(additive_pipeline_state_.RawPtr()) || bound;
    bound =
        bind_constants_to_pipeline(additive_double_sided_pipeline_state_.RawPtr()) || bound;
    if (!bound) {
    }

    if (depth_prepass_pipeline_state_) {
      bool depth_prepass_constants_bound = false;
      if (auto* variable = depth_prepass_pipeline_state_->GetStaticVariableByName(
              Diligent::SHADER_TYPE_VERTEX, "Constants")) {
        variable->Set(constants_);
        depth_prepass_constants_bound = true;
      }
      if (!depth_prepass_constants_bound) {
        depth_prepass_pipeline_state_.Release();
      }
    }
  }

  if (forward_plus_cs) {
    Diligent::ComputePipelineStateCreateInfo forward_plus_pso_ci{};
    forward_plus_pso_ci.PSODesc.Name = "Karma Forward+ Compute Pipeline";
    forward_plus_pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    forward_plus_pso_ci.pCS = forward_plus_cs;

    Diligent::ShaderResourceVariableDesc forward_plus_vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "ForwardPlusConstants",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {Diligent::SHADER_TYPE_COMPUTE, "g_ForwardPlusLights",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "g_ForwardPlusTileLightCounts",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "g_ForwardPlusTileLightIndices",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    forward_plus_pso_ci.PSODesc.ResourceLayout.Variables = forward_plus_vars;
    forward_plus_pso_ci.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(forward_plus_vars) / sizeof(forward_plus_vars[0]));

    forward_plus_compute_pso_ = device_with_cache_.CreateComputePipelineState(forward_plus_pso_ci);
    if (forward_plus_compute_pso_) {
      Diligent::BufferDesc fp_cb_desc{};
      fp_cb_desc.Name = "Karma Forward+ Compute Constants";
      fp_cb_desc.Usage = Diligent::USAGE_DYNAMIC;
      fp_cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
      fp_cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
      fp_cb_desc.Size = sizeof(ForwardPlusComputeConstants);
      device_->CreateBuffer(fp_cb_desc, nullptr, &forward_plus_compute_cb_);
      if (forward_plus_compute_cb_) {
        if (auto* var = forward_plus_compute_pso_->GetStaticVariableByName(
                Diligent::SHADER_TYPE_COMPUTE, "ForwardPlusConstants")) {
          var->Set(forward_plus_compute_cb_);
        }
      }
      forward_plus_compute_pso_->CreateShaderResourceBinding(&forward_plus_compute_srb_, true);
      if (forward_plus_compute_srb_) {
        forward_plus_compute_lights_var_ = forward_plus_compute_srb_->GetVariableByName(
            Diligent::SHADER_TYPE_COMPUTE, "g_ForwardPlusLights");
        forward_plus_compute_tile_counts_var_ = forward_plus_compute_srb_->GetVariableByName(
            Diligent::SHADER_TYPE_COMPUTE, "g_ForwardPlusTileLightCounts");
        forward_plus_compute_tile_indices_var_ = forward_plus_compute_srb_->GetVariableByName(
            Diligent::SHADER_TYPE_COMPUTE, "g_ForwardPlusTileLightIndices");
      }
    }
  }

  default_base_color_ = createSolidTextureSRV(255, 255, 255, 255, true, "DefaultBaseColor",
                                              default_base_color_tex_);
  default_normal_ = createSolidTextureSRV(128, 128, 255, 255, false, "DefaultNormal",
                                          default_normal_tex_);
  default_metallic_roughness_ = createSolidTextureSRV(0, 255, 255, 255, false, "DefaultMetalRough",
                                                      default_metallic_roughness_tex_);
  default_occlusion_ = createSolidTextureSRV(255, 255, 255, 255, false, "DefaultOcclusion",
                                             default_occlusion_tex_);
  default_emissive_ = createSolidTextureSRV(0, 0, 0, 255, true, "DefaultEmissive",
                                            default_emissive_tex_);
  default_env_ = createSolidTextureSRV(0, 0, 0, 255, true, "DefaultEnv",
                                       default_env_tex_);
  env_srv_ = default_env_;

  if (pipeline_state_ || transparent_pipeline_state_ || transparent_double_sided_pipeline_state_ ||
      additive_pipeline_state_ || additive_double_sided_pipeline_state_) {
    auto bind_shadow_static_resources = [&](Diligent::IPipelineState* pso) {
      if (!pso) {
        return;
      }
      if (shadow_map_srv_) {
        if (auto* var =
                pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
          var->Set(shadow_map_srv_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
      }
      if (auto* var =
              pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap")) {
        var->Set(point_shadow_map_srv_ ? point_shadow_map_srv_ : shadow_map_srv_,
                 Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
      if (shadow_sampler_) {
        if (auto* var =
                pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler")) {
          var->Set(shadow_sampler_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
      }
    };
    bind_shadow_static_resources(pipeline_state_.RawPtr());
    bind_shadow_static_resources(transparent_pipeline_state_.RawPtr());
    bind_shadow_static_resources(transparent_double_sided_pipeline_state_.RawPtr());
    bind_shadow_static_resources(additive_pipeline_state_.RawPtr());
    bind_shadow_static_resources(additive_double_sided_pipeline_state_.RawPtr());
  }

  recreateShadowPipeline();
  ensureParticleFallbackDepthResource();

  auto initialize_default_material_srb =
      [&](Diligent::IPipelineState* pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb) {
        out_srb.Release();
        if (!pso) {
          return;
        }
        pso->CreateShaderResourceBinding(&out_srb, true);
        if (!out_srb) {
          return;
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
          var->Set(sampler_color_);
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
          var->Set(sampler_data_);
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex")) {
          var->Set(default_base_color_);
        }
        if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_NormalTex")) {
          var->Set(default_normal_);
        }
        if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                   "g_MetallicRoughnessTex")) {
          var->Set(default_metallic_roughness_);
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex")) {
          var->Set(default_occlusion_);
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex")) {
          var->Set(default_emissive_);
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex")) {
          var->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
        }
        if (auto* var =
                out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex")) {
          var->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
        }
        if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT")) {
          var->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_);
        }
        if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor")) {
          var->Set(default_base_color_);
        }
        if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth")) {
          var->Set(particle_fallback_depth_srv_);
        }
        bindShadowResourcesToSrb(out_srb);
      };

  if (pipeline_state_) {
    pipeline_state_->CreateShaderResourceBinding(&shader_resources_, true);
    initialize_default_material_srb(pipeline_state_.RawPtr(), default_material_srb_);
    if (shader_resources_) {
      if (auto* var = shader_resources_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex")) {
        var->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
      }
      if (auto* var = shader_resources_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex")) {
        var->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
      }
      if (auto* var = shader_resources_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT")) {
        var->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_);
      }
    }
  }
  initialize_default_material_srb(transparent_pipeline_state_.RawPtr(),
                                  transparent_default_material_srb_);
  initialize_default_material_srb(transparent_double_sided_pipeline_state_.RawPtr(),
                                  transparent_double_sided_default_material_srb_);
  initialize_default_material_srb(additive_pipeline_state_.RawPtr(),
                                  additive_default_material_srb_);
  initialize_default_material_srb(additive_double_sided_pipeline_state_.RawPtr(),
                                  additive_double_sided_default_material_srb_);

  ensureLineResources();

  if (shader_cache_enabled_ && shader_cache_flush_ && device_with_cache_.GetCache()) {
    device_with_cache_.SaveCache(render_state_cache_path_.string().c_str());
    if (shader_cache_log_) {
      std::error_code ec;
      const auto size = std::filesystem::file_size(render_state_cache_path_, ec);
      (void)size;
    }
  }
}

}  // namespace karma::renderer_backend
