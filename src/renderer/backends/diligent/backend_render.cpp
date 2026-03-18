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
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace karma::renderer_backend {

namespace {
struct alignas(16) LineConstants {
  float view_proj[16];
};

struct alignas(16) EnvConstants {
  float view_proj[16];
  float params[4];
};

struct alignas(16) InstanceTransformData {
  float col0[4];
  float col1[4];
  float col2[4];
  float col3[4];
};

struct alignas(16) ForwardPlusGpuLight {
  float position_range[4];
  float direction_type[4];
  float color_intensity[4];
  float spot_params[4];
  float screen_rect[4];
};

InstanceTransformData packInstanceTransform(const glm::mat4& transform) {
  InstanceTransformData out{};
  const float* ptr = glm::value_ptr(transform);
  std::memcpy(out.col0, ptr, sizeof(out.col0));
  std::memcpy(out.col1, ptr + 4, sizeof(out.col1));
  std::memcpy(out.col2, ptr + 8, sizeof(out.col2));
  std::memcpy(out.col3, ptr + 12, sizeof(out.col3));
  return out;
}

bool projectSphereToScreenRect(const glm::mat4& view_proj,
                               const glm::vec3& center_ws,
                               float radius_ws,
                               float screen_width,
                               float screen_height,
                               glm::vec4& out_rect) {
  if (radius_ws <= 0.0f || screen_width <= 0.0f || screen_height <= 0.0f) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = -std::numeric_limits<float>::max();
  float max_y = -std::numeric_limits<float>::max();
  bool any_point = false;
  for (int z = -1; z <= 1; z += 2) {
    for (int y = -1; y <= 1; y += 2) {
      for (int x = -1; x <= 1; x += 2) {
        const glm::vec3 sample =
            center_ws + glm::vec3(static_cast<float>(x),
                                  static_cast<float>(y),
                                  static_cast<float>(z)) *
                            radius_ws;
        const glm::vec4 clip = view_proj * glm::vec4(sample, 1.0f);
        if (std::abs(clip.w) <= 1e-6f) {
          continue;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        const float sx = (ndc.x * 0.5f + 0.5f) * screen_width;
        const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * screen_height;
        min_x = std::min(min_x, sx);
        min_y = std::min(min_y, sy);
        max_x = std::max(max_x, sx);
        max_y = std::max(max_y, sy);
        any_point = true;
      }
    }
  }

  if (!any_point) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  if (max_x < 0.0f || max_y < 0.0f || min_x > screen_width || min_y > screen_height) {
    out_rect = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    return false;
  }

  const float max_screen_x = std::max(screen_width - 1.0f, 0.0f);
  const float max_screen_y = std::max(screen_height - 1.0f, 0.0f);
  out_rect.x = std::clamp(min_x, 0.0f, max_screen_x);
  out_rect.y = std::clamp(min_y, 0.0f, max_screen_y);
  out_rect.z = std::clamp(max_x, 0.0f, max_screen_x);
  out_rect.w = std::clamp(max_y, 0.0f, max_screen_y);
  return out_rect.z >= out_rect.x && out_rect.w >= out_rect.y;
}

ForwardPlusGpuLight packForwardPlusLight(const renderer::LightData& light,
                                         const glm::vec4& screen_rect) {
  ForwardPlusGpuLight gpu{};
  gpu.position_range[0] = light.position.x;
  gpu.position_range[1] = light.position.y;
  gpu.position_range[2] = light.position.z;
  gpu.position_range[3] = std::max(light.range, 0.0f);
  gpu.direction_type[0] = light.direction.x;
  gpu.direction_type[1] = light.direction.y;
  gpu.direction_type[2] = light.direction.z;
  gpu.direction_type[3] = static_cast<float>(static_cast<uint32_t>(light.type));
  gpu.color_intensity[0] = light.color.r;
  gpu.color_intensity[1] = light.color.g;
  gpu.color_intensity[2] = light.color.b;
  gpu.color_intensity[3] = std::max(light.intensity, 0.0f);
  gpu.spot_params[0] = light.inner_cone_cos;
  gpu.spot_params[1] = light.outer_cone_cos;
  gpu.spot_params[2] = -1.0f;
  gpu.spot_params[3] = light.casts_shadows ? 1.0f : 0.0f;
  gpu.screen_rect[0] = screen_rect.x;
  gpu.screen_rect[1] = screen_rect.y;
  gpu.screen_rect[2] = screen_rect.z;
  gpu.screen_rect[3] = screen_rect.w;
  return gpu;
}

struct ShadowBatchKey {
  renderer::MeshId mesh = renderer::kInvalidMesh;
  Diligent::Uint32 index_offset = 0;
  Diligent::Uint32 index_count = 0;
  bool indexed = false;

  bool operator==(const ShadowBatchKey& other) const {
    return mesh == other.mesh &&
           index_offset == other.index_offset &&
           index_count == other.index_count &&
           indexed == other.indexed;
  }
};

struct ShadowBatchKeyHash {
  size_t operator()(const ShadowBatchKey& key) const noexcept {
    size_t h = static_cast<size_t>(key.mesh);
    h ^= static_cast<size_t>(key.index_offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.index_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.indexed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct ShadowBatch {
  ShadowBatchKey key{};
  std::vector<InstanceTransformData> transforms;
  std::vector<glm::vec4> bounds_spheres;
};

struct ForwardBatchKey {
  renderer::MeshId mesh = renderer::kInvalidMesh;
  renderer::MaterialId material = renderer::kInvalidMaterial;
  Diligent::Uint32 index_offset = 0;
  Diligent::Uint32 index_count = 0;
  bool indexed = false;

  bool operator==(const ForwardBatchKey& other) const {
    return mesh == other.mesh &&
           material == other.material &&
           index_offset == other.index_offset &&
           index_count == other.index_count &&
           indexed == other.indexed;
  }
};

struct ForwardBatchKeyHash {
  size_t operator()(const ForwardBatchKey& key) const noexcept {
    size_t h = static_cast<size_t>(key.mesh);
    h ^= static_cast<size_t>(key.material) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.index_offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.index_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.indexed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct ForwardBatch {
  ForwardBatchKey key{};
  std::vector<InstanceTransformData> transforms;
};

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max(sx, std::max(sy, sz));
}

glm::vec4 transformBoundingSphere(const glm::mat4& world,
                                  const glm::vec3& local_center,
                                  float local_radius) {
  if (local_radius <= 0.0f) {
    return glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
  }
  const glm::vec3 center = glm::vec3(world * glm::vec4(local_center, 1.0f));
  const float radius = local_radius * maxTransformScale(world);
  return glm::vec4(center, radius);
}

bool sphereIntersectsClipVolume(const glm::mat4& clip_from_world,
                                const glm::vec4& sphere,
                                bool is_gl_ndc) {
  if (sphere.w <= 0.0f) {
    return true;
  }
  const glm::vec4 clip = clip_from_world * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0f);
  if (std::abs(clip.w) <= 1e-5f) {
    return true;
  }
  const float sx = glm::length(glm::vec3(clip_from_world[0]));
  const float sy = glm::length(glm::vec3(clip_from_world[1]));
  const float sz = glm::length(glm::vec3(clip_from_world[2]));
  const float r = sphere.w * std::max(sx, std::max(sy, sz));

  if (clip.x < -clip.w - r || clip.x > clip.w + r) {
    return false;
  }
  if (clip.y < -clip.w - r || clip.y > clip.w + r) {
    return false;
  }
  if (is_gl_ndc) {
    if (clip.z < -clip.w - r || clip.z > clip.w + r) {
      return false;
    }
  } else {
    if (clip.z < -r || clip.z > clip.w + r) {
      return false;
    }
  }
  return true;
}

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

static const std::array<glm::vec3, 6> kPointShadowFaceDirs{
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
};

static const std::array<glm::vec3, 6> kPointShadowFaceUps{
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
};

#if defined(NDEBUG)
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_NONE;
#else
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_VERIFY_ALL;
#endif

glm::mat4 buildLightView(const renderer::DirectionalLightData& light) {
  glm::vec3 dir = light.direction;
  if (glm::length(dir) < 1e-4f) {
    dir = glm::vec3(0.3f, -1.0f, 0.2f);
  }
  // Match glm::lookAt convention used by the shadow projection mapping:
  // camera forward is +dir, but view-space +Z points opposite that.
  const glm::vec3 z = glm::normalize(-dir);
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

bool directionChangedBeyondThreshold(const glm::vec3& a, const glm::vec3& b, float max_angle_deg) {
  if (glm::length(a) <= 1e-4f || glm::length(b) <= 1e-4f) {
    return true;
  }
  const glm::vec3 an = glm::normalize(a);
  const glm::vec3 bn = glm::normalize(b);
  const float dot_v = std::clamp(glm::dot(an, bn), -1.0f, 1.0f);
  const float cos_threshold = std::cos(glm::radians(std::max(max_angle_deg, 0.0f)));
  return dot_v < cos_threshold;
}

bool matrixChangedBeyondEpsilon(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      if (std::abs(a[col][row] - b[col][row]) > eps) {
        return true;
      }
    }
  }
  return false;
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
    swap_chain_->Present(vsync_enabled_ ? 1u : 0u);
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
  for (auto& [id, target] : targets_) {
    (void)id;
    if (target.desc.width <= 0 || target.desc.height <= 0) {
      recreateRenderTargetResources(target, width, height);
    }
  }
}

void DiligentBackend::submit(const renderer::DrawItem& item) {
  if (item.instance == renderer::kInvalidInstance) {
    return;
  }

  if (meshes_.find(item.mesh) == meshes_.end()) {
    return;
  }

  auto it = instances_.find(item.instance);
  if (it == instances_.end()) {
    it = instances_.emplace(item.instance, InstanceRecord{}).first;
  }
  auto& record = it->second;
  const bool mesh_changed = record.mesh != item.mesh;
  record.transform_changed = mesh_changed || matrixChangedBeyondEpsilon(record.transform, item.transform);
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.material_set = item.material_set;
  record.transform = item.transform;
  record.visible = item.visible;
  record.shadow_visible = item.shadow_visible;
}

void DiligentBackend::retireInstance(renderer::InstanceId instance) {
  if (instance == renderer::kInvalidInstance) {
    return;
  }
  instances_.erase(instance);
}

void DiligentBackend::drawLine(const math::Vec3& start, const math::Vec3& end,
                               const math::Color& color, bool depth_test, float thickness) {
  if (!warned_line_thickness_ && thickness != 1.0f) {
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

    out_pso = device_with_cache_.CreateGraphicsPipelineState(pso);
    if (!out_pso) {
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

      out_pso = device_with_cache_.CreateGraphicsPipelineState(pso);
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
      } else {
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
    } else {
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
    } else {
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
    } else {
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
    } else {
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
    } else {
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

void DiligentBackend::renderLayer(renderer::LayerId layer, renderer::RenderTargetId target) {
  if (!context_ || !swap_chain_) {
    return;
  }

  Diligent::ITextureView* active_rtv = swap_chain_->GetCurrentBackBufferRTV();
  Diligent::ITextureView* active_dsv = swap_chain_->GetDepthBufferDSV();
  int render_width = current_width_;
  int render_height = current_height_;
  if (target != renderer::kDefaultRenderTarget) {
    auto target_it = targets_.find(target);
    if (target_it == targets_.end() || !target_it->second.color_rtv) {
      return;
    }
    active_rtv = target_it->second.color_rtv;
    active_dsv = target_it->second.depth_dsv;
    render_width = std::max(target_it->second.width, 1);
    render_height = std::max(target_it->second.height, 1);
  }
  if (!active_rtv || render_width <= 0 || render_height <= 0) {
    return;
  }

  auto bind_active_target = [&]() {
    context_->SetRenderTargets(1, &active_rtv, active_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(render_width);
    viewport.Height = static_cast<float>(render_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context_->SetViewports(1,
                           &viewport,
                           static_cast<Diligent::Uint32>(render_width),
                           static_cast<Diligent::Uint32>(render_height));
  };

  auto clear_active_target = [&](const float* color, bool clear_depth) {
    bind_active_target();
    context_->ClearRenderTarget(active_rtv, color, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (clear_depth && active_dsv) {
      context_->ClearDepthStencil(active_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
  };

  if (!camera_active_) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    clear_active_target(black, true);
    return;
  }

  clear_active_target(clear_color_, true);

  if (!constants_ || !pipeline_state_ || !shader_resources_) {
    if (!warned_no_draws_) {
      warned_no_draws_ = true;
    }
    return;
  }
  bool use_custom_shader_override = !camera_.shader_override_fragment_path.empty();
  if (use_custom_shader_override && !ensureCameraOverridePipeline(camera_)) {
    use_custom_shader_override = false;
  }
  if (use_custom_shader_override) {
    updateCameraOverrideUserConstants(camera_);
  }

  const float aspect = (render_height > 0)
                           ? static_cast<float>(render_width) / static_cast<float>(render_height)
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
  bind_active_target();
  float env_max_mip = 0.0f;
  if (env_prefilter_tex_) {
    const auto& desc = env_prefilter_tex_->GetDesc();
    if (desc.MipLevels > 0) {
      env_max_mip = static_cast<float>(desc.MipLevels - 1);
    }
  }

  if (env_debug_mode_ > 0 && !warned_env_debug_) {
    warned_env_debug_ = true;
  }

  renderSkybox(projection, view);

  const glm::mat4 camera_view_proj = projection * view;
  const Diligent::Uint32 forward_plus_tile_size =
      static_cast<Diligent::Uint32>(std::max(forward_plus_tile_size_, 1));
  const Diligent::Uint32 forward_plus_tiles_x_unclamped = static_cast<Diligent::Uint32>(std::max(
      (render_width + static_cast<int>(forward_plus_tile_size) - 1) /
          static_cast<int>(forward_plus_tile_size),
      1));
  const Diligent::Uint32 forward_plus_tiles_y_unclamped = static_cast<Diligent::Uint32>(std::max(
      (render_height + static_cast<int>(forward_plus_tile_size) - 1) /
          static_cast<int>(forward_plus_tile_size),
      1));
  static constexpr Diligent::Uint32 kMaxForwardPlusTilesPerAxis = 2048;
  const Diligent::Uint32 forward_plus_tiles_x =
      std::min(forward_plus_tiles_x_unclamped, kMaxForwardPlusTilesPerAxis);
  const Diligent::Uint32 forward_plus_tiles_y =
      std::min(forward_plus_tiles_y_unclamped, kMaxForwardPlusTilesPerAxis);
  const Diligent::Uint32 forward_plus_max_lights_per_tile = static_cast<Diligent::Uint32>(
      std::max(forward_plus_max_lights_per_tile_, 1));
  Diligent::Uint32 active_forward_plus_tile_size = forward_plus_tile_size;
  Diligent::Uint32 active_forward_plus_tiles_x = forward_plus_tiles_x;
  Diligent::Uint32 active_forward_plus_tiles_y = forward_plus_tiles_y;
  Diligent::Uint32 active_forward_plus_max_lights_per_tile = forward_plus_max_lights_per_tile;
  bool forward_plus_ready = false;
  bool forward_plus_overflow_risk =
      forward_plus_tiles_x != forward_plus_tiles_x_unclamped ||
      forward_plus_tiles_y != forward_plus_tiles_y_unclamped;

  auto is_local_light = [](const renderer::LightData& light) {
    return light.type != renderer::LightType::Directional &&
           light.intensity > 0.0f &&
           light.range > 0.0f;
  };
  std::vector<ForwardPlusGpuLight> forward_plus_lights_gpu;
  std::vector<size_t> forward_plus_light_source_index;
  forward_plus_lights_gpu.reserve(lights_.size());
  forward_plus_light_source_index.reserve(lights_.size());
  for (size_t idx = 0; idx < lights_.size(); ++idx) {
    const auto& light = lights_[idx];
    if (is_local_light(light)) {
      glm::vec4 screen_rect(1.0f, 1.0f, 0.0f, 0.0f);
      projectSphereToScreenRect(camera_view_proj,
                                light.position,
                                std::max(light.range, 0.0f),
                                static_cast<float>(std::max(render_width, 1)),
                                static_cast<float>(std::max(render_height, 1)),
                                screen_rect);
      forward_plus_lights_gpu.push_back(packForwardPlusLight(light, screen_rect));
      forward_plus_light_source_index.push_back(idx);
    }
  }
  static constexpr size_t kMaxForwardPlusLightCount = 4096u;
  if (forward_plus_lights_gpu.size() > kMaxForwardPlusLightCount) {
    forward_plus_lights_gpu.resize(kMaxForwardPlusLightCount);
    forward_plus_light_source_index.resize(kMaxForwardPlusLightCount);
    forward_plus_overflow_risk = true;
  }

  struct PointShadowSelection {
    size_t local_light_index = 0;
    float distance_sq = 0.0f;
  };
  std::vector<PointShadowSelection> point_shadow_candidates;
  point_shadow_candidates.reserve(forward_plus_lights_gpu.size());
  for (size_t local_idx = 0; local_idx < forward_plus_light_source_index.size(); ++local_idx) {
    const renderer::LightData& source_light = lights_[forward_plus_light_source_index[local_idx]];
    if (source_light.type != renderer::LightType::Point || !source_light.casts_shadows) {
      continue;
    }
    const glm::vec3 to_camera = source_light.position - camera_.position;
    point_shadow_candidates.push_back(
        PointShadowSelection{.local_light_index = local_idx,
                             .distance_sq = glm::dot(to_camera, to_camera)});
  }
  std::sort(point_shadow_candidates.begin(),
            point_shadow_candidates.end(),
            [](const PointShadowSelection& a, const PointShadowSelection& b) {
              return a.distance_sq < b.distance_sq;
            });
  std::array<renderer::LightData, kMaxPointShadowLights> point_shadow_lights{};
  std::array<size_t, kMaxPointShadowLights> point_shadow_light_source_indices{};
  std::array<size_t, kMaxPointShadowLights> point_shadow_local_light_indices{};
  Diligent::Uint32 point_shadow_light_count = 0;
  for (const auto& candidate : point_shadow_candidates) {
    if (point_shadow_light_count >= static_cast<Diligent::Uint32>(kMaxPointShadowLights)) {
      break;
    }
    if (candidate.local_light_index >= forward_plus_lights_gpu.size() ||
        candidate.local_light_index >= forward_plus_light_source_index.size()) {
      continue;
    }
    const renderer::LightData& source_light =
        lights_[forward_plus_light_source_index[candidate.local_light_index]];
    point_shadow_lights[point_shadow_light_count] = source_light;
    point_shadow_light_source_indices[point_shadow_light_count] =
        forward_plus_light_source_index[candidate.local_light_index];
    point_shadow_local_light_indices[point_shadow_light_count] = candidate.local_light_index;
    point_shadow_light_count += 1;
  }

  auto ensure_forward_plus_buffer = [&](Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer,
                                        Diligent::RefCntAutoPtr<Diligent::IBufferView>& srv,
                                        Diligent::RefCntAutoPtr<Diligent::IBufferView>* uav,
                                        size_t& capacity,
                                        size_t required_count,
                                        Diligent::Uint32 element_stride,
                                        Diligent::BIND_FLAGS bind_flags,
                                        const char* name) {
    const bool needs_uav = (bind_flags & Diligent::BIND_UNORDERED_ACCESS) != 0;
    const size_t safe_required = std::max<size_t>(required_count, 1);
    if (buffer && srv && capacity >= safe_required && (!needs_uav || (uav && *uav))) {
      return true;
    }
    const size_t new_capacity =
        std::max(safe_required, capacity > 0 ? capacity * 2 : safe_required);
    Diligent::BufferDesc desc{};
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = element_stride;
    desc.Size = static_cast<Diligent::Uint64>(new_capacity) *
                static_cast<Diligent::Uint64>(element_stride);
    device_->CreateBuffer(desc, nullptr, &buffer);
    if (!buffer) {
      return false;
    }
    srv = buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
    if (!srv) {
      return false;
    }
    if (needs_uav) {
      if (!uav) {
        return false;
      }
      *uav = buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
      if (!*uav) {
        return false;
      }
    } else if (uav) {
      uav->Release();
    }
    capacity = new_capacity;
    return true;
  };

  static constexpr size_t kCpuForwardPlusDirectLightCount = 8u;
  static constexpr size_t kCpuForwardPlusFallbackLightCount = 64u;
  const bool forward_plus_compute_available =
      forward_plus_compute_pso_ && forward_plus_compute_srb_ && forward_plus_compute_cb_;
  std::array<ForwardPlusGpuLight, kCpuForwardPlusFallbackLightCount> cpu_forward_plus_lights{};
  Diligent::Uint32 cpu_forward_plus_light_count = 0;
  bool cpu_forward_plus_ready = false;
  const bool use_cpu_forward_plus_fallback =
      !forward_plus_lights_gpu.empty() &&
      render_width > 0 &&
      render_height > 0 &&
      (!forward_plus_compute_available ||
       forward_plus_lights_gpu.size() <= kCpuForwardPlusDirectLightCount);

  if (use_cpu_forward_plus_fallback) {
    active_forward_plus_tiles_x = 1;
    active_forward_plus_tiles_y = 1;
    active_forward_plus_tile_size = static_cast<Diligent::Uint32>(
        std::max(std::max(render_width, render_height), 1));
    active_forward_plus_max_lights_per_tile = static_cast<Diligent::Uint32>(
        std::max<size_t>(forward_plus_lights_gpu.size(), 1));
    active_forward_plus_max_lights_per_tile = std::min(
        active_forward_plus_max_lights_per_tile,
        static_cast<Diligent::Uint32>(kCpuForwardPlusFallbackLightCount));
    cpu_forward_plus_light_count = static_cast<Diligent::Uint32>(std::min<size_t>(
        forward_plus_lights_gpu.size(), kCpuForwardPlusFallbackLightCount));
    for (Diligent::Uint32 i = 0; i < cpu_forward_plus_light_count; ++i) {
      cpu_forward_plus_lights[i] = forward_plus_lights_gpu[i];
    }
    if (forward_plus_lights_gpu.size() > static_cast<size_t>(cpu_forward_plus_light_count)) {
      forward_plus_overflow_risk = true;
    }
    cpu_forward_plus_ready = cpu_forward_plus_light_count > 0;
  }

  if (!use_cpu_forward_plus_fallback &&
      !forward_plus_lights_gpu.empty() &&
      render_width > 0 &&
      render_height > 0 &&
      forward_plus_compute_available) {
    const size_t forward_plus_tile_count =
        static_cast<size_t>(forward_plus_tiles_x) * static_cast<size_t>(forward_plus_tiles_y);
    const size_t forward_plus_index_count =
        forward_plus_tile_count * static_cast<size_t>(forward_plus_max_lights_per_tile);
    static constexpr size_t kMaxForwardPlusIndexCount = 8u * 1024u * 1024u;

    if (forward_plus_index_count <= kMaxForwardPlusIndexCount) {
      const bool buffers_ready =
          ensure_forward_plus_buffer(forward_plus_light_buffer_,
                                     forward_plus_light_srv_,
                                     nullptr,
                                     forward_plus_light_capacity_,
                                     forward_plus_lights_gpu.size(),
                                     static_cast<Diligent::Uint32>(sizeof(ForwardPlusGpuLight)),
                                     Diligent::BIND_SHADER_RESOURCE,
                                     "Karma Forward+ Lights") &&
          ensure_forward_plus_buffer(forward_plus_tile_count_buffer_,
                                     forward_plus_tile_count_srv_,
                                     std::addressof(forward_plus_tile_count_uav_),
                                     forward_plus_tile_count_capacity_,
                                     forward_plus_tile_count,
                                     static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                     Diligent::BIND_SHADER_RESOURCE |
                                         Diligent::BIND_UNORDERED_ACCESS,
                                     "Karma Forward+ Tile Counts") &&
          ensure_forward_plus_buffer(forward_plus_tile_index_buffer_,
                                     forward_plus_tile_index_srv_,
                                     std::addressof(forward_plus_tile_index_uav_),
                                     forward_plus_tile_index_capacity_,
                                     forward_plus_index_count,
                                     static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                     Diligent::BIND_SHADER_RESOURCE |
                                         Diligent::BIND_UNORDERED_ACCESS,
                                     "Karma Forward+ Tile Indices");

      if (buffers_ready) {
        context_->UpdateBuffer(forward_plus_light_buffer_,
                               0,
                               static_cast<Diligent::Uint32>(forward_plus_lights_gpu.size() *
                                                             sizeof(ForwardPlusGpuLight)),
                               forward_plus_lights_gpu.data(),
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        ForwardPlusComputeConstants fp_constants{};
        copyMat4(fp_constants.view_proj, camera_view_proj);
        fp_constants.forward_plus_params[0] = static_cast<float>(active_forward_plus_tile_size);
        fp_constants.forward_plus_params[1] = static_cast<float>(active_forward_plus_tiles_x);
        fp_constants.forward_plus_params[2] = static_cast<float>(active_forward_plus_tiles_y);
        fp_constants.forward_plus_params[3] =
            static_cast<float>(active_forward_plus_max_lights_per_tile);
        fp_constants.screen_params[0] = static_cast<float>(render_width);
        fp_constants.screen_params[1] = static_cast<float>(render_height);
        fp_constants.screen_params[2] = static_cast<float>(forward_plus_lights_gpu.size());
        fp_constants.screen_params[3] = 0.0f;
        {
          Diligent::MapHelper<ForwardPlusComputeConstants> mapped(
              context_, forward_plus_compute_cb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
          *mapped = fp_constants;
        }

        if (forward_plus_compute_lights_var_) {
          forward_plus_compute_lights_var_->Set(forward_plus_light_srv_);
        }
        if (forward_plus_compute_tile_counts_var_) {
          forward_plus_compute_tile_counts_var_->Set(forward_plus_tile_count_uav_);
        }
        if (forward_plus_compute_tile_indices_var_) {
          forward_plus_compute_tile_indices_var_->Set(forward_plus_tile_index_uav_);
        }

        context_->SetPipelineState(forward_plus_compute_pso_);
        context_->CommitShaderResources(forward_plus_compute_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        static constexpr Diligent::Uint32 kForwardPlusGroupSize = 8u;
        Diligent::DispatchComputeAttribs dispatch{};
        dispatch.ThreadGroupCountX =
            (forward_plus_tiles_x + kForwardPlusGroupSize - 1u) / kForwardPlusGroupSize;
        dispatch.ThreadGroupCountY =
            (forward_plus_tiles_y + kForwardPlusGroupSize - 1u) / kForwardPlusGroupSize;
        dispatch.ThreadGroupCountZ = 1;
        context_->DispatchCompute(dispatch);

        Diligent::StateTransitionDesc barriers[2];
        barriers[0].pResource = forward_plus_tile_count_buffer_;
        barriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        barriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        barriers[1].pResource = forward_plus_tile_index_buffer_;
        barriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[1].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        barriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        context_->TransitionResourceStates(2, barriers);

        forward_plus_ready = true;
      }
    } else {
      forward_plus_overflow_risk = true;
    }
  }

  forward_plus_overflow_risk =
      forward_plus_overflow_risk ||
      (forward_plus_lights_gpu.size() >
       static_cast<size_t>(active_forward_plus_max_lights_per_tile));
  forward_plus_stats_.tile_size = active_forward_plus_tile_size;
  forward_plus_stats_.max_lights_per_tile = active_forward_plus_max_lights_per_tile;
  forward_plus_stats_.tiles_x = active_forward_plus_tiles_x;
  forward_plus_stats_.tiles_y = active_forward_plus_tiles_y;
  forward_plus_stats_.local_light_count =
      static_cast<uint32_t>(std::min<size_t>(forward_plus_lights_gpu.size(),
                                             std::numeric_limits<uint32_t>::max()));
  forward_plus_stats_.active = forward_plus_ready || cpu_forward_plus_ready;
  forward_plus_stats_.overflow_risk = forward_plus_overflow_risk;

  if (pipeline_state_) {
    if (forward_plus_ready) {
      if (auto* var = pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                               "g_ForwardPlusLights")) {
        var->Set(forward_plus_light_srv_);
      }
      if (auto* var =
              pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                       "g_ForwardPlusTileLightCounts")) {
        var->Set(forward_plus_tile_count_srv_);
      }
      if (auto* var =
              pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                       "g_ForwardPlusTileLightIndices")) {
        var->Set(forward_plus_tile_index_srv_);
      }
    } else {
      const bool fallback_srvs_ready =
          ensure_forward_plus_buffer(forward_plus_light_buffer_,
                                     forward_plus_light_srv_,
                                     nullptr,
                                     forward_plus_light_capacity_,
                                     1u,
                                     static_cast<Diligent::Uint32>(sizeof(ForwardPlusGpuLight)),
                                     Diligent::BIND_SHADER_RESOURCE,
                                     "Karma Forward+ Fallback Lights") &&
          ensure_forward_plus_buffer(forward_plus_tile_count_buffer_,
                                     forward_plus_tile_count_srv_,
                                     nullptr,
                                     forward_plus_tile_count_capacity_,
                                     1u,
                                     static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                     Diligent::BIND_SHADER_RESOURCE,
                                     "Karma Forward+ Fallback Tile Counts") &&
          ensure_forward_plus_buffer(forward_plus_tile_index_buffer_,
                                     forward_plus_tile_index_srv_,
                                     nullptr,
                                     forward_plus_tile_index_capacity_,
                                     1u,
                                     static_cast<Diligent::Uint32>(sizeof(uint32_t)),
                                     Diligent::BIND_SHADER_RESOURCE,
                                     "Karma Forward+ Fallback Tile Indices");
      if (fallback_srvs_ready) {
        if (auto* var = pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                                 "g_ForwardPlusLights")) {
          var->Set(forward_plus_light_srv_);
        }
        if (auto* var =
                pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                         "g_ForwardPlusTileLightCounts")) {
          var->Set(forward_plus_tile_count_srv_);
        }
        if (auto* var =
                pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                         "g_ForwardPlusTileLightIndices")) {
          var->Set(forward_plus_tile_index_srv_);
        }
      }
    }
  }

  glm::vec3 shadow_light_dir = directional_light_.direction;
  if (glm::length(shadow_light_dir) < 1e-4f) {
    shadow_light_dir = glm::vec3(0.3f, -1.0f, 0.2f);
  }
  shadow_light_dir = glm::normalize(shadow_light_dir);
  if (shadow_light_dir.y > 0.0f) {
    shadow_light_dir = -shadow_light_dir;
  }
  renderer::DirectionalLightData shadow_light = directional_light_;
  shadow_light.direction = shadow_light_dir;
  const glm::mat4 stable_light_view = buildLightView(shadow_light);
  const glm::vec3 cam_forward = glm::normalize(cam_basis * glm::vec3(0.0f, 0.0f, -1.0f));
  glm::vec3 cam_up = glm::normalize(cam_basis * glm::vec3(0.0f, 1.0f, 0.0f));
  glm::vec3 cam_right = glm::normalize(glm::cross(cam_forward, cam_up));
  if (glm::length(cam_right) < 1e-4f) {
    cam_right = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  cam_up = glm::normalize(glm::cross(cam_right, cam_forward));

  const float shadow_map_extent =
      shadow_map_tex_ ? static_cast<float>(shadow_map_tex_->GetDesc().Width)
                      : static_cast<float>(shadow_map_size_);
  const float safe_shadow_map_extent = std::max(shadow_map_extent, 1.0f);
  const float shadow_texel_size = 1.0f / safe_shadow_map_extent;
  const float point_shadow_map_extent =
      point_shadow_map_tex_ ? static_cast<float>(point_shadow_map_tex_->GetDesc().Width)
                            : static_cast<float>(point_shadow_map_size_);
  const float safe_point_shadow_map_extent = std::max(point_shadow_map_extent, 1.0f);
  const float point_shadow_texel_size = 1.0f / safe_point_shadow_map_extent;
  const float fixed_bias = std::max(shadow_bias_, 0.0f);
  const float shadow_texel_param = shadow_texel_size;
  const bool is_gl = device_->GetDeviceInfo().IsGLDevice();
  const auto& ndc = device_->GetDeviceInfo().GetNDCAttribs();
  const glm::mat4 uv_scale = glm::scale(glm::mat4(1.0f),
                                        glm::vec3(0.5f, ndc.YtoVScale, ndc.ZtoDepthScale));
  const glm::mat4 uv_bias = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(0.5f, 0.5f, ndc.GetZtoDepthBias()));

  const float shadow_near = std::max(camera_.near_clip, 0.05f);
  float shadow_far = directional_light_.shadow_extent > 0.0f
                         ? directional_light_.shadow_extent
                         : 80.0f;
  shadow_far = std::max(shadow_far, shadow_near + 1.0f);
  if (camera_.perspective) {
    shadow_far = std::min(shadow_far, std::max(camera_.far_clip, shadow_near + 1.0f));
  }
  const float split_lambda = std::clamp(shadow_split_lambda_, 0.0f, 1.0f);

  std::array<float, kShadowCascadeCount> cascade_splits{};
  float prev_split = shadow_near;
  for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
    const float p = static_cast<float>(cascade + 1) / static_cast<float>(kShadowCascadeCount);
    const float uniform_split = shadow_near + (shadow_far - shadow_near) * p;
    float split = uniform_split;
    if (camera_.perspective) {
      const float log_split = shadow_near * std::pow(shadow_far / shadow_near, p);
      split = glm::mix(uniform_split, log_split, split_lambda);
    }
    split = std::clamp(split, shadow_near + 0.001f, shadow_far);
    cascade_splits[cascade] = split;
  }

  auto build_slice_corners = [&](float slice_near, float slice_far) {
    std::array<glm::vec3, 8> corners{};
    if (camera_.perspective) {
      const float fov_rad = glm::radians(camera_.fov_y_degrees);
      const float tan_half_fov = std::tan(fov_rad * 0.5f);
      const float near_h = tan_half_fov * slice_near;
      const float near_w = near_h * aspect;
      const float far_h = tan_half_fov * slice_far;
      const float far_w = far_h * aspect;
      const glm::vec3 near_center = camera_.position + cam_forward * slice_near;
      const glm::vec3 far_center = camera_.position + cam_forward * slice_far;
      corners = {
          near_center + cam_up * near_h - cam_right * near_w,
          near_center + cam_up * near_h + cam_right * near_w,
          near_center - cam_up * near_h - cam_right * near_w,
          near_center - cam_up * near_h + cam_right * near_w,
          far_center + cam_up * far_h - cam_right * far_w,
          far_center + cam_up * far_h + cam_right * far_w,
          far_center - cam_up * far_h - cam_right * far_w,
          far_center - cam_up * far_h + cam_right * far_w,
      };
    } else {
      const glm::vec3 near_center = camera_.position + cam_forward * slice_near;
      const glm::vec3 far_center = camera_.position + cam_forward * slice_far;
      corners = {
          near_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_left,
          near_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_right,
          near_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_left,
          near_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_right,
          far_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_left,
          far_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_right,
          far_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_left,
          far_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_right,
      };
    }
    return corners;
  };

  std::array<glm::mat4, kShadowCascadeCount> cascade_light_view_proj = cached_cascade_light_view_proj_;
  std::array<glm::mat4, kShadowCascadeCount> cascade_shadow_uv_proj = cached_cascade_shadow_uv_proj_;
  std::array<float, kShadowCascadeCount> cascade_world_texel = cached_cascade_world_texel_;
  std::array<glm::mat4, kPointShadowMatrixCount> point_shadow_uv_proj = cached_point_shadow_uv_proj_;
  bool point_shadow_ready = point_shadow_cache_initialized_;

  float directional_shadow_position_threshold = directional_shadow_position_threshold_;
  if (directional_shadow_cache_valid_ && cached_cascade_world_texel_[0] > 0.0f) {
    directional_shadow_position_threshold = std::max(
        directional_shadow_position_threshold, cached_cascade_world_texel_[0] * 1.5f);
  }

  bool directional_shadow_needs_update = !directional_shadow_cache_valid_;
  if (!directional_shadow_needs_update) {
    const float camera_delta =
        glm::length(camera_.position - cached_shadow_camera_position_);
    directional_shadow_needs_update =
        camera_delta > directional_shadow_position_threshold ||
        std::abs(aspect - cached_shadow_camera_aspect_) > 1e-4f ||
        std::abs(camera_.fov_y_degrees - cached_shadow_camera_fov_y_degrees_) > 1e-3f ||
        std::abs(camera_.near_clip - cached_shadow_camera_near_) > 1e-4f ||
        std::abs(camera_.far_clip - cached_shadow_camera_far_) > 1e-3f ||
        camera_.perspective != cached_shadow_camera_perspective_ ||
        directionChangedBeyondThreshold(cam_forward,
                                        cached_shadow_camera_forward_,
                                        directional_shadow_angle_threshold_deg_) ||
        directionChangedBeyondThreshold(shadow_light_dir,
                                        cached_shadow_light_direction_,
                                        directional_shadow_angle_threshold_deg_);
  }
  if (!directional_shadow_needs_update && directional_shadow_cache_valid_) {
    cascade_splits = cached_cascade_splits_;
  } else {
    float slice_prev_split = shadow_near;
    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
      const float split_near = slice_prev_split;
      const float split_far = cascade_splits[cascade];
      slice_prev_split = split_far;
      if (split_far <= split_near + 1e-4f) {
        continue;
      }

      const auto frustum_corners = build_slice_corners(split_near, split_far);
      glm::vec3 frustum_center{0.0f};
      for (const glm::vec3& corner : frustum_corners) {
        frustum_center += corner;
      }
      frustum_center /= static_cast<float>(frustum_corners.size());

      float radius_ws = 0.0f;
      for (const glm::vec3& corner : frustum_corners) {
        radius_ws = std::max(radius_ws, glm::length(corner - frustum_center));
      }
      radius_ws = std::ceil(radius_ws * 16.0f) / 16.0f;
      radius_ws = std::max(radius_ws + 2.0f, 1.0f);

      const glm::mat4 light_view = stable_light_view;

      glm::vec3 center_ls = glm::vec3(light_view * glm::vec4(frustum_center, 1.0f));
      const float units_per_texel = (2.0f * radius_ws) / safe_shadow_map_extent;
      if (units_per_texel > 0.0f) {
        center_ls.x = std::floor(center_ls.x / units_per_texel + 0.5f) * units_per_texel;
        center_ls.y = std::floor(center_ls.y / units_per_texel + 0.5f) * units_per_texel;
      }

      glm::vec3 light_min{
          center_ls.x - radius_ws,
          center_ls.y - radius_ws,
          std::numeric_limits<float>::max()};
      glm::vec3 light_max{
          center_ls.x + radius_ws,
          center_ls.y + radius_ws,
          std::numeric_limits<float>::lowest()};
      for (const glm::vec3& corner : frustum_corners) {
        const glm::vec3 corner_ls = glm::vec3(light_view * glm::vec4(corner, 1.0f));
        light_min.z = std::min(light_min.z, corner_ls.z);
        light_max.z = std::max(light_max.z, corner_ls.z);
      }
      const float depth_span = std::max(light_max.z - light_min.z, 1.0f);
      const float depth_padding = std::max(5.0f, depth_span * 0.2f);
      // Keep additional headroom toward the light so off-screen casters can still
      // project onto visible receivers inside the camera slice.
      const float caster_padding_toward_light = std::max(depth_padding, radius_ws * 2.0f);
      light_min.z -= depth_padding;
      light_max.z += caster_padding_toward_light;
      const glm::vec3 extent = light_max - light_min;

      const float scale_x = (extent.x > 0.0f) ? (2.0f / extent.x) : 1.0f;
      const float scale_y = (extent.y > 0.0f) ? (2.0f / extent.y) : 1.0f;
      const float near_z = light_max.z;
      const float far_z = light_min.z;
      const float z_denom = far_z - near_z;
      float scale_z = 1.0f;
      float bias_z = 0.0f;
      if (std::abs(z_denom) > 1e-6f) {
        if (is_gl) {
          scale_z = 2.0f / z_denom;
          bias_z = -(far_z + near_z) / z_denom;
        } else {
          scale_z = 1.0f / z_denom;
          bias_z = -near_z * scale_z;
        }
      } else {
        scale_z = is_gl ? 2.0f : 1.0f;
        bias_z = is_gl ? -1.0f : 0.0f;
      }
      const float bias_x = -light_min.x * scale_x - 1.0f;
      const float bias_y = -light_min.y * scale_y - 1.0f;

      const glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), glm::vec3(scale_x, scale_y, scale_z));
      const glm::mat4 bias_mat = glm::translate(glm::mat4(1.0f), glm::vec3(bias_x, bias_y, bias_z));
      const glm::mat4 shadow_proj = bias_mat * scale_mat;
      const glm::mat4 light_view_proj = shadow_proj * light_view;
      const glm::mat4 shadow_uv_proj = uv_bias * uv_scale * light_view_proj;

      cascade_light_view_proj[cascade] = light_view_proj;
      cascade_shadow_uv_proj[cascade] = shadow_uv_proj;
      cascade_world_texel[cascade] =
          std::max(std::max(extent.x, extent.y), 0.0f) / safe_shadow_map_extent;
    }
  }

  const glm::mat4 light_view_proj = cascade_light_view_proj[0];
  const glm::mat4 shadow_uv_proj = cascade_shadow_uv_proj[0];
  const float shadow_world_texel = cascade_world_texel[0];

  auto ensure_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t new_capacity = std::max(instance_count,
                                         instance_vb_capacity_ > 0 ? instance_vb_capacity_ * 2 : static_cast<size_t>(128));
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma Instance Buffer";
    ib_desc.Usage = Diligent::USAGE_DYNAMIC;
    ib_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    ib_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    ib_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(InstanceTransformData));
    device_->CreateBuffer(ib_desc, nullptr, &instance_vb_);
    if (!instance_vb_) {
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto is_valid_indexed_draw = [&](const auto& mesh,
                                   Diligent::Uint32 first_index,
                                   Diligent::Uint32 index_count) {
    if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0) {
      return false;
    }
    const uint64_t first = static_cast<uint64_t>(first_index);
    const uint64_t count = static_cast<uint64_t>(index_count);
    const uint64_t total = static_cast<uint64_t>(mesh.index_count);
    if (first >= total) {
      return false;
    }
    return first + count <= total;
  };

  bool has_shadow_dsv = false;
  for (const auto& dsv : shadow_map_dsv_cascades_) {
    if (dsv) {
      has_shadow_dsv = true;
      break;
    }
  }
  bool has_point_shadow_dsv = false;
  for (const auto& dsv : point_shadow_map_dsv_faces_) {
    if (dsv) {
      has_point_shadow_dsv = true;
      break;
    }
  }
  const bool camera_renders_shadows = camera_.render_shadows;
  const bool can_render_directional_shadows =
      camera_renders_shadows && shadow_pipeline_state_ && has_shadow_dsv;
  const bool can_render_point_shadows =
      camera_renders_shadows && shadow_pipeline_state_ && point_shadow_map_srv_ && has_point_shadow_dsv &&
      point_shadow_light_count > 0;
  if (!directional_shadow_needs_update && can_render_directional_shadows) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible || !instance.transform_changed) {
        continue;
      }
      if (meshes_.find(instance.mesh) == meshes_.end()) {
        continue;
      }
      directional_shadow_needs_update = true;
      break;
    }
  }

  auto mark_point_shadow_slot_dirty = [&](Diligent::Uint32 slot, bool invalidate_slot) {
    if (slot >= static_cast<Diligent::Uint32>(kMaxPointShadowLights)) {
      return;
    }
    if (invalidate_slot) {
      point_shadow_slot_valid_[slot] = false;
    }
    const Diligent::Uint32 face_base = slot * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
    for (Diligent::Uint32 face = 0; face < static_cast<Diligent::Uint32>(kPointShadowFaceCount);
         ++face) {
      point_shadow_face_dirty_[face_base + face] = 1u;
    }
  };

  bool point_shadow_force_full_refresh = false;
  for (Diligent::Uint32 slot = 0; slot < point_shadow_light_count; ++slot) {
    if (slot >= static_cast<Diligent::Uint32>(kMaxPointShadowLights)) {
      break;
    }
    const int32_t source_index = static_cast<int32_t>(point_shadow_light_source_indices[slot]);
    const renderer::LightData& light = point_shadow_lights[slot];
    const bool slot_identity_changed = point_shadow_slot_source_index_[slot] != source_index;
    const bool slot_position_changed =
        glm::length(light.position - point_shadow_slot_position_[slot]) >
        point_shadow_position_threshold_;
    const bool slot_range_changed =
        std::abs(light.range - point_shadow_slot_range_[slot]) > point_shadow_range_threshold_;
    if (!point_shadow_cache_initialized_ || slot_identity_changed) {
      mark_point_shadow_slot_dirty(slot, true);
      point_shadow_force_full_refresh = point_shadow_force_full_refresh || slot_identity_changed;
    } else if (slot_position_changed || slot_range_changed) {
      mark_point_shadow_slot_dirty(slot, false);
    }
    point_shadow_slot_source_index_[slot] = source_index;
    point_shadow_slot_position_[slot] = light.position;
    point_shadow_slot_range_[slot] = light.range;
  }

  bool moving_shadow_caster_affects_point_shadow = false;
  if (can_render_point_shadows) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible || !instance.transform_changed) {
        continue;
      }
      const auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (mesh.bounds_radius <= 0.0f) {
        continue;
      }
      const glm::vec4 bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);
      if (bounds_sphere.w <= 0.0f) {
        continue;
      }
      const glm::vec3 caster_center{bounds_sphere.x, bounds_sphere.y, bounds_sphere.z};
      for (Diligent::Uint32 slot = 0; slot < point_shadow_light_count; ++slot) {
        const renderer::LightData& point_light = point_shadow_lights[slot];
        const float influence_radius = std::max(point_light.range, 0.0f) + bounds_sphere.w;
        if (influence_radius <= 0.0f) {
          continue;
        }
        const glm::vec3 to_caster = caster_center - point_light.position;
        if (glm::dot(to_caster, to_caster) <= influence_radius * influence_radius) {
          mark_point_shadow_slot_dirty(slot, false);
          moving_shadow_caster_affects_point_shadow = true;
        }
      }
    }
  }
  point_shadow_force_full_refresh =
      point_shadow_force_full_refresh || moving_shadow_caster_affects_point_shadow;

  for (Diligent::Uint32 slot = point_shadow_light_count;
       slot < static_cast<Diligent::Uint32>(kMaxPointShadowLights);
       ++slot) {
    point_shadow_slot_source_index_[slot] = -1;
    point_shadow_slot_valid_[slot] = false;
  }
  if (point_shadow_light_count == 0) {
    point_shadow_cache_initialized_ = false;
    point_shadow_face_dirty_.fill(1u);
  }

  std::array<uint8_t, kPointShadowMatrixCount> point_shadow_faces_to_update{};
  Diligent::Uint32 point_shadow_face_update_count = 0;
  if (can_render_point_shadows) {
    if (!point_shadow_cache_initialized_ || point_shadow_force_full_refresh) {
      for (Diligent::Uint32 slot = 0; slot < point_shadow_light_count; ++slot) {
        const Diligent::Uint32 face_base = slot * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
        for (Diligent::Uint32 face = 0;
             face < static_cast<Diligent::Uint32>(kPointShadowFaceCount);
             ++face) {
          const Diligent::Uint32 matrix_idx = face_base + face;
          point_shadow_faces_to_update[matrix_idx] = 1u;
          point_shadow_face_update_count += 1;
        }
      }
    } else {
      const Diligent::Uint32 active_face_count =
          point_shadow_light_count * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
      const Diligent::Uint32 update_budget = std::max(point_shadow_faces_per_frame_budget_, 1u);
      Diligent::Uint32 visited = 0;
      while (point_shadow_face_update_count < update_budget && visited < active_face_count) {
        if (active_face_count == 0) {
          break;
        }
        const Diligent::Uint32 matrix_idx = point_shadow_face_cursor_ % active_face_count;
        point_shadow_face_cursor_ = (point_shadow_face_cursor_ + 1u) % active_face_count;
        visited += 1;
        if (point_shadow_face_dirty_[matrix_idx] == 0u) {
          continue;
        }
        point_shadow_faces_to_update[matrix_idx] = 1u;
        point_shadow_face_update_count += 1;
      }
    }
  }

  const bool render_directional_shadows =
      can_render_directional_shadows && directional_shadow_needs_update;
  const bool render_point_shadows =
      can_render_point_shadows && point_shadow_face_update_count > 0;

  thread_local std::vector<ShadowBatch> shadow_batches;
  thread_local std::unordered_map<ShadowBatchKey, size_t, ShadowBatchKeyHash> shadow_batch_lookup;
  shadow_batches.clear();
  shadow_batch_lookup.clear();
  if (render_directional_shadows || render_point_shadows) {
    shadow_batches.reserve(instances_.size());
    shadow_batch_lookup.reserve(instances_.size());
    auto append_shadow_batch = [&](const ShadowBatchKey& key,
                                   const glm::mat4& transform,
                                   const glm::vec4& bounds_sphere) {
      auto it = shadow_batch_lookup.find(key);
      if (it == shadow_batch_lookup.end()) {
        const size_t idx = shadow_batches.size();
        shadow_batches.push_back(ShadowBatch{.key = key});
        shadow_batch_lookup.emplace(key, idx);
        it = shadow_batch_lookup.find(key);
      }
      shadow_batches[it->second].transforms.push_back(packInstanceTransform(transform));
      shadow_batches[it->second].bounds_spheres.push_back(bounds_sphere);
    };

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
      const glm::vec4 world_bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);

      const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
      if (!mesh.submeshes.empty()) {
        for (const auto& submesh : mesh.submeshes) {
          const ShadowBatchKey key{
              .mesh = instance.mesh,
              .index_offset = submesh.index_offset,
              .index_count = submesh.index_count,
              .indexed = indexed_mesh && submesh.index_count > 0,
          };
          append_shadow_batch(key, instance.transform, world_bounds_sphere);
        }
      } else {
        const ShadowBatchKey key{
            .mesh = instance.mesh,
            .index_offset = 0,
            .index_count = mesh.index_count,
            .indexed = indexed_mesh,
        };
        append_shadow_batch(key, instance.transform, world_bounds_sphere);
      }
    }
    std::sort(shadow_batches.begin(),
              shadow_batches.end(),
              [](const ShadowBatch& a, const ShadowBatch& b) {
                if (a.key.mesh != b.key.mesh) {
                  return a.key.mesh < b.key.mesh;
                }
                if (a.key.index_offset != b.key.index_offset) {
                  return a.key.index_offset < b.key.index_offset;
                }
                if (a.key.index_count != b.key.index_count) {
                  return a.key.index_count < b.key.index_count;
                }
                return static_cast<uint32_t>(a.key.indexed) < static_cast<uint32_t>(b.key.indexed);
              });
  }

  thread_local std::vector<InstanceTransformData> filtered_shadow_transforms;
  filtered_shadow_transforms.clear();
  auto draw_shadow_batches = [&](const DrawConstants& pass_constants, auto&& sphere_visible) {
    Diligent::IBuffer* bound_mesh_vb = nullptr;
    Diligent::IBuffer* bound_instance_vb = nullptr;
    Diligent::IBuffer* bound_index_buffer = nullptr;
    {
      Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                Diligent::MAP_FLAG_DISCARD);
      *mapped = pass_constants;
    }
    for (const auto& batch : shadow_batches) {
      if (batch.transforms.empty()) {
        continue;
      }
      filtered_shadow_transforms.clear();
      filtered_shadow_transforms.reserve(batch.transforms.size());
      const size_t bounds_count = batch.bounds_spheres.size();
      for (size_t i = 0; i < batch.transforms.size(); ++i) {
        if (i < bounds_count && !sphere_visible(batch.bounds_spheres[i])) {
          continue;
        }
        filtered_shadow_transforms.push_back(batch.transforms[i]);
      }
      if (filtered_shadow_transforms.empty()) {
        continue;
      }
      auto mesh_it = meshes_.find(batch.key.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }
      if (!ensure_instance_buffer(filtered_shadow_transforms.size())) {
        continue;
      }
      {
        Diligent::MapHelper<InstanceTransformData> instance_map(context_, instance_vb_,
                                                                Diligent::MAP_WRITE,
                                                                Diligent::MAP_FLAG_DISCARD);
        std::memcpy(instance_map, filtered_shadow_transforms.data(),
                    filtered_shadow_transforms.size() * sizeof(InstanceTransformData));
      }

      Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
      Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
      if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
        Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
        Diligent::Uint64 offsets[] = {0, 0};
        context_->SetVertexBuffers(0,
                                   2,
                                   vbs,
                                   offsets,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        bound_mesh_vb = mesh_vb;
        bound_instance_vb = instance_vb;
      }

      if (batch.key.indexed) {
        if (!is_valid_indexed_draw(mesh, batch.key.index_offset, batch.key.index_count)) {
          continue;
        }
        Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
        if (index_buffer != bound_index_buffer) {
          context_->SetIndexBuffer(mesh.index_buffer, 0,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          bound_index_buffer = index_buffer;
        }
        Diligent::DrawIndexedAttribs indexed{};
        indexed.IndexType = Diligent::VT_UINT32;
        indexed.NumIndices = batch.key.index_count;
        indexed.FirstIndexLocation = batch.key.index_offset;
        indexed.NumInstances = static_cast<Diligent::Uint32>(filtered_shadow_transforms.size());
        indexed.Flags = kHotPathDrawFlags;
        context_->DrawIndexed(indexed);
      } else {
        Diligent::DrawAttribs draw_attrs{};
        draw_attrs.NumVertices = mesh.vertex_count;
        draw_attrs.NumInstances = static_cast<Diligent::Uint32>(filtered_shadow_transforms.size());
        draw_attrs.Flags = kHotPathDrawFlags;
        context_->Draw(draw_attrs);
      }
    }
  };

  if (render_directional_shadows) {
    Diligent::Viewport shadow_viewport{};
    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(shadow_map_size_);
    shadow_viewport.Height = static_cast<float>(shadow_map_size_);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;
    context_->SetViewports(1, &shadow_viewport, static_cast<Diligent::Uint32>(shadow_map_size_),
                           static_cast<Diligent::Uint32>(shadow_map_size_));
    context_->SetPipelineState(shadow_pipeline_state_);
    if (shadow_srb_) {
      context_->CommitShaderResources(shadow_srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
      Diligent::ITextureView* cascade_dsv =
          shadow_map_dsv_cascades_[cascade] ? shadow_map_dsv_cascades_[cascade].RawPtr()
                                            : shadow_map_dsv_.RawPtr();
      if (!cascade_dsv) {
        continue;
      }

      context_->SetRenderTargets(0, nullptr, cascade_dsv,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      context_->ClearDepthStencil(cascade_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      DrawConstants shadow_constants{};
      copyMat4(shadow_constants.mvp, cascade_light_view_proj[cascade]);
      copyMat4(shadow_constants.light_view_proj, cascade_light_view_proj[cascade]);
      copyMat4(shadow_constants.shadow_uv_proj, cascade_shadow_uv_proj[cascade]);
      for (int idx = 0; idx < kShadowCascadeCount; ++idx) {
        copyMat4(shadow_constants.shadow_cascade_uv_proj[idx], cascade_shadow_uv_proj[idx]);
        shadow_constants.shadow_cascade_splits[idx] = cascade_splits[idx];
        shadow_constants.shadow_cascade_world_texel[idx] = cascade_world_texel[idx];
      }
      shadow_constants.shadow_cascade_params[0] = 0.08f;
      shadow_constants.shadow_cascade_params[1] = 0.0f;
      shadow_constants.shadow_cascade_params[2] = 0.0f;
      shadow_constants.shadow_cascade_params[3] = 0.0f;
      shadow_constants.shadow_params[0] = 0.0f;
      shadow_constants.shadow_params[1] = fixed_bias;
      shadow_constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
      shadow_constants.shadow_params[3] = shadow_texel_param;
      shadow_constants.point_shadow_params[0] = 0.0f;
      shadow_constants.point_shadow_params[1] = point_shadow_texel_size;
      shadow_constants.point_shadow_params[2] = static_cast<float>(point_shadow_map_size_);
      shadow_constants.point_shadow_params[3] = 0.0f;
      shadow_constants.local_light_params[0] = local_light_distance_damping_;
      shadow_constants.local_light_params[1] = local_light_range_exponent_;
      shadow_constants.local_light_params[2] = ao_affects_local_lights_ ? 1.0f : 0.0f;
      shadow_constants.local_light_params[3] = local_light_directional_shadow_lift_;
      shadow_constants.point_shadow_tuning[0] = point_shadow_constant_bias_;
      shadow_constants.point_shadow_tuning[1] = point_shadow_slope_bias_scale_;
      shadow_constants.point_shadow_tuning[2] = point_shadow_normal_bias_scale_;
      shadow_constants.point_shadow_tuning[3] = point_shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[0] = shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[1] = shadow_normal_bias_scale_;
      shadow_constants.shadow_bias_params[2] = cascade_world_texel[cascade];
      shadow_constants.shadow_bias_params[3] = 0.0f;
      shadow_constants.forward_plus_params[0] = 0.0f;
      shadow_constants.forward_plus_params[1] = 0.0f;
      shadow_constants.forward_plus_params[2] = 0.0f;
      shadow_constants.forward_plus_params[3] = 0.0f;
      shadow_constants.camera_forward[0] = cam_forward.x;
      shadow_constants.camera_forward[1] = cam_forward.y;
      shadow_constants.camera_forward[2] = cam_forward.z;
      shadow_constants.camera_forward[3] = 0.0f;
      const glm::mat4 cascade_cull_matrix = cascade_light_view_proj[cascade];
      draw_shadow_batches(shadow_constants,
                          [&](const glm::vec4& sphere) {
                            return sphereIntersectsClipVolume(cascade_cull_matrix, sphere, is_gl);
                          });
    }

    if (shadow_map_tex_) {
      Diligent::StateTransitionDesc barrier{};
      barrier.pResource = shadow_map_tex_;
      barrier.OldState = Diligent::RESOURCE_STATE_DEPTH_WRITE;
      barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
      barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
      context_->TransitionResourceStates(1, &barrier);
    }

    cached_cascade_light_view_proj_ = cascade_light_view_proj;
    cached_cascade_shadow_uv_proj_ = cascade_shadow_uv_proj;
    cached_cascade_world_texel_ = cascade_world_texel;
    cached_cascade_splits_ = cascade_splits;
    cached_shadow_camera_position_ = camera_.position;
    cached_shadow_camera_forward_ = cam_forward;
    cached_shadow_light_direction_ = shadow_light_dir;
    cached_shadow_camera_aspect_ = aspect;
    cached_shadow_camera_fov_y_degrees_ = camera_.fov_y_degrees;
    cached_shadow_camera_near_ = camera_.near_clip;
    cached_shadow_camera_far_ = camera_.far_clip;
    cached_shadow_camera_perspective_ = camera_.perspective;
    directional_shadow_cache_valid_ = true;
  }

  if (render_point_shadows) {
    Diligent::Viewport shadow_viewport{};
    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(point_shadow_map_size_);
    shadow_viewport.Height = static_cast<float>(point_shadow_map_size_);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;
    context_->SetViewports(1, &shadow_viewport,
                           static_cast<Diligent::Uint32>(point_shadow_map_size_),
                           static_cast<Diligent::Uint32>(point_shadow_map_size_));
    context_->SetPipelineState(shadow_pipeline_state_);
    if (shadow_srb_) {
      context_->CommitShaderResources(shadow_srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    const Diligent::Uint32 active_face_count =
        point_shadow_light_count * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
    for (Diligent::Uint32 matrix_idx = 0; matrix_idx < active_face_count; ++matrix_idx) {
      if (point_shadow_faces_to_update[matrix_idx] == 0u) {
        continue;
      }
      const Diligent::Uint32 point_idx =
          matrix_idx / static_cast<Diligent::Uint32>(kPointShadowFaceCount);
      const int face =
          static_cast<int>(matrix_idx % static_cast<Diligent::Uint32>(kPointShadowFaceCount));
      if (point_idx >= point_shadow_light_count) {
        continue;
      }

      const renderer::LightData& point_light = point_shadow_lights[point_idx];
      const float range_ws = std::max(point_light.range, 0.1f);
      const float near_plane = std::max(range_ws * 0.02f, 0.05f);
      const float far_plane = std::max(range_ws, near_plane + 0.1f);
      const glm::mat4 face_proj = glm::perspective(glm::radians(90.0f), 1.0f, near_plane, far_plane);
      const glm::mat4 face_view =
          glm::lookAt(point_light.position,
                      point_light.position + kPointShadowFaceDirs[face],
                      kPointShadowFaceUps[face]);
      const glm::mat4 face_light_view_proj = depth_fix * face_proj * face_view;
      point_shadow_uv_proj[matrix_idx] = uv_bias * uv_scale * face_light_view_proj;
      cached_point_shadow_uv_proj_[matrix_idx] = point_shadow_uv_proj[matrix_idx];

      Diligent::ITextureView* face_dsv =
          point_shadow_map_dsv_faces_[matrix_idx] ? point_shadow_map_dsv_faces_[matrix_idx].RawPtr()
                                                  : nullptr;
      if (!face_dsv) {
        continue;
      }
      context_->SetRenderTargets(0, nullptr, face_dsv,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      context_->ClearDepthStencil(face_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      DrawConstants shadow_constants{};
      copyMat4(shadow_constants.mvp, face_light_view_proj);
      copyMat4(shadow_constants.light_view_proj, face_light_view_proj);
      copyMat4(shadow_constants.shadow_uv_proj, point_shadow_uv_proj[matrix_idx]);
      for (int idx = 0; idx < kShadowCascadeCount; ++idx) {
        copyMat4(shadow_constants.shadow_cascade_uv_proj[idx], cascade_shadow_uv_proj[idx]);
        shadow_constants.shadow_cascade_splits[idx] = cascade_splits[idx];
        shadow_constants.shadow_cascade_world_texel[idx] = cascade_world_texel[idx];
      }
      shadow_constants.shadow_cascade_params[0] = 0.08f;
      shadow_constants.shadow_cascade_params[1] = 0.0f;
      shadow_constants.shadow_cascade_params[2] = 0.0f;
      shadow_constants.shadow_cascade_params[3] = 0.0f;
      shadow_constants.shadow_params[0] = 0.0f;
      shadow_constants.shadow_params[1] = fixed_bias;
      shadow_constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
      shadow_constants.shadow_params[3] = point_shadow_texel_size;
      shadow_constants.point_shadow_params[0] = 0.0f;
      shadow_constants.point_shadow_params[1] = point_shadow_texel_size;
      shadow_constants.point_shadow_params[2] = static_cast<float>(point_shadow_map_size_);
      shadow_constants.point_shadow_params[3] = static_cast<float>(point_shadow_light_count);
      shadow_constants.local_light_params[0] = local_light_distance_damping_;
      shadow_constants.local_light_params[1] = local_light_range_exponent_;
      shadow_constants.local_light_params[2] = ao_affects_local_lights_ ? 1.0f : 0.0f;
      shadow_constants.local_light_params[3] = local_light_directional_shadow_lift_;
      shadow_constants.point_shadow_tuning[0] = point_shadow_constant_bias_;
      shadow_constants.point_shadow_tuning[1] = point_shadow_slope_bias_scale_;
      shadow_constants.point_shadow_tuning[2] = point_shadow_normal_bias_scale_;
      shadow_constants.point_shadow_tuning[3] = point_shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[0] = shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[1] = shadow_normal_bias_scale_;
      shadow_constants.shadow_bias_params[2] = range_ws / safe_point_shadow_map_extent;
      shadow_constants.shadow_bias_params[3] = 0.0f;
      shadow_constants.forward_plus_params[0] = 0.0f;
      shadow_constants.forward_plus_params[1] = 0.0f;
      shadow_constants.forward_plus_params[2] = 0.0f;
      shadow_constants.forward_plus_params[3] = 0.0f;
      shadow_constants.camera_forward[0] = cam_forward.x;
      shadow_constants.camera_forward[1] = cam_forward.y;
      shadow_constants.camera_forward[2] = cam_forward.z;
      shadow_constants.camera_forward[3] = 0.0f;
      const glm::mat4 point_cull_matrix = face_light_view_proj;
      draw_shadow_batches(shadow_constants,
                          [&](const glm::vec4& sphere) {
                            if (sphere.w > 0.0f) {
                              const glm::vec3 delta =
                                  glm::vec3(sphere) - point_light.position;
                              const float max_dist = range_ws + sphere.w;
                              if (glm::dot(delta, delta) > max_dist * max_dist) {
                                return false;
                              }
                            }
                            return sphereIntersectsClipVolume(point_cull_matrix, sphere, is_gl);
                          });
      point_shadow_face_dirty_[matrix_idx] = 0u;
    }

    if (point_shadow_map_tex_) {
      Diligent::StateTransitionDesc barrier{};
      barrier.pResource = point_shadow_map_tex_;
      barrier.OldState = Diligent::RESOURCE_STATE_DEPTH_WRITE;
      barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
      barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
      context_->TransitionResourceStates(1, &barrier);
      point_shadow_ready = true;
    }
  }

  bool any_point_shadow_slot_valid = false;
  for (Diligent::Uint32 slot = 0; slot < point_shadow_light_count; ++slot) {
    bool slot_fully_updated = true;
    const Diligent::Uint32 face_base = slot * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
    for (Diligent::Uint32 face = 0;
         face < static_cast<Diligent::Uint32>(kPointShadowFaceCount);
         ++face) {
      if (point_shadow_face_dirty_[face_base + face] != 0u) {
        slot_fully_updated = false;
        break;
      }
    }
    if (slot_fully_updated) {
      point_shadow_slot_valid_[slot] = true;
    }
    any_point_shadow_slot_valid = any_point_shadow_slot_valid || point_shadow_slot_valid_[slot];
    const size_t local_light_index = point_shadow_local_light_indices[slot];
    if (local_light_index < forward_plus_lights_gpu.size()) {
      forward_plus_lights_gpu[local_light_index].spot_params[2] =
          point_shadow_slot_valid_[slot] ? static_cast<float>(slot) : -1.0f;
    }
  }
  if (cpu_forward_plus_ready) {
    const Diligent::Uint32 copy_count = static_cast<Diligent::Uint32>(std::min<size_t>(
        forward_plus_lights_gpu.size(), static_cast<size_t>(cpu_forward_plus_light_count)));
    for (Diligent::Uint32 i = 0; i < copy_count; ++i) {
      cpu_forward_plus_lights[i] = forward_plus_lights_gpu[i];
    }
  }
  if (forward_plus_ready && forward_plus_light_buffer_ && !forward_plus_lights_gpu.empty()) {
    context_->UpdateBuffer(forward_plus_light_buffer_,
                           0,
                           static_cast<Diligent::Uint32>(forward_plus_lights_gpu.size() *
                                                         sizeof(ForwardPlusGpuLight)),
                           forward_plus_lights_gpu.data(),
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
  point_shadow_cache_initialized_ =
      point_shadow_cache_initialized_ || any_point_shadow_slot_valid;
  point_shadow_ready = point_shadow_ready && any_point_shadow_slot_valid;

  auto* rtv = active_rtv;
  auto* dsv = active_dsv;
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(render_width);
  viewport.Height = static_cast<float>(render_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(render_width),
                         static_cast<Diligent::Uint32>(render_height));

  Diligent::IPipelineState* active_forward_pipeline =
      use_custom_shader_override ? camera_override_pipeline_state_.RawPtr() : pipeline_state_.RawPtr();
  if (!active_forward_pipeline) {
    return;
  }
  context_->SetPipelineState(active_forward_pipeline);

  static bool logged_frame = false;
  if (!logged_frame) {
    logged_frame = true;
  }

  Diligent::Uint32 draw_count = 0;
  Diligent::Uint32 skipped_hidden = 0;
  Diligent::Uint32 skipped_missing_vb = 0;
  Diligent::Uint32 skipped_missing_mesh = 0;
  Diligent::Uint32 skipped_layer = 0;

  const glm::mat4 view_proj = depth_fix * projection * view;
  DrawConstants base_constants{};
  copyMat4(base_constants.mvp, view_proj);
  copyMat4(base_constants.light_view_proj, light_view_proj);
  copyMat4(base_constants.shadow_uv_proj, shadow_uv_proj);
  for (int idx = 0; idx < kShadowCascadeCount; ++idx) {
    copyMat4(base_constants.shadow_cascade_uv_proj[idx], cascade_shadow_uv_proj[idx]);
    base_constants.shadow_cascade_splits[idx] = cascade_splits[idx];
    base_constants.shadow_cascade_world_texel[idx] = cascade_world_texel[idx];
  }
  for (int idx = 0; idx < kPointShadowMatrixCount; ++idx) {
    copyMat4(base_constants.point_shadow_uv_proj[idx], point_shadow_uv_proj[idx]);
  }
  base_constants.shadow_cascade_params[0] = 0.08f;
  base_constants.shadow_cascade_params[1] = 0.0f;
  base_constants.shadow_cascade_params[2] = 0.0f;
  base_constants.shadow_cascade_params[3] = 0.0f;
  bool has_shadow_cascade_dsv = false;
  for (const auto& dsv : shadow_map_dsv_cascades_) {
    if (dsv) {
      has_shadow_cascade_dsv = true;
      break;
    }
  }
  const bool shadow_ready = camera_renders_shadows && directional_shadow_cache_valid_ &&
                            shadow_pipeline_state_ && shadow_map_srv_ &&
                            has_shadow_cascade_dsv && shadow_sampler_;
  base_constants.shadow_params[0] = shadow_ready ? 1.0f : 0.0f;
  base_constants.shadow_params[1] = fixed_bias;
  base_constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
  base_constants.shadow_params[3] = shadow_texel_param;
  const bool point_shadow_enabled =
      camera_renders_shadows &&
      point_shadow_ready && point_shadow_map_srv_ && has_point_shadow_dsv &&
      point_shadow_light_count > 0;
  base_constants.point_shadow_params[0] = point_shadow_enabled ? 1.0f : 0.0f;
  base_constants.point_shadow_params[1] = point_shadow_texel_size;
  base_constants.point_shadow_params[2] = static_cast<float>(point_shadow_map_size_);
  base_constants.point_shadow_params[3] = static_cast<float>(point_shadow_light_count);
  base_constants.local_light_params[0] = local_light_distance_damping_;
  base_constants.local_light_params[1] = local_light_range_exponent_;
  base_constants.local_light_params[2] = ao_affects_local_lights_ ? 1.0f : 0.0f;
  base_constants.local_light_params[3] = local_light_directional_shadow_lift_;
  base_constants.point_shadow_tuning[0] = point_shadow_constant_bias_;
  base_constants.point_shadow_tuning[1] = point_shadow_slope_bias_scale_;
  base_constants.point_shadow_tuning[2] = point_shadow_normal_bias_scale_;
  base_constants.point_shadow_tuning[3] = point_shadow_receiver_bias_scale_;
  base_constants.shadow_bias_params[0] = shadow_receiver_bias_scale_;
  base_constants.shadow_bias_params[1] = shadow_normal_bias_scale_;
  base_constants.shadow_bias_params[2] = shadow_world_texel;
  base_constants.shadow_bias_params[3] = 0.0f;

  glm::vec3 light_dir = directional_light_.direction;
  if (glm::length(light_dir) < 1e-4f) {
    light_dir = glm::vec3(0.3f, 1.0f, 0.2f);
  }
  light_dir = glm::normalize(light_dir);
  base_constants.light_dir[0] = light_dir.x;
  base_constants.light_dir[1] = light_dir.y;
  base_constants.light_dir[2] = light_dir.z;
  base_constants.light_dir[3] = 0.0f;
  base_constants.light_color[0] = directional_light_.color.r * directional_light_.intensity;
  base_constants.light_color[1] = directional_light_.color.g * directional_light_.intensity;
  base_constants.light_color[2] = directional_light_.color.b * directional_light_.intensity;
  base_constants.light_color[3] = 1.0f;
  base_constants.camera_pos[0] = camera_.position.x;
  base_constants.camera_pos[1] = camera_.position.y;
  base_constants.camera_pos[2] = camera_.position.z;
  base_constants.camera_pos[3] = 1.0f;
  base_constants.camera_forward[0] = cam_forward.x;
  base_constants.camera_forward[1] = cam_forward.y;
  base_constants.camera_forward[2] = cam_forward.z;
  base_constants.camera_forward[3] = 0.0f;
  if (cpu_forward_plus_ready) {
    base_constants.forward_plus_params[0] = 1.0f;
    base_constants.forward_plus_params[1] = 1.0f;
    base_constants.forward_plus_params[2] = 1.0f;
    base_constants.forward_plus_params[3] = 0.0f;
    base_constants.local_light_meta[0] = static_cast<float>(cpu_forward_plus_light_count);
    base_constants.local_light_meta[1] = 0.0f;
    base_constants.local_light_meta[2] = 0.0f;
    base_constants.local_light_meta[3] = 0.0f;
    for (Diligent::Uint32 idx = 0; idx < cpu_forward_plus_light_count; ++idx) {
      std::memcpy(base_constants.local_light_position_range[idx],
                  cpu_forward_plus_lights[idx].position_range,
                  sizeof(cpu_forward_plus_lights[idx].position_range));
      std::memcpy(base_constants.local_light_direction_type[idx],
                  cpu_forward_plus_lights[idx].direction_type,
                  sizeof(cpu_forward_plus_lights[idx].direction_type));
      std::memcpy(base_constants.local_light_color_intensity[idx],
                  cpu_forward_plus_lights[idx].color_intensity,
                  sizeof(cpu_forward_plus_lights[idx].color_intensity));
      std::memcpy(base_constants.local_light_spot_params[idx],
                  cpu_forward_plus_lights[idx].spot_params,
                  sizeof(cpu_forward_plus_lights[idx].spot_params));
    }
  } else {
    base_constants.forward_plus_params[0] = static_cast<float>(active_forward_plus_tile_size);
    base_constants.forward_plus_params[1] = static_cast<float>(active_forward_plus_tiles_x);
    base_constants.forward_plus_params[2] = static_cast<float>(active_forward_plus_tiles_y);
    base_constants.forward_plus_params[3] =
        forward_plus_ready ? static_cast<float>(active_forward_plus_max_lights_per_tile) : 0.0f;
    base_constants.local_light_meta[0] = 0.0f;
    base_constants.local_light_meta[1] = 0.0f;
    base_constants.local_light_meta[2] = 0.0f;
    base_constants.local_light_meta[3] = 0.0f;
  }
  base_constants.env_params[0] = environment_intensity_;
  base_constants.env_params[1] = env_max_mip;
  base_constants.env_params[2] = static_cast<float>(env_debug_mode_);
  base_constants.env_params[3] = lighting_exposure_;

  thread_local std::vector<ForwardBatch> forward_batches;
  thread_local std::unordered_map<ForwardBatchKey, size_t, ForwardBatchKeyHash> forward_batch_lookup;
  forward_batches.clear();
  forward_batch_lookup.clear();
  forward_batches.reserve(instances_.size());
  forward_batch_lookup.reserve(instances_.size());

  auto append_forward_batch = [&](const ForwardBatchKey& key, const glm::mat4& transform) {
    auto it = forward_batch_lookup.find(key);
    if (it == forward_batch_lookup.end()) {
      const size_t idx = forward_batches.size();
      forward_batches.push_back(ForwardBatch{.key = key});
      forward_batch_lookup.emplace(key, idx);
      it = forward_batch_lookup.find(key);
    }
    forward_batches[it->second].transforms.push_back(packInstanceTransform(transform));
  };

  auto resolve_instance_material =
      [&](const InstanceRecord& instance,
          size_t submesh_index,
          renderer::MaterialId fallback_material) -> renderer::MaterialId {
    if (instance.material_set != renderer::kInvalidMaterialSet) {
      auto set_it = material_sets_.find(instance.material_set);
      if (set_it != material_sets_.end() &&
          set_it->second.source_mesh == instance.mesh &&
          submesh_index < set_it->second.materials.size()) {
        const renderer::MaterialId set_material = set_it->second.materials[submesh_index];
        if (set_material != renderer::kInvalidMaterial) {
          return set_material;
        }
      }
    }
    if (instance.material != renderer::kInvalidMaterial) {
      return instance.material;
    }
    return fallback_material;
  };

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
    if (mesh.bounds_radius > 0.0f) {
      const glm::vec4 world_bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);
      if (!sphereIntersectsClipVolume(view_proj, world_bounds_sphere, is_gl)) {
        skipped_hidden += 1;
        continue;
      }
    }

    const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
    if (!mesh.submeshes.empty()) {
      for (size_t submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index) {
        const auto& submesh = mesh.submeshes[submesh_index];
        const renderer::MaterialId mat_id =
            resolve_instance_material(instance, submesh_index, submesh.material);
        const ForwardBatchKey key{
            .mesh = instance.mesh,
            .material = mat_id,
            .index_offset = submesh.index_offset,
            .index_count = submesh.index_count,
            .indexed = indexed_mesh && submesh.index_count > 0,
        };
        append_forward_batch(key, instance.transform);
      }
    } else {
      const ForwardBatchKey key{
          .mesh = instance.mesh,
          .material = resolve_instance_material(instance, 0, renderer::kInvalidMaterial),
          .index_offset = 0,
          .index_count = mesh.index_count,
          .indexed = indexed_mesh,
      };
      append_forward_batch(key, instance.transform);
    }
  }
  std::sort(forward_batches.begin(),
            forward_batches.end(),
            [](const ForwardBatch& a, const ForwardBatch& b) {
              if (a.key.material != b.key.material) {
                return a.key.material < b.key.material;
              }
              if (a.key.mesh != b.key.mesh) {
                return a.key.mesh < b.key.mesh;
              }
              if (a.key.index_offset != b.key.index_offset) {
                return a.key.index_offset < b.key.index_offset;
              }
              if (a.key.index_count != b.key.index_count) {
                return a.key.index_count < b.key.index_count;
              }
              return static_cast<uint32_t>(a.key.indexed) < static_cast<uint32_t>(b.key.indexed);
            });

  const auto& adapter_info = device_->GetAdapterInfo();
  const bool disable_depth_prepass_for_driver =
      device_->GetDeviceInfo().IsVulkanDevice() &&
      adapter_info.Vendor == Diligent::ADAPTER_VENDOR_NVIDIA;
  const bool run_depth_prepass =
      depth_prepass_pipeline_state_ && dsv && forward_batches.size() > 1 &&
      !disable_depth_prepass_for_driver && !use_custom_shader_override;
  if (run_depth_prepass) {
    context_->SetRenderTargets(0, nullptr, dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->SetPipelineState(depth_prepass_pipeline_state_);
    {
      Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                Diligent::MAP_FLAG_DISCARD);
      *mapped = base_constants;
    }

    for (const auto& batch : forward_batches) {
      if (batch.transforms.empty()) {
        continue;
      }
      auto mesh_it = meshes_.find(batch.key.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }
      if (!ensure_instance_buffer(batch.transforms.size())) {
        continue;
      }
      {
        Diligent::MapHelper<InstanceTransformData> instance_map(context_, instance_vb_,
                                                                Diligent::MAP_WRITE,
                                                                Diligent::MAP_FLAG_DISCARD);
        std::memcpy(instance_map, batch.transforms.data(),
                    batch.transforms.size() * sizeof(InstanceTransformData));
      }

      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

      if (batch.key.indexed) {
        if (!is_valid_indexed_draw(mesh, batch.key.index_offset, batch.key.index_count)) {
          continue;
        }
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::DrawIndexedAttribs indexed{};
        indexed.IndexType = Diligent::VT_UINT32;
        indexed.NumIndices = batch.key.index_count;
        indexed.FirstIndexLocation = batch.key.index_offset;
        indexed.NumInstances = static_cast<Diligent::Uint32>(batch.transforms.size());
        indexed.Flags = kHotPathDrawFlags;
        context_->DrawIndexed(indexed);
      } else {
        Diligent::DrawAttribs draw_attrs{};
        draw_attrs.NumVertices = mesh.vertex_count;
        draw_attrs.NumInstances = static_cast<Diligent::Uint32>(batch.transforms.size());
        draw_attrs.Flags = kHotPathDrawFlags;
        context_->Draw(draw_attrs);
      }
    }

    context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(render_width),
                           static_cast<Diligent::Uint32>(render_height));
  }
  context_->SetPipelineState(active_forward_pipeline);
  Diligent::IShaderResourceBinding* bound_forward_srb = nullptr;
  Diligent::IBuffer* bound_mesh_vb = nullptr;
  Diligent::IBuffer* bound_instance_vb = nullptr;
  Diligent::IBuffer* bound_index_buffer = nullptr;
  renderer::MaterialId last_constants_material = renderer::kInvalidMaterial;
  renderer::MeshId last_constants_mesh = renderer::kInvalidMesh;

  {
    Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                              Diligent::MAP_FLAG_DISCARD);
    *mapped = base_constants;
  }
  if (use_custom_shader_override && camera_override_srb_) {
    context_->CommitShaderResources(camera_override_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    bound_forward_srb = camera_override_srb_;
  }

  for (const auto& batch : forward_batches) {
    if (batch.transforms.empty()) {
      continue;
    }
    auto mesh_it = meshes_.find(batch.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    const MaterialRecord* mat = nullptr;
    if (batch.key.material != renderer::kInvalidMaterial) {
      auto mat_it = materials_.find(batch.key.material);
      if (mat_it != materials_.end()) {
        mat = &mat_it->second;
      }
    }

    if (batch.key.material != last_constants_material ||
        (batch.key.material == renderer::kInvalidMaterial && batch.key.mesh != last_constants_mesh)) {
      DrawConstants constants = base_constants;
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
      {
        Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                  Diligent::MAP_FLAG_DISCARD);
        *mapped = constants;
      }
      last_constants_material = batch.key.material;
      last_constants_mesh = batch.key.mesh;
    }

    if (!use_custom_shader_override) {
      Diligent::IShaderResourceBinding* srb = shader_resources_;
      if (mat && mat->srb) {
        srb = mat->srb;
      } else if (default_material_srb_) {
        srb = default_material_srb_;
      }
      if (srb && srb != bound_forward_srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = srb;
      }
    }

    if (!ensure_instance_buffer(batch.transforms.size())) {
      continue;
    }
    {
      Diligent::MapHelper<InstanceTransformData> instance_map(context_, instance_vb_,
                                                              Diligent::MAP_WRITE,
                                                              Diligent::MAP_FLAG_DISCARD);
      std::memcpy(instance_map, batch.transforms.data(),
                  batch.transforms.size() * sizeof(InstanceTransformData));
    }

    Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
    Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
    if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      bound_mesh_vb = mesh_vb;
      bound_instance_vb = instance_vb;
    }

    if (batch.key.indexed) {
      if (!is_valid_indexed_draw(mesh, batch.key.index_offset, batch.key.index_count)) {
        continue;
      }
      Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
      if (index_buffer != bound_index_buffer) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_index_buffer = index_buffer;
      }
      Diligent::DrawIndexedAttribs indexed{};
      indexed.IndexType = Diligent::VT_UINT32;
      indexed.NumIndices = batch.key.index_count;
      indexed.FirstIndexLocation = batch.key.index_offset;
      indexed.NumInstances = static_cast<Diligent::Uint32>(batch.transforms.size());
      indexed.Flags = kHotPathDrawFlags;
      context_->DrawIndexed(indexed);
    } else {
      Diligent::DrawAttribs draw_attrs{};
      draw_attrs.NumVertices = mesh.vertex_count;
      draw_attrs.NumInstances = static_cast<Diligent::Uint32>(batch.transforms.size());
      draw_attrs.Flags = kHotPathDrawFlags;
      context_->Draw(draw_attrs);
    }
    draw_count += 1;
  }

  auto draw_lines = [&](const std::vector<LineVertex>& lines,
                        Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
                        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
    if (lines.empty()) {
      return;
    }
    if (pso && line_cb_ && line_vb_) {
      if (lines.size() > line_vb_size_) {
        const size_t new_capacity =
            std::max(lines.size(), line_vb_size_ > 0 ? line_vb_size_ * 2 : static_cast<size_t>(1024));
        Diligent::BufferDesc vb_desc{};
        vb_desc.Name = "Karma Line VB";
        vb_desc.Usage = Diligent::USAGE_DYNAMIC;
        vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
        vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        vb_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(LineVertex));
        device_->CreateBuffer(vb_desc, nullptr, &line_vb_);
        if (!line_vb_) {
          line_vb_size_ = 0;
          return;
        }
        line_vb_size_ = new_capacity;
      }
      auto* rtv = active_rtv;
      auto* dsv = active_dsv;
      context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      Diligent::Viewport viewport{};
      viewport.TopLeftX = 0.0f;
      viewport.TopLeftY = 0.0f;
      viewport.Width = static_cast<float>(render_width);
      viewport.Height = static_cast<float>(render_height);
      viewport.MinDepth = 0.0f;
      viewport.MaxDepth = 1.0f;
      context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(render_width),
                             static_cast<Diligent::Uint32>(render_height));

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
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
    }
  };

  draw_lines(line_vertices_depth_, line_pipeline_state_depth_, line_srb_depth_);
  draw_lines(line_vertices_no_depth_, line_pipeline_state_no_depth_, line_srb_no_depth_);

  if (!warned_no_draws_) {
    warned_no_draws_ = true;
  }
}

bool DiligentBackend::ensureCameraOverridePipeline(const renderer::CameraData& camera) {
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
      {Diligent::SHADER_TYPE_PIXEL, "CameraOverrideUser", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
  };
  pso_ci.PSODesc.ResourceLayout.Variables = vars;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
  pso_ci.PSODesc.ResourceLayout.ImmutableSamplers = nullptr;
  pso_ci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;

  camera_override_pipeline_state_ = device_with_cache_.CreateGraphicsPipelineState(pso_ci);
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
  camera_override_pipeline_state_->CreateShaderResourceBinding(&camera_override_srb_, true);
  camera_override_vertex_path_ = camera.shader_override_vertex_path;
  camera_override_fragment_path_ = camera.shader_override_fragment_path;
  return true;
}

void DiligentBackend::updateCameraOverrideUserConstants(const renderer::CameraData& camera) {
  if (!context_ || !camera_override_user_constants_) {
    return;
  }
  CameraOverrideUserConstants constants{};
  const uint32_t count =
      std::min(camera.shader_user_param_count, renderer::kCameraShaderUserParamCapacity);
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
  *mapped = constants;
}

unsigned int DiligentBackend::getRenderTargetTextureId(renderer::RenderTargetId target) const {
  if (target == renderer::kDefaultRenderTarget) {
    return 0u;
  }
  auto it = targets_.find(target);
  if (it == targets_.end() || !it->second.color_srv) {
    return 0u;
  }
  if ((target & kRenderTargetTextureHandleBit) != 0u) {
    return 0u;
  }
  return static_cast<unsigned int>(target | kRenderTargetTextureHandleBit);
}

void DiligentBackend::setCamera(const renderer::CameraData& camera) {
  camera_ = camera;
}

void DiligentBackend::setCameraActive(bool active) {
  camera_active_ = active;
}

void DiligentBackend::setDirectionalLight(const renderer::DirectionalLightData& light) {
  directional_light_ = light;
  if (glm::length(directional_light_.direction) < 1e-4f) {
    directional_light_.direction = glm::vec3(0.3f, -1.0f, 0.2f);
  } else {
    directional_light_.direction = glm::normalize(directional_light_.direction);
  }
  // Directional sun lights should point toward the scene (negative Y in world-up convention).
  if (directional_light_.direction.y > 0.0f) {
    directional_light_.direction = -directional_light_.direction;
  }
}

void DiligentBackend::setLights(const std::vector<renderer::LightData>& lights) {
  lights_ = lights;
  for (auto& light : lights_) {
    if (light.intensity < 0.0f) {
      light.intensity = 0.0f;
    }
    if (light.range < 0.0f) {
      light.range = 0.0f;
    }
    if (glm::length(light.direction) < 1e-4f) {
      light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    } else {
      light.direction = glm::normalize(light.direction);
    }
    light.inner_cone_cos = std::clamp(light.inner_cone_cos, -1.0f, 1.0f);
    light.outer_cone_cos = std::clamp(light.outer_cone_cos, -1.0f, 1.0f);
    if (light.inner_cone_cos < light.outer_cone_cos) {
      std::swap(light.inner_cone_cos, light.outer_cone_cos);
    }
  }
}

void DiligentBackend::setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                        bool draw_skybox) {
  environment_intensity_ = intensity;
  environment_map_ = path;
  draw_skybox_ = draw_skybox;
  env_dirty_ = true;
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

  auto bind_env_to_srb = [&](Diligent::IShaderResourceBinding* srb) {
    if (!srb) {
      return;
    }
    auto* irr = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex");
    auto* pre = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex");
    auto* brdf = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT");
    if (!irr) {
    }
    if (!pre) {
    }
    if (!brdf) {
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
  };

  bind_env_to_srb(shader_resources_);
  bind_env_to_srb(default_material_srb_);
  for (auto& entry : materials_) {
    bind_env_to_srb(entry.second.srb);
  }
}

void DiligentBackend::setVsync(bool enabled) {
  vsync_enabled_ = enabled;
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

void DiligentBackend::setForwardPlusSettings(int tile_size, int max_lights_per_tile) {
  forward_plus_tile_size_ = std::clamp(tile_size, 8, 64);
  forward_plus_max_lights_per_tile_ = std::clamp(max_lights_per_tile, 8, 512);
  forward_plus_stats_.tile_size = static_cast<uint32_t>(forward_plus_tile_size_);
  forward_plus_stats_.max_lights_per_tile =
      static_cast<uint32_t>(forward_plus_max_lights_per_tile_);
}

renderer::ForwardPlusStats DiligentBackend::getForwardPlusStats() const {
  return forward_plus_stats_;
}

void DiligentBackend::setShadowSettings(float bias,
                                        int map_size,
                                        int pcf_radius,
                                        int raster_depth_bias,
                                        float raster_slope_bias,
                                        float receiver_bias_scale,
                                        float normal_bias_scale) {
  shadow_bias_ = std::max(0.0f, bias);
  shadow_pcf_radius_ = std::clamp(pcf_radius, 0, 4);
  shadow_receiver_bias_scale_ = std::clamp(receiver_bias_scale, 0.0f, 16.0f);
  shadow_normal_bias_scale_ = std::clamp(normal_bias_scale, 0.0f, 16.0f);

  const int clamped_depth_bias = std::clamp(raster_depth_bias, -65536, 65536);
  const float clamped_slope_bias = std::clamp(raster_slope_bias, -64.0f, 64.0f);
  const bool raster_bias_changed = clamped_depth_bias != shadow_raster_depth_bias_ ||
                                   clamped_slope_bias != shadow_raster_slope_bias_;
  if (raster_bias_changed) {
    shadow_raster_depth_bias_ = clamped_depth_bias;
    shadow_raster_slope_bias_ = clamped_slope_bias;
    recreateShadowPipeline();
    directional_shadow_cache_valid_ = false;
    point_shadow_cache_initialized_ = false;
    point_shadow_slot_valid_.fill(false);
    point_shadow_face_dirty_.fill(1u);
  }

  const int clamped_size = std::max(256, map_size);
  if (clamped_size != shadow_map_size_) {
    shadow_map_size_ = clamped_size;
    point_shadow_map_size_ = std::max(256, shadow_map_size_ / 2);
    recreateShadowMap();
    recreatePointShadowMap();
  }
}

void DiligentBackend::setPointShadowSettings(float constant_bias,
                                             float slope_bias_scale,
                                             float normal_bias_scale,
                                             float receiver_bias_scale) {
  point_shadow_constant_bias_ = std::clamp(constant_bias, 0.0f, 0.05f);
  point_shadow_slope_bias_scale_ = std::clamp(slope_bias_scale, 0.0f, 16.0f);
  point_shadow_normal_bias_scale_ = std::clamp(normal_bias_scale, 0.0f, 16.0f);
  point_shadow_receiver_bias_scale_ = std::clamp(receiver_bias_scale, 0.0f, 8.0f);
}

void DiligentBackend::setLocalLightingSettings(float distance_damping,
                                               float range_falloff_exponent,
                                               bool ao_affects_local_lights,
                                               float directional_shadow_lift_strength) {
  local_light_distance_damping_ = std::clamp(distance_damping, 0.0f, 4.0f);
  local_light_range_exponent_ = std::clamp(range_falloff_exponent, 0.1f, 8.0f);
  ao_affects_local_lights_ = ao_affects_local_lights;
  local_light_directional_shadow_lift_ = std::clamp(directional_shadow_lift_strength, 0.0f, 8.0f);
}

void DiligentBackend::setExposure(float exposure) {
  lighting_exposure_ = std::clamp(exposure, 0.01f, 32.0f);
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
