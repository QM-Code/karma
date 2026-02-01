#include "karma/renderer/backends/diligent/backend.hpp"

#include "backend_internal.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace karma::renderer_backend {

namespace {
struct alignas(16) LineConstants {
  float view_proj[16];
};

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

glm::mat4 buildLightView(const renderer::DirectionalLightData& light) {
  glm::vec3 dir = light.direction;
  if (glm::length(dir) < 1e-4f) {
    dir = glm::vec3(0.3f, -1.0f, 0.2f);
  }
  const glm::vec3 z = glm::normalize(dir);
  glm::vec3 x;
  const float min_cmp = std::min({std::abs(z.x), std::abs(z.y), std::abs(z.z)});
  if (min_cmp == std::abs(z.x)) {
    x = glm::vec3(1.0f, 0.0f, 0.0f);
  } else if (min_cmp == std::abs(z.y)) {
    x = glm::vec3(0.0f, 1.0f, 0.0f);
  } else {
    x = glm::vec3(0.0f, 0.0f, 1.0f);
  }
  glm::vec3 y = glm::normalize(glm::cross(z, x));
  x = glm::normalize(glm::cross(y, z));

  glm::mat4 view(1.0f);
  view[0][0] = x.x;
  view[1][0] = x.y;
  view[2][0] = x.z;
  view[0][1] = y.x;
  view[1][1] = y.y;
  view[2][1] = y.z;
  view[0][2] = z.x;
  view[1][2] = z.y;
  view[2][2] = z.z;
  return view;
}

float maxScaleComponent(const glm::mat4& m) {
  const glm::vec3 x{m[0][0], m[0][1], m[0][2]};
  const glm::vec3 y{m[1][0], m[1][1], m[1][2]};
  const glm::vec3 z{m[2][0], m[2][1], m[2][2]};
  return std::max({glm::length(x), glm::length(y), glm::length(z)});
}

}  // namespace

void DiligentBackend::beginFrame(const renderer::FrameInfo& frame) {
  if (isValidSize(frame.width, frame.height) &&
      (frame.width != current_width_ || frame.height != current_height_)) {
    resize(frame.width, frame.height);
  }
}

void DiligentBackend::endFrame() {
  if (swap_chain_) {
    swap_chain_->Present();
  }
  if (!line_vertices_depth_.empty()) {
    line_vertices_depth_.clear();
  }
  if (!line_vertices_no_depth_.empty()) {
    line_vertices_no_depth_.clear();
  }
}

void DiligentBackend::resize(int width, int height) {
  if (!isValidSize(width, height)) {
    return;
  }

  current_width_ = width;
  current_height_ = height;
  if (swap_chain_) {
    swap_chain_->Resize(static_cast<Diligent::Uint32>(width),
                        static_cast<Diligent::Uint32>(height));
  }
}

void DiligentBackend::submit(const renderer::DrawItem& item) {
  if (item.instance == renderer::kInvalidInstance) {
    return;
  }

  if (meshes_.find(item.mesh) == meshes_.end()) {
    spdlog::warn("Karma: Diligent submit missing mesh id={}", item.mesh);
    return;
  }

  auto& record = instances_[item.instance];
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.transform = item.transform;
  record.visible = item.visible;
  record.shadow_visible = item.shadow_visible;
}

void DiligentBackend::drawLine(const math::Vec3& start, const math::Vec3& end,
                               const math::Color& color, bool depth_test, float thickness) {
  if (!warned_line_thickness_ && thickness != 1.0f) {
    spdlog::warn("Karma: Line thickness {} requested; only 1.0 is supported right now.", thickness);
    warned_line_thickness_ = true;
  }
  LineVertex a{};
  a.position[0] = start.x;
  a.position[1] = start.y;
  a.position[2] = start.z;
  a.position[3] = 1.0f;
  a.color[0] = color.r;
  a.color[1] = color.g;
  a.color[2] = color.b;
  a.color[3] = color.a;

  LineVertex b{};
  b.position[0] = end.x;
  b.position[1] = end.y;
  b.position[2] = end.z;
  b.position[3] = 1.0f;
  b.color[0] = color.r;
  b.color[1] = color.g;
  b.color[2] = color.b;
  b.color[3] = color.a;

  auto& bucket = depth_test ? line_vertices_depth_ : line_vertices_no_depth_;
  bucket.push_back(a);
  bucket.push_back(b);
}

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
  device_->CreateShader(shader_ci, &vs);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Line PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kLinePS;
  device_->CreateShader(shader_ci, &ps);

  if (!vs || !ps) {
    spdlog::error("Karma: Failed to create line shaders.");
    return;
  }

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(LineVertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(LineVertex))},
      Diligent::LayoutElement{1, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(LineVertex, color)),
                              static_cast<Diligent::Uint32>(sizeof(LineVertex))}
  };

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
  };

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

    device_->CreateGraphicsPipelineState(pso, &out_pso);
    if (!out_pso) {
      spdlog::error("Karma: Failed to create {} pipeline state.", name);
      return false;
    }
    if (auto* var =
            out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      var->Set(line_cb_);
    }
    out_pso->CreateShaderResourceBinding(&out_srb, true);
    return true;
  };

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "Karma Line Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(LineConstants);
  device_->CreateBuffer(cb_desc, nullptr, &line_cb_);

  if (!line_cb_) {
    spdlog::error("Karma: Failed to create line constants buffer.");
    return;
  }

  create_pipeline("Karma Line Pipeline (Depth)", true, line_pipeline_state_depth_, line_srb_depth_);
  create_pipeline("Karma Line Pipeline (NoDepth)", false, line_pipeline_state_no_depth_, line_srb_no_depth_);

  if (!line_vb_) {
    line_vb_size_ = 1024;
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma Line VB";
    vb_desc.Usage = Diligent::USAGE_DYNAMIC;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    vb_desc.Size = static_cast<Diligent::Uint32>(line_vb_size_ * sizeof(LineVertex));
    device_->CreateBuffer(vb_desc, nullptr, &line_vb_);
    if (!line_vb_) {
      spdlog::error("Karma: Failed to create line vertex buffer.");
      line_vb_size_ = 0;
    }
  }
}

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
    if (!env_cb_) {
      spdlog::warn("Karma: Failed to create env constant buffer.");
    }
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
    if (!env_cube_vb_) {
      spdlog::warn("Karma: Failed to create env cube vertex buffer.");
    }
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
    device_->CreateShader(shader_ci, &vs);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_equirect;
    shader_ci.Desc.Name = "Karma Env Equirect PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kEquirectToCubePS;
    device_->CreateShader(shader_ci, &ps_equirect);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_skybox;
    shader_ci.Desc.Name = "Karma Skybox PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kSkyboxPS;
    device_->CreateShader(shader_ci, &ps_skybox);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_irradiance;
    shader_ci.Desc.Name = "Karma Env Irradiance PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kIrradiancePS;
    device_->CreateShader(shader_ci, &ps_irradiance);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_prefilter;
    shader_ci.Desc.Name = "Karma Env Prefilter PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kPrefilterPS;
    device_->CreateShader(shader_ci, &ps_prefilter);

    Diligent::RefCntAutoPtr<Diligent::IShader> vs_brdf;
    shader_ci.Desc.Name = "Karma BRDF LUT VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kBrdfLutVS;
    device_->CreateShader(shader_ci, &vs_brdf);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps_brdf;
    shader_ci.Desc.Name = "Karma BRDF LUT PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "main";
    shader_ci.Source = kBrdfLutPS;
    device_->CreateShader(shader_ci, &ps_brdf);

    if (!vs || !ps_equirect || !ps_skybox || !ps_irradiance || !ps_prefilter || !vs_brdf ||
        !ps_brdf) {
      spdlog::warn("Karma: Failed to create environment shaders.");
      return;
    }

    Diligent::LayoutElement layout[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false}
    };

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
      graphics.DSVFormat = depth_test && swap_chain_
                               ? swap_chain_->GetDesc().DepthBufferFormat
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
      graphics.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));

      Diligent::ShaderResourceVariableDesc vars[] = {
          {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, tex_name, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
      };
      pso.PSODesc.ResourceLayout.Variables = vars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(std::size(vars));

      Diligent::SamplerDesc sampler{};
      sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
      sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
      sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
      sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
      sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
      sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
      Diligent::ImmutableSamplerDesc samplers[] = {
          {Diligent::SHADER_TYPE_PIXEL, "g_Sampler", sampler}
      };
      pso.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
      pso.PSODesc.ResourceLayout.NumImmutableSamplers =
          static_cast<Diligent::Uint32>(std::size(samplers));

      device_->CreateGraphicsPipelineState(pso, &out_pso);
      if (!out_pso) {
        return;
      }
      if (env_cb_) {
        if (auto* var = out_pso->GetStaticVariableByName(
                Diligent::SHADER_TYPE_VERTEX, "Constants")) {
          var->Set(env_cb_);
        }
        if (auto* var = out_pso->GetStaticVariableByName(
                Diligent::SHADER_TYPE_PIXEL, "Constants")) {
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

      device_->CreateGraphicsPipelineState(pso, &brdf_lut_pso_);
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
        spdlog::info("Karma: Created default env cubemap {}x{}.", desc.Width, desc.Height);
      } else {
        spdlog::warn("Karma: Failed to create default env cubemap.");
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
    spdlog::warn("Karma: Failed to load HDR env map '{}'", environment_map_.string());
    env_cubemap_srv_ = default_env_;
    env_irradiance_srv_ = default_env_;
    env_prefilter_srv_ = default_env_;
    env_brdf_lut_srv_ = default_base_color_;
    env_dirty_ = false;
    return;
  }
  spdlog::info("Karma: Loaded HDR env map '{}' ({}x{}).",
               environment_map_.string(),
               hdr.width,
               hdr.height);

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
      spdlog::info("Karma: Created env equirect texture {}x{}.", desc.Width, desc.Height);
    } else {
      spdlog::warn("Karma: Failed to create env equirect texture.");
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
      spdlog::info("Karma: Created env cubemap {}x{}.", desc.Width, desc.Height);
    } else {
      spdlog::warn("Karma: Failed to create env cubemap.");
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
      spdlog::info("Karma: Created env irradiance cubemap {}x{}.", desc.Width, desc.Height);
    } else {
      spdlog::warn("Karma: Failed to create env irradiance cubemap.");
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
      spdlog::info("Karma: Created env prefilter cubemap {}x{}.", desc.Width, desc.Height);
    } else {
      spdlog::warn("Karma: Failed to create env prefilter cubemap.");
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
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
    };

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
      const glm::mat4 view_proj = capture_proj * capture_views[face];
      copyMat4(constants.view_proj, view_proj);
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
      draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
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
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
    };

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
      const glm::mat4 view_proj = capture_proj * capture_views[face];
      copyMat4(constants.view_proj, view_proj);
      constants.params[0] = 0.0f;
      {
        Diligent::MapHelper<EnvConstants> cb_map(context_, env_cb_, Diligent::MAP_WRITE,
                                                 Diligent::MAP_FLAG_DISCARD);
        *cb_map = constants;
      }

      context_->SetPipelineState(env_irradiance_pso_);
      if (auto* var = env_irradiance_srb_->GetVariableByName(
              Diligent::SHADER_TYPE_PIXEL, "g_EnvMap")) {
        var->Set(env_cubemap_srv_);
      }
      context_->CommitShaderResources(env_irradiance_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      Diligent::DrawAttribs draw{};
      draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
      draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
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
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
    };

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
        const glm::mat4 view_proj = capture_proj * capture_views[face];
        copyMat4(constants.view_proj, view_proj);
        constants.params[0] = roughness;
        {
          Diligent::MapHelper<EnvConstants> cb_map(context_, env_cb_, Diligent::MAP_WRITE,
                                                   Diligent::MAP_FLAG_DISCARD);
          *cb_map = constants;
        }

        context_->SetPipelineState(env_prefilter_pso_);
        if (auto* var = env_prefilter_srb_->GetVariableByName(
                Diligent::SHADER_TYPE_PIXEL, "g_EnvMap")) {
          var->Set(env_cubemap_srv_);
        }
        context_->CommitShaderResources(env_prefilter_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawAttribs draw{};
        draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
        draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
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
      spdlog::info("Karma: Created env BRDF LUT {}x{}.", desc.Width, desc.Height);
    } else {
      spdlog::warn("Karma: Failed to create env BRDF LUT.");
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
      draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
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
  if (auto* var = skybox_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Skybox")) {
    var->Set(env_cubemap_srv_);
  }
  context_->CommitShaderResources(skybox_srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::IBuffer* vbs[] = {env_cube_vb_};
  Diligent::Uint64 offsets[] = {0};
  context_->SetVertexBuffers(0, 1, vbs, offsets,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

  Diligent::DrawAttribs draw{};
  draw.NumVertices = static_cast<Diligent::Uint32>(sizeof(kEnvCubeVertices) / (sizeof(float) * 3));
  draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
  context_->Draw(draw);
}

void DiligentBackend::renderLayer(renderer::LayerId layer, renderer::RenderTargetId /*target*/) {
  if (!context_ || !swap_chain_) {
    return;
  }

  if (!camera_active_) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    clearFrame(black, true);
    return;
  }

  clearFrame(clear_color_, true);

  if (!pipeline_state_ || !shader_resources_ || !constants_) {
    if (!warned_no_draws_) {
      spdlog::warn("Karma: Diligent backend missing pipeline/resources; skipping draw.");
      warned_no_draws_ = true;
    }
    return;
  }

  const float aspect = (current_height_ > 0)
                           ? static_cast<float>(current_width_) / static_cast<float>(current_height_)
                           : camera_.aspect;
  glm::mat4 projection(1.0f);
  if (camera_.perspective) {
    projection = glm::perspective(glm::radians(camera_.fov_y_degrees),
                                  aspect,
                                  camera_.near_clip,
                                  camera_.far_clip);
  } else {
    projection = glm::ortho(camera_.ortho_left,
                            camera_.ortho_right,
                            camera_.ortho_bottom,
                            camera_.ortho_top,
                            camera_.near_clip,
                            camera_.far_clip);
  }
  glm::mat4 depth_fix(1.0f);
  depth_fix[2][2] = 0.5f;
  depth_fix[3][2] = 0.5f;
  const glm::mat3 cam_basis = glm::mat3_cast(camera_.rotation);
  const glm::vec3 forward = cam_basis * glm::vec3(0.0f, 0.0f, -1.0f);
  const glm::vec3 up = cam_basis * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::mat4 view = glm::lookAt(camera_.position, camera_.position + forward, up);

  ensureEnvironmentResources();
  float env_max_mip = 0.0f;
  if (env_prefilter_tex_) {
    const auto& desc = env_prefilter_tex_->GetDesc();
    if (desc.MipLevels > 0) {
      env_max_mip = static_cast<float>(desc.MipLevels - 1);
    }
  }

  if (env_debug_mode_ > 0 && !warned_env_debug_) {
    spdlog::warn("Karma: ENV DEBUG mode={} (1=irradiance,2=prefilter,3=brdf,4=params,5=irradiance_up,6=prefilter_up,7=base_color,8=uv,9=normal).",
                 env_debug_mode_);
    warned_env_debug_ = true;
    if (env_irradiance_srv_) {
      const auto& desc = env_irradiance_srv_->GetTexture()->GetDesc();
      spdlog::info("Karma: Env irradiance desc {}x{} mips={} fmt={}.",
                   desc.Width, desc.Height, desc.MipLevels, static_cast<int>(desc.Format));
    } else {
      spdlog::warn("Karma: Env irradiance SRV is null.");
    }
    if (env_prefilter_srv_) {
      const auto& desc = env_prefilter_srv_->GetTexture()->GetDesc();
      spdlog::info("Karma: Env prefilter desc {}x{} mips={} fmt={}.",
                   desc.Width, desc.Height, desc.MipLevels, static_cast<int>(desc.Format));
    } else {
      spdlog::warn("Karma: Env prefilter SRV is null.");
    }
    if (env_cubemap_srv_) {
      const auto& desc = env_cubemap_srv_->GetTexture()->GetDesc();
      spdlog::info("Karma: Env cubemap desc {}x{} mips={} fmt={}.",
                   desc.Width, desc.Height, desc.MipLevels, static_cast<int>(desc.Format));
    } else {
      spdlog::warn("Karma: Env cubemap SRV is null.");
    }
  }

  renderSkybox(projection, view);

  const glm::mat4 light_view = buildLightView(directional_light_);
  glm::vec3 light_min{std::numeric_limits<float>::max()};
  glm::vec3 light_max{std::numeric_limits<float>::lowest()};
  bool has_bounds = false;
  if (directional_light_.shadow_extent > 0.0f) {
    const glm::vec3 center_ls =
        glm::vec3(light_view * glm::vec4(directional_light_.position, 1.0f));
    const glm::vec3 extent{directional_light_.shadow_extent};
    light_min = center_ls - extent;
    light_max = center_ls + extent;
    has_bounds = true;
  }
  if (directional_light_.shadow_extent <= 0.0f) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible) {
        continue;
      }
      auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      const glm::vec3 world_center =
          glm::vec3(instance.transform * glm::vec4(mesh.bounds_center, 1.0f));
      const float radius = mesh.bounds_radius * maxScaleComponent(instance.transform);
      const glm::vec3 center_ls = glm::vec3(light_view * glm::vec4(world_center, 1.0f));
      const glm::vec3 extents{radius};
      light_min = glm::min(light_min, center_ls - extents);
      light_max = glm::max(light_max, center_ls + extents);
      has_bounds = true;
    }
  }
  if (!has_bounds) {
    light_min = glm::vec3(-50.0f, -50.0f, -50.0f);
    light_max = glm::vec3(50.0f, 50.0f, 50.0f);
  }

  const glm::vec3 extent = light_max - light_min;
  const bool is_gl = device_->GetDeviceInfo().IsGLDevice();
  const float scale_x = (extent.x > 0.0f) ? (2.0f / extent.x) : 1.0f;
  const float scale_y = (extent.y > 0.0f) ? (2.0f / extent.y) : 1.0f;
  const float scale_z = (extent.z > 0.0f) ? ((is_gl ? 2.0f : 1.0f) / extent.z) : 1.0f;
  const float bias_x = -light_min.x * scale_x - 1.0f;
  const float bias_y = -light_min.y * scale_y - 1.0f;
  const float bias_z = -light_min.z * scale_z + (is_gl ? -1.0f : 0.0f);

  const glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), glm::vec3(scale_x, scale_y, scale_z));
  const glm::mat4 bias_mat = glm::translate(glm::mat4(1.0f), glm::vec3(bias_x, bias_y, bias_z));
  const glm::mat4 shadow_proj = bias_mat * scale_mat;
  const glm::mat4 light_view_proj = shadow_proj * light_view;

  const auto& ndc = device_->GetDeviceInfo().GetNDCAttribs();
  const glm::mat4 uv_scale = glm::scale(glm::mat4(1.0f),
                                        glm::vec3(0.5f, ndc.YtoVScale, ndc.ZtoDepthScale));
  const glm::mat4 uv_bias = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(0.5f, 0.5f, ndc.GetZtoDepthBias()));
  const glm::mat4 shadow_uv_proj = uv_bias * uv_scale * light_view_proj;

  if (shadow_pipeline_state_ && shadow_map_dsv_) {
    Diligent::Uint32 shadow_draws = 0;
    Diligent::Viewport shadow_viewport{};
    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(shadow_map_size_);
    shadow_viewport.Height = static_cast<float>(shadow_map_size_);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;
    context_->SetViewports(1, &shadow_viewport,
                           static_cast<Diligent::Uint32>(shadow_map_size_),
                           static_cast<Diligent::Uint32>(shadow_map_size_));
    context_->SetRenderTargets(0, nullptr, shadow_map_dsv_,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->ClearDepthStencil(shadow_map_dsv_,
                                Diligent::CLEAR_DEPTH_FLAG,
                                1.0f,
                                0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->SetPipelineState(shadow_pipeline_state_);
    if (shadow_srb_) {
      context_->CommitShaderResources(shadow_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible) {
        continue;
      }
      auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }

      const glm::mat4 shadow_mvp = light_view_proj * instance.transform;
      DrawConstants constants{};
      copyMat4(constants.mvp, shadow_mvp);
      copyMat4(constants.model, instance.transform);
      copyMat4(constants.light_view_proj, light_view_proj);
      copyMat4(constants.shadow_uv_proj, shadow_uv_proj);
      constants.shadow_params[0] = 0.0f;
      float fixed_bias = shadow_bias_;
      if (shadow_map_size_ >= 2048) {
        fixed_bias = 0.0025f;
      } else if (shadow_map_size_ >= 1024) {
        fixed_bias = 0.005f;
      } else {
        fixed_bias = 0.0075f;
      }
      constants.shadow_params[1] = fixed_bias;
      constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
      constants.shadow_params[3] = shadow_debug_ ? -static_cast<float>(shadow_map_size_)
                                                 : (shadow_map_size_ > 0
                                                        ? 1.0f / static_cast<float>(shadow_map_size_)
                                                        : 0.0f);

      {
        Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                  Diligent::MAP_FLAG_DISCARD);
        *mapped = constants;
      }

      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer};
      Diligent::Uint64 offsets[] = {0};
      context_->SetVertexBuffers(0,
                                 1,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      if (mesh.index_buffer && mesh.index_count > 0) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      }

      auto draw_shadow = [&](Diligent::Uint32 index_offset, Diligent::Uint32 index_count) {
        if (mesh.index_buffer && index_count > 0) {
          Diligent::DrawIndexedAttribs indexed{};
          indexed.IndexType = Diligent::VT_UINT32;
          indexed.NumIndices = index_count;
          indexed.FirstIndexLocation = index_offset;
          indexed.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
          context_->DrawIndexed(indexed);
        } else {
          Diligent::DrawAttribs draw_attrs{};
          draw_attrs.NumVertices = mesh.vertex_count;
          draw_attrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
          context_->Draw(draw_attrs);
        }
        shadow_draws += 1;
      };

      if (!mesh.submeshes.empty()) {
        for (const auto& submesh : mesh.submeshes) {
          draw_shadow(submesh.index_offset, submesh.index_count);
        }
      } else {
        draw_shadow(0, mesh.index_count);
      }
    }
    if (shadow_map_tex_) {
      Diligent::StateTransitionDesc barrier{};
      barrier.pResource = shadow_map_tex_;
      barrier.OldState = Diligent::RESOURCE_STATE_DEPTH_WRITE;
      barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
      barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
      context_->TransitionResourceStates(1, &barrier);
    }
  }

  auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
  auto* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(current_width_);
  viewport.Height = static_cast<float>(current_height_);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(current_width_),
                         static_cast<Diligent::Uint32>(current_height_));

  context_->SetPipelineState(pipeline_state_);

  static bool logged_frame = false;
  if (!logged_frame) {
    spdlog::info("Karma: Diligent render layer {} viewport={}x{} aspect={}",
                 layer,
                 current_width_,
                 current_height_,
                 aspect);
    spdlog::info("Karma: Camera pos=({}, {}, {}) fov={} near={} far={}",
                 camera_.position.x,
                 camera_.position.y,
                 camera_.position.z,
                 camera_.fov_y_degrees,
                 camera_.near_clip,
                 camera_.far_clip);
    logged_frame = true;
  }

  Diligent::Uint32 draw_count = 0;
  Diligent::Uint32 skipped_hidden = 0;
  Diligent::Uint32 skipped_missing_vb = 0;
  Diligent::Uint32 skipped_missing_mesh = 0;
  Diligent::Uint32 skipped_layer = 0;
  for (const auto& entry : instances_) {
    const auto& instance = entry.second;
    if (instance.layer != layer) {
      skipped_layer += 1;
      continue;
    }
    if (!instance.visible) {
      skipped_hidden += 1;
      continue;
    }
    auto mesh_it = meshes_.find(instance.mesh);
    if (mesh_it == meshes_.end()) {
      skipped_missing_mesh += 1;
      continue;
    }

    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      skipped_missing_vb += 1;
      continue;
    }

    const glm::mat4 mvp = depth_fix * projection * view * instance.transform;
    DrawConstants constants{};
    copyMat4(constants.mvp, mvp);
    copyMat4(constants.model, instance.transform);
    copyMat4(constants.light_view_proj, light_view_proj);
    copyMat4(constants.shadow_uv_proj, shadow_uv_proj);
    const bool shadow_ready = shadow_pipeline_state_ && shadow_map_srv_ && shadow_map_dsv_ &&
                              shadow_sampler_;
    constants.shadow_params[0] = shadow_ready ? 1.0f : 0.0f;
    if (!shadow_ready) {
      spdlog::warn("Karma: Shadow not ready (pipeline={} dsv={} srv={} sampler={})",
                   shadow_pipeline_state_ ? 1 : 0,
                   shadow_map_dsv_ ? 1 : 0,
                   shadow_map_srv_ ? 1 : 0,
                   shadow_sampler_ ? 1 : 0);
    }
    float fixed_bias = shadow_bias_;
    if (shadow_map_size_ >= 2048) {
      fixed_bias = 0.0025f;
    } else if (shadow_map_size_ >= 1024) {
      fixed_bias = 0.005f;
    } else {
      fixed_bias = 0.0075f;
    }
    constants.shadow_params[1] = fixed_bias;
    constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
    constants.shadow_params[3] = shadow_debug_ ? -static_cast<float>(shadow_map_size_)
                                               : (shadow_map_size_ > 0
                                                      ? 1.0f / static_cast<float>(shadow_map_size_)
                                                      : 0.0f);

    glm::vec3 light_dir = directional_light_.direction;
    if (glm::length(light_dir) < 1e-4f) {
      light_dir = glm::vec3(0.3f, 1.0f, 0.2f);
    }
    light_dir = glm::normalize(light_dir);
    constants.light_dir[0] = light_dir.x;
    constants.light_dir[1] = light_dir.y;
    constants.light_dir[2] = light_dir.z;
    constants.light_dir[3] = 0.0f;
    constants.light_color[0] = directional_light_.color.r * directional_light_.intensity;
    constants.light_color[1] = directional_light_.color.g * directional_light_.intensity;
    constants.light_color[2] = directional_light_.color.b * directional_light_.intensity;
    constants.light_color[3] = 1.0f;
    constants.camera_pos[0] = camera_.position.x;
    constants.camera_pos[1] = camera_.position.y;
    constants.camera_pos[2] = camera_.position.z;
    constants.camera_pos[3] = 1.0f;

    Diligent::IBuffer* vbs[] = {mesh.vertex_buffer};
    Diligent::Uint64 offsets[] = {0};
    context_->SetVertexBuffers(0,
                               1,
                               vbs,
                               offsets,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                               Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    if (mesh.index_buffer && mesh.index_count > 0) {
      context_->SetIndexBuffer(mesh.index_buffer,
                               0,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    auto draw_with_material = [&](renderer::MaterialId material,
                                  Diligent::Uint32 index_offset,
                                  Diligent::Uint32 index_count) {
      const MaterialRecord* mat = nullptr;
      if (material != renderer::kInvalidMaterial) {
        auto mat_it = materials_.find(material);
        if (mat_it != materials_.end()) {
          mat = &mat_it->second;
        }
      }

      glm::vec4 base_color = mat ? mat->base_color_factor : mesh.base_color;
      if (!mat && base_color == glm::vec4(1.0f)) {
        base_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
      }
      constants.base_color_factor[0] = base_color.r;
      constants.base_color_factor[1] = base_color.g;
      constants.base_color_factor[2] = base_color.b;
      constants.base_color_factor[3] = base_color.a;
      const glm::vec3 emissive = mat ? mat->emissive_factor : glm::vec3(0.0f);
      constants.emissive_factor[0] = emissive.x;
      constants.emissive_factor[1] = emissive.y;
      constants.emissive_factor[2] = emissive.z;
      constants.emissive_factor[3] = 1.0f;
      constants.pbr_params[0] = mat ? mat->metallic_factor : 1.0f;
      constants.pbr_params[1] = mat ? mat->roughness_factor : 1.0f;
      constants.pbr_params[2] = mat ? mat->occlusion_strength : 1.0f;
      constants.pbr_params[3] = mat ? mat->normal_scale : 1.0f;
      constants.env_params[0] = environment_intensity_;
      constants.env_params[1] = env_max_mip;
      constants.env_params[2] = static_cast<float>(env_debug_mode_);
      constants.env_params[3] = 0.0f;
      if (env_debug_mode_ > 0 && !warned_env_debug_) {
        spdlog::info("Karma: Env params intensity={} max_mip={}.",
                     constants.env_params[0], constants.env_params[1]);
      }

      {
        Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                  Diligent::MAP_FLAG_DISCARD);
        *mapped = constants;
      }

      Diligent::IShaderResourceBinding* srb = shader_resources_;
      if (mat && mat->srb) {
        srb = mat->srb;
      } else if (default_material_srb_) {
        srb = default_material_srb_;
      }
      if (srb) {
        auto* irr = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex");
        auto* pre = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex");
        auto* brdf = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT");
        if (env_debug_mode_ > 0 && !warned_env_debug_) {
          auto* base = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex");
          spdlog::info("Karma: SRB debug base_color var={} material={} default={}.",
                       base ? "ok" : "null",
                       mat ? "material" : "default",
                       mat ? "no" : "yes");
        }
        if (!irr || !pre || !brdf) {
          if (!warned_env_bind_missing_) {
            spdlog::warn("Karma: Missing env vars on SRB irr={} pre={} brdf={}.",
                         irr ? "ok" : "null",
                         pre ? "ok" : "null",
                         brdf ? "ok" : "null");
            warned_env_bind_missing_ = true;
          }
        }
        if (irr) {
          irr->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
        }
        if (pre) {
          pre->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
        }
        if (brdf) {
          brdf->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_);
        }
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
      }

      if (mesh.index_buffer && index_count > 0) {
        Diligent::DrawIndexedAttribs indexed{};
        indexed.IndexType = Diligent::VT_UINT32;
        indexed.NumIndices = index_count;
        indexed.FirstIndexLocation = index_offset;
        indexed.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context_->DrawIndexed(indexed);
      } else {
        Diligent::DrawAttribs draw_attrs{};
        draw_attrs.NumVertices = mesh.vertex_count;
        draw_attrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context_->Draw(draw_attrs);
      }
      draw_count += 1;
    };

    if (!mesh.submeshes.empty()) {
      for (const auto& submesh : mesh.submeshes) {
        const renderer::MaterialId mat_id =
            (instance.material != renderer::kInvalidMaterial) ? instance.material : submesh.material;
        draw_with_material(mat_id, submesh.index_offset, submesh.index_count);
      }
    } else {
      draw_with_material(instance.material, 0, mesh.index_count);
    }
  }

  auto draw_lines = [&](const std::vector<LineVertex>& lines,
                        Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
                        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
    if (lines.empty()) {
      return;
    }
    if (pso && line_cb_ && line_vb_) {
      if (lines.size() > line_vb_size_) {
        spdlog::warn("Karma: Line draw skipped ({} vertices > capacity {}).",
                     lines.size(), line_vb_size_);
      } else {
        auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
        auto* dsv = swap_chain_->GetDepthBufferDSV();
        context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::Viewport viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(current_width_);
        viewport.Height = static_cast<float>(current_height_);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(current_width_),
                               static_cast<Diligent::Uint32>(current_height_));

        {
          Diligent::MapHelper<LineVertex> vb_map(context_, line_vb_, Diligent::MAP_WRITE,
                                                 Diligent::MAP_FLAG_DISCARD);
          std::memcpy(vb_map, lines.data(), lines.size() * sizeof(LineVertex));
        }

        const glm::mat4 view_proj = depth_fix * projection * view;
        LineConstants constants{};
        copyMat4(constants.view_proj, view_proj);
        {
          Diligent::MapHelper<LineConstants> cb_map(context_, line_cb_, Diligent::MAP_WRITE,
                                                    Diligent::MAP_FLAG_DISCARD);
          *cb_map = constants;
        }

        context_->SetPipelineState(pso);
        if (srb) {
          context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        Diligent::IBuffer* vbs[] = {line_vb_};
        Diligent::Uint64 offsets[] = {0};
        context_->SetVertexBuffers(0, 1, vbs, offsets,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

        Diligent::DrawAttribs draw{};
        draw.NumVertices = static_cast<Diligent::Uint32>(lines.size());
        draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context_->Draw(draw);
      }
    }
  };

  draw_lines(line_vertices_depth_, line_pipeline_state_depth_, line_srb_depth_);
  draw_lines(line_vertices_no_depth_, line_pipeline_state_no_depth_, line_srb_no_depth_);

  if (!warned_no_draws_) {
    spdlog::info("Karma: Diligent drew {} instance(s) this frame (instances={}).", draw_count,
                 instances_.size());
    spdlog::info("Karma: Diligent skipped: hidden={} missing_vb={} missing_mesh={} layer={}",
                 skipped_hidden,
                 skipped_missing_vb,
                 skipped_missing_mesh,
                 skipped_layer);
    warned_no_draws_ = true;
  }
}

unsigned int DiligentBackend::getRenderTargetTextureId(renderer::RenderTargetId /*target*/) const {
  return 0u;
}

void DiligentBackend::setCamera(const renderer::CameraData& camera) {
  camera_ = camera;
}

void DiligentBackend::setCameraActive(bool active) {
  camera_active_ = active;
}

void DiligentBackend::setDirectionalLight(const renderer::DirectionalLightData& light) {
  directional_light_ = light;
}

void DiligentBackend::setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                        bool draw_skybox) {
  environment_intensity_ = intensity;
  environment_map_ = path;
  draw_skybox_ = draw_skybox;
  env_dirty_ = true;
  spdlog::info("Karma: setEnvironmentMap path='{}' intensity={} draw_skybox={} debug_mode={}",
               environment_map_.string(),
               environment_intensity_,
               draw_skybox_ ? 1 : 0,
               env_debug_mode_);
  if (!device_) {
    return;
  }

  if (path.empty()) {
    env_cubemap_srv_ = default_env_;
    env_irradiance_srv_ = default_env_;
    env_prefilter_srv_ = default_env_;
    env_brdf_lut_srv_ = default_base_color_;
    env_dirty_ = false;
  } else {
    ensureEnvironmentResources();
  }

  auto bind_env_to_srb = [&](Diligent::IShaderResourceBinding* srb, const char* label) {
    if (!srb) {
      spdlog::warn("Karma: Env SRV bind skipped for {} (null srb).", label);
      return;
    }
    auto* irr = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex");
    auto* pre = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex");
    auto* brdf = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT");
    if (!irr) {
      spdlog::warn("Karma: {} missing g_IrradianceTex.", label);
    }
    if (!pre) {
      spdlog::warn("Karma: {} missing g_PrefilterTex.", label);
    }
    if (!brdf) {
      spdlog::warn("Karma: {} missing g_BRDFLUT.", label);
    }
    if (irr) {
      irr->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
    }
    if (pre) {
      pre->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
    }
    if (brdf) {
      brdf->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_);
    }
    spdlog::info("Karma: Env SRVs bound for {} irr={} pre={} brdf={} irr_ptr={} pre_ptr={} brdf_ptr={}",
                 label,
                 env_irradiance_srv_ ? "ok" : "null",
                 env_prefilter_srv_ ? "ok" : "null",
                 env_brdf_lut_srv_ ? "ok" : "null",
                 static_cast<const void*>(env_irradiance_srv_.RawPtr()),
                 static_cast<const void*>(env_prefilter_srv_.RawPtr()),
                 static_cast<const void*>(env_brdf_lut_srv_.RawPtr()));
  };

  bind_env_to_srb(shader_resources_, "shader_resources");
  bind_env_to_srb(default_material_srb_, "default_material");
  for (auto& entry : materials_) {
    bind_env_to_srb(entry.second.srb, "material");
  }
}

void DiligentBackend::setAnisotropy(bool enabled, int level) {
  anisotropy_enabled_ = enabled;
  anisotropy_level_ = std::max(1, level);

  if (!device_) {
    return;
  }

  Diligent::SamplerDesc sampler_color{};
  sampler_color.MinFilter = enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MagFilter = enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MipFilter = enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MaxAnisotropy = static_cast<Diligent::Uint8>(std::clamp(anisotropy_level_, 1, 16));
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

  for (auto& entry : materials_) {
    if (entry.second.srb) {
      if (auto* var = entry.second.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_);
      }
      if (auto* var = entry.second.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_);
      }
    }
  }
  if (default_material_srb_) {
    if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
      var->Set(sampler_color_);
    }
    if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
      var->Set(sampler_data_);
    }
  }
}

void DiligentBackend::setGenerateMips(bool enabled) {
  generate_mips_enabled_ = enabled;
}

void DiligentBackend::setShadowSettings(float bias, int map_size, int pcf_radius) {
  shadow_bias_ = std::max(0.0f, bias);
  shadow_pcf_radius_ = std::clamp(pcf_radius, 0, 4);
  const int clamped_size = std::max(256, map_size);
  if (clamped_size != shadow_map_size_) {
    shadow_map_size_ = clamped_size;
    recreateShadowMap();
  }
}

void DiligentBackend::clearFrame(const float* color, bool clear_depth) {
  if (!context_ || !swap_chain_) {
    return;
  }

  auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
  auto* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context_->ClearRenderTarget(rtv, color, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  if (clear_depth && dsv) {
    context_->ClearDepthStencil(dsv,
                                Diligent::CLEAR_DEPTH_FLAG,
                                1.0f,
                                0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
}

}  // namespace karma::renderer_backend
