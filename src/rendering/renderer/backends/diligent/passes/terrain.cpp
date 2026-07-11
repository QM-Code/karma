#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace karma::rendering::backend {
namespace {

struct alignas(16) TerrainConstants {
  float view_proj[16];
  float model[16];
  float light_dir[4];
  float light_color[4];
  float camera_pos[4];
  float tile_info[4];
  float render_params[4];
  float material_scales[4];
  float exposure_params[4];
  float camera_forward[4];
  float env_params[4];
  float shadow_params[4];
  float point_shadow_params[4];
  float local_light_params[4];
  float point_shadow_tuning[4];
  float shadow_bias_params[4];
  float shadow_cascade_splits[4];
  float shadow_cascade_world_texel[4];
  float shadow_cascade_params[4];
  float forward_plus_params[4];
  float local_light_position_range[64][4];
  float local_light_direction_type[64][4];
  float local_light_color_intensity[64][4];
  float local_light_spot_params[64][4];
  float local_light_meta[4];
  float shadow_cascade_uv_proj[4][16];
  float point_shadow_uv_proj[96][16];
  float layer_base_color[4][4];
  float layer_emissive[4][4];
  float layer_pbr[4][4];
  float layer_specular[4][4];
  float layer_flags[4][4];
};

static_assert(alignof(TerrainConstants) >= 16u);
static_assert(sizeof(TerrainConstants) % 16u == 0u);

struct TerrainVertex {
  float position[3] = {0.0f, 0.0f, 0.0f};
  float uv[2] = {0.0f, 0.0f};
};

struct FrustumPlane {
  glm::vec3 normal{0.0f};
  float distance = 0.0f;
};

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

static constexpr const char* kTerrainTessVS = R"(
struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
};

struct VSOutput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Pos = input.Pos;
    output.UV = input.UV;
    return output;
}
)";

static constexpr const char* kTerrainTessHS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
    float4 g_ExposureParams;
};

struct HSInput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

struct HSOutput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

struct HSConst
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

float2 projectToPixels(float3 local_pos)
{
    float4 world_pos = mul(g_Model, float4(local_pos, 1.0));
    float4 clip = mul(g_ViewProj, world_pos);
    float inv_w = 1.0 / max(abs(clip.w), 1.0e-5);
    float2 ndc = clip.xy * inv_w;
    return (ndc * 0.5 + 0.5) * g_RenderParams.yz;
}

float edgeTess(float3 a, float3 b)
{
    float pixels = length(projectToPixels(a) - projectToPixels(b));
    float target = max(g_RenderParams.x, 1.0);
    return clamp(pixels / target, 1.0, clamp(g_TileInfo.w, 1.0, 64.0));
}

HSConst patchConstants(InputPatch<HSInput, 3> patch, uint patch_id : SV_PrimitiveID)
{
    HSConst output;
    output.EdgeTess[0] = edgeTess(patch[1].Pos, patch[2].Pos);
    output.EdgeTess[1] = edgeTess(patch[2].Pos, patch[0].Pos);
    output.EdgeTess[2] = edgeTess(patch[0].Pos, patch[1].Pos);
    output.InsideTess = clamp((output.EdgeTess[0] + output.EdgeTess[1] + output.EdgeTess[2]) / 3.0,
                              1.0,
                              clamp(g_TileInfo.w, 1.0, 64.0));
    return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patchConstants")]
HSOutput main(InputPatch<HSInput, 3> patch,
              uint control_point_id : SV_OutputControlPointID,
              uint patch_id : SV_PrimitiveID)
{
    HSOutput output;
    output.Pos = patch[control_point_id].Pos;
    output.UV = patch[control_point_id].UV;
    return output;
}
)";

static constexpr const char* kTerrainTessDS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
    float4 g_ExposureParams;
};

Texture2D<float> g_HeightTexture;
SamplerState g_HeightSampler;

struct HSOutput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

struct HSConst
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

struct DSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float3 WorldNormal : TEXCOORD2;
};

[domain("tri")]
DSOutput main(HSConst input,
              const OutputPatch<HSOutput, 3> patch,
              float3 bary : SV_DomainLocation)
{
    float3 local_pos = patch[0].Pos * bary.x + patch[1].Pos * bary.y + patch[2].Pos * bary.z;
    float2 uv = patch[0].UV * bary.x + patch[1].UV * bary.y + patch[2].UV * bary.z;
    float h = g_HeightTexture.SampleLevel(g_HeightSampler, uv, 0.0);
    local_pos.y = h * g_TileInfo.y + g_TileInfo.z;
    float4 world_pos = mul(g_Model, float4(local_pos, 1.0));

    uint height_width;
    uint height_height;
    g_HeightTexture.GetDimensions(height_width, height_height);
    float2 texel = rcp(float2(max(height_width, 1u), max(height_height, 1u)));
    float h_left = g_HeightTexture.SampleLevel(g_HeightSampler, uv - float2(texel.x, 0.0), 0.0);
    float h_right = g_HeightTexture.SampleLevel(g_HeightSampler, uv + float2(texel.x, 0.0), 0.0);
    float h_down = g_HeightTexture.SampleLevel(g_HeightSampler, uv - float2(0.0, texel.y), 0.0);
    float h_up = g_HeightTexture.SampleLevel(g_HeightSampler, uv + float2(0.0, texel.y), 0.0);
    float3 local_dx = float3(2.0 * texel.x * g_TileInfo.x,
                             (h_right - h_left) * g_TileInfo.y,
                             0.0);
    float3 local_dz = float3(0.0,
                             (h_up - h_down) * g_TileInfo.y,
                             2.0 * texel.y * g_TileInfo.x);
    float3 world_dx = mul((float3x3)g_Model, local_dx);
    float3 world_dz = mul((float3x3)g_Model, local_dz);
    DSOutput output;
    output.Pos = mul(g_ViewProj, world_pos);
    output.UV = uv;
    output.WorldPos = world_pos.xyz;
    output.WorldNormal = normalize(cross(world_dz, world_dx));
    return output;
}
)";

static constexpr const char* kTerrainCpuVS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
    float4 g_ExposureParams;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float3 WorldNormal : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    float4 world_pos = mul(g_Model, float4(input.Pos, 1.0));
    VSOutput output;
    output.Pos = mul(g_ViewProj, world_pos);
    output.UV = input.UV;
    output.WorldPos = world_pos.xyz;
    output.WorldNormal = float3(0.0, 0.0, 0.0);
    return output;
}
)";

static constexpr const char* kTerrainPS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
    float4 g_ExposureParams;
    float4 g_CameraForward;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_PointShadowParams;
    float4 g_LocalLightParams;
    float4 g_PointShadowTuning;
    float4 g_ShadowBiasParams;
    float4 g_ShadowCascadeSplits;
    float4 g_ShadowCascadeWorldTexel;
    float4 g_ShadowCascadeParams;
    float4 g_ForwardPlusParams;
    float4 g_LocalLightPositionRange[64];
    float4 g_LocalLightDirectionType[64];
    float4 g_LocalLightColorIntensity[64];
    float4 g_LocalLightSpotParams[64];
    float4 g_LocalLightMeta;
    float4x4 g_ShadowCascadeUVProj[4];
    float4x4 g_PointShadowUVProj[96];
    float4 g_LayerBaseColor[4];
    float4 g_LayerEmissive[4];
    float4 g_LayerPbr[4];
    float4 g_LayerSpecular[4];
    float4 g_LayerFlags[4];
};

Texture2D<float4> g_ColorTexture;
Texture2D<float4> g_ControlTexture;
Texture2D<float4> g_MaterialAlbedo0;
Texture2D<float4> g_MaterialAlbedo1;
Texture2D<float4> g_MaterialAlbedo2;
Texture2D<float4> g_MaterialAlbedo3;
Texture2D<float4> g_MaterialNormal0;
Texture2D<float4> g_MaterialNormal1;
Texture2D<float4> g_MaterialNormal2;
Texture2D<float4> g_MaterialNormal3;
Texture2D<float4> g_MaterialMetallicRoughness0;
Texture2D<float4> g_MaterialMetallicRoughness1;
Texture2D<float4> g_MaterialMetallicRoughness2;
Texture2D<float4> g_MaterialMetallicRoughness3;
Texture2D<float4> g_MaterialOcclusion0;
Texture2D<float4> g_MaterialOcclusion1;
Texture2D<float4> g_MaterialOcclusion2;
Texture2D<float4> g_MaterialOcclusion3;
Texture2D<float4> g_MaterialEmissive0;
Texture2D<float4> g_MaterialEmissive1;
Texture2D<float4> g_MaterialEmissive2;
Texture2D<float4> g_MaterialEmissive3;
Texture2D<float4> g_MaterialSpecular0;
Texture2D<float4> g_MaterialSpecular1;
Texture2D<float4> g_MaterialSpecular2;
Texture2D<float4> g_MaterialSpecular3;
Texture2D<float4> g_MaterialSpecularColor0;
Texture2D<float4> g_MaterialSpecularColor1;
Texture2D<float4> g_MaterialSpecularColor2;
Texture2D<float4> g_MaterialSpecularColor3;
TextureCube g_IrradianceTex;
TextureCube g_PrefilterTex;
Texture2D g_BRDFLUT;
Texture2DArray<float> g_ShadowMap;
Texture2DArray<float> g_PointShadowMap;
SamplerState g_ColorSampler;
SamplerState g_MaterialSampler;
SamplerComparisonState g_ShadowSampler;

struct ForwardPlusLight
{
    float4 position_range;
    float4 direction_type;
    float4 color_intensity;
    float4 spot_params;
};

StructuredBuffer<ForwardPlusLight> g_ForwardPlusLights;
StructuredBuffer<uint> g_ForwardPlusTileLightCounts;
StructuredBuffer<uint> g_ForwardPlusTileLightIndices;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float3 WorldNormal : TEXCOORD2;
};

float DistributionGGX(float3 n, float3 h, float roughness)
{
    const float PI = 3.14159265;
    float a = max(roughness * roughness, 0.001);
    float a2 = a * a;
    float ndoth = max(dot(n, h), 0.0);
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1.0e-5);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r * 0.125;
    return ndotv / max(ndotv * (1.0 - k) + k, 1.0e-5);
}

float GeometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    return GeometrySchlickGGX(max(dot(n, v), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(n, l), 0.0), roughness);
}

float3 FresnelSchlick(float cos_theta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cos_theta), 5.0);
}

float3 FresnelSchlickRoughness(float cos_theta, float3 f0, float roughness)
{
    float3 grazing = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0);
    return f0 + (grazing - f0) * pow(saturate(1.0 - cos_theta), 5.0);
}

float3 EvaluatePbrLight(float3 n,
                        float3 v,
                        float3 l,
                        float3 radiance,
                        float3 base_color,
                        float metallic,
                        float roughness,
                        float3 dielectric_f0,
                        bool glossy_off)
{
    const float PI = 3.14159265;
    float ndotl = max(dot(n, l), 0.0);
    float ndotv = max(dot(n, v), 0.0);
    if (ndotl <= 0.0 || ndotv <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 h = normalize(v + l);
    float3 f0 = lerp(dielectric_f0, base_color, metallic);
    float3 fresnel = glossy_off ? float3(0.0, 0.0, 0.0)
                                : FresnelSchlick(max(dot(h, v), 0.0), f0);
    float3 specular = float3(0.0, 0.0, 0.0);
    if (!glossy_off)
    {
        float distribution = DistributionGGX(n, h, roughness);
        float geometry = GeometrySmith(n, v, l, roughness);
        specular = distribution * geometry * fresnel /
                   max(4.0 * ndotv * ndotl, 1.0e-4);
    }
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color / PI;
    return (diffuse + specular) * radiance * ndotl;
}

float SampleCascadeShadow(uint cascade_idx,
                          float3 world_pos,
                          float3 geom_n,
                          float3 light_dir,
                          float slope)
{
    float world_texel = max(g_ShadowCascadeWorldTexel[cascade_idx], 0.0);
    float normal_scale = max(g_ShadowBiasParams.y, 0.0);
    float receiver_scale = max(g_ShadowBiasParams.x, 0.0);
    float3 shadow_world_pos =
        world_pos + geom_n * world_texel * normal_scale * (0.4 + 1.2 * slope) +
        light_dir * world_texel * receiver_scale * (0.03 + 0.07 * slope);
    float4 shadow_uv_depth =
        mul(g_ShadowCascadeUVProj[cascade_idx], float4(shadow_world_pos, 1.0));
    shadow_uv_depth.xyz /= max(shadow_uv_depth.w, 1.0e-7);
    float2 uv = shadow_uv_depth.xy;
    float depth = max(shadow_uv_depth.z, 1.0e-7);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    int radius = clamp((int)g_ShadowParams.z, 0, 4);
    float receiver_plane_bias = abs(ddx(depth)) + abs(ddy(depth));
    float bias = max(g_ShadowParams.y, 0.0) +
                 receiver_plane_bias * (0.5 * receiver_scale) +
                 max(g_ShadowParams.w, 0.0) * normal_scale * (0.45 + 0.9 * slope);
    bias = min(bias, 0.01);
    if (radius == 0)
    {
        return g_ShadowMap.SampleCmpLevelZero(
            g_ShadowSampler, float3(uv, (float)cascade_idx), depth - bias);
    }

    float sum = 0.0;
    float weight_sum = 0.0;
    float2 texel = g_ShadowParams.ww;
    [loop]
    for (int y = -radius; y <= radius; ++y)
    {
        [loop]
        for (int x = -radius; x <= radius; ++x)
        {
            float weight = (float)(radius + 1 - abs(x)) *
                           (float)(radius + 1 - abs(y));
            sum += weight * g_ShadowMap.SampleCmpLevelZero(
                                g_ShadowSampler,
                                float3(uv + float2((float)x, (float)y) * texel,
                                       (float)cascade_idx),
                                depth - bias);
            weight_sum += weight;
        }
    }
    return weight_sum > 0.0 ? sum / weight_sum : 1.0;
}

uint SelectPointShadowFace(float3 direction)
{
    float3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) return direction.x >= 0.0 ? 0u : 1u;
    if (a.y >= a.x && a.y >= a.z) return direction.y >= 0.0 ? 2u : 3u;
    return direction.z >= 0.0 ? 4u : 5u;
}

float SampleLocalShadow(ForwardPlusLight light,
                        float3 world_pos,
                        float3 geom_n,
                        float3 light_dir)
{
    if (g_PointShadowParams.x < 0.5 ||
        light.direction_type.w < 0.5 || light.direction_type.w > 2.5 ||
        light.spot_params.z < 0.0)
    {
        return 1.0;
    }
    uint slot = (uint)(light.spot_params.z + 0.5);
    uint active_slots = (uint)max(g_PointShadowParams.w, 0.0);
    if (slot >= active_slots || slot >= 16u)
    {
        return 1.0;
    }

    float texel_size = max(g_PointShadowParams.y, 0.0);
    float slope = 1.0 - saturate(dot(geom_n, light_dir));
    float3 shadow_world_pos =
        world_pos + geom_n * texel_size * max(g_PointShadowTuning.z, 0.0) *
                        (0.5 + slope);
    float3 to_sample = shadow_world_pos - light.position_range.xyz;
    uint face = SelectPointShadowFace(to_sample);
    uint matrix_idx = slot * 6u + face;
    float4 shadow_uv_depth =
        mul(g_PointShadowUVProj[matrix_idx], float4(shadow_world_pos, 1.0));
    shadow_uv_depth.xyz /= max(shadow_uv_depth.w, 1.0e-7);
    float2 uv = shadow_uv_depth.xy;
    float depth = max(shadow_uv_depth.z, 1.0e-7);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    float bias = max(g_PointShadowTuning.x, 0.0) +
                 texel_size * max(g_PointShadowTuning.y, 0.0) * (0.4 + slope) +
                 (abs(ddx(depth)) + abs(ddy(depth))) *
                     max(g_PointShadowTuning.w, 0.0);
    bias = min(bias, 0.04);
    float sum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            sum += g_PointShadowMap.SampleCmpLevelZero(
                g_ShadowSampler,
                float3(uv + float2((float)x, (float)y) * texel_size,
                       (float)matrix_idx),
                depth - bias);
        }
    }
    return sum / 9.0;
}

void AccumulateLocalLight(ForwardPlusLight light,
                          float3 world_pos,
                          float3 geom_n,
                          float3 n,
                          float3 v,
                          float3 base_color,
                          float metallic,
                          float roughness,
                          float3 dielectric_f0,
                          bool glossy_off,
                          inout float3 lit,
                          inout float shadow_lift_energy)
{
    float3 to_light = light.position_range.xyz - world_pos;
    float distance = length(to_light);
    if (distance <= 1.0e-4 || distance >= light.position_range.w)
    {
        return;
    }
    float3 l = to_light / distance;
    float ndotl = max(dot(n, l), 0.0);
    if (ndotl <= 0.0)
    {
        return;
    }

    float range_t = saturate(distance / light.position_range.w);
    float range_falloff = saturate(1.0 - range_t);
    range_falloff = range_falloff * range_falloff *
                    (3.0 - 2.0 * range_falloff);
    range_falloff = pow(range_falloff, max(g_LocalLightParams.y, 0.1));
    float attenuation = range_falloff /
                        max(dot(to_light, to_light) + max(g_LocalLightParams.x, 0.0),
                            1.0e-4);
    if (light.direction_type.w > 1.5)
    {
        float3 spot_direction = normalize(-light.direction_type.xyz);
        float cone = dot(spot_direction, l);
        float cone_width = max(light.spot_params.x - light.spot_params.y, 1.0e-4);
        attenuation *= saturate((cone - light.spot_params.y) / cone_width);
    }
    if (attenuation <= 0.0)
    {
        return;
    }

    float shadow = SampleLocalShadow(light, world_pos, geom_n, l);
    float3 radiance =
        light.color_intensity.rgb * light.color_intensity.w * attenuation * shadow;
    lit += EvaluatePbrLight(n,
                            v,
                            l,
                            radiance,
                            base_color,
                            metallic,
                            roughness,
                            dielectric_f0,
                            glossy_off);
    shadow_lift_energy += dot(radiance, float3(0.2126, 0.7152, 0.0722)) * ndotl;
}

float3 LayerNormal(float3 sample_value, float normal_scale, float normal_y_sign)
{
    float3 mapped = sample_value * 2.0 - 1.0;
    mapped.xy *= max(normal_scale, 0.0);
    mapped.y *= normal_y_sign < 0.0 ? -1.0 : 1.0;
    float length_sq = dot(mapped, mapped);
    return length_sq > 1.0e-8
        ? mapped * rsqrt(length_sq)
        : float3(0.0, 0.0, 1.0);
}

float4 main(PSInput input) : SV_TARGET
{
    uint editor_view_mode = min((uint)round(max(g_ExposureParams.y, 0.0)), 3u);
    if (editor_view_mode == 3u)
    {
        return float4(0.22, 0.82, 1.0, 1.0);
    }

    float4 macro = g_ColorTexture.Sample(g_ColorSampler, input.UV);
    float4 albedo = macro;
    float metallic = 0.0;
    float roughness = 1.0;
    float occlusion = 1.0;
    float3 emissive = float3(0.0, 0.0, 0.0);
    float specular_weight = 1.0;
    float3 specular_color = float3(1.0, 1.0, 1.0);
    float3 geom_normal = dot(input.WorldNormal, input.WorldNormal) > 1.0e-6
        ? normalize(input.WorldNormal)
        : normalize(cross(ddy(input.WorldPos), ddx(input.WorldPos)));
    if (geom_normal.y < 0.0)
    {
        geom_normal = -geom_normal;
    }
    float3 normal = geom_normal;

    float layer_count = floor(g_RenderParams.w + 0.5);
    if (layer_count > 0.5)
    {
        float4 weights = g_ControlTexture.Sample(g_ColorSampler, input.UV);
        if (layer_count < 3.5) weights.a = 0.0;
        if (layer_count < 2.5) weights.b = 0.0;
        if (layer_count < 1.5) weights.g = 0.0;
        float weight_sum = weights.r + weights.g + weights.b + weights.a;
        weights = weight_sum > 1.0e-5
            ? weights / weight_sum
            : float4(1.0, 0.0, 0.0, 0.0);

        float2 detail_uv = (input.WorldPos.xz / max(g_TileInfo.x, 0.001));
        float2 uv0 = detail_uv * max(g_MaterialScales.x, 0.001);
        float2 uv1 = detail_uv * max(g_MaterialScales.y, 0.001);
        float2 uv2 = detail_uv * max(g_MaterialScales.z, 0.001);
        float2 uv3 = detail_uv * max(g_MaterialScales.w, 0.001);

        float4 a0 = g_MaterialAlbedo0.Sample(g_MaterialSampler, uv0);
        float4 a1 = g_MaterialAlbedo1.Sample(g_MaterialSampler, uv1);
        float4 a2 = g_MaterialAlbedo2.Sample(g_MaterialSampler, uv2);
        float4 a3 = g_MaterialAlbedo3.Sample(g_MaterialSampler, uv3);
        albedo = a0 * g_LayerBaseColor[0] * weights.r +
                 a1 * g_LayerBaseColor[1] * weights.g +
                 a2 * g_LayerBaseColor[2] * weights.b +
                 a3 * g_LayerBaseColor[3] * weights.a;
        albedo.rgb *= macro.rgb;
        albedo.a *= macro.a;

        float3 n0 = LayerNormal(g_MaterialNormal0.Sample(g_MaterialSampler, uv0).xyz,
                                g_LayerPbr[0].z, g_LayerFlags[0].x);
        float3 n1 = LayerNormal(g_MaterialNormal1.Sample(g_MaterialSampler, uv1).xyz,
                                g_LayerPbr[1].z, g_LayerFlags[1].x);
        float3 n2 = LayerNormal(g_MaterialNormal2.Sample(g_MaterialSampler, uv2).xyz,
                                g_LayerPbr[2].z, g_LayerFlags[2].x);
        float3 n3 = LayerNormal(g_MaterialNormal3.Sample(g_MaterialSampler, uv3).xyz,
                                g_LayerPbr[3].z, g_LayerFlags[3].x);
        float3 tangent_normal =
            n0 * weights.r + n1 * weights.g + n2 * weights.b + n3 * weights.a;
        tangent_normal = dot(tangent_normal, tangent_normal) > 1.0e-8
            ? normalize(tangent_normal)
            : float3(0.0, 0.0, 1.0);
        float3 tangent_candidate = float3(1.0, 0.0, 0.0) -
                                   geom_normal * geom_normal.x;
        if (dot(tangent_candidate, tangent_candidate) <= 1.0e-6)
        {
            tangent_candidate = float3(0.0, 0.0, 1.0) -
                                geom_normal * geom_normal.z;
        }
        float3 tangent = normalize(tangent_candidate);
        float3 bitangent = normalize(cross(tangent, geom_normal));
        normal = normalize(tangent_normal.x * tangent +
                           tangent_normal.y * bitangent +
                           tangent_normal.z * geom_normal);

        float4 mr0 = g_MaterialMetallicRoughness0.Sample(g_MaterialSampler, uv0);
        float4 mr1 = g_MaterialMetallicRoughness1.Sample(g_MaterialSampler, uv1);
        float4 mr2 = g_MaterialMetallicRoughness2.Sample(g_MaterialSampler, uv2);
        float4 mr3 = g_MaterialMetallicRoughness3.Sample(g_MaterialSampler, uv3);
        metallic = saturate(mr0.b * g_LayerPbr[0].x * weights.r +
                            mr1.b * g_LayerPbr[1].x * weights.g +
                            mr2.b * g_LayerPbr[2].x * weights.b +
                            mr3.b * g_LayerPbr[3].x * weights.a);
        roughness = saturate(mr0.g * g_LayerPbr[0].y * weights.r +
                            mr1.g * g_LayerPbr[1].y * weights.g +
                            mr2.g * g_LayerPbr[2].y * weights.b +
                            mr3.g * g_LayerPbr[3].y * weights.a);

        float ao0 = lerp(1.0, g_MaterialOcclusion0.Sample(g_MaterialSampler, uv0).r,
                         saturate(g_LayerPbr[0].w));
        float ao1 = lerp(1.0, g_MaterialOcclusion1.Sample(g_MaterialSampler, uv1).r,
                         saturate(g_LayerPbr[1].w));
        float ao2 = lerp(1.0, g_MaterialOcclusion2.Sample(g_MaterialSampler, uv2).r,
                         saturate(g_LayerPbr[2].w));
        float ao3 = lerp(1.0, g_MaterialOcclusion3.Sample(g_MaterialSampler, uv3).r,
                         saturate(g_LayerPbr[3].w));
        occlusion = saturate(ao0 * weights.r + ao1 * weights.g +
                             ao2 * weights.b + ao3 * weights.a);

        emissive =
            g_MaterialEmissive0.Sample(g_MaterialSampler, uv0).rgb *
                g_LayerEmissive[0].rgb * weights.r +
            g_MaterialEmissive1.Sample(g_MaterialSampler, uv1).rgb *
                g_LayerEmissive[1].rgb * weights.g +
            g_MaterialEmissive2.Sample(g_MaterialSampler, uv2).rgb *
                g_LayerEmissive[2].rgb * weights.b +
            g_MaterialEmissive3.Sample(g_MaterialSampler, uv3).rgb *
                g_LayerEmissive[3].rgb * weights.a;

        specular_weight = saturate(
            g_MaterialSpecular0.Sample(g_MaterialSampler, uv0).a *
                g_LayerSpecular[0].a * weights.r +
            g_MaterialSpecular1.Sample(g_MaterialSampler, uv1).a *
                g_LayerSpecular[1].a * weights.g +
            g_MaterialSpecular2.Sample(g_MaterialSampler, uv2).a *
                g_LayerSpecular[2].a * weights.b +
            g_MaterialSpecular3.Sample(g_MaterialSampler, uv3).a *
                g_LayerSpecular[3].a * weights.a);
        specular_color = saturate(
            g_MaterialSpecularColor0.Sample(g_MaterialSampler, uv0).rgb *
                g_LayerSpecular[0].rgb * weights.r +
            g_MaterialSpecularColor1.Sample(g_MaterialSampler, uv1).rgb *
                g_LayerSpecular[1].rgb * weights.g +
            g_MaterialSpecularColor2.Sample(g_MaterialSampler, uv2).rgb *
                g_LayerSpecular[2].rgb * weights.b +
            g_MaterialSpecularColor3.Sample(g_MaterialSampler, uv3).rgb *
                g_LayerSpecular[3].rgb * weights.a);
    }

    if (editor_view_mode == 2u)
    {
        return float4(saturate(albedo.rgb), albedo.a);
    }

    bool glossy_off = editor_view_mode == 1u;
    if (glossy_off)
    {
        metallic = 0.0;
        roughness = 1.0;
    }
    roughness = clamp(roughness, 0.045, 1.0);
    float3 dielectric_f0 = saturate(
        float3(0.04, 0.04, 0.04) * specular_color * specular_weight);
    float3 specular_f0 = lerp(dielectric_f0, albedo.rgb, metallic);
    float3 view_dir = normalize(g_CameraPos.xyz - input.WorldPos);
    float3 light_dir = normalize(-g_LightDir.xyz);

    float directional_shadow = 1.0;
    if (g_ShadowParams.x > 0.5)
    {
        float view_depth = max(dot(input.WorldPos - g_CameraPos.xyz,
                                   g_CameraForward.xyz), 0.0);
        uint cascade = 0u;
        if (view_depth > g_ShadowCascadeSplits.x) cascade = 1u;
        if (view_depth > g_ShadowCascadeSplits.y) cascade = 2u;
        if (view_depth > g_ShadowCascadeSplits.z) cascade = 3u;
        float slope = 1.0 - saturate(dot(geom_normal, light_dir));
        directional_shadow =
            SampleCascadeShadow(cascade, input.WorldPos, geom_normal, light_dir, slope);
        if (cascade < 3u)
        {
            float split_depth = g_ShadowCascadeSplits[cascade];
            float transition = max(split_depth * max(g_ShadowCascadeParams.x, 0.0), 0.25);
            float blend = saturate((view_depth - (split_depth - transition)) /
                                   max(transition, 1.0e-4));
            if (blend > 0.0)
            {
                float next_shadow = SampleCascadeShadow(
                    cascade + 1u, input.WorldPos, geom_normal, light_dir, slope);
                directional_shadow = lerp(directional_shadow, next_shadow, blend);
            }
        }
    }

    float3 local_lighting = float3(0.0, 0.0, 0.0);
    float shadow_lift_energy = 0.0;
    uint cb_light_count = min((uint)max(g_LocalLightMeta.x, 0.0), 64u);
    uint total_light_count = (uint)max(g_LocalLightMeta.y, 0.0);
    if (cb_light_count > 0u)
    {
        [loop]
        for (uint i = 0u; i < cb_light_count; ++i)
        {
            ForwardPlusLight light;
            light.position_range = g_LocalLightPositionRange[i];
            light.direction_type = g_LocalLightDirectionType[i];
            light.color_intensity = g_LocalLightColorIntensity[i];
            light.spot_params = g_LocalLightSpotParams[i];
            AccumulateLocalLight(light, input.WorldPos, geom_normal, normal, view_dir,
                                 albedo.rgb, metallic, roughness, dielectric_f0,
                                 glossy_off, local_lighting, shadow_lift_energy);
        }
    }
    else if (g_ForwardPlusParams.w > 0.0)
    {
        uint tile_size = (uint)max(g_ForwardPlusParams.x, 1.0);
        uint tiles_x = (uint)max(g_ForwardPlusParams.y, 1.0);
        uint tiles_y = (uint)max(g_ForwardPlusParams.z, 1.0);
        uint max_lights_per_tile = (uint)max(g_ForwardPlusParams.w, 0.0);
        uint2 pixel = uint2(input.Pos.xy);
        uint tile_x = min(pixel.x / tile_size, max(tiles_x, 1u) - 1u);
        uint tile_y = min(pixel.y / tile_size, max(tiles_y, 1u) - 1u);
        uint tile_index = tile_y * max(tiles_x, 1u) + tile_x;
        uint light_count = min(g_ForwardPlusTileLightCounts[tile_index],
                               max_lights_per_tile);
        light_count = min(light_count, total_light_count);
        uint base_index = tile_index * max_lights_per_tile;
        [loop]
        for (uint i = 0u; i < light_count; ++i)
        {
            uint light_index = g_ForwardPlusTileLightIndices[base_index + i];
            if (light_index < total_light_count)
            {
                ForwardPlusLight light = g_ForwardPlusLights[light_index];
                AccumulateLocalLight(light, input.WorldPos, geom_normal, normal, view_dir,
                                     albedo.rgb, metallic, roughness, dielectric_f0,
                                     glossy_off, local_lighting, shadow_lift_energy);
            }
        }
    }

    float shadow_lift = 1.0 - exp(-shadow_lift_energy *
                                  max(g_LocalLightParams.w, 0.0));
    float lifted_shadow = lerp(directional_shadow, 1.0, saturate(shadow_lift));
    float3 directional_radiance = g_LightColor.rgb * lifted_shadow;
    float3 lit = EvaluatePbrLight(normal,
                                  view_dir,
                                  light_dir,
                                  directional_radiance,
                                  albedo.rgb,
                                  metallic,
                                  roughness,
                                  dielectric_f0,
                                  glossy_off) * occlusion;
    float local_ao = lerp(1.0, occlusion, saturate(g_LocalLightParams.z));
    lit += local_lighting * local_ao;

    float3 env_diffuse = float3(0.0, 0.0, 0.0);
    if (g_EnvParams.x > 0.0 || g_EnvParams.z > 0.5)
    {
        env_diffuse = g_IrradianceTex.Sample(g_ColorSampler, normal).rgb *
                      g_EnvParams.x;
        float ndotv = max(dot(normal, view_dir), 0.0);
        float3 ibl_fresnel = glossy_off
            ? float3(0.0, 0.0, 0.0)
            : FresnelSchlickRoughness(ndotv, specular_f0, roughness);
        float3 ibl_diffuse_weight = (1.0 - ibl_fresnel) * (1.0 - metallic);
        lit += env_diffuse * albedo.rgb * ibl_diffuse_weight * occlusion;
        if (!glossy_off)
        {
            float3 reflection = reflect(-view_dir, normal);
            float mip = roughness * g_EnvParams.y;
            float3 prefiltered =
                g_PrefilterTex.SampleLevel(g_ColorSampler, reflection, mip).rgb;
            float2 brdf =
                g_BRDFLUT.Sample(g_ColorSampler, float2(ndotv, roughness)).rg;
            lit += prefiltered * (specular_f0 * brdf.x + brdf.y) * g_EnvParams.x;
        }
    }

    if (glossy_off)
    {
        float direct_matte = max(dot(normal, light_dir), 0.0);
        float3 matte_direct = g_LightColor.rgb * lifted_shadow * direct_matte;
        lit = albedo.rgb * (env_diffuse * occlusion + matte_direct) +
              local_lighting * local_ao;
    }
    lit += emissive;

    float exposure = abs(g_ExposureParams.x);
    lit = max(lit, float3(0.0, 0.0, 0.0));
    lit = g_ExposureParams.x < 0.0
        ? lit * exposure
        : 1.0 - exp(-lit * exposure);
    return float4(lit, albedo.a);
}
)";

bool terrainTessellationEnabled(Diligent::IRenderDevice* device) {
  return device != nullptr &&
         device->GetDeviceInfo().Features.Tessellation ==
             Diligent::DEVICE_FEATURE_STATE_ENABLED;
}

glm::vec4 matrixRow(const glm::mat4& matrix, int row) {
  return {
      matrix[0][row],
      matrix[1][row],
      matrix[2][row],
      matrix[3][row],
  };
}

FrustumPlane normalizePlane(const glm::vec4& plane) {
  const glm::vec3 normal{plane.x, plane.y, plane.z};
  const float len = glm::length(normal);
  if (len <= 1.0e-6f) {
    return {};
  }
  return {.normal = normal / len, .distance = plane.w / len};
}

std::array<FrustumPlane, 6> extractFrustumPlanes(const glm::mat4& clip_from_world,
                                                 bool is_gl_ndc) {
  const glm::vec4 row0 = matrixRow(clip_from_world, 0);
  const glm::vec4 row1 = matrixRow(clip_from_world, 1);
  const glm::vec4 row2 = matrixRow(clip_from_world, 2);
  const glm::vec4 row3 = matrixRow(clip_from_world, 3);
  return {
      normalizePlane(row3 + row0),
      normalizePlane(row3 - row0),
      normalizePlane(row3 + row1),
      normalizePlane(row3 - row1),
      normalizePlane(is_gl_ndc ? row3 + row2 : row2),
      normalizePlane(row3 - row2),
  };
}

bool sphereIntersectsFrustum(const std::array<FrustumPlane, 6>& planes,
                             const glm::vec4& sphere,
                             float guard_band) {
  if (sphere.w <= 0.0f) {
    return true;
  }
  const glm::vec3 center{sphere.x, sphere.y, sphere.z};
  const float radius = sphere.w + std::max(guard_band, 0.0f);
  for (const FrustumPlane& plane : planes) {
    if (glm::dot(plane.normal, center) + plane.distance < -radius) {
      return false;
    }
  }
  return true;
}

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max({sx, sy, sz, 1.0f});
}

std::vector<TerrainVertex> buildPatchVertices(
    const rendering::TerrainDesc& desc,
    uint32_t resolution) {
  const uint32_t step = std::max(desc.base_patch_size, 1u);
  const float inv = 1.0f / static_cast<float>(std::max(resolution - 1u, 1u));
  std::vector<TerrainVertex> vertices;
  const uint32_t patch_count_per_axis = (resolution + step - 2u) / step;
  vertices.reserve(static_cast<std::size_t>(patch_count_per_axis) *
                   static_cast<std::size_t>(patch_count_per_axis) * 6u);

  auto make_vertex = [&](uint32_t x, uint32_t z) {
    const float u = static_cast<float>(x) * inv;
    const float v = static_cast<float>(z) * inv;
    TerrainVertex vertex{};
    vertex.position[0] = u * desc.tile_size;
    vertex.position[1] = 0.0f;
    vertex.position[2] = v * desc.tile_size;
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    return vertex;
  };

  for (uint32_t z = 0u; z + 1u < resolution; z += step) {
    const uint32_t z1 = std::min(z + step, resolution - 1u);
    for (uint32_t x = 0u; x + 1u < resolution; x += step) {
      const uint32_t x1 = std::min(x + step, resolution - 1u);
      const auto v00 = make_vertex(x, z);
      const auto v10 = make_vertex(x1, z);
      const auto v11 = make_vertex(x1, z1);
      const auto v01 = make_vertex(x, z1);
      vertices.insert(vertices.end(), {v00, v11, v10, v00, v01, v11});
    }
  }
  return vertices;
}

void buildCpuMesh(const rendering::TerrainDesc& desc,
                  const rendering::TerrainTileData& tile,
                  std::vector<TerrainVertex>& vertices,
                  std::vector<uint32_t>& indices,
                  float& out_min_height,
                  float& out_max_height) {
  const uint32_t resolution = tile.resolution;
  const float inv = 1.0f / static_cast<float>(std::max(resolution - 1u, 1u));
  vertices.resize(static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution));
  out_min_height = std::numeric_limits<float>::max();
  out_max_height = -std::numeric_limits<float>::max();
  for (uint32_t z = 0u; z < resolution; ++z) {
    for (uint32_t x = 0u; x < resolution; ++x) {
      const float u = static_cast<float>(x) * inv;
      const float v = static_cast<float>(z) * inv;
      const float normalized =
          tile.heights[static_cast<std::size_t>(z) * resolution + x];
      const float height = normalized * desc.height_scale + desc.height_offset;
      out_min_height = std::min(out_min_height, height);
      out_max_height = std::max(out_max_height, height);
      auto& vertex = vertices[static_cast<std::size_t>(z) * resolution + x];
      vertex.position[0] = u * desc.tile_size;
      vertex.position[1] = height;
      vertex.position[2] = v * desc.tile_size;
      vertex.uv[0] = u;
      vertex.uv[1] = v;
    }
  }
  indices.clear();
  indices.reserve(static_cast<std::size_t>(resolution - 1u) *
                  static_cast<std::size_t>(resolution - 1u) * 6u);
  for (uint32_t z = 0u; z + 1u < resolution; ++z) {
    for (uint32_t x = 0u; x + 1u < resolution; ++x) {
      const uint32_t base = z * resolution + x;
      indices.insert(indices.end(),
                     {base, base + resolution + 1u, base + 1u,
                      base, base + resolution, base + resolution + 1u});
    }
  }
  if (out_min_height == std::numeric_limits<float>::max()) {
    out_min_height = desc.height_offset;
    out_max_height = desc.height_offset;
  }
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> createStaticBuffer(
    Diligent::IRenderDevice* device,
    const void* data,
    std::size_t byte_count,
    Diligent::BIND_FLAGS bind_flags,
    const char* name) {
  Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
  if (device == nullptr || data == nullptr || byte_count == 0u) {
    return buffer;
  }
  Diligent::BufferDesc desc{};
  desc.Name = name;
  desc.Usage = Diligent::USAGE_IMMUTABLE;
  desc.BindFlags = bind_flags;
  desc.Size = static_cast<Diligent::Uint64>(byte_count);
  Diligent::BufferData buffer_data{};
  buffer_data.pData = data;
  buffer_data.DataSize = byte_count;
  device->CreateBuffer(desc, &buffer_data, &buffer);
  return buffer;
}

uint64_t terrainPipelineKey(Diligent::TEXTURE_FORMAT rtv_format,
                            Diligent::TEXTURE_FORMAT dsv_format,
                            uint32_t sample_count,
                            bool wireframe) {
  return (wireframe ? (uint64_t{1} << 63u) : 0u) |
         (static_cast<uint64_t>(static_cast<uint32_t>(rtv_format)) << 32u) |
         (static_cast<uint64_t>(static_cast<uint32_t>(dsv_format) & 0xffffu) << 8u) |
         static_cast<uint64_t>(std::max(sample_count, 1u));
}

Diligent::TEXTURE_FORMAT textureViewFormat(Diligent::ITextureView* view,
                                           Diligent::TEXTURE_FORMAT fallback) {
  if (view == nullptr) {
    return fallback;
  }
  const Diligent::TEXTURE_FORMAT format = view->GetDesc().Format;
  return format == Diligent::TEX_FORMAT_UNKNOWN ? fallback : format;
}

std::vector<uint8_t> packTerrainRoughnessRgba(
    const rendering::TerrainTextureData& roughness) {
  if (!roughness.valid()) {
    return {};
  }
  std::vector<uint8_t> packed(roughness.rgba8.size(), 255u);
  for (std::size_t pixel = 0u; pixel < roughness.rgba8.size(); pixel += 4u) {
    packed[pixel + 0u] = 255u;
    packed[pixel + 1u] = roughness.rgba8[pixel + 0u];
    packed[pixel + 2u] = 255u;
    packed[pixel + 3u] = roughness.rgba8[pixel + 3u];
  }
  return packed;
}

}  // namespace

rendering::TerrainId DiligentBackend::allocateTerrainId() noexcept {
  if (nextTerrainId_ == rendering::kInvalidTerrain) {
    return rendering::kInvalidTerrain;
  }
  const rendering::TerrainId id = nextTerrainId_;
  nextTerrainId_ = id == std::numeric_limits<rendering::TerrainId>::max()
                       ? rendering::kInvalidTerrain
                       : id + 1u;
  return id;
}

rendering::TerrainId DiligentBackend::createTerrain(const rendering::TerrainDesc& desc) {
  const rendering::TerrainId id = allocateTerrainId();
  if (id == rendering::kInvalidTerrain) {
    return rendering::kInvalidTerrain;
  }
  TerrainRecord record{};
  record.desc = desc;
  record.desc.tile_size = std::max(record.desc.tile_size, 0.001f);
  record.desc.tile_resolution = std::max(record.desc.tile_resolution, 2u);
  record.desc.base_patch_size = std::max(record.desc.base_patch_size, 1u);
  record.desc.max_tessellation_factor =
      std::clamp(record.desc.max_tessellation_factor, 1.0f, 64.0f);
  record.desc.target_tessellated_edge_size =
      std::max(record.desc.target_tessellated_edge_size, 1.0f);
  terrains_.emplace(id, std::move(record));
  return id;
}

void DiligentBackend::destroyTerrain(rendering::TerrainId terrain) {
  if (terrain == rendering::kInvalidTerrain) {
    return;
  }
  terrains_.erase(terrain);
  terrain_submissions_.erase(
      std::remove_if(terrain_submissions_.begin(),
                     terrain_submissions_.end(),
                     [&](const TerrainSubmission& submission) {
                       return submission.item.terrain == terrain;
                     }),
      terrain_submissions_.end());
}

void DiligentBackend::uploadTerrainTile(rendering::TerrainId terrain,
                                        const rendering::TerrainTileData& tile) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end() || !tile.valid()) {
    return;
  }

  TerrainTileRecord record{};
  record.coord = tile.coord;

  if (device_) {
    Diligent::TextureSubResData height_subres{};
    height_subres.pData = tile.heights.data();
    height_subres.Stride = static_cast<Diligent::Uint32>(tile.resolution * sizeof(float));
    Diligent::TextureData height_data{};
    height_data.pSubResources = &height_subres;
    height_data.NumSubresources = 1u;
    Diligent::TextureDesc height_desc{};
    height_desc.Name = "Karma Terrain Height Tile";
    height_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    height_desc.Width = tile.resolution;
    height_desc.Height = tile.resolution;
    height_desc.MipLevels = 1u;
    height_desc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    height_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(height_desc, &height_data, &record.height_texture);
    if (record.height_texture) {
      record.height_srv =
          record.height_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }

    Diligent::TextureSubResData color_subres{};
    color_subres.pData = tile.color_rgba8.data();
    color_subres.Stride = static_cast<Diligent::Uint32>(tile.color_width * 4u);
    Diligent::TextureData color_data{};
    color_data.pSubResources = &color_subres;
    color_data.NumSubresources = 1u;
    Diligent::TextureDesc color_desc{};
    color_desc.Name = "Karma Terrain Color Tile";
    color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    color_desc.Width = tile.color_width;
    color_desc.Height = tile.color_height;
    color_desc.MipLevels = 1u;
    color_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    color_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(color_desc, &color_data, &record.color_texture);
    if (record.color_texture) {
      record.color_srv =
          record.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }

    if (!tile.control_rgba8.empty() && tile.control_width > 0u && tile.control_height > 0u) {
      record.control_srv =
          createTextureSRV(tile.control_rgba8.data(),
                           static_cast<int>(tile.control_width),
                           static_cast<int>(tile.control_height),
                           false,
                           false,
                           "Karma Terrain Control Tile",
                           record.control_texture);
    }

    const auto patch_vertices = buildPatchVertices(terrain_it->second.desc, tile.resolution);
    record.patch_vertex_count = static_cast<Diligent::Uint32>(std::min<std::size_t>(
        patch_vertices.size(), std::numeric_limits<Diligent::Uint32>::max()));
    record.patch_vertex_buffer =
        createStaticBuffer(device_,
                           patch_vertices.data(),
                           patch_vertices.size() * sizeof(TerrainVertex),
                           Diligent::BIND_VERTEX_BUFFER,
                           "Karma Terrain Patch VB");

    std::vector<TerrainVertex> cpu_vertices;
    std::vector<uint32_t> cpu_indices;
    buildCpuMesh(terrain_it->second.desc,
                 tile,
                 cpu_vertices,
                 cpu_indices,
                 record.min_height,
                 record.max_height);
    record.cpu_index_count = static_cast<Diligent::Uint32>(std::min<std::size_t>(
        cpu_indices.size(), std::numeric_limits<Diligent::Uint32>::max()));
    record.cpu_vertex_buffer =
        createStaticBuffer(device_,
                           cpu_vertices.data(),
                           cpu_vertices.size() * sizeof(TerrainVertex),
                           Diligent::BIND_VERTEX_BUFFER,
                           "Karma Terrain CPU VB");
    record.cpu_index_buffer =
        createStaticBuffer(device_,
                           cpu_indices.data(),
                           cpu_indices.size() * sizeof(uint32_t),
                           Diligent::BIND_INDEX_BUFFER,
                           "Karma Terrain CPU IB");
  } else {
    auto minmax = std::minmax_element(tile.heights.begin(), tile.heights.end());
    record.min_height = *minmax.first * terrain_it->second.desc.height_scale +
                        terrain_it->second.desc.height_offset;
    record.max_height = *minmax.second * terrain_it->second.desc.height_scale +
                        terrain_it->second.desc.height_offset;
  }

  terrain_it->second.tiles[tile.coord] = std::move(record);
  terrain_stats_.upload_count += 1u;
}

void DiligentBackend::uploadTerrainMaterialLayer(
    rendering::TerrainId terrain,
    const rendering::TerrainMaterialLayerData& layer) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end() || !layer.valid()) {
    return;
  }
  TerrainRecord& terrain_record = terrain_it->second;
  TerrainMaterialLayerRecord record{};
  record.name = layer.name;
  record.uv_scale = std::max(layer.uv_scale, 0.001f);
  record.enabled = true;
  record.material = layer.material;

  if (device_) {
    record.albedo_srv =
        createTextureSRV(layer.albedo.rgba8.data(),
                         static_cast<int>(layer.albedo.width),
                         static_cast<int>(layer.albedo.height),
                         true,
                         true,
                         "Karma Terrain Material Albedo",
                         record.albedo_texture);
    if (layer.normal.valid()) {
      record.normal_srv =
          createTextureSRV(layer.normal.rgba8.data(),
                           static_cast<int>(layer.normal.width),
                           static_cast<int>(layer.normal.height),
                           false,
                           true,
                           "Karma Terrain Material Normal",
                           record.normal_texture);
    }
    if (layer.metallic_roughness.valid()) {
      record.metallic_roughness_srv =
          createTextureSRV(layer.metallic_roughness.rgba8.data(),
                           static_cast<int>(layer.metallic_roughness.width),
                           static_cast<int>(layer.metallic_roughness.height),
                           false,
                           true,
                           "Karma Terrain Material Metallic Roughness",
                           record.metallic_roughness_texture);
    } else if (layer.roughness.valid()) {
      const std::vector<uint8_t> packed = packTerrainRoughnessRgba(layer.roughness);
      record.metallic_roughness_srv =
          createTextureSRV(packed.data(),
                           static_cast<int>(layer.roughness.width),
                           static_cast<int>(layer.roughness.height),
                           false,
                           true,
                           "Karma Terrain Material Packed Roughness",
                           record.metallic_roughness_texture);
    }
    if (layer.occlusion.valid()) {
      record.occlusion_srv =
          createTextureSRV(layer.occlusion.rgba8.data(),
                           static_cast<int>(layer.occlusion.width),
                           static_cast<int>(layer.occlusion.height),
                           false,
                           true,
                           "Karma Terrain Material Occlusion",
                           record.occlusion_texture);
    }
    if (layer.emissive.valid()) {
      record.emissive_srv =
          createTextureSRV(layer.emissive.rgba8.data(),
                           static_cast<int>(layer.emissive.width),
                           static_cast<int>(layer.emissive.height),
                           true,
                           true,
                           "Karma Terrain Material Emissive",
                           record.emissive_texture);
    }
    if (layer.specular.valid()) {
      record.specular_srv =
          createTextureSRV(layer.specular.rgba8.data(),
                           static_cast<int>(layer.specular.width),
                           static_cast<int>(layer.specular.height),
                           false,
                           true,
                           "Karma Terrain Material Specular",
                           record.specular_texture);
    }
    if (layer.specular_color.valid()) {
      record.specular_color_srv =
          createTextureSRV(layer.specular_color.rgba8.data(),
                           static_cast<int>(layer.specular_color.width),
                           static_cast<int>(layer.specular_color.height),
                           true,
                           true,
                           "Karma Terrain Material Specular Color",
                           record.specular_color_texture);
    }
  }

  terrain_record.material_layers[layer.layer] = std::move(record);
  terrain_record.material_layer_count =
      std::max(terrain_record.material_layer_count, layer.layer + 1u);
  for (auto& [coord, tile] : terrain_record.tiles) {
    (void)coord;
    tile.tess_srbs.clear();
    tile.cpu_srbs.clear();
  }
}

void DiligentBackend::clearTerrainMaterialLayers(rendering::TerrainId terrain) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end()) {
    return;
  }
  TerrainRecord& terrain_record = terrain_it->second;
  terrain_record.material_layers = {};
  terrain_record.material_layer_count = 0u;
  for (auto& [coord, tile] : terrain_record.tiles) {
    (void)coord;
    tile.tess_srbs.clear();
    tile.cpu_srbs.clear();
  }
}

void DiligentBackend::evictTerrainTile(rendering::TerrainId terrain,
                                       rendering::TerrainTileCoord coord) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end()) {
    return;
  }
  terrain_it->second.tiles.erase(coord);
  terrain_stats_.eviction_count += 1u;
}

void DiligentBackend::submitTerrain(const rendering::TerrainDrawItem& item) {
  if (item.terrain == rendering::kInvalidTerrain ||
      item.instance == rendering::kInvalidInstance ||
      terrains_.find(item.terrain) == terrains_.end()) {
    return;
  }
  terrain_submissions_.push_back(TerrainSubmission{.item = item});
  terrain_stats_.submitted_tiles += 1u;
}

rendering::TerrainCapabilities DiligentBackend::getTerrainCapabilities() const {
  rendering::TerrainCapabilities caps{};
  caps.supported = true;
  caps.hardware_tessellation = terrainTessellationEnabled(device_);
  caps.cpu_fallback = true;
  caps.max_tessellation_factor = 64u;
  return caps;
}

rendering::TerrainStats DiligentBackend::getTerrainStats() const {
  rendering::TerrainStats stats = terrain_stats_;
  stats.terrain_count = static_cast<uint32_t>(std::min<std::size_t>(
      terrains_.size(), std::numeric_limits<uint32_t>::max()));
  uint32_t resident_tiles = 0u;
  for (const auto& [id, terrain] : terrains_) {
    (void)id;
    resident_tiles += static_cast<uint32_t>(std::min<std::size_t>(
        terrain.tiles.size(), std::numeric_limits<uint32_t>::max() - resident_tiles));
  }
  stats.resident_tiles = resident_tiles;
  return stats;
}

void DiligentBackend::bindTerrainFrameResourcesToSrb(
    Diligent::IShaderResourceBinding* srb) const {
  if (srb == nullptr) {
    return;
  }
  constexpr auto overwrite =
      Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE;
  auto set_texture = [&](const char* name, Diligent::ITextureView* view) {
    if (view == nullptr) {
      return;
    }
    if (auto* var =
            srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, name)) {
      var->Set(view, overwrite);
    }
  };
  auto set_buffer = [&](const char* name, Diligent::IBufferView* view) {
    if (view == nullptr) {
      return;
    }
    if (auto* var =
            srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, name)) {
      var->Set(view, overwrite);
    }
  };

  set_texture("g_IrradianceTex",
              env_irradiance_srv_ ? env_irradiance_srv_.RawPtr()
                                  : default_env_.RawPtr());
  set_texture("g_PrefilterTex",
              env_prefilter_srv_ ? env_prefilter_srv_.RawPtr()
                                 : default_env_.RawPtr());
  set_texture("g_BRDFLUT", brdfLutSrv());
  set_buffer("g_ForwardPlusLights",
             active_forward_plus_light_srv_
                 ? active_forward_plus_light_srv_
                 : forward_plus_light_srv_.RawPtr());
  set_buffer("g_ForwardPlusTileLightCounts",
             active_forward_plus_tile_count_srv_
                 ? active_forward_plus_tile_count_srv_
                 : forward_plus_tile_count_srv_.RawPtr());
  set_buffer("g_ForwardPlusTileLightIndices",
             active_forward_plus_tile_index_srv_
                 ? active_forward_plus_tile_index_srv_
                 : forward_plus_tile_index_srv_.RawPtr());
  bindShadowResourcesToSrb(srb);
}

DiligentBackend::TerrainPipelineSet* DiligentBackend::ensureTerrainResources(
    Diligent::TEXTURE_FORMAT rtv_format,
    Diligent::TEXTURE_FORMAT dsv_format) {
  if (!device_ || rtv_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return nullptr;
  }

  if (!terrain_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Terrain Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(TerrainConstants);
    device_->CreateBuffer(cb_desc, nullptr, &terrain_cb_);
  }
  if (!terrain_cb_) {
    return nullptr;
  }

  if (!terrain_color_sampler_) {
    Diligent::SamplerDesc sampler{};
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    device_->CreateSampler(sampler, &terrain_color_sampler_);
  }
  if (!terrain_height_sampler_) {
    Diligent::SamplerDesc sampler{};
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    device_->CreateSampler(sampler, &terrain_height_sampler_);
  }
  if (!terrain_material_sampler_) {
    Diligent::SamplerDesc sampler{};
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    device_->CreateSampler(sampler, &terrain_material_sampler_);
  }
  if (!terrain_default_control_) {
    terrain_default_control_ = createSolidTextureSRV(255,
                                                     0,
                                                     0,
                                                     0,
                                                     false,
                                                     "Karma Terrain Default Control",
                                                     terrain_default_control_tex_);
  }

  const bool editor_wireframe = editorWireframeViewEnabled();
  const uint64_t pipeline_key = terrainPipelineKey(
      rtv_format, dsv_format, activeRasterSampleCount(), editor_wireframe);
  TerrainPipelineSet& pipelines = terrain_pipeline_sets_[pipeline_key];
  pipelines.rtv_format = rtv_format;
  pipelines.dsv_format = dsv_format;

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(TerrainVertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(TerrainVertex))},
      Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(TerrainVertex, uv)),
                              static_cast<Diligent::Uint32>(sizeof(TerrainVertex))},
  };
  const bool depth_enabled = dsv_format != Diligent::TEX_FORMAT_UNKNOWN;

  auto bind_static_constants = [&](Diligent::IPipelineState* pso,
                                   std::initializer_list<Diligent::SHADER_TYPE> stages) {
    if (!pso || !terrain_cb_) {
      return;
    }
    for (Diligent::SHADER_TYPE stage : stages) {
      if (auto* var = pso->GetStaticVariableByName(stage, "TerrainConstants")) {
        var->Set(terrain_cb_);
      }
    }
  };

  if (!pipelines.cpu_pipeline_state) {
    Diligent::ShaderCreateInfo shader_ci{};
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.EntryPoint = "main";

    shader_ci.Desc.Name = "Karma Terrain CPU VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.Source = kTerrainCpuVS;
    Diligent::RefCntAutoPtr<Diligent::IShader> vs = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.Source = kTerrainPS;
    Diligent::RefCntAutoPtr<Diligent::IShader> ps = device_with_cache_.CreateShader(shader_ci);

    if (vs && ps) {
      Diligent::GraphicsPipelineStateCreateInfo pso{};
      pso.PSODesc.Name = "Karma Terrain CPU Pipeline";
      pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      pso.pVS = vs;
      pso.pPS = ps;
      auto& graphics = pso.GraphicsPipeline;
      graphics.NumRenderTargets = 1u;
      graphics.SmplDesc.Count = static_cast<Diligent::Uint8>(activeRasterSampleCount());
      graphics.RTVFormats[0] = rtv_format;
      graphics.DSVFormat = dsv_format;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.RasterizerDesc.FrontCounterClockwise = true;
      if (editor_wireframe) {
        graphics.RasterizerDesc.FillMode = Diligent::FILL_MODE_WIREFRAME;
      }
      graphics.DepthStencilDesc.DepthEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthWriteEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
      graphics.InputLayout.LayoutElements = layout;
      graphics.InputLayout.NumElements =
          static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));
      Diligent::ShaderResourceVariableDesc vars[] = {
          {Diligent::SHADER_TYPE_VERTEX, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ControlTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusLights",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightCounts",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightIndices",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      };
      pso.PSODesc.ResourceLayout.Variables = vars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
      const auto pso_start = core::SteadyClock::now();
      pipelines.cpu_pipeline_state = createGraphicsPipelineState(pso);
      logRenderPipelineDiag("terrain",
                            "Karma Terrain CPU Pipeline",
                            pso_start,
                            core::SteadyClock::now());
      bind_static_constants(pipelines.cpu_pipeline_state.RawPtr(),
                            {Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL});
    }
  }

  if (!pipelines.tess_pipeline_state && terrainTessellationEnabled(device_)) {
    Diligent::ShaderCreateInfo shader_ci{};
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.EntryPoint = "main";

    shader_ci.Desc.Name = "Karma Terrain Tess VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.Source = kTerrainTessVS;
    Diligent::RefCntAutoPtr<Diligent::IShader> vs = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain Tess HS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_HULL;
    shader_ci.Source = kTerrainTessHS;
    Diligent::RefCntAutoPtr<Diligent::IShader> hs = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain Tess DS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_DOMAIN;
    shader_ci.Source = kTerrainTessDS;
    Diligent::RefCntAutoPtr<Diligent::IShader> ds = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain Tess PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.Source = kTerrainPS;
    Diligent::RefCntAutoPtr<Diligent::IShader> ps = device_with_cache_.CreateShader(shader_ci);

    if (vs && hs && ds && ps) {
      Diligent::GraphicsPipelineStateCreateInfo pso{};
      pso.PSODesc.Name = "Karma Terrain Tessellation Pipeline";
      pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      pso.pVS = vs;
      pso.pHS = hs;
      pso.pDS = ds;
      pso.pPS = ps;
      auto& graphics = pso.GraphicsPipeline;
      graphics.NumRenderTargets = 1u;
      graphics.SmplDesc.Count = static_cast<Diligent::Uint8>(activeRasterSampleCount());
      graphics.RTVFormats[0] = rtv_format;
      graphics.DSVFormat = dsv_format;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.RasterizerDesc.FrontCounterClockwise = true;
      if (editor_wireframe) {
        graphics.RasterizerDesc.FillMode = Diligent::FILL_MODE_WIREFRAME;
      }
      graphics.DepthStencilDesc.DepthEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthWriteEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
      graphics.InputLayout.LayoutElements = layout;
      graphics.InputLayout.NumElements =
          static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));
      Diligent::ShaderResourceVariableDesc vars[] = {
          {Diligent::SHADER_TYPE_VERTEX, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_HULL, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_DOMAIN, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_DOMAIN, "g_HeightTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_DOMAIN, "g_HeightSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ControlTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialMetallicRoughness3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialOcclusion3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialEmissive3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecular3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSpecularColor3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusLights",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightCounts",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightIndices",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      };
      pso.PSODesc.ResourceLayout.Variables = vars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
      const auto pso_start = core::SteadyClock::now();
      pipelines.tess_pipeline_state = createGraphicsPipelineState(pso);
      logRenderPipelineDiag("terrain",
                            "Karma Terrain Tessellation Pipeline",
                            pso_start,
                            core::SteadyClock::now());
      bind_static_constants(pipelines.tess_pipeline_state.RawPtr(),
                            {Diligent::SHADER_TYPE_VERTEX,
                             Diligent::SHADER_TYPE_HULL,
                             Diligent::SHADER_TYPE_DOMAIN,
                             Diligent::SHADER_TYPE_PIXEL});
    }
  }

  auto bind_pixel_resources = [&](Diligent::IShaderResourceBinding* srb,
                                  TerrainRecord& terrain,
                                  TerrainTileRecord& tile) {
    if (!srb) {
      return;
    }
    auto set_texture = [&](const char* name, Diligent::ITextureView* srv) {
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, name)) {
        var->Set(srv);
      }
    };
    set_texture("g_ColorTexture", tile.color_srv);
    set_texture("g_ControlTexture",
                tile.control_srv ? tile.control_srv.RawPtr()
                                 : terrain_default_control_.RawPtr());

    const auto layer_srv = [&](uint32_t index,
                               Diligent::RefCntAutoPtr<Diligent::ITextureView>
                                   TerrainMaterialLayerRecord::*member,
                               Diligent::ITextureView* fallback) -> Diligent::ITextureView* {
      if (index >= terrain.material_layers.size()) {
        return fallback;
      }
      const TerrainMaterialLayerRecord& layer = terrain.material_layers[index];
      Diligent::ITextureView* const value = (layer.*member).RawPtr();
      return layer.enabled && value != nullptr ? value : fallback;
    };
    const auto bind_layer_family =
        [&](std::string_view prefix,
            Diligent::RefCntAutoPtr<Diligent::ITextureView>
                TerrainMaterialLayerRecord::*member,
            Diligent::ITextureView* fallback) {
          for (uint32_t index = 0u; index < 4u; ++index) {
            const std::string name =
                std::string(prefix) + std::to_string(index);
            set_texture(name.c_str(), layer_srv(index, member, fallback));
          }
        };
    bind_layer_family("g_MaterialAlbedo",
                      &TerrainMaterialLayerRecord::albedo_srv,
                      default_base_color_.RawPtr());
    bind_layer_family("g_MaterialNormal",
                      &TerrainMaterialLayerRecord::normal_srv,
                      default_normal_.RawPtr());
    bind_layer_family("g_MaterialMetallicRoughness",
                      &TerrainMaterialLayerRecord::metallic_roughness_srv,
                      default_base_color_.RawPtr());
    bind_layer_family("g_MaterialOcclusion",
                      &TerrainMaterialLayerRecord::occlusion_srv,
                      default_occlusion_.RawPtr());
    bind_layer_family("g_MaterialEmissive",
                      &TerrainMaterialLayerRecord::emissive_srv,
                      default_emissive_.RawPtr());
    bind_layer_family("g_MaterialSpecular",
                      &TerrainMaterialLayerRecord::specular_srv,
                      default_base_color_.RawPtr());
    bind_layer_family("g_MaterialSpecularColor",
                      &TerrainMaterialLayerRecord::specular_color_srv,
                      default_base_color_.RawPtr());

    if (auto* var =
            srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ColorSampler")) {
      var->Set(terrain_color_sampler_ ? terrain_color_sampler_.RawPtr()
                                      : sampler_color_.RawPtr());
    }
    if (auto* var =
            srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MaterialSampler")) {
      var->Set(terrain_material_sampler_ ? terrain_material_sampler_.RawPtr()
                                         : sampler_color_.RawPtr());
    }
    bindTerrainFrameResourcesToSrb(srb);
  };

  auto initialize_tile_srb = [&](TerrainRecord& terrain, TerrainTileRecord& tile) {
    auto& cpu_srb = tile.cpu_srbs[pipeline_key];
    if (!cpu_srb && pipelines.cpu_pipeline_state) {
      pipelines.cpu_pipeline_state->CreateShaderResourceBinding(&cpu_srb, true);
      if (cpu_srb) {
        bind_pixel_resources(cpu_srb.RawPtr(), terrain, tile);
      }
    }

    auto& tess_srb = tile.tess_srbs[pipeline_key];
    if (!tess_srb && pipelines.tess_pipeline_state) {
      pipelines.tess_pipeline_state->CreateShaderResourceBinding(&tess_srb, true);
      if (tess_srb) {
        if (auto* var = tess_srb->GetVariableByName(Diligent::SHADER_TYPE_DOMAIN,
                                                    "g_HeightTexture")) {
          var->Set(tile.height_srv);
        }
        if (auto* var = tess_srb->GetVariableByName(Diligent::SHADER_TYPE_DOMAIN,
                                                    "g_HeightSampler")) {
          var->Set(terrain_height_sampler_ ? terrain_height_sampler_.RawPtr()
                                           : sampler_data_.RawPtr());
        }
        bind_pixel_resources(tess_srb.RawPtr(), terrain, tile);
      }
    }
  };

  for (auto& [terrain_id, terrain] : terrains_) {
    (void)terrain_id;
    for (auto& [coord, tile] : terrain.tiles) {
      (void)coord;
      initialize_tile_srb(terrain, tile);
    }
  }
  return &pipelines;
}

Diligent::Uint32 DiligentBackend::renderTerrainLayer(rendering::LayerId layer,
                                                     const DrawConstants& base_constants,
                                                     const glm::mat4& view_proj,
                                                     bool is_gl,
                                                     Diligent::ITextureView* active_rtv,
                                                     Diligent::ITextureView* active_dsv,
                                                     int render_width,
                                                     int render_height) {
  if (!context_ || !active_rtv || terrain_submissions_.empty()) {
    return 0u;
  }
  const Diligent::TEXTURE_FORMAT fallback_rtv_format = sceneColorFormat();
  const Diligent::TEXTURE_FORMAT fallback_dsv_format =
      swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                  : Diligent::TEX_FORMAT_D32_FLOAT;
  const Diligent::TEXTURE_FORMAT rtv_format =
      textureViewFormat(active_rtv, fallback_rtv_format);
  const Diligent::TEXTURE_FORMAT dsv_format =
      active_dsv ? textureViewFormat(active_dsv, fallback_dsv_format)
                 : Diligent::TEX_FORMAT_UNKNOWN;
  TerrainPipelineSet* pipelines = ensureTerrainResources(rtv_format, dsv_format);
  if (!pipelines ||
      (!pipelines->cpu_pipeline_state && !pipelines->tess_pipeline_state)) {
    return 0u;
  }
  const uint64_t pipeline_key = terrainPipelineKey(
      rtv_format,
      dsv_format,
      activeRasterSampleCount(),
      editorWireframeViewEnabled());

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
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(render_width),
                         static_cast<Diligent::Uint32>(render_height));

  const auto frustum_planes = extractFrustumPlanes(view_proj, is_gl);
  Diligent::Uint32 draw_count = 0u;
  for (const TerrainSubmission& submission : terrain_submissions_) {
    const auto& item = submission.item;
    if (!item.visible || item.layer != layer) {
      continue;
    }
    auto terrain_it = terrains_.find(item.terrain);
    if (terrain_it == terrains_.end()) {
      continue;
    }
    TerrainRecord& terrain = terrain_it->second;
    auto tile_it = terrain.tiles.find(item.coord);
    if (tile_it == terrain.tiles.end()) {
      continue;
    }
    TerrainTileRecord& tile = tile_it->second;

    const float tile_origin_x =
        static_cast<float>(static_cast<int64_t>(item.coord.x) -
                           static_cast<int64_t>(terrain.desc.origin_tile_x)) *
        terrain.desc.tile_size;
    const float tile_origin_z =
        static_cast<float>(static_cast<int64_t>(item.coord.z) -
                           static_cast<int64_t>(terrain.desc.origin_tile_z)) *
        terrain.desc.tile_size;
    glm::mat4 model = item.transform;
    model = glm::translate(model, glm::vec3(tile_origin_x, 0.0f, tile_origin_z));

    const float mid_y = (tile.min_height + tile.max_height) * 0.5f;
    const float half_y = std::max((tile.max_height - tile.min_height) * 0.5f, 1.0f);
    const glm::vec3 local_center{terrain.desc.tile_size * 0.5f,
                                 mid_y,
                                 terrain.desc.tile_size * 0.5f};
    const float local_radius =
        std::sqrt(terrain.desc.tile_size * terrain.desc.tile_size * 0.5f +
                  half_y * half_y);
    const glm::vec3 world_center = glm::vec3(model * glm::vec4(local_center, 1.0f));
    const glm::vec4 sphere(world_center, local_radius * maxTransformScale(model));
    const float cull_guard_band = sphere.w * 0.12f;
    if (!sphereIntersectsFrustum(frustum_planes, sphere, cull_guard_band)) {
      terrain_stats_.culled_tiles += 1u;
      continue;
    }

    auto tess_srb_it = tile.tess_srbs.find(pipeline_key);
    Diligent::IShaderResourceBinding* tess_srb =
        tess_srb_it != tile.tess_srbs.end() ? tess_srb_it->second.RawPtr() : nullptr;
    auto cpu_srb_it = tile.cpu_srbs.find(pipeline_key);
    Diligent::IShaderResourceBinding* cpu_srb =
        cpu_srb_it != tile.cpu_srbs.end() ? cpu_srb_it->second.RawPtr() : nullptr;

    const bool use_tessellation =
        pipelines->tess_pipeline_state &&
        terrainTessellationEnabled(device_) &&
        tess_srb &&
        tile.patch_vertex_buffer &&
        tile.patch_vertex_count > 0u;
    const bool use_cpu =
        !use_tessellation &&
        terrain.desc.cpu_fallback_enabled &&
        pipelines->cpu_pipeline_state &&
        cpu_srb &&
        tile.cpu_vertex_buffer &&
        tile.cpu_index_buffer &&
        tile.cpu_index_count > 0u;
    if (!use_tessellation && !use_cpu) {
      continue;
    }

    TerrainConstants constants{};
    copyMat4(constants.view_proj, view_proj);
    copyMat4(constants.model, model);
    std::memcpy(constants.light_dir, base_constants.light_dir, sizeof(constants.light_dir));
    std::memcpy(constants.light_color, base_constants.light_color, sizeof(constants.light_color));
    std::memcpy(constants.camera_pos, base_constants.camera_pos, sizeof(constants.camera_pos));
    std::memcpy(constants.camera_forward,
                base_constants.camera_forward,
                sizeof(constants.camera_forward));
    std::memcpy(constants.env_params,
                base_constants.env_params,
                sizeof(constants.env_params));
    std::memcpy(constants.shadow_params,
                base_constants.shadow_params,
                sizeof(constants.shadow_params));
    std::memcpy(constants.point_shadow_params,
                base_constants.point_shadow_params,
                sizeof(constants.point_shadow_params));
    std::memcpy(constants.local_light_params,
                base_constants.local_light_params,
                sizeof(constants.local_light_params));
    std::memcpy(constants.point_shadow_tuning,
                base_constants.point_shadow_tuning,
                sizeof(constants.point_shadow_tuning));
    std::memcpy(constants.shadow_bias_params,
                base_constants.shadow_bias_params,
                sizeof(constants.shadow_bias_params));
    std::memcpy(constants.shadow_cascade_splits,
                base_constants.shadow_cascade_splits,
                sizeof(constants.shadow_cascade_splits));
    std::memcpy(constants.shadow_cascade_world_texel,
                base_constants.shadow_cascade_world_texel,
                sizeof(constants.shadow_cascade_world_texel));
    std::memcpy(constants.shadow_cascade_params,
                base_constants.shadow_cascade_params,
                sizeof(constants.shadow_cascade_params));
    std::memcpy(constants.forward_plus_params,
                base_constants.forward_plus_params,
                sizeof(constants.forward_plus_params));
    std::memcpy(constants.local_light_position_range,
                base_constants.local_light_position_range,
                sizeof(constants.local_light_position_range));
    std::memcpy(constants.local_light_direction_type,
                base_constants.local_light_direction_type,
                sizeof(constants.local_light_direction_type));
    std::memcpy(constants.local_light_color_intensity,
                base_constants.local_light_color_intensity,
                sizeof(constants.local_light_color_intensity));
    std::memcpy(constants.local_light_spot_params,
                base_constants.local_light_spot_params,
                sizeof(constants.local_light_spot_params));
    std::memcpy(constants.local_light_meta,
                base_constants.local_light_meta,
                sizeof(constants.local_light_meta));
    std::memcpy(constants.shadow_cascade_uv_proj,
                base_constants.shadow_cascade_uv_proj,
                sizeof(constants.shadow_cascade_uv_proj));
    std::memcpy(constants.point_shadow_uv_proj,
                base_constants.point_shadow_uv_proj,
                sizeof(constants.point_shadow_uv_proj));
    constants.tile_info[0] = terrain.desc.tile_size;
    constants.tile_info[1] = terrain.desc.height_scale;
    constants.tile_info[2] = terrain.desc.height_offset;
    constants.tile_info[3] = terrain.desc.max_tessellation_factor;
    constants.render_params[0] = terrain.desc.target_tessellated_edge_size;
    constants.render_params[1] = static_cast<float>(std::max(render_width, 1));
    constants.render_params[2] = static_cast<float>(std::max(render_height, 1));
    constants.render_params[3] = static_cast<float>(std::min<uint32_t>(
        terrain.material_layer_count, static_cast<uint32_t>(terrain.material_layers.size())));
    for (uint32_t i = 0u; i < terrain.material_layers.size(); ++i) {
      const TerrainMaterialLayerRecord& layer_record = terrain.material_layers[i];
      constants.material_scales[i] = layer_record.enabled
                                          ? std::max(layer_record.uv_scale, 0.001f)
                                          : 1.0f;
      const auto finite_or = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
      };
      const auto unit = [&](float value, float fallback) {
        return std::clamp(finite_or(value, fallback), 0.0f, 1.0f);
      };
      if (!layer_record.enabled) {
        constants.layer_base_color[i][0] = 1.0f;
        constants.layer_base_color[i][1] = 1.0f;
        constants.layer_base_color[i][2] = 1.0f;
        constants.layer_base_color[i][3] = 1.0f;
        constants.layer_pbr[i][0] = 0.0f;
        constants.layer_pbr[i][1] = 1.0f;
        constants.layer_pbr[i][2] = 1.0f;
        constants.layer_pbr[i][3] = 1.0f;
        constants.layer_specular[i][0] = 1.0f;
        constants.layer_specular[i][1] = 1.0f;
        constants.layer_specular[i][2] = 1.0f;
        constants.layer_specular[i][3] = 1.0f;
        constants.layer_flags[i][0] = 1.0f;
        continue;
      }

      const rendering::MaterialDesc& material = layer_record.material;
      constants.layer_base_color[i][0] = unit(material.base_color.r, 1.0f);
      constants.layer_base_color[i][1] = unit(material.base_color.g, 1.0f);
      constants.layer_base_color[i][2] = unit(material.base_color.b, 1.0f);
      constants.layer_base_color[i][3] = unit(material.base_color.a, 1.0f);
      const float emissive_strength =
          std::max(finite_or(material.emissive_strength, 1.0f), 0.0f);
      constants.layer_emissive[i][0] =
          std::max(finite_or(material.emissive_color.r, 0.0f), 0.0f) *
          emissive_strength;
      constants.layer_emissive[i][1] =
          std::max(finite_or(material.emissive_color.g, 0.0f), 0.0f) *
          emissive_strength;
      constants.layer_emissive[i][2] =
          std::max(finite_or(material.emissive_color.b, 0.0f), 0.0f) *
          emissive_strength;
      constants.layer_pbr[i][0] = unit(material.metallic, 0.0f);
      constants.layer_pbr[i][1] = unit(material.roughness, 1.0f);
      constants.layer_pbr[i][2] =
          std::max(finite_or(material.normal_scale, 1.0f), 0.0f);
      constants.layer_pbr[i][3] = unit(material.occlusion_strength, 1.0f);
      constants.layer_specular[i][0] = unit(material.specular_color.r, 1.0f);
      constants.layer_specular[i][1] = unit(material.specular_color.g, 1.0f);
      constants.layer_specular[i][2] = unit(material.specular_color.b, 1.0f);
      constants.layer_specular[i][3] = unit(material.specular_factor, 1.0f);
      constants.layer_flags[i][0] =
          material.normal_map_convention ==
                  rendering::MaterialDesc::NormalMapConvention::DirectX
              ? -1.0f
              : 1.0f;
    }
    constants.exposure_params[0] = base_constants.env_params[3];
    constants.exposure_params[1] = static_cast<float>(editor_view_mode_);
    {
      Diligent::MapHelper<TerrainConstants> mapped(
          context_, terrain_cb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (!mapped_constants) {
        continue;
      }
      *mapped_constants = constants;
    }

    if (use_tessellation) {
      bindTerrainFrameResourcesToSrb(tess_srb);
      context_->SetPipelineState(pipelines->tess_pipeline_state);
      context_->CommitShaderResources(tess_srb,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::IBuffer* vbs[] = {tile.patch_vertex_buffer};
      Diligent::Uint64 offsets[] = {0u};
      context_->SetVertexBuffers(0,
                                 1,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      Diligent::DrawAttribs draw{};
      draw.NumVertices = tile.patch_vertex_count;
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
      terrain_stats_.tessellated_tiles += 1u;
    } else {
      bindTerrainFrameResourcesToSrb(cpu_srb);
      context_->SetPipelineState(pipelines->cpu_pipeline_state);
      context_->CommitShaderResources(cpu_srb,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::IBuffer* vbs[] = {tile.cpu_vertex_buffer};
      Diligent::Uint64 offsets[] = {0u};
      context_->SetVertexBuffers(0,
                                 1,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      context_->SetIndexBuffer(tile.cpu_index_buffer,
                               0u,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::DrawIndexedAttribs draw{};
      draw.IndexType = Diligent::VT_UINT32;
      draw.NumIndices = tile.cpu_index_count;
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->DrawIndexed(draw);
      terrain_stats_.cpu_fallback_tiles += 1u;
    }
    terrain_stats_.drawn_tiles += 1u;
    draw_count += 1u;
  }
  return draw_count;
}

}  // namespace karma::rendering::backend
