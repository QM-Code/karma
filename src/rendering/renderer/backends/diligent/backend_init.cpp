#include "backend.hpp"

#include "karma/platform/window/window.h"

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
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#if !defined(KARMA_WINDOW_BACKEND_SDL)
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

const char* adapterTypeName(Diligent::ADAPTER_TYPE type) {
  switch (type) {
    case Diligent::ADAPTER_TYPE_DISCRETE:
      return "discrete";
    case Diligent::ADAPTER_TYPE_INTEGRATED:
      return "integrated";
    case Diligent::ADAPTER_TYPE_SOFTWARE:
      return "software";
    case Diligent::ADAPTER_TYPE_UNKNOWN:
      return "unknown";
    default:
      return "invalid";
  }
}

Diligent::Uint32 adapterIdFromEnv(const char* name) {
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') {
    return Diligent::DEFAULT_ADAPTER_ID;
  }

  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (!end || *end != '\0' || parsed > std::numeric_limits<Diligent::Uint32>::max()) {
    std::fprintf(stderr, "[Karma] Ignoring invalid %s=%s\n", name, value);
    std::fflush(stderr);
    return Diligent::DEFAULT_ADAPTER_ID;
  }
  return static_cast<Diligent::Uint32>(parsed);
}

Diligent::Uint32 uintFromEnv(const char* name,
                             Diligent::Uint32 fallback,
                             Diligent::Uint32 min_value,
                             Diligent::Uint32 max_value) {
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') {
    return fallback;
  }

  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (!end || *end != '\0' || parsed > std::numeric_limits<Diligent::Uint32>::max()) {
    std::fprintf(stderr, "[Karma] Ignoring invalid %s=%s\n", name, value);
    std::fflush(stderr);
    return fallback;
  }

  const Diligent::Uint32 clamped =
      std::clamp(static_cast<Diligent::Uint32>(parsed), min_value, max_value);
  if (clamped != parsed) {
    std::fprintf(stderr,
                 "[Karma] Clamping %s=%s to %u\n",
                 name,
                 value,
                 static_cast<unsigned>(clamped));
    std::fflush(stderr);
  }
  return clamped;
}

Diligent::Uint32 chooseVulkanAdapter(Diligent::IEngineFactoryVk& factory) {
  Diligent::Uint32 adapter_count = 0;
  factory.EnumerateAdapters(Diligent::Version{}, adapter_count, nullptr);
  if (adapter_count == 0) {
    return Diligent::DEFAULT_ADAPTER_ID;
  }

  std::vector<Diligent::GraphicsAdapterInfo> adapters(adapter_count);
  factory.EnumerateAdapters(Diligent::Version{}, adapter_count, adapters.data());

  for (Diligent::Uint32 i = 0; i < adapter_count; ++i) {
    const auto& adapter = adapters[i];
    std::fprintf(stderr,
                 "[Karma] Vulkan adapter %u: %s (%s)\n",
                 i,
                 adapter.Description,
                 adapterTypeName(adapter.Type));
  }

  const Diligent::Uint32 requested = adapterIdFromEnv("KARMA_VK_ADAPTER");
  if (requested != Diligent::DEFAULT_ADAPTER_ID) {
    if (requested < adapter_count) {
      std::fprintf(stderr,
                   "[Karma] Using Vulkan adapter %u from KARMA_VK_ADAPTER\n",
                   requested);
      std::fflush(stderr);
      return requested;
    }
    std::fprintf(stderr,
                 "[Karma] Ignoring KARMA_VK_ADAPTER=%u; only %u adapter(s) available\n",
                 requested,
                 adapter_count);
  }

  auto find_type = [&](Diligent::ADAPTER_TYPE type) {
    for (Diligent::Uint32 i = 0; i < adapter_count; ++i) {
      if (adapters[i].Type == type) {
        return i;
      }
    }
    return Diligent::DEFAULT_ADAPTER_ID;
  };

  Diligent::Uint32 selected = find_type(Diligent::ADAPTER_TYPE_DISCRETE);
  if (selected == Diligent::DEFAULT_ADAPTER_ID) {
    selected = find_type(Diligent::ADAPTER_TYPE_INTEGRATED);
  }
  if (selected == Diligent::DEFAULT_ADAPTER_ID) {
    for (Diligent::Uint32 i = 0; i < adapter_count; ++i) {
      if (adapters[i].Type != Diligent::ADAPTER_TYPE_SOFTWARE) {
        selected = i;
        break;
      }
    }
  }
  if (selected == Diligent::DEFAULT_ADAPTER_ID && envFlagEnabled("KARMA_ALLOW_SOFTWARE_VULKAN")) {
    selected = find_type(Diligent::ADAPTER_TYPE_SOFTWARE);
  }

  if (selected == Diligent::DEFAULT_ADAPTER_ID) {
    std::fprintf(stderr,
                 "[Karma] No hardware Vulkan adapter found; using Diligent default adapter\n");
    std::fflush(stderr);
    return selected;
  }

  std::fprintf(stderr,
               "[Karma] Using Vulkan adapter %u: %s (%s)\n",
               selected,
               adapters[selected].Description,
               adapterTypeName(adapters[selected].Type));
  std::fflush(stderr);
  return selected;
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
    float4 g_InstanceParams;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
    float4 g_MaterialParams3;
    float4 g_MaterialParams4;
    float4 g_MaterialParams5;
    float4 g_MaterialParams6;
    float4 g_TexCoordRow0[12];
    float4 g_TexCoordRow1[12];
};

cbuffer DeformationConstants
{
    float4 g_DeformationParams;
};

struct MorphTargetDelta
{
    float4 position;
    float4 normal;
    float4 tangent;
};

StructuredBuffer<float4x4> g_DeformationMatrices;
StructuredBuffer<float> g_MorphWeights;
StructuredBuffer<MorphTargetDelta> g_MorphTargetDeltas;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
    float2 UV1 : ATTRIB10;
    float4 ModelCol0 : ATTRIB4;
    float4 ModelCol1 : ATTRIB5;
    float4 ModelCol2 : ATTRIB6;
    float4 ModelCol3 : ATTRIB7;
    float4 InstanceParams : ATTRIB11;
    float4 JointIndices : ATTRIB8;
    float4 JointWeights : ATTRIB9;
    uint VertexId : SV_VertexID;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 local_pos = input.Pos;
    uint morph_count = (uint)max(g_DeformationParams.z, 0.0);
    uint vertex_count = (uint)max(g_DeformationParams.w, 0.0);
    if (morph_count > 0u && vertex_count > 0u)
    {
        uint vertex_id = min(input.VertexId, vertex_count - 1u);
        for (uint target = 0u; target < morph_count; ++target)
        {
            float weight = g_MorphWeights[target];
            if (abs(weight) <= 1.0e-6)
            {
                continue;
            }
            MorphTargetDelta delta = g_MorphTargetDeltas[target * vertex_count + vertex_id];
            local_pos += delta.position.xyz * weight;
        }
    }
    if (g_DeformationParams.x > 0.5)
    {
        uint4 joints = (uint4)round(input.JointIndices);
        uint joint_count = max((uint)g_DeformationParams.y, 1u);
        float4 weights = input.JointWeights;
        float weight_sum = weights.x + weights.y + weights.z + weights.w;
        if (weight_sum > 1.0e-5)
        {
            float4 bind_pos = float4(local_pos, 1.0);
            float4 skinned_pos =
                mul(g_DeformationMatrices[min(joints.x, joint_count - 1u)], bind_pos) * weights.x +
                mul(g_DeformationMatrices[min(joints.y, joint_count - 1u)], bind_pos) * weights.y +
                mul(g_DeformationMatrices[min(joints.z, joint_count - 1u)], bind_pos) * weights.z +
                mul(g_DeformationMatrices[min(joints.w, joint_count - 1u)], bind_pos) * weights.w;
            local_pos = skinned_pos.xyz / max(skinned_pos.w, 1.0e-5);
        }
    }
    uint shading_mode = (uint)round(g_MaterialParams0.x);
    if (shading_mode == 7u)
    {
        float blade_height = saturate(1.0 - input.UV.y);
        float sway_weight = blade_height * blade_height;
        float phase = g_LocalLightMeta.w * 1.7 +
                      input.InstanceParams.x * 0.13 +
                      input.InstanceParams.y * 0.19;
        local_pos.x += sin(phase + local_pos.y * 2.4) * 0.055 * sway_weight;
        local_pos.z += cos(phase * 0.73 + local_pos.y * 1.9) * 0.035 * sway_weight;
    }
    float4 world_pos;
    if (g_InstanceParams.x > 0.5)
    {
        float3 scale = input.ModelCol1.xyz;
        float yaw = input.ModelCol0.w;
        float s = sin(yaw);
        float c = cos(yaw);
        float3 scaled = local_pos * scale;
        float3 rotated = float3(scaled.x * c + scaled.z * s,
                                scaled.y,
                                -scaled.x * s + scaled.z * c);
        world_pos = float4(rotated + input.ModelCol0.xyz, 1.0);
    }
    else
    {
        world_pos = input.ModelCol0 * local_pos.x +
                    input.ModelCol1 * local_pos.y +
                    input.ModelCol2 * local_pos.z +
                    input.ModelCol3;
    }
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
                      opaque_double_sided_pipeline_state_.RawPtr(),
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
    for (auto& pso_ref : compact_forward_pipeline_states_) {
      auto* pso = pso_ref.RawPtr();
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
                    opaque_double_sided_pipeline_state_.RawPtr(),
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
  for (auto& pso_ref : compact_forward_pipeline_states_) {
    auto* pso = pso_ref.RawPtr();
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

  constexpr Diligent::Uint32 kInstanceStride =
      static_cast<Diligent::Uint32>(sizeof(float) * 20);
  const Diligent::Uint32 model_col1_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 4);
  const Diligent::Uint32 model_col2_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 8);
  const Diligent::Uint32 model_col3_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 12);
  const Diligent::Uint32 params_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 16);
  Diligent::LayoutElement layout_elems[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{3, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{10, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{8, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{9, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false,
                              0u,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col1_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col2_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col3_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{11, 1, 4, Diligent::VT_FLOAT32, false,
                              params_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
  };
  shadow_graphics.InputLayout.LayoutElements = layout_elems;
  shadow_graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc shadow_vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "DeformationConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_DeformationMatrices",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_MorphWeights",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_MorphTargetDeltas",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
  };
  shadow_pso.PSODesc.ResourceLayout.Variables = shadow_vars;
  shadow_pso.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(shadow_vars) / sizeof(shadow_vars[0]));

  const auto shadow_pso_start = core::SteadyClock::now();
  shadow_pipeline_state_ = device_with_cache_.CreateGraphicsPipelineState(shadow_pso);
  recordPipelineCreation("shadow",
                         "Karma Shadow Pipeline",
                         shadow_pso_start,
                         core::SteadyClock::now());
  if (!shadow_pipeline_state_) {
    return;
  }

  if (constants_) {
    if (auto* variable =
            shadow_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      variable->Set(constants_);
    }
  }
  if (deformation_constants_) {
    if (auto* variable = shadow_pipeline_state_->GetStaticVariableByName(
            Diligent::SHADER_TYPE_VERTEX, "DeformationConstants")) {
      variable->Set(deformation_constants_);
    }
  }
  const auto shadow_srb_start = core::SteadyClock::now();
  shadow_pipeline_state_->CreateShaderResourceBinding(&shadow_srb_, true);
  recordResourceCreation("shadow",
                         "Karma Shadow Pipeline SRB",
                         shadow_srb_start,
                         core::SteadyClock::now());
}

void DiligentBackend::bindForwardPipelineStaticResources(Diligent::IPipelineState* pso) const {
  if (!pso) {
    return;
  }
  if (constants_) {
    if (auto* variable =
            pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      variable->Set(constants_);
    }
    if (auto* variable =
            pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) {
      variable->Set(constants_);
    }
  }
  if (deformation_constants_) {
    if (auto* variable = pso->GetStaticVariableByName(
            Diligent::SHADER_TYPE_VERTEX, "DeformationConstants")) {
      variable->Set(deformation_constants_);
    }
  }
  if (shadow_map_srv_) {
    if (auto* variable =
            pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
      variable->Set(shadow_map_srv_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
  if (auto* variable =
          pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap")) {
    if (point_shadow_map_srv_ || shadow_map_srv_) {
      variable->Set(point_shadow_map_srv_ ? point_shadow_map_srv_ : shadow_map_srv_,
                    Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
  if (shadow_sampler_) {
    if (auto* variable =
            pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler")) {
      variable->Set(shadow_sampler_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
}

size_t DiligentBackend::forwardPipelineVariantIndex(ForwardPipelineVariant variant) {
  switch (variant) {
    case ForwardPipelineVariant::Opaque:
      return 0u;
    case ForwardPipelineVariant::OpaqueDoubleSided:
      return 1u;
    case ForwardPipelineVariant::DepthPrepass:
      return 2u;
    case ForwardPipelineVariant::Transparent:
      return 3u;
    case ForwardPipelineVariant::TransparentDoubleSided:
      return 4u;
    case ForwardPipelineVariant::Additive:
      return 5u;
    case ForwardPipelineVariant::AdditiveDoubleSided:
      return 6u;
  }
  return 0u;
}

size_t DiligentBackend::instanceGpuLayoutIndex(renderer::InstanceGpuLayout layout) {
  switch (layout) {
    case renderer::InstanceGpuLayout::Matrix4x4Params:
      return 0u;
    case renderer::InstanceGpuLayout::PositionYawScaleParams:
      return 1u;
  }
  return 0u;
}

Diligent::IPipelineState* DiligentBackend::ensureForwardPipeline(
    ForwardPipelineVariant variant,
    renderer::InstanceGpuLayout layout) {
  Diligent::RefCntAutoPtr<Diligent::IPipelineState>* out_pso = nullptr;
  const char* name = "Karma Pipeline";
  const bool compact_layout = layout == renderer::InstanceGpuLayout::PositionYawScaleParams;
  if (compact_layout) {
    out_pso = std::addressof(compact_forward_pipeline_states_[forwardPipelineVariantIndex(variant)]);
  } else {
    switch (variant) {
      case ForwardPipelineVariant::Opaque:
        out_pso = std::addressof(pipeline_state_);
        break;
      case ForwardPipelineVariant::OpaqueDoubleSided:
        out_pso = std::addressof(opaque_double_sided_pipeline_state_);
        break;
      case ForwardPipelineVariant::DepthPrepass:
        out_pso = std::addressof(depth_prepass_pipeline_state_);
        break;
      case ForwardPipelineVariant::Transparent:
        out_pso = std::addressof(transparent_pipeline_state_);
        break;
      case ForwardPipelineVariant::TransparentDoubleSided:
        out_pso = std::addressof(transparent_double_sided_pipeline_state_);
        break;
      case ForwardPipelineVariant::Additive:
        out_pso = std::addressof(additive_pipeline_state_);
        break;
      case ForwardPipelineVariant::AdditiveDoubleSided:
        out_pso = std::addressof(additive_double_sided_pipeline_state_);
        break;
    }
  }
  switch (variant) {
    case ForwardPipelineVariant::Opaque:
      name = compact_layout ? "Karma Pipeline Compact" : "Karma Pipeline";
      break;
    case ForwardPipelineVariant::OpaqueDoubleSided:
      name = compact_layout ? "Karma Pipeline Compact (DoubleSided)"
                            : "Karma Pipeline (DoubleSided)";
      break;
    case ForwardPipelineVariant::DepthPrepass:
      name = compact_layout ? "Karma Depth Prepass Pipeline Compact"
                            : "Karma Depth Prepass Pipeline";
      break;
    case ForwardPipelineVariant::Transparent:
      name = compact_layout ? "Karma Transparent Pipeline Compact"
                            : "Karma Transparent Pipeline";
      break;
    case ForwardPipelineVariant::TransparentDoubleSided:
      name = compact_layout ? "Karma Transparent Pipeline Compact (DoubleSided)"
                            : "Karma Transparent Pipeline (DoubleSided)";
      break;
    case ForwardPipelineVariant::Additive:
      name = compact_layout ? "Karma Additive Pipeline Compact" : "Karma Additive Pipeline";
      break;
    case ForwardPipelineVariant::AdditiveDoubleSided:
      name = compact_layout ? "Karma Additive Pipeline Compact (DoubleSided)"
                            : "Karma Additive Pipeline (DoubleSided)";
      break;
  }

  if (out_pso == nullptr) {
    return nullptr;
  }
  if (*out_pso) {
    return out_pso->RawPtr();
  }
  if (!device_ || !forward_vs_) {
    return nullptr;
  }
  const bool depth_prepass = variant == ForwardPipelineVariant::DepthPrepass;
  if (!depth_prepass && !forward_ps_) {
    return nullptr;
  }

  Diligent::GraphicsPipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.Name = name;
  pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso_ci.pVS = forward_vs_;
  pso_ci.pPS = depth_prepass ? nullptr : forward_ps_.RawPtr();

  auto& graphics = pso_ci.GraphicsPipeline;
  graphics.NumRenderTargets = depth_prepass ? 0u : 1u;
  if (!depth_prepass) {
    graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                         : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
  }
  graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                   : Diligent::TEX_FORMAT_D32_FLOAT;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
  graphics.RasterizerDesc.FrontCounterClockwise = true;
  graphics.DepthStencilDesc.DepthEnable = true;
  graphics.DepthStencilDesc.DepthWriteEnable = true;
  graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;

  const bool double_sided = variant == ForwardPipelineVariant::OpaqueDoubleSided ||
                            variant == ForwardPipelineVariant::TransparentDoubleSided ||
                            variant == ForwardPipelineVariant::AdditiveDoubleSided;
  const bool transparent = variant == ForwardPipelineVariant::Transparent ||
                           variant == ForwardPipelineVariant::TransparentDoubleSided ||
                           variant == ForwardPipelineVariant::Additive ||
                           variant == ForwardPipelineVariant::AdditiveDoubleSided;
  const bool additive = variant == ForwardPipelineVariant::Additive ||
                        variant == ForwardPipelineVariant::AdditiveDoubleSided;
  if (!depth_prepass && !transparent) {
    graphics.BlendDesc.AlphaToCoverageEnable = true;
  }
  if (double_sided) {
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
  }
  if (transparent) {
    graphics.DepthStencilDesc.DepthWriteEnable = false;
    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = additive ? Diligent::BLEND_FACTOR_ONE
                               : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = additive ? Diligent::BLEND_FACTOR_ONE
                                    : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
  }

  const Diligent::Uint32 kInstanceStride =
      static_cast<Diligent::Uint32>(renderer::instanceGpuLayoutStride(layout));
  const Diligent::Uint32 model_col1_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 4);
  const Diligent::Uint32 model_col2_offset =
      layout == renderer::InstanceGpuLayout::PositionYawScaleParams
          ? 0u
          : static_cast<Diligent::Uint32>(sizeof(float) * 8);
  const Diligent::Uint32 model_col3_offset =
      layout == renderer::InstanceGpuLayout::PositionYawScaleParams
          ? static_cast<Diligent::Uint32>(sizeof(float) * 4)
          : static_cast<Diligent::Uint32>(sizeof(float) * 12);
  const Diligent::Uint32 params_offset =
      layout == renderer::InstanceGpuLayout::PositionYawScaleParams
          ? static_cast<Diligent::Uint32>(sizeof(float) * 8)
          : static_cast<Diligent::Uint32>(sizeof(float) * 16);
  Diligent::LayoutElement layout_elems[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{3, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{10, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{8, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{9, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false,
                              0u,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col1_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col2_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col3_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{11, 1, 4, Diligent::VT_FLOAT32, false,
                              params_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
  };
  graphics.InputLayout.LayoutElements = layout_elems;
  graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "DeformationConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_DeformationMatrices",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_MorphWeights",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_MorphTargetDeltas",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
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
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerData", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_NormalTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatRoughnessTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatNormalTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SheenColorTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SheenRoughnessTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_TransmissionTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ThicknessTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
  };
  Diligent::ShaderResourceVariableDesc depth_prepass_vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "DeformationConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_DeformationMatrices",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_MorphWeights",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX, "g_MorphTargetDeltas",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
  };
  if (depth_prepass) {
    pso_ci.PSODesc.ResourceLayout.Variables = depth_prepass_vars;
    pso_ci.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(depth_prepass_vars) / sizeof(depth_prepass_vars[0]));
    pso_ci.PSODesc.ResourceLayout.ImmutableSamplers = nullptr;
    pso_ci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;
  } else {
    pso_ci.PSODesc.ResourceLayout.Variables = vars;
    pso_ci.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
  }

  const auto pso_start = core::SteadyClock::now();
  *out_pso = device_with_cache_.CreateGraphicsPipelineState(pso_ci);
  recordPipelineCreation("forward", name, pso_start, core::SteadyClock::now());
  bindForwardPipelineStaticResources(out_pso->RawPtr());
  if (depth_prepass && *out_pso) {
    const auto depth_srb_start = core::SteadyClock::now();
    (*out_pso)->CreateShaderResourceBinding(&depth_prepass_srb_, true);
    recordResourceCreation("forward", "depth prepass SRB", depth_srb_start, core::SteadyClock::now());
  }
  if (!depth_prepass && default_base_color_ && default_normal_ && default_metallic_roughness_ &&
      default_occlusion_ && default_emissive_) {
    auto initialize_default_for_variant =
        [&](Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
      initializeDefaultMaterialBinding(out_pso->RawPtr(), srb);
    };
    switch (variant) {
      case ForwardPipelineVariant::Opaque:
        initialize_default_for_variant(compact_layout
                                           ? compact_default_material_srbs_[forwardPipelineVariantIndex(variant)]
                                           : default_material_srb_);
        break;
      case ForwardPipelineVariant::OpaqueDoubleSided:
        initialize_default_for_variant(
            compact_layout
                ? compact_default_material_srbs_[forwardPipelineVariantIndex(variant)]
                : opaque_double_sided_default_material_srb_);
        break;
      case ForwardPipelineVariant::Transparent:
        initialize_default_for_variant(compact_layout
                                           ? compact_default_material_srbs_[forwardPipelineVariantIndex(variant)]
                                           : transparent_default_material_srb_);
        break;
      case ForwardPipelineVariant::TransparentDoubleSided:
        initialize_default_for_variant(
            compact_layout
                ? compact_default_material_srbs_[forwardPipelineVariantIndex(variant)]
                : transparent_double_sided_default_material_srb_);
        break;
      case ForwardPipelineVariant::Additive:
        initialize_default_for_variant(compact_layout
                                           ? compact_default_material_srbs_[forwardPipelineVariantIndex(variant)]
                                           : additive_default_material_srb_);
        break;
      case ForwardPipelineVariant::AdditiveDoubleSided:
        initialize_default_for_variant(
            compact_layout
                ? compact_default_material_srbs_[forwardPipelineVariantIndex(variant)]
                : additive_double_sided_default_material_srb_);
        break;
      case ForwardPipelineVariant::DepthPrepass:
        break;
    }
  }
  return out_pso->RawPtr();
}

Diligent::IPipelineState* DiligentBackend::ensureCustomForwardPipeline(
    const MaterialRecord& material,
    ForwardPipelineVariant variant,
    renderer::InstanceGpuLayout layout) {
  if (variant == ForwardPipelineVariant::DepthPrepass ||
      !materialUsesCustomForwardPipeline(material)) {
    return nullptr;
  }

  const bool pipeline_desc_custom = material.pipeline.name == "custom";
  const std::filesystem::path vertex_path = material.pipeline.vertex_shader_path;
  const std::filesystem::path fragment_path = material.pipeline.fragment_shader_path;
  const std::string vertex_entry =
      pipeline_desc_custom && !material.pipeline.vertex_entry_point.empty()
          ? material.pipeline.vertex_entry_point
          : std::string("main");
  const std::string fragment_entry =
      pipeline_desc_custom && !material.pipeline.fragment_entry_point.empty()
          ? material.pipeline.fragment_entry_point
          : std::string("main");

  std::string cache_key = std::to_string(static_cast<uint32_t>(variant));
  cache_key.append("|layout=");
  cache_key.append(std::to_string(static_cast<uint32_t>(layout)));
  cache_key.push_back('|');
  cache_key.append(vertex_path.string());
  cache_key.push_back('|');
  cache_key.append(fragment_path.string());
  cache_key.push_back('|');
  cache_key.append(vertex_entry);
  cache_key.push_back('|');
  cache_key.append(fragment_entry);
  cache_key.append("|depth_test=");
  cache_key.append(material.desc.depth_test ? "1" : "0");
  cache_key.append("|depth_write=");
  cache_key.append(material.desc.depth_write ? "1" : "0");
  cache_key.append("|double_sided=");
  cache_key.append(material.desc.double_sided ? "1" : "0");
  for (const std::string& define : material.pipeline.defines) {
    cache_key.push_back('|');
    cache_key.append(define);
  }

  auto& cached = custom_forward_pipelines_[cache_key];
  if (cached.attempted) {
    return cached.pso.RawPtr();
  }
  cached.attempted = true;

  if (!device_ || vertex_path.empty() || fragment_path.empty()) {
    return nullptr;
  }

  const std::vector<unsigned char> vs_bytes = readFileBytes(vertex_path);
  const std::vector<unsigned char> ps_bytes = readFileBytes(fragment_path);
  if (vs_bytes.empty() || ps_bytes.empty()) {
    spdlog::warn("Failed to load custom material shaders: vertex='{}' fragment='{}'",
                 vertex_path.string(),
                 fragment_path.string());
    return nullptr;
  }
  std::string vs_source(vs_bytes.begin(), vs_bytes.end());
  std::string ps_source(ps_bytes.begin(), ps_bytes.end());

  struct ParsedDefine {
    std::string name;
    std::string value;
  };
  std::vector<ParsedDefine> parsed_defines;
  parsed_defines.reserve(material.pipeline.defines.size() + 2u);
  parsed_defines.push_back(ParsedDefine{.name = "KARMA_CUSTOM_MATERIAL", .value = "1"});
  if (layout == renderer::InstanceGpuLayout::PositionYawScaleParams) {
    parsed_defines.push_back(ParsedDefine{
        .name = "KARMA_INSTANCE_LAYOUT_POSITION_YAW_SCALE",
        .value = "1",
    });
  }
  for (const std::string& define : material.pipeline.defines) {
    if (define.empty()) {
      continue;
    }
    const size_t equals = define.find('=');
    if (equals == std::string::npos) {
      parsed_defines.push_back(ParsedDefine{.name = define, .value = "1"});
    } else if (equals > 0) {
      parsed_defines.push_back(ParsedDefine{
          .name = define.substr(0, equals),
          .value = define.substr(equals + 1),
      });
    }
  }
  std::vector<Diligent::ShaderMacro> macros;
  macros.reserve(parsed_defines.size());
  for (const ParsedDefine& define : parsed_defines) {
    macros.push_back(Diligent::ShaderMacro{define.name.c_str(), define.value.c_str()});
  }
  Diligent::ShaderMacroArray macro_array{
      macros.empty() ? nullptr : macros.data(),
      static_cast<Diligent::Uint32>(macros.size()),
  };

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.Macros = macro_array;

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Custom Material VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = vertex_entry.c_str();
  shader_ci.Source = vs_source.c_str();
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Custom Material PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = fragment_entry.c_str();
  shader_ci.Source = ps_source.c_str();
  ps = device_with_cache_.CreateShader(shader_ci);
  if (!vs || !ps) {
    spdlog::warn("Failed to compile custom material shaders: vertex='{}' fragment='{}'",
                 vertex_path.string(),
                 fragment_path.string());
    return nullptr;
  }

  const bool double_sided = material.desc.double_sided ||
                            variant == ForwardPipelineVariant::OpaqueDoubleSided ||
                            variant == ForwardPipelineVariant::TransparentDoubleSided ||
                            variant == ForwardPipelineVariant::AdditiveDoubleSided;
  const bool transparent = variant == ForwardPipelineVariant::Transparent ||
                           variant == ForwardPipelineVariant::TransparentDoubleSided ||
                           variant == ForwardPipelineVariant::Additive ||
                           variant == ForwardPipelineVariant::AdditiveDoubleSided;
  const bool additive = variant == ForwardPipelineVariant::Additive ||
                        variant == ForwardPipelineVariant::AdditiveDoubleSided;
  std::string pso_name = "Karma Custom Material Pipeline";
  if (transparent) {
    pso_name.append(additive ? " Additive" : " Transparent");
  }
  if (double_sided) {
    pso_name.append(" DoubleSided");
  }

  Diligent::GraphicsPipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.Name = pso_name.c_str();
  pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso_ci.pVS = vs;
  pso_ci.pPS = ps;

  auto& graphics = pso_ci.GraphicsPipeline;
  graphics.NumRenderTargets = 1u;
  graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                       : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
  graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                   : Diligent::TEX_FORMAT_D32_FLOAT;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.RasterizerDesc.CullMode =
      double_sided ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK;
  graphics.RasterizerDesc.FrontCounterClockwise = true;
  graphics.DepthStencilDesc.DepthEnable = material.desc.depth_test;
  graphics.DepthStencilDesc.DepthWriteEnable = transparent ? false : material.desc.depth_write;
  graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
  if (!transparent &&
      material.desc.alpha_mode == renderer::MaterialDesc::AlphaMode::Masked &&
      material.desc.alpha_to_coverage) {
    graphics.BlendDesc.AlphaToCoverageEnable = true;
  }
  if (transparent) {
    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = additive ? Diligent::BLEND_FACTOR_ONE
                               : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = additive ? Diligent::BLEND_FACTOR_ONE
                                    : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
  }

  const Diligent::Uint32 kInstanceStride =
      static_cast<Diligent::Uint32>(renderer::instanceGpuLayoutStride(layout));
  const Diligent::Uint32 model_col1_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 4);
  const Diligent::Uint32 model_col2_offset =
      layout == renderer::InstanceGpuLayout::PositionYawScaleParams
          ? 0u
          : static_cast<Diligent::Uint32>(sizeof(float) * 8);
  const Diligent::Uint32 model_col3_offset =
      layout == renderer::InstanceGpuLayout::PositionYawScaleParams
          ? static_cast<Diligent::Uint32>(sizeof(float) * 4)
          : static_cast<Diligent::Uint32>(sizeof(float) * 12);
  const Diligent::Uint32 params_offset =
      layout == renderer::InstanceGpuLayout::PositionYawScaleParams
          ? static_cast<Diligent::Uint32>(sizeof(float) * 8)
          : static_cast<Diligent::Uint32>(sizeof(float) * 16);
  Diligent::LayoutElement layout_elems[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{3, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{10, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{8, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{9, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false,
                              0u,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col1_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col2_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false,
                              model_col3_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{11, 1, 4, Diligent::VT_FLOAT32, false,
                              params_offset,
                              kInstanceStride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
  };
  graphics.InputLayout.LayoutElements = layout_elems;
  graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX, "SkinningConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
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
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerData", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_NormalTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatRoughnessTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatNormalTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SheenColorTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SheenRoughnessTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_TransmissionTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ThicknessTex",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
  };
  pso_ci.PSODesc.ResourceLayout.Variables = vars;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

  const auto pso_start = core::SteadyClock::now();
  cached.pso = device_with_cache_.CreateGraphicsPipelineState(pso_ci);
  recordPipelineCreation("custom_forward", pso_name.c_str(), pso_start, core::SteadyClock::now());
  if (!cached.pso) {
    spdlog::warn("Failed to create custom material pipeline: vertex='{}' fragment='{}'",
                 vertex_path.string(),
                 fragment_path.string());
    return nullptr;
  }
  bindForwardPipelineStaticResources(cached.pso.RawPtr());
  return cached.pso.RawPtr();
}

void DiligentBackend::initializeDevice() {
#if defined(ENGINE_FORCE_VULKAN)
  (void)window_;
#endif
  const auto init_start = core::SteadyClock::now();
  auto stage_start = init_start;
  auto mark_stage = [&](const char* stage) {
    const auto stage_end = core::SteadyClock::now();
    logStartupDiag("diligent_device", stage, stage_start, stage_end);
    stage_start = stage_end;
  };

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
  engine_ci.Features.ShaderResourceRuntimeArray = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
  engine_ci.Features.Tessellation = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
  engine_ci.DynamicHeapSize = 64u << 20;
  engine_ci.DynamicHeapPageSize = 4u << 20;
  engine_ci.AdapterId = chooseVulkanAdapter(*factory);
  mark_stage("factory and adapter selection");

  if (window_) {
#if !defined(KARMA_WINDOW_BACKEND_SDL)
    Diligent::NativeWindow native = toNativeWindow(static_cast<GLFWwindow*>(window_->nativeHandle()));
    Diligent::SwapChainDesc sc_desc{};
    sc_desc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    sc_desc.DepthBufferFormat = Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
    sc_desc.Width = static_cast<Diligent::Uint32>(current_width_);
    sc_desc.Height = static_cast<Diligent::Uint32>(current_height_);
    sc_desc.BufferCount =
        uintFromEnv("KARMA_RENDER_SWAPCHAIN_BUFFERS", 2u, 2u, 8u);
    if (std::getenv("KARMA_RENDER_SWAPCHAIN_BUFFERS") != nullptr) {
      spdlog::info("Requested Vulkan swapchain buffer count: {}", sc_desc.BufferCount);
    }
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
  mark_stage("device and swapchain create");

  if (!device_) {
    logStartupDiag("diligent_device", "total", init_start, core::SteadyClock::now());
    return;
  }

  device_with_cache_ = Diligent::RenderDeviceWithCache<false>{device_};
  if (shader_cache_enabled_) {
    Diligent::RenderStateCacheCreateInfo cache_ci{};
    cache_ci.LogLevel = shader_cache_log_ ? Diligent::RENDER_STATE_CACHE_LOG_LEVEL_VERBOSE
                                           : Diligent::RENDER_STATE_CACHE_LOG_LEVEL_DISABLED;
    device_with_cache_.CreateRenderStateCache(cache_ci);
    if (!device_with_cache_.GetCache()) {
    }
    std::error_code ec;
    const auto cache_parent = render_state_cache_path_.parent_path();
    if (!cache_parent.empty()) {
      std::filesystem::create_directories(cache_parent, ec);
      if (ec && shader_cache_log_) {
        spdlog::warn("Render state cache directory create failed: path='{}' error='{}'",
                     cache_parent.string(),
                     ec.message());
      }
    }
    const auto cache_before = renderStateCacheFileInfo();
    if (shader_cache_log_) {
      spdlog::info(
          "Render state cache load begin: path='{}' existed={} bytes={} version={} "
          "update_on_exit=true",
          render_state_cache_path_.string(),
          cache_before.exists,
          cache_before.size,
          shader_cache_version_);
    }
    const auto cache_load_start = core::SteadyClock::now();
    device_with_cache_.LoadCacheFromFile(render_state_cache_path_.string().c_str(),
                                         true,
                                         shader_cache_version_);
    const auto cache_load_end = core::SteadyClock::now();
    logStartupDiag("diligent_device", "render state cache load file", cache_load_start, cache_load_end);
    if (shader_cache_log_) {
      const auto cache_after = renderStateCacheFileInfo();
      const auto* cache = device_with_cache_.GetCache();
      const Diligent::Uint32 content_version =
          cache ? cache->GetContentVersion() : static_cast<Diligent::Uint32>(~0u);
      spdlog::info(
          "Render state cache load end: path='{}' existed={} bytes={} content_version={} "
          "ms={:.2f}",
          render_state_cache_path_.string(),
          cache_after.exists,
          cache_after.size,
          content_version,
          core::elapsedMilliseconds(cache_load_start, cache_load_end));
    }
  } else if (shader_cache_log_) {
    spdlog::info("Render state cache disabled by KARMA_SHADER_CACHE");
  }
  mark_stage("render state cache load");

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
    float4 g_InstanceParams;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
    float4 g_MaterialParams3;
    float4 g_MaterialParams4;
    float4 g_MaterialParams5;
    float4 g_MaterialParams6;
    float4 g_TexCoordRow0[12];
    float4 g_TexCoordRow1[12];
};

cbuffer DeformationConstants
{
    float4 g_DeformationParams;
};

struct MorphTargetDelta
{
    float4 position;
    float4 normal;
    float4 tangent;
};

StructuredBuffer<float4x4> g_DeformationMatrices;
StructuredBuffer<float> g_MorphWeights;
StructuredBuffer<MorphTargetDelta> g_MorphTargetDeltas;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
    float2 UV1 : ATTRIB10;
    float4 ModelCol0 : ATTRIB4;
    float4 ModelCol1 : ATTRIB5;
    float4 ModelCol2 : ATTRIB6;
    float4 ModelCol3 : ATTRIB7;
    float4 InstanceParams : ATTRIB11;
    float4 JointIndices : ATTRIB8;
    float4 JointWeights : ATTRIB9;
    uint VertexId : SV_VertexID;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    float2 UV : TEXCOORD0;
    float2 UV1 : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float3 WorldPos : TEXCOORD3;
    float4 InstanceParams : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 local_pos = input.Pos;
    float3 local_normal = input.Normal;
    float3 local_tangent = input.Tangent.xyz;
    uint morph_count = (uint)max(g_DeformationParams.z, 0.0);
    uint vertex_count = (uint)max(g_DeformationParams.w, 0.0);
    if (morph_count > 0u && vertex_count > 0u)
    {
        uint vertex_id = min(input.VertexId, vertex_count - 1u);
        for (uint target = 0u; target < morph_count; ++target)
        {
            float weight = g_MorphWeights[target];
            if (abs(weight) <= 1.0e-6)
            {
                continue;
            }
            MorphTargetDelta delta = g_MorphTargetDeltas[target * vertex_count + vertex_id];
            local_pos += delta.position.xyz * weight;
            local_normal += delta.normal.xyz * weight;
            local_tangent += delta.tangent.xyz * weight;
        }
        local_normal = normalize(local_normal);
        local_tangent = normalize(local_tangent);
    }
    if (g_DeformationParams.x > 0.5)
    {
        uint4 joints = (uint4)round(input.JointIndices);
        uint joint_count = max((uint)g_DeformationParams.y, 1u);
        float4 weights = input.JointWeights;
        float weight_sum = weights.x + weights.y + weights.z + weights.w;
        if (weight_sum > 1.0e-5)
        {
            float4 bind_pos = float4(local_pos, 1.0);
            float4 skinned_pos =
                mul(g_DeformationMatrices[min(joints.x, joint_count - 1u)], bind_pos) * weights.x +
                mul(g_DeformationMatrices[min(joints.y, joint_count - 1u)], bind_pos) * weights.y +
                mul(g_DeformationMatrices[min(joints.z, joint_count - 1u)], bind_pos) * weights.z +
                mul(g_DeformationMatrices[min(joints.w, joint_count - 1u)], bind_pos) * weights.w;
            local_pos = skinned_pos.xyz / max(skinned_pos.w, 1.0e-5);
            local_normal =
                mul((float3x3)g_DeformationMatrices[min(joints.x, joint_count - 1u)], local_normal) * weights.x +
                mul((float3x3)g_DeformationMatrices[min(joints.y, joint_count - 1u)], local_normal) * weights.y +
                mul((float3x3)g_DeformationMatrices[min(joints.z, joint_count - 1u)], local_normal) * weights.z +
                mul((float3x3)g_DeformationMatrices[min(joints.w, joint_count - 1u)], local_normal) * weights.w;
            local_tangent =
                mul((float3x3)g_DeformationMatrices[min(joints.x, joint_count - 1u)], local_tangent) * weights.x +
                mul((float3x3)g_DeformationMatrices[min(joints.y, joint_count - 1u)], local_tangent) * weights.y +
                mul((float3x3)g_DeformationMatrices[min(joints.z, joint_count - 1u)], local_tangent) * weights.z +
                mul((float3x3)g_DeformationMatrices[min(joints.w, joint_count - 1u)], local_tangent) * weights.w;
        }
    }
    uint shading_mode = (uint)round(g_MaterialParams0.x);
    if (shading_mode == 7u)
    {
        float blade_height = saturate(1.0 - input.UV.y);
        float sway_weight = blade_height * blade_height;
        float phase = g_LocalLightMeta.w * 1.7 +
                      input.InstanceParams.x * 0.13 +
                      input.InstanceParams.y * 0.19;
        local_pos.x += sin(phase + local_pos.y * 2.4) * 0.055 * sway_weight;
        local_pos.z += cos(phase * 0.73 + local_pos.y * 1.9) * 0.035 * sway_weight;
    }
    float4 world_pos;
    float3 world_normal;
    float3 world_tangent;
    if (g_InstanceParams.x > 0.5)
    {
        float3 scale = input.ModelCol1.xyz;
        float3 safe_scale = max(abs(scale), float3(1.0e-5, 1.0e-5, 1.0e-5));
        float3 scaled = local_pos * scale;
        float3 normal_scaled = local_normal / safe_scale;
        if (g_InstanceParams.y > 0.5)
        {
            float3 center = input.ModelCol0.xyz;
            float3 up_axis = float3(0.0, 1.0, 0.0);
            float3 forward_axis = g_CameraPos.xyz - center;
            forward_axis.y = 0.0;
            if (dot(forward_axis, forward_axis) <= 1.0e-6)
            {
                forward_axis = float3(0.0, 0.0, 1.0);
            }
            else
            {
                forward_axis = normalize(forward_axis);
            }
            float3 right_axis = cross(up_axis, forward_axis);
            if (dot(right_axis, right_axis) <= 1.0e-6)
            {
                right_axis = float3(1.0, 0.0, 0.0);
            }
            else
            {
                right_axis = normalize(right_axis);
            }
            forward_axis = normalize(cross(right_axis, up_axis));
            float3 billboard_pos =
                right_axis * scaled.x + up_axis * scaled.y + forward_axis * scaled.z;
            world_pos = float4(billboard_pos + center, 1.0);
            world_normal = right_axis * normal_scaled.x +
                           up_axis * normal_scaled.y +
                           forward_axis * normal_scaled.z;
            world_tangent = right_axis * local_tangent.x +
                            up_axis * local_tangent.y +
                            forward_axis * local_tangent.z;
        }
        else
        {
            float yaw = input.ModelCol0.w;
            float s = sin(yaw);
            float c = cos(yaw);
            float3 rotated = float3(scaled.x * c + scaled.z * s,
                                    scaled.y,
                                    -scaled.x * s + scaled.z * c);
            world_pos = float4(rotated + input.ModelCol0.xyz, 1.0);
            world_normal = float3(normal_scaled.x * c + normal_scaled.z * s,
                                  normal_scaled.y,
                                  -normal_scaled.x * s + normal_scaled.z * c);
            world_tangent = float3(local_tangent.x * c + local_tangent.z * s,
                                   local_tangent.y,
                                   -local_tangent.x * s + local_tangent.z * c);
        }
    }
    else
    {
        world_pos = input.ModelCol0 * local_pos.x +
                    input.ModelCol1 * local_pos.y +
                    input.ModelCol2 * local_pos.z +
                    input.ModelCol3;
        world_normal = input.ModelCol0.xyz * local_normal.x +
                       input.ModelCol1.xyz * local_normal.y +
                       input.ModelCol2.xyz * local_normal.z;
        world_tangent = local_tangent;
    }
    output.Pos = mul(g_MVP, world_pos);
    output.WorldPos = world_pos.xyz;
    output.Normal = normalize(world_normal);
    output.UV = input.UV;
    output.UV1 = input.UV1;
    output.Tangent = float4(normalize(world_tangent), input.Tangent.w);
    output.InstanceParams = input.InstanceParams;
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
    float4 g_InstanceParams;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
    float4 g_MaterialParams3;
    float4 g_MaterialParams4;
    float4 g_MaterialParams5;
    float4 g_MaterialParams6;
    float4 g_TexCoordRow0[12];
    float4 g_TexCoordRow1[12];
};

Texture2D g_BaseColorTex;
Texture2D g_NormalTex;
Texture2D g_MetallicRoughnessTex;
Texture2D g_OcclusionTex;
Texture2D g_EmissiveTex;
Texture2D g_ClearcoatTex;
Texture2D g_ClearcoatRoughnessTex;
Texture2D g_ClearcoatNormalTex;
Texture2D g_SheenColorTex;
Texture2D g_SheenRoughnessTex;
Texture2D g_TransmissionTex;
Texture2D g_ThicknessTex;
TextureCube g_IrradianceTex;
TextureCube g_PrefilterTex;
Texture2D g_BRDFLUT;
Texture2D g_SceneColor;
Texture2D<float> g_SceneDepth;
Texture2DArray<float> g_ShadowMap;
Texture2DArray<float> g_PointShadowMap;
SamplerState g_SamplerColor;
SamplerState g_SamplerClamp;
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
    float2 UV1 : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float3 WorldPos : TEXCOORD3;
    float4 InstanceParams : TEXCOORD4;
    bool FrontFace : SV_IsFrontFace;
};

float2 MaterialUV(float2 uv0, float2 uv1, uint slot)
{
    float4 row0 = g_TexCoordRow0[slot];
    float4 row1 = g_TexCoordRow1[slot];
    float2 uv = lerp(uv0, uv1, step(0.5, row0.w));
    return float2(dot(row0.xy, uv) + row0.z,
                  dot(row1.xy, uv) + row1.z);
}

float Bayer4x4(float2 pixel)
{
    static const float values[16] =
    {
        0.0, 8.0, 2.0, 10.0,
        12.0, 4.0, 14.0, 6.0,
        3.0, 11.0, 1.0, 9.0,
        15.0, 7.0, 13.0, 5.0
    };
    uint2 p = uint2(pixel) & 3u;
    return (values[p.y * 4u + p.x] + 0.5) / 16.0;
}

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
            float weight_sum = 0.0;
            [unroll]
            for (int y = -4; y <= 4; ++y)
            {
                [unroll]
                for (int x = -4; x <= 4; ++x)
                {
                    if (abs(x) <= radius && abs(y) <= radius)
                    {
                        float weight = ((float)(radius + 1 - abs(x))) *
                                       ((float)(radius + 1 - abs(y)));
                        float2 offset = float2((float)x, (float)y) * texel;
                        sum += weight * g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                                       float3(shadow_uv + offset,
                                                                              (float)cascade_idx),
                                                                       shadow_depth - bias);
                        weight_sum += weight;
                    }
                }
            }
            shadow = (weight_sum > 0.0) ? (sum / weight_sum) : 1.0;
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
        float weight_sum = 0.0;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                if (abs(x) <= radius && abs(y) <= radius)
                {
                    float weight = ((float)(radius + 1 - abs(x))) *
                                   ((float)(radius + 1 - abs(y)));
                    float2 offset = float2((float)x, (float)y) * texel;
                    sum += weight * g_PointShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                                        float3(shadow_uv + offset,
                                                                               (float)matrix_idx),
                                                                        compare_depth);
                    weight_sum += weight;
                }
            }
        }
        shadow = (weight_sum > 0.0) ? (sum / weight_sum) : 1.0;
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

float DistributionGGX(float3 n, float3 h, float perceptual_roughness)
{
    const float PI = 3.14159265;
    float a = max(perceptual_roughness * perceptual_roughness, 0.001);
    float a2 = a * a;
    float n_dot_h = max(dot(n, h), 0.0);
    float n_dot_h2 = n_dot_h * n_dot_h;
    float denom = n_dot_h2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1.0e-5);
}

float DistributionGGXAnisotropic(float3 n,
                                 float3 h,
                                 float3 tangent,
                                 float3 bitangent,
                                 float perceptual_roughness,
                                 float anisotropy)
{
    const float PI = 3.14159265;
    float alpha = max(perceptual_roughness * perceptual_roughness, 0.001);
    float stretch = sqrt(max(1.0 - 0.9 * abs(anisotropy), 0.1));
    float alpha_x = anisotropy >= 0.0 ? alpha / stretch : alpha * stretch;
    float alpha_y = anisotropy >= 0.0 ? alpha * stretch : alpha / stretch;
    alpha_x = max(alpha_x, 0.001);
    alpha_y = max(alpha_y, 0.001);

    float t_dot_h = dot(tangent, h);
    float b_dot_h = dot(bitangent, h);
    float n_dot_h = max(dot(n, h), 0.0);
    float denom = (t_dot_h * t_dot_h) / (alpha_x * alpha_x) +
                  (b_dot_h * b_dot_h) / (alpha_y * alpha_y) +
                  n_dot_h * n_dot_h;
    return 1.0 / max(PI * alpha_x * alpha_y * denom * denom, 1.0e-5);
}

float GeometrySchlickGGXDirect(float n_dot_v, float perceptual_roughness)
{
    float r = perceptual_roughness + 1.0;
    float k = (r * r) * 0.125;
    return n_dot_v / max(n_dot_v * (1.0 - k) + k, 1.0e-5);
}

float GeometrySmithDirect(float3 n, float3 v, float3 l, float perceptual_roughness)
{
    float n_dot_v = max(dot(n, v), 0.0);
    float n_dot_l = max(dot(n, l), 0.0);
    return GeometrySchlickGGXDirect(n_dot_v, perceptual_roughness) *
           GeometrySchlickGGXDirect(n_dot_l, perceptual_roughness);
}

float3 FresnelSchlick(float cos_theta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cos_theta), 5.0);
}

float3 EvaluatePbrLight(float3 n,
                        float3 v,
                        float3 l,
                        float3 radiance,
                        float3 base_color,
                        float metallic,
                        float perceptual_roughness)
{
    const float PI = 3.14159265;
    float n_dot_l = max(dot(n, l), 0.0);
    float n_dot_v = max(dot(n, v), 0.0);
    if (n_dot_l <= 0.0 || n_dot_v <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 h = normalize(v + l);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);
    float3 fresnel = FresnelSchlick(max(dot(h, v), 0.0), f0);
    float distribution = DistributionGGX(n, h, perceptual_roughness);
    float geometry = GeometrySmithDirect(n, v, l, perceptual_roughness);
    float3 specular = distribution * geometry * fresnel / max(4.0 * n_dot_v * n_dot_l, 1.0e-4);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color / PI;
    return (diffuse + specular) * radiance * n_dot_l;
}

float3 EvaluatePbrLightAnisotropic(float3 n,
                                   float3 v,
                                   float3 l,
                                   float3 radiance,
                                   float3 base_color,
                                   float metallic,
                                   float perceptual_roughness,
                                   float3 tangent,
                                   float3 bitangent,
                                   float anisotropy)
{
    const float PI = 3.14159265;
    float n_dot_l = max(dot(n, l), 0.0);
    float n_dot_v = max(dot(n, v), 0.0);
    if (n_dot_l <= 0.0 || n_dot_v <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 h = normalize(v + l);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);
    float3 fresnel = FresnelSchlick(max(dot(h, v), 0.0), f0);
    float distribution = abs(anisotropy) > 0.001
        ? DistributionGGXAnisotropic(n, h, tangent, bitangent, perceptual_roughness, anisotropy)
        : DistributionGGX(n, h, perceptual_roughness);
    float geometry = GeometrySmithDirect(n, v, l, perceptual_roughness);
    float3 specular = distribution * geometry * fresnel / max(4.0 * n_dot_v * n_dot_l, 1.0e-4);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color / PI;
    return (diffuse + specular) * radiance * n_dot_l;
}

float ClearcoatFresnel(float3 n, float3 v, float3 l)
{
    float n_dot_l = max(dot(n, l), 0.0);
    float n_dot_v = max(dot(n, v), 0.0);
    if (n_dot_l <= 0.0 || n_dot_v <= 0.0)
    {
        return 0.0;
    }
    float3 h = normalize(v + l);
    return FresnelSchlick(max(dot(h, v), 0.0), float3(0.04, 0.04, 0.04)).r;
}

float3 EvaluateClearcoatLight(float3 n,
                              float3 v,
                              float3 l,
                              float3 radiance,
                              float factor,
                              float perceptual_roughness)
{
    float n_dot_l = max(dot(n, l), 0.0);
    float n_dot_v = max(dot(n, v), 0.0);
    if (factor <= 0.0 || n_dot_l <= 0.0 || n_dot_v <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 h = normalize(v + l);
    float3 fresnel = FresnelSchlick(max(dot(h, v), 0.0), float3(0.04, 0.04, 0.04));
    float distribution = DistributionGGX(n, h, perceptual_roughness);
    float geometry = GeometrySmithDirect(n, v, l, perceptual_roughness);
    float3 specular = distribution * geometry * fresnel / max(4.0 * n_dot_v * n_dot_l, 1.0e-4);
    return specular * radiance * n_dot_l * factor;
}

float CharlieDistribution(float n_dot_h, float perceptual_roughness)
{
    const float PI = 3.14159265;
    float alpha = clamp(perceptual_roughness, 0.045, 1.0);
    float inv_alpha = 1.0 / max(alpha, 1.0e-4);
    float sin2h = max(1.0 - n_dot_h * n_dot_h, 1.0e-4);
    return (2.0 + inv_alpha) * pow(sin2h, inv_alpha * 0.5) / (2.0 * PI);
}

float SheenVisibility(float n_dot_l, float n_dot_v)
{
    return 1.0 / max(4.0 * (n_dot_l + n_dot_v - n_dot_l * n_dot_v), 1.0e-4);
}

float3 EvaluateSheenLight(float3 n,
                          float3 v,
                          float3 l,
                          float3 radiance,
                          float3 sheen_color,
                          float perceptual_roughness)
{
    float n_dot_l = max(dot(n, l), 0.0);
    float n_dot_v = max(dot(n, v), 0.0);
    float sheen_strength = max(max(sheen_color.r, sheen_color.g), sheen_color.b);
    if (sheen_strength <= 0.0 || n_dot_l <= 0.0 || n_dot_v <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 h = normalize(v + l);
    float n_dot_h = saturate(dot(n, h));
    float distribution = CharlieDistribution(n_dot_h, perceptual_roughness);
    float visibility = SheenVisibility(n_dot_l, n_dot_v);
    return sheen_color * distribution * visibility * radiance * n_dot_l;
}

float3 Uncharted2Tonemap(float3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) /
            max(x * (A * x + B) + D * F, float3(1.0e-4, 1.0e-4, 1.0e-4))) -
           E / F;
}

float3 ApplyFilmicTonemap(float3 color, float exposure)
{
    float average_log_lum = 0.30;
    float middle_gray = 0.18;
    float white_point = 3.0;
    float luminance_scale = middle_gray / max(average_log_lum, 1.0e-4);
    float3 mapped = Uncharted2Tonemap(max(color, float3(0.0, 0.0, 0.0)) * exposure * luminance_scale);
    float white_scale = 1.0 / max(Uncharted2Tonemap(float3(white_point, white_point, white_point)).r,
                                  1.0e-4);
    return saturate(mapped * white_scale);
}

void AccumulateLocalLight(ForwardPlusLight light,
                          float3 world_pos,
                          float3 geom_n,
                          float3 n,
                          float3 clearcoat_n,
                          float3 v,
                          float perceptual_roughness,
                          float metallic,
                          float3 base_color,
                          float3 tangent,
                          float3 bitangent,
                          float anisotropy,
                          float clearcoat_factor,
                          float clearcoat_roughness,
                          float3 sheen_color,
                          float sheen_roughness,
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
    float point_shadow = SamplePointShadow(light, world_pos, geom_n, l_local);
    float3 light_color = light.color_intensity.rgb * light.color_intensity.w * atten * point_shadow;
    float clearcoat_fresnel = ClearcoatFresnel(clearcoat_n, v, l_local) * clearcoat_factor;
    lit += EvaluatePbrLightAnisotropic(n,
                                       v,
                                       l_local,
                                       light_color,
                                       base_color,
                                       metallic,
                                       perceptual_roughness,
                                       tangent,
                                       bitangent,
                                       anisotropy) *
           (1.0 - clearcoat_fresnel);
    lit += EvaluateClearcoatLight(clearcoat_n,
                                  v,
                                  l_local,
                                  light_color,
                                  clearcoat_factor,
                                  clearcoat_roughness);
    lit += EvaluateSheenLight(n,
                              v,
                              l_local,
                              light_color,
                              sheen_color,
                              sheen_roughness);
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

void MergeInterval(float candidate_enter,
                   float candidate_exit,
                   inout float t_enter,
                   inout float t_exit,
                   inout float hit)
{
    if (candidate_exit <= 0.0 || candidate_exit < candidate_enter)
    {
        return;
    }
    t_enter = hit > 0.5 ? min(t_enter, candidate_enter) : candidate_enter;
    t_exit = hit > 0.5 ? max(t_exit, candidate_exit) : candidate_exit;
    hit = 1.0;
}

bool IntersectSphere(float3 ro, float3 rd, float radius, out float t_enter, out float t_exit)
{
    float half_b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float h = half_b * half_b - c;
    if (h <= 0.0)
    {
        t_enter = 0.0;
        t_exit = 0.0;
        return false;
    }
    h = sqrt(h);
    t_enter = -half_b - h;
    t_exit = -half_b + h;
    return t_exit > 0.0;
}

bool IntersectCapsule(float3 ro,
                      float3 rd,
                      float3 axis,
                      float half_length,
                      float radius,
                      out float t_enter,
                      out float t_exit)
{
    t_enter = 0.0;
    t_exit = 0.0;
    float hit = 0.0;

    float ro_axis = dot(ro, axis);
    float rd_axis = dot(rd, axis);
    float3 ro_perp = ro - axis * ro_axis;
    float3 rd_perp = rd - axis * rd_axis;
    float radial_a = dot(rd_perp, rd_perp);
    float radial_b = dot(ro_perp, rd_perp);
    float radial_c = dot(ro_perp, ro_perp) - radius * radius;
    if (radial_a > 1.0e-6)
    {
        float h = radial_b * radial_b - radial_a * radial_c;
        if (h >= 0.0)
        {
            h = sqrt(h);
            float cyl_enter = (-radial_b - h) / radial_a;
            float cyl_exit = (-radial_b + h) / radial_a;
            float slab_enter = -1.0e20;
            float slab_exit = 1.0e20;
            if (abs(rd_axis) > 1.0e-6)
            {
                float a = (-half_length - ro_axis) / rd_axis;
                float b = (half_length - ro_axis) / rd_axis;
                slab_enter = min(a, b);
                slab_exit = max(a, b);
            }
            else if (ro_axis < -half_length || ro_axis > half_length)
            {
                slab_enter = 1.0;
                slab_exit = 0.0;
            }
            MergeInterval(max(cyl_enter, slab_enter), min(cyl_exit, slab_exit), t_enter, t_exit, hit);
        }
    }

    float cap_enter = 0.0;
    float cap_exit = 0.0;
    if (IntersectSphere(ro - axis * half_length, rd, radius, cap_enter, cap_exit))
    {
        MergeInterval(cap_enter, cap_exit, t_enter, t_exit, hit);
    }
    if (IntersectSphere(ro + axis * half_length, rd, radius, cap_enter, cap_exit))
    {
        MergeInterval(cap_enter, cap_exit, t_enter, t_exit, hit);
    }
    return hit > 0.5;
}

float VolumePhase(float3 light_dir, float3 view_dir, float anisotropy)
{
    float g = clamp(anisotropy, -0.85, 0.85);
    float cos_theta = dot(light_dir, view_dir);
    float denom = max(1.0 + g * g - 2.0 * g * cos_theta, 1.0e-3);
    return (1.0 - g * g) / pow(denom, 1.5);
}

void AccumulateVolumeLocalLight(ForwardPlusLight light,
                                float3 world_pos,
                                float3 view_dir,
                                float anisotropy,
                                inout float3 light_accum)
{
    float3 to_light = light.position_range.xyz - world_pos;
    float dist = length(to_light);
    if (dist <= 1.0e-4 || dist >= light.position_range.w)
    {
        return;
    }
    float3 l_local = to_light / dist;
    float dist_sq = max(dot(to_light, to_light), 1.0e-4);
    float range_t = saturate(dist / light.position_range.w);
    float range_falloff = saturate(1.0 - range_t);
    range_falloff = range_falloff * range_falloff * (3.0 - 2.0 * range_falloff);
    range_falloff = pow(range_falloff, max(g_LocalLightParams.y, 0.1));
    float atten = range_falloff / max(dist_sq + max(g_LocalLightParams.x, 0.0), 1.0e-4);
    if (light.direction_type.w > 1.5)
    {
        float3 spot_dir = normalize(-light.direction_type.xyz);
        float cone = dot(spot_dir, l_local);
        float denom = max(light.spot_params.x - light.spot_params.y, 1.0e-4);
        atten *= saturate((cone - light.spot_params.y) / denom);
    }
    float phase = VolumePhase(l_local, view_dir, anisotropy);
    light_accum += light.color_intensity.rgb * light.color_intensity.w * atten * phase;
}

float3 SampleVolumeLighting(float3 world_pos, float3 ray_dir, float anisotropy, uint2 pixel)
{
    float3 view_dir = -ray_dir;
    float3 directional_dir = normalize(-g_LightDir.xyz);
    float3 lighting = g_LightColor.rgb * (0.16 + 0.84 * VolumePhase(directional_dir, view_dir, anisotropy));

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
            AccumulateVolumeLocalLight(light, world_pos, view_dir, anisotropy, lighting);
        }
    }

    uint max_lights_per_tile = (uint)max(g_ForwardPlusParams.w, 0.0);
    if (cb_local_light_count == 0u && max_lights_per_tile > 0u)
    {
        uint tile_size = (uint)max(g_ForwardPlusParams.x, 1.0);
        uint safe_tiles_x = (uint)max(g_ForwardPlusParams.y, 1.0);
        uint safe_tiles_y = (uint)max(g_ForwardPlusParams.z, 1.0);
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
            AccumulateVolumeLocalLight(light, world_pos, view_dir, anisotropy, lighting);
        }
    }
    return lighting;
}

float4 main(PSInput input) : SV_TARGET
{
    const float PI = 3.14159265;
    float3 geom_n = normalize(input.Normal);
    if (!input.FrontFace)
    {
        geom_n = -geom_n;
    }
    float3 n = geom_n;
    float3 t = normalize(input.Tangent.xyz);
    float3 b = normalize(cross(geom_n, t) * input.Tangent.w);
    float2 base_uv = MaterialUV(input.UV, input.UV1, 0u);
    float2 normal_uv = MaterialUV(input.UV, input.UV1, 1u);
    float2 metallic_roughness_uv = MaterialUV(input.UV, input.UV1, 2u);
    float2 occlusion_uv = MaterialUV(input.UV, input.UV1, 3u);
    float2 emissive_uv = MaterialUV(input.UV, input.UV1, 4u);
    float2 clearcoat_uv = MaterialUV(input.UV, input.UV1, 5u);
    float2 clearcoat_roughness_uv = MaterialUV(input.UV, input.UV1, 6u);
    float2 clearcoat_normal_uv = MaterialUV(input.UV, input.UV1, 7u);
    float2 sheen_color_uv = MaterialUV(input.UV, input.UV1, 8u);
    float2 sheen_roughness_uv = MaterialUV(input.UV, input.UV1, 9u);
    float2 transmission_uv = MaterialUV(input.UV, input.UV1, 10u);
    float2 thickness_uv = MaterialUV(input.UV, input.UV1, 11u);
    uint shading_mode = (uint)round(g_MaterialParams0.x);
    bool standard_material = shading_mode == 0u;
    bool foliage_material = shading_mode == 7u;
    bool surface_material = standard_material || foliage_material;
    bool surface_unlit = surface_material && g_MaterialParams2.z > 0.5;
    if (foliage_material)
    {
        uint base_width = 1u;
        uint base_height = 1u;
        g_BaseColorTex.GetDimensions(base_width, base_height);
        float2 base_texel = 0.5 / max(float2((float)base_width, (float)base_height),
                                      float2(1.0, 1.0));
        base_uv = clamp(base_uv, base_texel, float2(1.0, 1.0) - base_texel);
    }
    float3 normal_tex = g_NormalTex.Sample(g_SamplerData, normal_uv).xyz * 2.0 - 1.0;
    normal_tex.xy *= g_PbrParams.w;
    normal_tex = normalize(normal_tex);
    n = normalize(normal_tex.x * t + normal_tex.y * b + normal_tex.z * n);
    float3 clearcoat_normal_tex =
        g_ClearcoatNormalTex.Sample(g_SamplerData, clearcoat_normal_uv).xyz * 2.0 - 1.0;
    clearcoat_normal_tex.xy *= g_PbrParams.w;
    clearcoat_normal_tex = normalize(clearcoat_normal_tex);
    float3 clearcoat_n =
        normalize(clearcoat_normal_tex.x * t +
                  clearcoat_normal_tex.y * b +
                  clearcoat_normal_tex.z * geom_n);
    float3 l_dir = normalize(-g_LightDir.xyz);
    float ndotl = max(dot(n, l_dir), 0.0);
    float4 base_tex = foliage_material
        ? g_BaseColorTex.Sample(g_SamplerClamp, base_uv)
        : g_BaseColorTex.Sample(g_SamplerColor, base_uv);
    float3 emissive_tex = g_EmissiveTex.Sample(g_SamplerColor, emissive_uv).rgb;
    float occlusion = g_OcclusionTex.Sample(g_SamplerData, occlusion_uv).r;
    float2 mr = g_MetallicRoughnessTex.Sample(g_SamplerData, metallic_roughness_uv).bg;
    float metallic = saturate(mr.x * g_PbrParams.x);
    float roughness = saturate(mr.y * g_PbrParams.y);

    float3 base_color = g_BaseColorFactor.rgb * base_tex.rgb;
    float3 emissive = g_EmissiveFactor.rgb * emissive_tex;

    float3 v = normalize(g_CameraPos.xyz - input.WorldPos);
    if (dot(clearcoat_n, v) < 0.0)
    {
        clearcoat_n = -clearcoat_n;
    }
    float perceptual_roughness = clamp(roughness, 0.045, 1.0);
    float3 spec_color = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);
    float clearcoat_factor =
        standard_material ? saturate(g_MaterialParams3.x *
                                     g_ClearcoatTex.Sample(g_SamplerData, clearcoat_uv).r)
                          : 0.0;
    float clearcoat_roughness =
        standard_material ? clamp(g_MaterialParams3.y *
                                  g_ClearcoatRoughnessTex.Sample(g_SamplerData,
                                                                 clearcoat_roughness_uv).g,
                                  0.045,
                                  1.0)
                          : 0.045;
    float sheen_roughness =
        standard_material ? clamp(g_MaterialParams3.z *
                                  g_SheenRoughnessTex.Sample(g_SamplerData,
                                                             sheen_roughness_uv).a,
                                  0.045,
                                  1.0)
                          : 0.045;
    float anisotropy = standard_material ? clamp(g_MaterialParams3.w, -1.0, 1.0) : 0.0;
    float3 sheen_color =
        standard_material ? saturate(g_MaterialParams4.rgb *
                                     g_SheenColorTex.Sample(g_SamplerColor, sheen_color_uv).rgb)
                          : float3(0.0, 0.0, 0.0);
    float transmission =
        standard_material ? saturate(g_MaterialParams4.w *
                                     g_TransmissionTex.Sample(g_SamplerData, transmission_uv).r)
                          : 0.0;
    float thickness =
        standard_material ? max(g_MaterialParams5.y *
                                g_ThicknessTex.Sample(g_SamplerData, thickness_uv).g,
                                0.0)
                          : 0.0;

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
                                 clearcoat_n,
                                 v,
                                 perceptual_roughness,
                                 metallic,
                                 base_color,
                                 t,
                                 b,
                                 anisotropy,
                                 clearcoat_factor,
                                 clearcoat_roughness,
                                 sheen_color,
                                 sheen_roughness,
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
                                 clearcoat_n,
                                 v,
                                 perceptual_roughness,
                                 metallic,
                                 base_color,
                                 t,
                                 b,
                                 anisotropy,
                                 clearcoat_factor,
                                 clearcoat_roughness,
                                 sheen_color,
                                 sheen_roughness,
                                 lit_local,
                                 local_shadow_lift_energy);
        }
    }
    float shadow_lift_strength = max(g_LocalLightParams.w, 0.0);
    float shadow_lift = 1.0 - exp(-local_shadow_lift_energy * shadow_lift_strength);
    float lifted_shadow = lerp(shadow, 1.0, saturate(shadow_lift));
    float3 directional_radiance = g_LightColor.rgb * lifted_shadow;
    float clearcoat_directional_fresnel =
        ClearcoatFresnel(clearcoat_n, v, l_dir) * clearcoat_factor;
    float3 lit_directional = EvaluatePbrLightAnisotropic(n,
                                                         v,
                                                         l_dir,
                                                         directional_radiance,
                                                         base_color,
                                                         metallic,
                                                         perceptual_roughness,
                                                         t,
                                                         b,
                                                         anisotropy) *
                             (1.0 - clearcoat_directional_fresnel);
    lit_directional += EvaluateClearcoatLight(clearcoat_n,
                                              v,
                                              l_dir,
                                              directional_radiance,
                                              clearcoat_factor,
                                              clearcoat_roughness);
    lit_directional += EvaluateSheenLight(n,
                                          v,
                                          l_dir,
                                          directional_radiance,
                                          sheen_color,
                                          sheen_roughness);
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
        float mip = saturate(perceptual_roughness) * g_EnvParams.y;
        float3 prefiltered = g_PrefilterTex.SampleLevel(g_SamplerColor, r, mip).rgb;
        float2 brdf = g_BRDFLUT.Sample(g_SamplerColor, float2(ndotv, perceptual_roughness)).rg;
        env_spec = prefiltered * (spec_color * brdf.x + brdf.y);
        float clearcoat_ndotv = max(dot(clearcoat_n, v), 0.0);
        float clearcoat_ibl_fresnel =
            FresnelSchlick(clearcoat_ndotv, float3(0.04, 0.04, 0.04)).r * clearcoat_factor;
        float base_layer_ibl_weight = 1.0 - clearcoat_ibl_fresnel;
        lit += env_diffuse * base_color * (1.0 - metallic) * occlusion * base_layer_ibl_weight;
        lit += env_spec * g_EnvParams.x * base_layer_ibl_weight;
        if (clearcoat_factor > 0.0)
        {
            float clearcoat_mip = saturate(clearcoat_roughness) * g_EnvParams.y;
            float3 clearcoat_r = reflect(-v, clearcoat_n);
            float3 clearcoat_prefiltered =
                g_PrefilterTex.SampleLevel(g_SamplerColor, clearcoat_r, clearcoat_mip).rgb;
            float2 clearcoat_brdf =
                g_BRDFLUT.Sample(g_SamplerColor, float2(clearcoat_ndotv, clearcoat_roughness)).rg;
            float3 clearcoat_spec =
                clearcoat_prefiltered * (float3(0.04, 0.04, 0.04) * clearcoat_brdf.x +
                                         clearcoat_brdf.y);
            lit += clearcoat_spec * g_EnvParams.x * clearcoat_factor;
        }
        if (max(max(sheen_color.r, sheen_color.g), sheen_color.b) > 0.0)
        {
            float sheen_facing = 0.25 + 0.75 * pow(saturate(1.0 - ndotv), 5.0);
            lit += env_diffuse * sheen_color * sheen_facing * (1.0 - metallic) * occlusion;
        }
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
                return float4(base_uv, 0.0, 1.0);
            }
            if (g_EnvParams.z < 9.5)
            {
                return float4(normal_tex.xyz * 0.5 + 0.5, 1.0);
            }
        }
    }
    float alpha_tex = base_tex.a;
    float base_alpha = saturate(g_BaseColorFactor.a * alpha_tex);
    if (surface_material && g_MaterialParams2.w >= 0.0)
    {
        float cutoff = saturate(g_MaterialParams2.w);
        float softness = max(g_MaterialParams1.x, 0.0);
        bool alpha_to_coverage = g_MaterialParams1.z > 0.5 && g_MaterialParams1.w > 0.5;
        bool dither_mask = !alpha_to_coverage && (g_MaterialParams1.y > 0.5 || g_MaterialParams1.z > 0.5);
        if (softness > 1.0e-5)
        {
            float coverage = saturate((base_alpha - (cutoff - softness * 0.5)) / softness);
            if (alpha_to_coverage)
            {
                if (coverage <= 0.0)
                {
                    discard;
                }
            }
            else if (dither_mask)
            {
                if (coverage <= Bayer4x4(input.Pos.xy))
                {
                    discard;
                }
            }
            else if (coverage <= 0.0)
            {
                discard;
            }
            base_alpha = coverage;
        }
        else if (base_alpha < cutoff)
        {
            discard;
        }
    }
    if (foliage_material)
    {
        float height_tint = saturate(1.0 - base_uv.y);
        float root_tint = saturate(base_uv.y);
        float3 foliage_ramp = lerp(float3(0.44, 0.53, 0.25),
                                   float3(0.76, 0.84, 0.44),
                                   height_tint);
        float wrap_light = saturate(ndotl * 0.72 + 0.28);
        float3 foliage_base = base_color * foliage_ramp;
        float3 ambient_term = foliage_base * (0.24 + 0.18 * saturate(g_EnvParams.x));
        float3 directional_term =
            foliage_base * g_LightColor.rgb * lifted_shadow * (0.42 + 0.46 * wrap_light);
        lit = ambient_term + directional_term + lit_local * 0.35 + emissive;
        lit *= lerp(0.82, 1.0, height_tint) * lerp(0.88, 1.0, 1.0 - root_tint * 0.35);
    }
    else if (surface_unlit)
    {
        lit = base_color + emissive;
    }
    else if (standard_material && g_MaterialParams6.w > 0.5)
    {
        float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
        float clearcoat_reflection_strength =
            clearcoat_factor * (1.0 - clearcoat_roughness * 0.55);
        float base_reflection_strength = metallic * (1.0 - roughness);
        float reflection_strength = saturate(clearcoat_reflection_strength +
                                             base_reflection_strength);
        float3 reflection_n = clearcoat_factor > 0.001 ? clearcoat_n : n;
        float3 reflection_dir = reflect(-v, reflection_n);
        float fresnel = pow(saturate(1.0 - max(dot(reflection_n, v), 0.0)), 5.0);
        float2 reflection_offset =
            reflection_dir.xy *
            (0.006 + 0.030 * reflection_strength) *
            (1.0 - roughness * 0.70);
        float2 reflection_uv = clamp(screen_uv + reflection_offset, 0.001, 0.999);
        float3 scene_reflection = g_SceneColor.Sample(g_SamplerColor, reflection_uv).rgb;
        lit = scene_reflection * lerp(float3(1.0, 1.0, 1.0), base_color, metallic * 0.28);
        base_alpha =
            saturate(reflection_strength * (0.08 + 0.40 * fresnel) *
                     (1.0 - roughness * 0.70));
    }
    else if (shading_mode == 1u)
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
        uint volume_shape = (uint)round(g_MaterialParams0.y);
        float anisotropy = clamp(g_MaterialParams0.z, -0.95, 0.95);
        float absorption = max(g_MaterialParams0.w, 0.0);
        float3 volume_center_ws = g_MaterialParams1.xyz;
        float volume_radius = max(g_MaterialParams1.w, 1.0e-4);
        float3 axis_x = SafeNormalize(g_MaterialParams2.xyz, float3(1.0, 0.0, 0.0));
        float capsule_half_length = max(g_MaterialParams2.w, 0.0);
        float3 axis_y = SafeNormalize(g_MaterialParams3.xyz, float3(0.0, 1.0, 0.0));
        float density = max(g_MaterialParams3.w, 0.0);
        float3 axis_z = SafeNormalize(g_MaterialParams4.xyz, float3(0.0, 0.0, 1.0));
        float scattering = max(g_MaterialParams4.w, 0.0);
        float distortion_strength = max(g_MaterialParams5.x, 0.0);
        float noise_strength = saturate(g_MaterialParams5.y);
        float time = g_LocalLightMeta.w;

        float3 ro = g_CameraPos.xyz - volume_center_ws;
        float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
        float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
        float t0 = 0.0;
        float t1 = 0.0;
        bool volume_hit = false;
        if (volume_shape == 1u)
        {
            volume_hit = IntersectCapsule(ro, ray_dir, axis_x, capsule_half_length, volume_radius, t0, t1);
        }
        else
        {
            volume_hit = IntersectSphere(ro, ray_dir, volume_radius, t0, t1);
        }
        if (!volume_hit || t1 <= 0.0)
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

        float transmittance = 1.0;
        float3 in_scatter = float3(0.0, 0.0, 0.0);
        float noise_a = 0.5;
        float noise_b = 0.5;
        float alpha_core = 0.0;
        const int step_count = 8;
        float step_length = path_length / (float)step_count;
        uint2 pixel = uint2(input.Pos.xy);
        [unroll]
        for (int step_idx = 0; step_idx < step_count; ++step_idx)
        {
            float sample_t = t_enter + ((float)step_idx + 0.5) * step_length;
            float3 sample_pos = g_CameraPos.xyz + ray_dir * sample_t;
            float3 offset = sample_pos - volume_center_ws;
            float axis_pos = dot(offset, axis_x);
            float radial_distance = length(offset) / volume_radius;
            if (volume_shape == 1u)
            {
                float closest_axis_pos = clamp(axis_pos, -capsule_half_length, capsule_half_length);
                radial_distance = length(offset - axis_x * closest_axis_pos) / volume_radius;
            }
            float local_x = axis_pos / max(capsule_half_length + volume_radius, volume_radius);
            float local_y = dot(offset, axis_y) / volume_radius;
            float local_z = dot(offset, axis_z) / volume_radius;
            float3 noise_domain =
                float3(local_x, local_y, local_z) * (2.4 + noise_strength * 1.6) +
                float3(time * 0.46, -time * 0.34, time * 0.29);
            noise_a = Noise3(noise_domain * 1.6);
            noise_b = Noise3(noise_domain.yzx * 2.3 + float3(2.1, -1.4, 4.3));
            float core_weight = pow(saturate(1.0 - radial_distance), 3.25);
            float glow_weight = pow(saturate(1.0 - radial_distance), 0.72);
            alpha_core = max(alpha_core, core_weight);
            float density_noise = lerp(1.0, 0.70 + 0.60 * noise_a, noise_strength);
            float radial_density = lerp(0.12, 1.85, core_weight);
            float sigma_t = max(density * density_noise * radial_density + absorption, 0.0);
            float step_alpha = saturate(1.0 - exp(-sigma_t * step_length));
            float3 lighting = SampleVolumeLighting(sample_pos, ray_dir, anisotropy, pixel);
            float emissive_strength = max(max(emissive.r, emissive.g), emissive.b);
            float3 hot_color = emissive_strength > 0.001 ? emissive : base_color * 1.20;
            float3 medium_color = saturate(lerp(base_color, hot_color, core_weight) *
                                           (0.72 + 0.28 * noise_b) + float3(0.03, 0.03, 0.03));
            float3 emission = hot_color * core_weight * (3.20 + step_alpha * 8.00) +
                              base_color * glow_weight * (0.08 + scattering * 0.15);
            float3 scattered = medium_color * lighting * scattering + emission;
            in_scatter += transmittance * step_alpha * scattered;
            transmittance *= exp(-sigma_t * step_length);
        }

        float opacity = saturate((1.0 - transmittance) * lerp(0.28, 1.0, alpha_core));
        float2 distort_dir = normalize(float2(noise_a - 0.5, noise_b - 0.5) + 1.0e-4);
        float distort_scale =
            (0.0012 + distortion_strength * 0.0060) *
            (0.82 + 0.18 * sin(time * 1.9 + noise_a * 5.8 + noise_b * 3.1));
        float2 distorted_uv = clamp(screen_uv + distort_dir * distort_scale, 0.001, 0.999);

        float3 background_color = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;
        float3 scene_color = g_SceneColor.Sample(g_SamplerColor, distorted_uv).rgb;
        float rim = pow(saturate(1.0 - path_length / max(volume_radius * 2.0, 1.0e-4)), 2.2);
        float shimmer = 0.82 + 0.18 * sin(time * 1.9 + noise_a * 5.8 + noise_b * 3.1);
        float3 boundary_glow =
            (emissive * 0.12 + base_color * 0.88) *
            rim * (0.08 + distortion_strength * 0.04 + noise_strength * 0.08) * shimmer;

        float alpha = saturate(opacity + rim * (0.035 + noise_strength * 0.015));
        float safe_alpha = max(alpha, 0.05);
        float3 composite = scene_color * transmittance + in_scatter + boundary_glow;
        lit = max((composite - background_color * (1.0 - alpha)) / safe_alpha, 0.0);
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
        if (standard_material && transmission > 0.001)
        {
            float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
            float ior = max(g_MaterialParams5.x, 1.0);
            float eta = 1.0 / ior;
            float3 refract_dir = refract(-v, n, eta);
            if (dot(refract_dir, refract_dir) <= 1.0e-5)
            {
                refract_dir = -v;
            }
            float thickness_scale = 0.003 + min(thickness, 4.0) * 0.004;
            float2 refract_uv =
                clamp(screen_uv +
                      (refract_dir.xy + n.xy * 0.25) *
                          thickness_scale *
                          (1.0 - roughness * 0.65),
                      0.001,
                      0.999);
            float3 scene_refract = g_SceneColor.Sample(g_SamplerColor, refract_uv).rgb;
            float3 attenuation_color =
                max(saturate(g_MaterialParams6.rgb), float3(1.0e-3, 1.0e-3, 1.0e-3));
            float attenuation_distance = max(g_MaterialParams5.z, 0.0);
            float3 transmittance = float3(1.0, 1.0, 1.0);
            if (attenuation_distance > 0.0 && thickness > 0.0)
            {
                transmittance = pow(attenuation_color, thickness / attenuation_distance);
            }
            float3 transmitted =
                scene_refract * transmittance * lerp(float3(1.0, 1.0, 1.0), base_color, 0.35);
            lit = lit * (1.0 - transmission) + transmitted * transmission;
            base_alpha = saturate(max(base_alpha * (1.0 - transmission),
                                      0.14 + transmission * 0.38));
        }
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
    float exposure = abs(g_EnvParams.w);
    if (g_EnvParams.w < 0.0)
    {
        return float4(max(lit * exposure, float3(0.0, 0.0, 0.0)), base_alpha);
    }
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

  forward_vs_.Release();
  shader_ci.Desc.Name = "Karma VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kVertexShader;
  forward_vs_ = device_with_cache_.CreateShader(shader_ci);
  if (!forward_vs_) {
  }

  forward_ps_.Release();
  shader_ci.Desc.Name = "Karma PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kPixelShader;
  forward_ps_ = device_with_cache_.CreateShader(shader_ci);
  if (!forward_ps_) {
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> forward_plus_cs;
  shader_ci.Desc.Name = "Karma Forward+ CS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kForwardPlusComputeShader;
  forward_plus_cs = device_with_cache_.CreateShader(shader_ci);
  mark_stage("main shader compile");

  Diligent::SamplerDesc sampler_color{};
  sampler_color.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_color.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  device_->CreateSampler(sampler_color, &sampler_color_);

  Diligent::SamplerDesc sampler_color_clamp = sampler_color;
  sampler_color_clamp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler_color_clamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler_color_clamp.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
  device_->CreateSampler(sampler_color_clamp, &sampler_color_clamp_);

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
  mark_stage("samplers and shadow maps");

  ensureForwardPipeline(ForwardPipelineVariant::Opaque);
  mark_stage("forward pipeline creation");

  if (!pipeline_state_) {
    logStartupDiag("diligent_device", "total", init_start, core::SteadyClock::now());
    return;
  }

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "Karma Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(DrawConstants);
  device_->CreateBuffer(cb_desc, nullptr, &constants_);

  Diligent::BufferDesc deformation_cb_desc{};
  deformation_cb_desc.Name = "Karma Deformation Constants";
  deformation_cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  deformation_cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  deformation_cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  deformation_cb_desc.Size = sizeof(DeformationConstants);
  device_->CreateBuffer(deformation_cb_desc, nullptr, &deformation_constants_);

  Diligent::BufferDesc camera_override_cb_desc{};
  camera_override_cb_desc.Name = "Karma Camera Override User Constants";
  camera_override_cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  camera_override_cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  camera_override_cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  camera_override_cb_desc.Size = sizeof(CameraOverrideUserConstants);
  device_->CreateBuffer(camera_override_cb_desc, nullptr, &camera_override_user_constants_);

  if (constants_) {
    bindForwardPipelineStaticResources(pipeline_state_.RawPtr());
    bindForwardPipelineStaticResources(transparent_pipeline_state_.RawPtr());
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

    const auto forward_plus_pso_start = core::SteadyClock::now();
    forward_plus_compute_pso_ = device_with_cache_.CreateComputePipelineState(forward_plus_pso_ci);
    recordPipelineCreation("forward",
                           "Karma Forward+ Compute Pipeline",
                           forward_plus_pso_start,
                           core::SteadyClock::now());
    if (forward_plus_compute_pso_) {
      Diligent::BufferDesc fp_cb_desc{};
      fp_cb_desc.Name = "Karma Forward+ Compute Constants";
      fp_cb_desc.Usage = Diligent::USAGE_DYNAMIC;
      fp_cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
      fp_cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
      fp_cb_desc.Size = sizeof(ForwardPlusComputeConstants);
      const auto fp_cb_start = core::SteadyClock::now();
      device_->CreateBuffer(fp_cb_desc, nullptr, &forward_plus_compute_cb_);
      recordResourceCreation("forward",
                             "forward plus constants buffer",
                             fp_cb_start,
                             core::SteadyClock::now());
      if (forward_plus_compute_cb_) {
        if (auto* var = forward_plus_compute_pso_->GetStaticVariableByName(
                Diligent::SHADER_TYPE_COMPUTE, "ForwardPlusConstants")) {
          var->Set(forward_plus_compute_cb_);
        }
      }
      const auto fp_srb_start = core::SteadyClock::now();
      forward_plus_compute_pso_->CreateShaderResourceBinding(&forward_plus_compute_srb_, true);
      recordResourceCreation("forward",
                             "forward plus compute SRB",
                             fp_srb_start,
                             core::SteadyClock::now());
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
  mark_stage("constant buffers and forward plus");

  default_base_color_ = createSolidTextureSRV(255, 255, 255, 255, true, "DefaultBaseColor",
                                              default_base_color_tex_);
  default_normal_ = createSolidTextureSRV(128, 128, 255, 255, false, "DefaultNormal",
                                          default_normal_tex_);
  default_metallic_roughness_ = createSolidTextureSRV(0, 255, 255, 255, false, "DefaultMetalRough",
                                                      default_metallic_roughness_tex_);
  default_occlusion_ = createSolidTextureSRV(255, 255, 255, 255, false, "DefaultOcclusion",
                                             default_occlusion_tex_);
  default_emissive_ = createSolidTextureSRV(255, 255, 255, 255, true, "DefaultEmissive",
                                            default_emissive_tex_);
  default_env_ = createSolidCubeTextureSRV(0, 0, 0, 255, true, "DefaultEnv",
                                           default_env_tex_);
  env_srv_ = default_env_;
  mark_stage("default textures");

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
  mark_stage("shadow pipeline and fallback depth");

  if (pipeline_state_) {
    const auto main_srb_start = core::SteadyClock::now();
    pipeline_state_->CreateShaderResourceBinding(&shader_resources_, true);
    recordResourceCreation("forward",
                           "main shader resources SRB",
                           main_srb_start,
                           core::SteadyClock::now());
    initializeDefaultMaterialBinding(pipeline_state_.RawPtr(), default_material_srb_);
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
  initializeDefaultMaterialBinding(transparent_pipeline_state_.RawPtr(),
                                   transparent_default_material_srb_);
  initializeDefaultMaterialBinding(transparent_double_sided_pipeline_state_.RawPtr(),
                                   transparent_double_sided_default_material_srb_);
  initializeDefaultMaterialBinding(additive_pipeline_state_.RawPtr(),
                                   additive_default_material_srb_);
  initializeDefaultMaterialBinding(additive_double_sided_pipeline_state_.RawPtr(),
                                   additive_double_sided_default_material_srb_);
  mark_stage("default material srbs");

  const auto line_start = core::SteadyClock::now();
  ensureLineResources();
  logStartupDiag("diligent_device", "line resources prewarm", line_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();

  if (shader_cache_enabled_ && shader_cache_flush_ && device_with_cache_.GetCache()) {
    saveRenderStateCache("device init shader cache flush");
  }
  mark_stage("shader cache flush");
  logStartupDiag("diligent_device", "total", init_start, core::SteadyClock::now());
}

}  // namespace karma::renderer_backend
