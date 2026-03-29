#include "karma/renderer/backends/diligent/backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>

#include <glm/gtc/matrix_transform.hpp>

namespace karma::renderer_backend {

namespace {
struct alignas(16) EnvConstants {
  float view_proj[16];
  float params[4];
};

static constexpr const char* kEnvCubeVS = R"(
cbuffer Constants
{
    row_major float4x4 g_ViewProj;
};

struct VSInput
{
    float3 pos : ATTRIB0;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.local_pos = input.pos;
    output.pos = mul(float4(input.pos, 1.0f), g_ViewProj);
    return output;
}
)";

static constexpr const char* kEquirectToCubePS = R"(
Texture2D g_Equirect;
SamplerState g_Sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 dir = normalize(input.local_pos);
    const float PI = 3.14159265;
    float2 uv;
    uv.x = 1.0 - (atan2(dir.z, dir.x) / (2.0 * PI) + 0.5);
    uv.y = 1.0 - (asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);
    return g_Equirect.Sample(g_Sampler, uv);
}
)";

static constexpr const char* kSkyboxPS = R"(
TextureCube g_Skybox;
SamplerState g_Sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 dir = normalize(input.local_pos);
    return g_Skybox.Sample(g_Sampler, dir);
}
)";

static constexpr const char* kIrradiancePS = R"(
TextureCube g_EnvMap;
SamplerState g_Sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.local_pos);
    float3 up = abs(n.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, n));
    up = cross(n, right);

    const float PI = 3.14159265;
    float3 irradiance = float3(0.0, 0.0, 0.0);
    const int SAMPLE_COUNT = 64;
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float xi1 = (float)i / (float)SAMPLE_COUNT;
        float xi2 = frac(sin((float)(i + 1) * 12.9898) * 43758.5453);

        float phi = 2.0 * PI * xi1;
        float cos_theta = sqrt(1.0 - xi2);
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

        float3 sample_dir = sin_theta * cos(phi) * right +
                            sin_theta * sin(phi) * up +
                            cos_theta * n;

        irradiance += g_EnvMap.Sample(g_Sampler, sample_dir).rgb * cos_theta;
    }

    irradiance = PI * irradiance / SAMPLE_COUNT;
    return float4(irradiance, 1.0);
}
)";

static constexpr const char* kPrefilterPS = R"(
TextureCube g_EnvMap;
SamplerState g_Sampler;

cbuffer Constants
{
    row_major float4x4 g_ViewProj;
    float4 g_Params;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159265 * Xi.x;
    float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    float3 H;
    H.x = cos(phi) * sin_theta;
    H.y = sin(phi) * sin_theta;
    H.z = cos_theta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    float3 sample_vec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sample_vec);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.local_pos);
    float3 V = N;

    const uint SAMPLE_COUNT = 256u;
    float total_weight = 0.0;
    float3 prefiltered = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, g_Params.x);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float n_dot_l = max(dot(N, L), 0.0);
        if (n_dot_l > 0.0)
        {
            prefiltered += g_EnvMap.Sample(g_Sampler, L).rgb * n_dot_l;
            total_weight += n_dot_l;
        }
    }

    prefiltered = prefiltered / max(total_weight, 0.001);
    return float4(prefiltered, 1.0);
}
)";

static constexpr const char* kBrdfLutVS = R"(
struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vid : SV_VertexID)
{
    VSOutput output;
    float2 pos = float2((vid << 1) & 2, vid & 2);
    output.uv = pos;
    output.pos = float4(pos * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
)";

static constexpr const char* kBrdfLutPS = R"(
struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159265 * Xi.x;
    float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    float3 H;
    H.x = cos(phi) * sin_theta;
    H.y = sin(phi) * sin_theta;
    H.z = cos_theta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    float3 sample_vec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sample_vec);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;
    const uint SAMPLE_COUNT = 256u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, float3(0.0, 0.0, 1.0), roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float G = GeometrySmith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 0.001);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= SAMPLE_COUNT;
    B /= SAMPLE_COUNT;
    return float2(A, B);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 integrated = IntegrateBRDF(input.uv.x, input.uv.y);
    return float4(integrated, 0.0, 1.0);
}
)";

static const float kEnvCubeVertices[] = {
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,

     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,

    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,

    -1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f
};
}  // namespace

void DiligentBackend::ensureEnvironmentResources() {
  if (!device_ || !context_) {
    return;
  }
  if (!env_dirty_ && env_cubemap_srv_ && skybox_pso_) {
    return;
  }

  if (!env_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Env Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(EnvConstants);
    device_->CreateBuffer(cb_desc, nullptr, &env_cb_);
  }

  if (!env_cube_vb_) {
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma Env Cube VB";
    vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.Size = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices));
    Diligent::BufferData vb_data{};
    vb_data.pData = kEnvCubeVertices;
    vb_data.DataSize = vb_desc.Size;
    device_->CreateBuffer(vb_desc, &vb_data, &env_cube_vb_);
  }

  if (!env_equirect_pso_ || !skybox_pso_ || !env_irradiance_pso_ || !env_prefilter_pso_ ||
      !brdf_lut_pso_) {
    Diligent::ShaderCreateInfo shader_ci{};
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

    Diligent::RefCntAutoPtr<Diligent::IShader> vs;
    shader_ci.Desc.Name = "Karma Env VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kEnvCubeVS;
    vs = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_equirect;
    shader_ci.Desc.Name = "Karma Env Equirect PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kEquirectToCubePS;
    ps_equirect = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_skybox;
    shader_ci.Desc.Name = "Karma Skybox PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kSkyboxPS;
    ps_skybox = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_irradiance;
    shader_ci.Desc.Name = "Karma Env Irradiance PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kIrradiancePS;
    ps_irradiance = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_prefilter;
    shader_ci.Desc.Name = "Karma Env Prefilter PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kPrefilterPS;
    ps_prefilter = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> vs_brdf;
    shader_ci.Desc.Name = "Karma BRDF LUT VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kBrdfLutVS;
    vs_brdf = device_with_cache_.CreateShader(shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_brdf;
    shader_ci.Desc.Name = "Karma BRDF LUT PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kBrdfLutPS;
    ps_brdf = device_with_cache_.CreateShader(shader_ci);

    if (!vs || !ps_equirect || !ps_skybox || !ps_irradiance || !ps_prefilter || !vs_brdf ||
        !ps_brdf) {
      return;
    }

    Diligent::LayoutElement layout[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false}};

    auto create_env_pso = [&](const char* name,
                              Diligent::IShader* ps,
                              const char* tex_name,
                              Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso,
                              Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb,
                              Diligent::TEXTURE_FORMAT rtv_format,
                              bool depth_test) {
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
      graphics.RTVFormats[0] = rtv_format;
      graphics.DSVFormat = depth_test && swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                                     : Diligent::TEX_FORMAT_UNKNOWN;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.DepthStencilDesc.DepthEnable = depth_test;
      graphics.DepthStencilDesc.DepthWriteEnable = false;
      if (depth_test) {
        graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
      }
      graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;
      graphics.InputLayout.LayoutElements = layout;
      graphics.InputLayout.NumElements =
          static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));

      Diligent::ShaderResourceVariableDesc vars[] = {
          {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, tex_name, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
      pso.PSODesc.ResourceLayout.Variables = vars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

      Diligent::SamplerDesc sampler{};
      sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
      sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
      sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
      sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
      sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
      sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
      Diligent::ImmutableSamplerDesc samplers[] = {
          {Diligent::SHADER_TYPE_PIXEL, "g_Sampler", sampler}};
      pso.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
      pso.PSODesc.ResourceLayout.NumImmutableSamplers =
          static_cast<Diligent::Uint32>(sizeof(samplers) / sizeof(samplers[0]));

      out_pso = device_with_cache_.CreateGraphicsPipelineState(pso);
      if (!out_pso) {
        return;
      }
      if (env_cb_) {
        if (auto* var =
                out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
          var->Set(env_cb_);
        }
        if (auto* var =
                out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) {
          var->Set(env_cb_);
        }
      }
      out_pso->CreateShaderResourceBinding(&out_srb, true);
    };

    create_env_pso("Karma Env Equirect PSO", ps_equirect, "g_Equirect",
                   env_equirect_pso_, env_equirect_srb_, Diligent::TEX_FORMAT_RGBA16_FLOAT,
                   false);
    create_env_pso("Karma Env Irradiance PSO", ps_irradiance, "g_EnvMap",
                   env_irradiance_pso_, env_irradiance_srb_, Diligent::TEX_FORMAT_RGBA16_FLOAT,
                   false);
    create_env_pso("Karma Env Prefilter PSO", ps_prefilter, "g_EnvMap",
                   env_prefilter_pso_, env_prefilter_srb_, Diligent::TEX_FORMAT_RGBA16_FLOAT,
                   false);
    create_env_pso("Karma Skybox PSO", ps_skybox, "g_Skybox",
                   skybox_pso_, skybox_srb_, swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                                         : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB,
                   true);

    if (!brdf_lut_pso_) {
      Diligent::GraphicsPipelineStateCreateInfo pso{};
      pso.PSODesc.Name = "Karma BRDF LUT PSO";
      pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      pso.pVS = vs_brdf;
      pso.pPS = ps_brdf;

      auto& graphics = pso.GraphicsPipeline;
      graphics.NumRenderTargets = 1;
      graphics.RTVFormats[0] = Diligent::TEX_FORMAT_RG16_FLOAT;
      graphics.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.DepthStencilDesc.DepthEnable = false;
      graphics.DepthStencilDesc.DepthWriteEnable = false;
      graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;
      graphics.InputLayout.LayoutElements = nullptr;
      graphics.InputLayout.NumElements = 0;

      brdf_lut_pso_ = device_with_cache_.CreateGraphicsPipelineState(pso);
    }
  }

  if (environment_map_.empty()) {
    if (!env_cubemap_tex_) {
      Diligent::TextureDesc desc{};
      desc.Name = "Karma Env Default Cube";
      desc.Type = Diligent::RESOURCE_DIM_TEX_CUBE;
      desc.Width = 1;
      desc.Height = 1;
      desc.ArraySize = 6;
      desc.MipLevels = 1;
      desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
      desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
      unsigned char pixel[4] = {0, 0, 0, 255};
      Diligent::TextureSubResData subres{};
      subres.pData = pixel;
      subres.Stride = 4;
      Diligent::TextureData init{};
      init.pSubResources = &subres;
      init.NumSubresources = 1;
      device_->CreateTexture(desc, &init, &env_cubemap_tex_);
      if (env_cubemap_tex_) {
        env_cubemap_srv_ = env_cubemap_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      }
    }
    env_cubemap_srv_ = env_cubemap_srv_ ? env_cubemap_srv_ : default_env_;
    env_irradiance_srv_ = env_cubemap_srv_;
    env_prefilter_srv_ = env_cubemap_srv_;
    env_brdf_lut_srv_ = default_base_color_;
    env_dirty_ = false;
    return;
  }

  LoadedImageHDR hdr = loadImageFromFileHDR(environment_map_);
  if (hdr.pixels.empty()) {
    env_cubemap_srv_ = default_env_;
    env_irradiance_srv_ = default_env_;
    env_prefilter_srv_ = default_env_;
    env_brdf_lut_srv_ = default_base_color_;
    env_dirty_ = false;
    return;
  }

  {
    Diligent::TextureDesc desc{};
    desc.Name = "Karma Env Equirect";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Diligent::Uint32>(hdr.width);
    desc.Height = static_cast<Diligent::Uint32>(hdr.height);
    desc.MipLevels = 1;
    desc.Format = Diligent::TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    Diligent::TextureSubResData subres{};
    subres.pData = hdr.pixels.data();
    subres.Stride = static_cast<Diligent::Uint32>(hdr.width * 4 * sizeof(float));
    Diligent::TextureData init{};
    init.pSubResources = &subres;
    init.NumSubresources = 1;
    device_->CreateTexture(desc, &init, &env_equirect_tex_);
    if (env_equirect_tex_) {
      env_equirect_srv_ = env_equirect_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
  }

  const int cube_size = 512;
  const int irradiance_size = 32;
  const int prefilter_size = 128;
  if (!env_cubemap_tex_) {
    Diligent::TextureDesc desc{};
    desc.Name = "Karma Env Cubemap";
    desc.Type = Diligent::RESOURCE_DIM_TEX_CUBE;
    desc.Width = cube_size;
    desc.Height = cube_size;
    desc.ArraySize = 6;
    desc.MipLevels = 0;
    desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    desc.MiscFlags = Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS;
    device_->CreateTexture(desc, nullptr, &env_cubemap_tex_);
    if (env_cubemap_tex_) {
      env_cubemap_srv_ = env_cubemap_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
  }

  bool restore_main_targets = false;
  if (!env_irradiance_tex_) {
    Diligent::TextureDesc desc{};
    desc.Name = "Karma Env Irradiance";
    desc.Type = Diligent::RESOURCE_DIM_TEX_CUBE;
    desc.Width = irradiance_size;
    desc.Height = irradiance_size;
    desc.ArraySize = 6;
    desc.MipLevels = 1;
    desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(desc, nullptr, &env_irradiance_tex_);
    if (env_irradiance_tex_) {
      env_irradiance_srv_ =
          env_irradiance_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
  }

  if (!env_prefilter_tex_) {
    Diligent::TextureDesc desc{};
    desc.Name = "Karma Env Prefilter";
    desc.Type = Diligent::RESOURCE_DIM_TEX_CUBE;
    desc.Width = prefilter_size;
    desc.Height = prefilter_size;
    desc.ArraySize = 6;
    desc.MipLevels = 0;
    desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    desc.MiscFlags = Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS;
    device_->CreateTexture(desc, nullptr, &env_prefilter_tex_);
    if (env_prefilter_tex_) {
      env_prefilter_srv_ =
          env_prefilter_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
  }

  if (env_cubemap_tex_ && env_equirect_srv_ && env_equirect_pso_) {
    const glm::mat4 capture_proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 capture_views[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};

    Diligent::IBuffer* vbs[] = {env_cube_vb_};
    Diligent::Uint64 offsets[] = {0};
    context_->SetVertexBuffers(0, 1, vbs, offsets,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                               Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    for (int face = 0; face < 6; ++face) {
      Diligent::TextureViewDesc rtv_desc{};
      rtv_desc.ViewType = Diligent::TEXTURE_VIEW_RENDER_TARGET;
      rtv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
      rtv_desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
      rtv_desc.MostDetailedMip = 0;
      rtv_desc.NumMipLevels = 1;
      rtv_desc.FirstArraySlice = face;
      rtv_desc.NumArraySlices = 1;
      Diligent::RefCntAutoPtr<Diligent::ITextureView> rtv;
      env_cubemap_tex_->CreateView(rtv_desc, &rtv);
      if (!rtv) {
        continue;
      }
      context_->SetRenderTargets(1, &rtv, nullptr,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      restore_main_targets = true;
      Diligent::Viewport vp{};
      vp.TopLeftX = 0.0f;
      vp.TopLeftY = 0.0f;
      vp.Width = static_cast<float>(cube_size);
      vp.Height = static_cast<float>(cube_size);
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      context_->SetViewports(1, &vp, cube_size, cube_size);

      EnvConstants constants{};
      copyMat4(constants.view_proj, capture_proj * capture_views[face]);
      {
        Diligent::MapHelper<EnvConstants> cb_map(context_, env_cb_, Diligent::MAP_WRITE,
                                                 Diligent::MAP_FLAG_DISCARD);
        *cb_map = constants;
      }

      context_->SetPipelineState(env_equirect_pso_);
      if (env_equirect_srb_) {
        if (auto* var = env_equirect_srb_->GetVariableByName(
                Diligent::SHADER_TYPE_PIXEL, "g_Equirect")) {
          var->Set(env_equirect_srv_);
        }
        context_->CommitShaderResources(env_equirect_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      }

      Diligent::DrawAttribs draw{};
      draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
    }

    if (env_cubemap_srv_) {
      context_->GenerateMips(env_cubemap_srv_);
    }
  }

  if (env_cubemap_tex_ && env_irradiance_tex_ && env_irradiance_pso_ && env_irradiance_srb_) {
    const glm::mat4 capture_proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 capture_views[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};

    Diligent::IBuffer* vbs[] = {env_cube_vb_};
    Diligent::Uint64 offsets[] = {0};
    context_->SetVertexBuffers(0, 1, vbs, offsets,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                               Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    for (int face = 0; face < 6; ++face) {
      Diligent::TextureViewDesc rtv_desc{};
      rtv_desc.ViewType = Diligent::TEXTURE_VIEW_RENDER_TARGET;
      rtv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
      rtv_desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
      rtv_desc.MostDetailedMip = 0;
      rtv_desc.NumMipLevels = 1;
      rtv_desc.FirstArraySlice = face;
      rtv_desc.NumArraySlices = 1;
      Diligent::RefCntAutoPtr<Diligent::ITextureView> rtv;
      env_irradiance_tex_->CreateView(rtv_desc, &rtv);
      if (!rtv) {
        continue;
      }
      context_->SetRenderTargets(1, &rtv, nullptr,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      restore_main_targets = true;

      Diligent::Viewport vp{};
      vp.TopLeftX = 0.0f;
      vp.TopLeftY = 0.0f;
      vp.Width = static_cast<float>(irradiance_size);
      vp.Height = static_cast<float>(irradiance_size);
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      context_->SetViewports(1, &vp, irradiance_size, irradiance_size);

      EnvConstants constants{};
      copyMat4(constants.view_proj, capture_proj * capture_views[face]);
      constants.params[0] = 0.0f;
      {
        Diligent::MapHelper<EnvConstants> cb_map(context_, env_cb_, Diligent::MAP_WRITE,
                                                 Diligent::MAP_FLAG_DISCARD);
        *cb_map = constants;
      }

      context_->SetPipelineState(env_irradiance_pso_);
      if (auto* var =
              env_irradiance_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EnvMap")) {
        var->Set(env_cubemap_srv_);
      }
      context_->CommitShaderResources(env_irradiance_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      Diligent::DrawAttribs draw{};
      draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
    }
  }

  if (env_cubemap_tex_ && env_prefilter_tex_ && env_prefilter_pso_ && env_prefilter_srb_) {
    const glm::mat4 capture_proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 capture_views[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};

    Diligent::IBuffer* vbs[] = {env_cube_vb_};
    Diligent::Uint64 offsets[] = {0};
    context_->SetVertexBuffers(0, 1, vbs, offsets,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                               Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    const auto& prefilter_desc = env_prefilter_tex_->GetDesc();
    const int mip_levels = static_cast<int>(prefilter_desc.MipLevels);
    for (int mip = 0; mip < mip_levels; ++mip) {
      const int mip_size = std::max(1, prefilter_size >> mip);
      const float roughness = mip_levels > 1 ? static_cast<float>(mip) / (mip_levels - 1) : 0.0f;
      for (int face = 0; face < 6; ++face) {
        Diligent::TextureViewDesc rtv_desc{};
        rtv_desc.ViewType = Diligent::TEXTURE_VIEW_RENDER_TARGET;
        rtv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
        rtv_desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
        rtv_desc.MostDetailedMip = static_cast<Diligent::Uint32>(mip);
        rtv_desc.NumMipLevels = 1;
        rtv_desc.FirstArraySlice = face;
        rtv_desc.NumArraySlices = 1;
        Diligent::RefCntAutoPtr<Diligent::ITextureView> rtv;
        env_prefilter_tex_->CreateView(rtv_desc, &rtv);
        if (!rtv) {
          continue;
        }
        context_->SetRenderTargets(1, &rtv, nullptr,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        restore_main_targets = true;

        Diligent::Viewport vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(mip_size);
        vp.Height = static_cast<float>(mip_size);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->SetViewports(1, &vp, mip_size, mip_size);

        EnvConstants constants{};
        copyMat4(constants.view_proj, capture_proj * capture_views[face]);
        constants.params[0] = roughness;
        {
          Diligent::MapHelper<EnvConstants> cb_map(context_, env_cb_, Diligent::MAP_WRITE,
                                                   Diligent::MAP_FLAG_DISCARD);
          *cb_map = constants;
        }

        context_->SetPipelineState(env_prefilter_pso_);
        if (auto* var =
                env_prefilter_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EnvMap")) {
          var->Set(env_cubemap_srv_);
        }
        context_->CommitShaderResources(env_prefilter_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawAttribs draw{};
        draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
        draw.Flags = Diligent::DRAW_FLAG_NONE;
        context_->Draw(draw);
      }
    }
  }

  if (!env_brdf_lut_tex_) {
    const int lut_size = 256;
    Diligent::TextureDesc desc{};
    desc.Name = "Karma BRDF LUT";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = lut_size;
    desc.Height = lut_size;
    desc.MipLevels = 1;
    desc.Format = Diligent::TEX_FORMAT_RG16_FLOAT;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(desc, nullptr, &env_brdf_lut_tex_);
    if (env_brdf_lut_tex_) {
      env_brdf_lut_srv_ = env_brdf_lut_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
  }

  if (env_brdf_lut_tex_ && env_brdf_lut_srv_ && brdf_lut_pso_) {
    auto* rtv = env_brdf_lut_tex_->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    if (rtv) {
      context_->SetRenderTargets(1, &rtv, nullptr,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      restore_main_targets = true;
      Diligent::Viewport vp{};
      vp.TopLeftX = 0.0f;
      vp.TopLeftY = 0.0f;
      vp.Width = static_cast<float>(256);
      vp.Height = static_cast<float>(256);
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      context_->SetViewports(1, &vp, 256, 256);
      context_->SetPipelineState(brdf_lut_pso_);
      Diligent::DrawAttribs draw{};
      draw.NumVertices = 3;
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
    }
  }

  if (restore_main_targets && context_ && swap_chain_) {
    auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
    auto* dsv = swap_chain_->GetDepthBufferDSV();
    context_->SetRenderTargets(1, &rtv, dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(current_width_);
    vp.Height = static_cast<float>(current_height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->SetViewports(1, &vp,
                           static_cast<Diligent::Uint32>(current_width_),
                           static_cast<Diligent::Uint32>(current_height_));
  }

  env_dirty_ = false;
}

void DiligentBackend::renderSkybox(const glm::mat4& projection, const glm::mat4& view) {
  if (!draw_skybox_ || !context_ || !skybox_pso_ || !skybox_srb_ || !env_cubemap_srv_ || !env_cb_) {
    return;
  }
  glm::mat4 view_no_translation = view;
  view_no_translation[3][0] = 0.0f;
  view_no_translation[3][1] = 0.0f;
  view_no_translation[3][2] = 0.0f;

  EnvConstants constants{};
  copyMat4(constants.view_proj, projection * view_no_translation);
  {
    Diligent::MapHelper<EnvConstants> cb_map(context_, env_cb_, Diligent::MAP_WRITE,
                                             Diligent::MAP_FLAG_DISCARD);
    *cb_map = constants;
  }

  context_->SetPipelineState(skybox_pso_);
  if (!skybox_texture_var_ && skybox_srb_) {
    skybox_texture_var_ = skybox_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Skybox");
  }
  if (skybox_texture_var_) {
    skybox_texture_var_->Set(env_cubemap_srv_);
  }
  context_->CommitShaderResources(skybox_srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::IBuffer* vbs[] = {env_cube_vb_};
  Diligent::Uint64 offsets[] = {0};
  context_->SetVertexBuffers(0, 1, vbs, offsets,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

  Diligent::DrawAttribs draw{};
  draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
  draw.Flags = Diligent::DRAW_FLAG_NONE;
  context_->Draw(draw);
}

}  // namespace karma::renderer_backend
