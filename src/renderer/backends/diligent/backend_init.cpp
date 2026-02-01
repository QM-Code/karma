#include "karma/renderer/backends/diligent/backend.hpp"

#include "karma/platform/window.h"

#include "backend_internal.h"

#include <spdlog/spdlog.h>

#include <Primitives/interface/BasicTypes.h>
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

#if !defined(BZ3_WINDOW_BACKEND_SDL)
  #include <GLFW/glfw3.h>
  #include <GLFW/glfw3native.h>
#endif

namespace karma::renderer_backend {

void DiligentBackend::recreateShadowMap() {
  if (!device_) {
    return;
  }
  const auto& adapter = device_->GetAdapterInfo();
  const int max_dim = static_cast<int>(adapter.Texture.MaxTexture2DDimension);
  spdlog::info("Karma: Shadow map requested size={} max_supported={}.",
               shadow_map_size_,
               max_dim);
  if (max_dim > 0 && shadow_map_size_ > max_dim) {
    spdlog::warn("Karma: Shadow map size {} exceeds device max {}; clamping.",
                 shadow_map_size_,
                 max_dim);
    shadow_map_size_ = max_dim;
  }
  shadow_map_tex_.Release();
  shadow_map_srv_.Release();
  shadow_map_dsv_.Release();

  Diligent::TextureDesc shadow_desc{};
  shadow_desc.Name = "Karma Shadow Map";
  shadow_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  shadow_desc.Width = static_cast<Diligent::Uint32>(shadow_map_size_);
  shadow_desc.Height = static_cast<Diligent::Uint32>(shadow_map_size_);
  shadow_desc.MipLevels = 1;
  shadow_desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
  spdlog::info("Karma: Shadow map format=D32_FLOAT.");
  shadow_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
  device_->CreateTexture(shadow_desc, nullptr, &shadow_map_tex_);
  if (shadow_map_tex_) {
    if (auto* dsv = shadow_map_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL)) {
      shadow_map_dsv_ = dsv;
    }
    if (auto* srv = shadow_map_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)) {
      shadow_map_srv_ = srv;
    }
  } else {
    spdlog::error("Karma: Failed to create shadow map texture ({}x{}).",
                  shadow_map_size_,
                  shadow_map_size_);
  }
  if (!shadow_map_srv_ || !shadow_map_dsv_) {
    spdlog::error("Karma: Shadow map views not created (srv={} dsv={}).",
                  shadow_map_srv_ ? 1 : 0,
                  shadow_map_dsv_ ? 1 : 0);
  }
  if (pipeline_state_ && shadow_map_srv_) {
    if (auto* var =
            pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
      var->Set(shadow_map_srv_);
    }
  }
}

void DiligentBackend::initializeDevice() {
#if defined(ENGINE_FORCE_VULKAN)
  (void)window_;
#endif
  Diligent::RefCntAutoPtr<Diligent::IEngineFactoryVk> factory;
  Diligent::EngineVkCreateInfo engine_ci{};

#if defined(PLATFORM_LINUX)
  factory = Diligent::GetEngineFactoryVk();
#else
  factory = Diligent::GetEngineFactoryVk();
#endif

  if (!factory) {
    spdlog::error("Karma: Failed to create Diligent Vulkan factory.");
    return;
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
    spdlog::error("Karma: Failed to initialize Diligent device/context.");
  }

  if (!device_) {
    return;
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
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
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
    output.Pos = mul(g_MVP, float4(input.Pos, 1.0));
    float3 world_pos = mul(g_Model, float4(input.Pos, 1.0)).xyz;
    output.WorldPos = world_pos;
    output.Normal = normalize(mul((float3x3)g_Model, input.Normal));
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
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
};

Texture2D g_BaseColorTex;
Texture2D g_NormalTex;
Texture2D g_MetallicRoughnessTex;
Texture2D g_OcclusionTex;
Texture2D g_EmissiveTex;
TextureCube g_IrradianceTex;
TextureCube g_PrefilterTex;
Texture2D g_BRDFLUT;
Texture2D<float> g_ShadowMap;
SamplerState g_SamplerColor;
SamplerState g_SamplerData;
SamplerComparisonState g_ShadowSampler;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    float2 UV : TEXCOORD0;
    float4 Tangent : TEXCOORD1;
    float3 WorldPos : TEXCOORD2;
    bool FrontFace : SV_IsFrontFace;
};

float4 main(PSInput input) : SV_TARGET
{
    const float PI = 3.14159265;
    float3 n = normalize(input.Normal);
    float3 t = normalize(input.Tangent.xyz);
    float3 b = normalize(cross(n, t) * input.Tangent.w);
    float3 normal_tex = g_NormalTex.Sample(g_SamplerData, input.UV).xyz * 2.0 - 1.0;
    normal_tex.xy *= g_PbrParams.w;
    normal_tex = normalize(normal_tex);
    n = normalize(normal_tex.x * t + normal_tex.y * b + normal_tex.z * n);
    float3 l = normalize(-g_LightDir.xyz);
    float ndotl = max(dot(n, l), 0.0);
    float4 base_tex = g_BaseColorTex.Sample(g_SamplerColor, input.UV);
    float3 emissive_tex = g_EmissiveTex.Sample(g_SamplerColor, input.UV).rgb;
    float occlusion = g_OcclusionTex.Sample(g_SamplerData, input.UV).r;
    float2 mr = g_MetallicRoughnessTex.Sample(g_SamplerData, input.UV).bg;
    float metallic = saturate(mr.x * g_PbrParams.x);
    float roughness = saturate(mr.y * g_PbrParams.y);

    float3 base_color = g_BaseColorFactor.rgb * base_tex.rgb;
    float3 emissive = g_EmissiveFactor.rgb * emissive_tex;

    float3 v = normalize(g_CameraPos.xyz - input.WorldPos);
    float3 h = normalize(v + l);
    float ndoth = max(dot(n, h), 0.0);
    float rough = max(roughness, 0.05);
    float shininess = 2.0 / (rough * rough) - 2.0;
    float spec = pow(ndoth, shininess);
    float3 spec_color = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);

    float shadow = 1.0;
    if (g_ShadowParams.x > 0.5)
    {
        float4 shadow_uv_depth = mul(g_ShadowUVProj, float4(input.WorldPos, 1.0));
        shadow_uv_depth.xyz /= max(shadow_uv_depth.w, 1e-7);
        float2 shadow_uv = shadow_uv_depth.xy;
        float shadow_depth = max(shadow_uv_depth.z, 1e-7);
        if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 &&
            shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0 &&
            shadow_depth >= 0.0 && shadow_depth <= 1.0)
        {
            if (g_ShadowParams.w < 0.0)
            {
                float size = -g_ShadowParams.w;
                float2 texel_f = shadow_uv * size;
                int2 texel = int2(clamp(texel_f, 0.0, size - 1.0));
                float depth_sample = g_ShadowMap.Load(int3(texel, 0));
                return float4(shadow_depth, depth_sample, 0.0, 1.0);
            }
            float slope = 1.0 - saturate(dot(n, l));
            float bias = g_ShadowParams.y * (1.0 + slope * 2.0);
            int radius = (int)g_ShadowParams.z;
            radius = clamp(radius, 0, 4);
            if (radius == 0)
            {
                shadow = g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                                                        shadow_uv,
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
                                                                  shadow_uv + offset,
                                                                  shadow_depth - bias);
                            count += 1;
                        }
                    }
                }
                shadow = (count > 0) ? (sum / count) : 1.0;
            }
        }
    }
    float3 lit = base_color * g_LightColor.rgb * (ndotl * shadow);
    lit += spec_color * spec * g_LightColor.rgb * shadow;
    occlusion = lerp(1.0, occlusion, g_PbrParams.z);
    lit *= occlusion;
    const bool env_debug = g_EnvParams.z > 0.5;
    if (g_EnvParams.x > 0.0 || env_debug)
    {
        float3 env_diffuse = g_IrradianceTex.Sample(g_SamplerColor, n).rgb * g_EnvParams.x;
        float3 r = reflect(-v, n);
        float mip = saturate(roughness) * g_EnvParams.y;
        float3 prefiltered = g_PrefilterTex.SampleLevel(g_SamplerColor, r, mip).rgb;
        float ndotv = max(dot(n, v), 0.0);
        float2 brdf = g_BRDFLUT.Sample(g_SamplerColor, float2(ndotv, roughness)).rg;
        float3 env_spec = prefiltered * (spec_color * brdf.x + brdf.y);
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
    lit += emissive;
    return float4(lit, g_BaseColorFactor.a * base_tex.a);
}
)";

  static constexpr const char* kShadowVertexShader = R"(
cbuffer Constants
{
    float4x4 g_MVP;
    float4x4 g_Model;
    float4x4 g_LightViewProj;
    float4x4 g_ShadowUVProj;
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Pos = mul(g_MVP, float4(input.Pos, 1.0));
    return output;
}
)";

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kVertexShader;
  device_->CreateShader(shader_ci, &vs);
  if (!vs) {
    spdlog::error("Karma: Failed to create Diligent vertex shader.");
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kPixelShader;
  device_->CreateShader(shader_ci, &ps);
  if (!ps) {
    spdlog::error("Karma: Failed to create Diligent pixel shader.");
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> shadow_vs;
  shader_ci.Desc.Name = "Karma Shadow VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kShadowVertexShader;
  device_->CreateShader(shader_ci, &shadow_vs);
  if (!shadow_vs) {
    spdlog::error("Karma: Failed to create Diligent shadow vertex shader.");
  }


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

  Diligent::LayoutElement layout_elems[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{3, 0, 2, Diligent::VT_FLOAT32, false}
  };
  graphics.InputLayout.LayoutElements = layout_elems;
  graphics.InputLayout.NumElements =
      static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_SamplerData", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_NormalTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
  };
  pso_ci.PSODesc.ResourceLayout.Variables = vars;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

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
  shadow_sampler.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS;
  shadow_sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  shadow_sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  shadow_sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
  device_->CreateSampler(shadow_sampler, &shadow_sampler_);

  recreateShadowMap();

  device_->CreateGraphicsPipelineState(pso_ci, &pipeline_state_);

  if (!pipeline_state_) {
    spdlog::error("Karma: Failed to create Diligent pipeline state.");
    return;
  }

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "Karma Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(DrawConstants);
  device_->CreateBuffer(cb_desc, nullptr, &constants_);

  if (constants_) {
    bool bound = false;
    if (auto* variable =
            pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      variable->Set(constants_);
      bound = true;
    }
    if (auto* variable =
            pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) {
      variable->Set(constants_);
      bound = true;
    }
    if (!bound) {
      spdlog::error("Karma: Failed to bind Diligent constant buffer.");
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

  if (pipeline_state_) {
    if (env_srv_) {
      if (default_material_srb_) {
        if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex")) {
          var->Set(env_srv_);
        }
        if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex")) {
          var->Set(env_srv_);
        }
      }
    } else {
      spdlog::warn("Karma: Default environment texture failed to create.");
    }
    if (default_base_color_) {
      if (default_material_srb_) {
        if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT")) {
          var->Set(default_base_color_);
        }
      }
    }
    if (shadow_map_srv_) {
      if (auto* var = pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
        var->Set(shadow_map_srv_);
      }
    }
    if (shadow_sampler_) {
      if (auto* var = pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler")) {
        var->Set(shadow_sampler_);
      }
    }
  }

  if (shadow_vs) {
    Diligent::GraphicsPipelineStateCreateInfo shadow_pso{};
    shadow_pso.PSODesc.Name = "Karma Shadow Pipeline";
    shadow_pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    shadow_pso.pVS = shadow_vs;
    shadow_pso.pPS = nullptr;

    auto& shadow_graphics = shadow_pso.GraphicsPipeline;
    shadow_graphics.NumRenderTargets = 0;
    shadow_graphics.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    shadow_graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    shadow_graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    shadow_graphics.RasterizerDesc.FrontCounterClockwise = true;
    shadow_graphics.DepthStencilDesc.DepthEnable = true;
    shadow_graphics.DepthStencilDesc.DepthWriteEnable = true;
    shadow_graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    shadow_graphics.InputLayout.LayoutElements = layout_elems;
    shadow_graphics.InputLayout.NumElements =
        static_cast<Diligent::Uint32>(sizeof(layout_elems) / sizeof(layout_elems[0]));

    Diligent::ShaderResourceVariableDesc shadow_vars[] = {
        {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
    };
    shadow_pso.PSODesc.ResourceLayout.Variables = shadow_vars;
    shadow_pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(shadow_vars) / sizeof(shadow_vars[0]));

    device_->CreateGraphicsPipelineState(shadow_pso, &shadow_pipeline_state_);
    if (shadow_pipeline_state_) {
      if (auto* variable =
              shadow_pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
        variable->Set(constants_);
      }
      shadow_pipeline_state_->CreateShaderResourceBinding(&shadow_srb_, true);
    } else {
      spdlog::error("Karma: Failed to create Diligent shadow pipeline state.");
    }
  }

  if (pipeline_state_) {
    pipeline_state_->CreateShaderResourceBinding(&shader_resources_, true);
    pipeline_state_->CreateShaderResourceBinding(&default_material_srb_, true);
    if (default_material_srb_) {
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_);
      }
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_);
      }
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex")) {
        var->Set(default_base_color_);
      }
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_NormalTex")) {
        var->Set(default_normal_);
      }
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex")) {
        var->Set(default_metallic_roughness_);
      }
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex")) {
        var->Set(default_occlusion_);
      }
      if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex")) {
        var->Set(default_emissive_);
      }
    }
  }

  ensureLineResources();
}

}  // namespace karma::renderer_backend
