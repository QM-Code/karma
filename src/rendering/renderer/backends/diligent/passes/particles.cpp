#include "../backend.hpp"

#include "../backend_internal.h"
#include "pass_shared.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>

namespace karma::renderer_backend {

namespace {
static constexpr const char* kParticleVS = R"(
cbuffer Constants
{
    row_major float4x4 g_ViewProj;
    float4 g_CameraRight;
    float4 g_CameraUp;
    float4 g_CameraForward;
    float4 g_Params;
    float4 g_ScreenParams;
    float4 g_CameraParams;
    float4 g_CameraPosition;
    float4 g_ShadingParams;
    float4 g_PresentationParams;
    float4 g_AtlasParams0;
    float4 g_AtlasParams1;
    float4 g_AtlasParams2;
};

#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
struct ParticleMaterialRecord
{
    uint texture_index;
    uint alignment;
    uint shading_mode;
    uint presentation_mode;
    float soft_particle_distance;
    float distortion_strength;
    float fresnel_power;
    float fresnel_strength;
    float refraction_strength;
    float interior_glow;
    float size_curve_exponent;
    float alpha_curve_exponent;
    uint atlas_columns;
    uint atlas_rows;
    uint atlas_frame_count;
    uint animate_over_lifetime;
    uint atlas_frame_width;
    uint atlas_frame_height;
    uint atlas_border_x;
    uint atlas_border_y;
    float atlas_spacing_x;
    float atlas_spacing_y;
    float animation_fps;
    float use_soft_mask;
};

StructuredBuffer<ParticleMaterialRecord> g_ParticleMaterials;
#endif

struct VSInput
{
    float2 corner : ATTRIB0;
    float2 uv : ATTRIB1;
    float4 position_age : ATTRIB2;
    float4 color_start : ATTRIB3;
    float4 color_end : ATTRIB4;
    float4 rotation_size : ATTRIB5;
    float4 uv_rect : ATTRIB6;
    float4 uv_rect_next : ATTRIB7;
    float4 params : ATTRIB8;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv_next : TEXCOORD1;
    float2 local_uv : TEXCOORD2;
    float4 color : COLOR0;
    float frame_blend : TEXCOORD3;
    float3 world_pos : TEXCOORD4;
#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
    nointerpolation uint material_id : TEXCOORD5;
#endif
};

float ApplyCurveExponent(float t, float exponent)
{
    float clamped_t = saturate(t);
    if (abs(exponent - 1.0) <= 1.0e-4)
    {
        return clamped_t;
    }
    return pow(clamped_t, max(exponent, 1.0e-3));
}

void ComputeFrameUv(uint frame_index,
                    uint columns,
                    uint rows,
                    uint frame_count,
                    float frame_width,
                    float frame_height,
                    float border_x,
                    float border_y,
                    float spacing_x,
                    float spacing_y,
                    out float2 uv_min,
                    out float2 uv_max)
{
    columns = max(columns, 1u);
    rows = max(rows, 1u);
    frame_count = max(frame_count, 1u);
    uint clamped_frame = min(frame_index, frame_count - 1u);
    uint column = clamped_frame % columns;
    uint row = clamped_frame / columns;

    if (frame_width > 0.0 && frame_height > 0.0)
    {
        float texture_width =
            (float)columns * frame_width +
            (columns > 0u ? (float)(columns - 1u) * spacing_x : 0.0) +
            border_x * 2.0;
        float texture_height =
            (float)rows * frame_height +
            (rows > 0u ? (float)(rows - 1u) * spacing_y : 0.0) +
            border_y * 2.0;
        float frame_x = border_x + (float)column * (frame_width + spacing_x);
        float frame_y = border_y + (float)row * (frame_height + spacing_y);
        uv_min = float2(frame_x / max(texture_width, 1.0e-4),
                        frame_y / max(texture_height, 1.0e-4));
        uv_max = float2((frame_x + frame_width) / max(texture_width, 1.0e-4),
                        (frame_y + frame_height) / max(texture_height, 1.0e-4));
        return;
    }

    float inv_columns = 1.0 / (float)columns;
    float inv_rows = 1.0 / (float)rows;
    uv_min = float2((float)column * inv_columns, (float)row * inv_rows);
    uv_max = float2((float)(column + 1u) * inv_columns, (float)(row + 1u) * inv_rows);
}

PSInput main(VSInput input)
{
    uint alignment = 0u;
    uint presentation_mode = (uint)max(g_PresentationParams.z, 0.0);
    float size_curve_exponent = g_PresentationParams.x;
    float alpha_curve_exponent = g_PresentationParams.y;
    uint atlas_columns = max((uint)g_AtlasParams0.x, 1u);
    uint atlas_rows = max((uint)g_AtlasParams0.y, 1u);
    uint atlas_frame_count = max((uint)g_AtlasParams0.z, 1u);
    uint animate_over_lifetime = g_AtlasParams0.w > 0.5 ? 1u : 0u;
    float atlas_frame_width = g_AtlasParams1.x;
    float atlas_frame_height = g_AtlasParams1.y;
    float atlas_border_x = g_AtlasParams1.z;
    float atlas_border_y = g_AtlasParams1.w;
    float atlas_spacing_x = g_AtlasParams2.x;
    float atlas_spacing_y = g_AtlasParams2.y;
    float animation_fps = g_AtlasParams2.z;
    uint material_id = 0u;
#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
    material_id = (uint)max(input.params.w + 0.5, 0.0);
    ParticleMaterialRecord material = g_ParticleMaterials[material_id];
    alignment = material.alignment;
    presentation_mode = material.presentation_mode;
    size_curve_exponent = material.size_curve_exponent;
    alpha_curve_exponent = material.alpha_curve_exponent;
    atlas_columns = max(material.atlas_columns, 1u);
    atlas_rows = max(material.atlas_rows, 1u);
    atlas_frame_count = max(material.atlas_frame_count, 1u);
    animate_over_lifetime = material.animate_over_lifetime;
    atlas_frame_width = (float)material.atlas_frame_width;
    atlas_frame_height = (float)material.atlas_frame_height;
    atlas_border_x = (float)material.atlas_border_x;
    atlas_border_y = (float)material.atlas_border_y;
    atlas_spacing_x = material.atlas_spacing_x;
    atlas_spacing_y = material.atlas_spacing_y;
    animation_fps = material.animation_fps;
#endif

    float normalized_age = saturate(input.position_age.w);
    float size = input.rotation_size.z;
    float4 color = input.color_start;
    float2 uv_min = input.uv_rect.xy;
    float2 uv_max = input.uv_rect.zw;
    float2 uv_min_next = input.uv_rect_next.xy;
    float2 uv_max_next = input.uv_rect_next.zw;
    float frame_blend = saturate(input.params.x);
    if (presentation_mode > 0u)
    {
        float size_t = ApplyCurveExponent(normalized_age, size_curve_exponent);
        float alpha_t = ApplyCurveExponent(normalized_age, alpha_curve_exponent);
        size = lerp(input.rotation_size.z, input.rotation_size.w, size_t);
        color.rgb = lerp(input.color_start.rgb, input.color_end.rgb, normalized_age);
        color.a = lerp(input.color_start.a, input.color_end.a, alpha_t);

        uint frame_count = max(atlas_frame_count, 1u);
        if (frame_count > 1u)
        {
            float frame_position = 0.0;
            if (animate_over_lifetime != 0u)
            {
                frame_position = normalized_age * (float)(frame_count - 1u);
                uint current_frame = min((uint)floor(frame_position), frame_count - 1u);
                uint next_frame = min(current_frame + 1u, frame_count - 1u);
                frame_blend = saturate(frame_position - (float)current_frame);
                ComputeFrameUv(current_frame,
                               atlas_columns,
                               atlas_rows,
                               frame_count,
                               atlas_frame_width,
                               atlas_frame_height,
                               atlas_border_x,
                               atlas_border_y,
                               atlas_spacing_x,
                               atlas_spacing_y,
                               uv_min,
                               uv_max);
                ComputeFrameUv(next_frame,
                               atlas_columns,
                               atlas_rows,
                               frame_count,
                               atlas_frame_width,
                               atlas_frame_height,
                               atlas_border_x,
                               atlas_border_y,
                               atlas_spacing_x,
                               atlas_spacing_y,
                               uv_min_next,
                               uv_max_next);
            }
            else
            {
                frame_position = max(input.params.z, 0.0) * animation_fps + input.params.y;
                float wrapped_position = fmod(frame_position, (float)frame_count);
                float normalized_position =
                    wrapped_position >= 0.0 ? wrapped_position : wrapped_position + (float)frame_count;
                uint current_frame = ((uint)floor(normalized_position)) % frame_count;
                uint next_frame = (current_frame + 1u) % frame_count;
                frame_blend = saturate(normalized_position - (float)current_frame);
                ComputeFrameUv(current_frame,
                               atlas_columns,
                               atlas_rows,
                               frame_count,
                               atlas_frame_width,
                               atlas_frame_height,
                               atlas_border_x,
                               atlas_border_y,
                               atlas_spacing_x,
                               atlas_spacing_y,
                               uv_min,
                               uv_max);
                ComputeFrameUv(next_frame,
                               atlas_columns,
                               atlas_rows,
                               frame_count,
                               atlas_frame_width,
                               atlas_frame_height,
                               atlas_border_x,
                               atlas_border_y,
                               atlas_spacing_x,
                               atlas_spacing_y,
                               uv_min_next,
                               uv_max_next);
            }
        }
        else
        {
            ComputeFrameUv(0u,
                           atlas_columns,
                           atlas_rows,
                           frame_count,
                           atlas_frame_width,
                           atlas_frame_height,
                           atlas_border_x,
                           atlas_border_y,
                           atlas_spacing_x,
                           atlas_spacing_y,
                           uv_min,
                           uv_max);
            uv_min_next = uv_min;
            uv_max_next = uv_max;
            frame_blend = 0.0;
        }
    }

    float3 particle_right = g_CameraRight.xyz;
    float3 particle_up = g_CameraUp.xyz;
    if (alignment == 1u)
    {
        particle_right = float3(1.0, 0.0, 0.0);
        particle_up = float3(0.0, 0.0, 1.0);
    }

    float2 local = input.corner * size;
    float2 rotated = float2(local.x * input.rotation_size.x - local.y * input.rotation_size.y,
                            local.x * input.rotation_size.y + local.y * input.rotation_size.x);
    float3 world = input.position_age.xyz +
                   particle_right * rotated.x +
                   particle_up * rotated.y;

    PSInput output;
    output.pos = mul(float4(world, 1.0f), g_ViewProj);
    output.uv = lerp(uv_min, uv_max, input.uv);
    output.uv_next = lerp(uv_min_next, uv_max_next, input.uv);
    output.local_uv = input.uv;
    output.color = color;
    output.frame_blend = frame_blend;
    output.world_pos = world;
#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
    output.material_id = material_id;
#endif
    return output;
}
)";

static constexpr const char* kParticlePS = R"(
cbuffer Constants
{
    row_major float4x4 g_ViewProj;
    float4 g_CameraRight;
    float4 g_CameraUp;
    float4 g_CameraForward;
    float4 g_Params;
    float4 g_ScreenParams;
    float4 g_CameraParams;
    float4 g_CameraPosition;
    float4 g_ShadingParams;
    float4 g_PresentationParams;
};

#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
struct ParticleMaterialRecord
{
    uint texture_index;
    uint alignment;
    uint shading_mode;
    uint presentation_mode;
    float soft_particle_distance;
    float distortion_strength;
    float fresnel_power;
    float fresnel_strength;
    float refraction_strength;
    float interior_glow;
    float size_curve_exponent;
    float alpha_curve_exponent;
    uint atlas_columns;
    uint atlas_rows;
    uint atlas_frame_count;
    uint animate_over_lifetime;
    uint atlas_frame_width;
    uint atlas_frame_height;
    uint atlas_border_x;
    uint atlas_border_y;
    float atlas_spacing_x;
    float atlas_spacing_y;
    float animation_fps;
    float use_soft_mask;
};

StructuredBuffer<ParticleMaterialRecord> g_ParticleMaterials;
Texture2D g_ParticleTextures[KARMA_PARTICLE_TEXTURE_TABLE_SIZE];
SamplerState g_ParticleTextures_sampler;
#else
Texture2D g_Texture;
SamplerState g_Texture_sampler;
#endif
Texture2D g_SceneColor;
SamplerState g_SceneColor_sampler;
Texture2D<float> g_SceneDepth;
SamplerState g_SceneDepth_sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv_next : TEXCOORD1;
    float2 local_uv : TEXCOORD2;
    float4 color : COLOR0;
    float frame_blend : TEXCOORD3;
    float3 world_pos : TEXCOORD4;
#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
    nointerpolation uint material_id : TEXCOORD5;
#endif
};

float linearizeDepth(float depth)
{
    float near_clip = g_CameraParams.x;
    float far_clip = g_CameraParams.y;
    if (g_CameraParams.z > 0.5f)
    {
        return (near_clip * far_clip) / max(far_clip - depth * (far_clip - near_clip), 1.0e-4f);
    }
    return near_clip + depth * (far_clip - near_clip);
}

float3 reconstructSphereNormal(float2 centered)
{
    const float radial_sq = dot(centered, centered);
    const float z = sqrt(saturate(1.0f - radial_sq));
    float3 normal_ws = centered.x * g_CameraRight.xyz +
                       centered.y * g_CameraUp.xyz +
                       z * g_CameraForward.xyz;
    const float normal_len_sq = dot(normal_ws, normal_ws);
    if (normal_len_sq <= 1.0e-6f)
    {
        return normalize(g_CameraForward.xyz);
    }
    return normal_ws * rsqrt(normal_len_sq);
}

float4 main(PSInput input) : SV_TARGET
{
    float use_soft_mask = g_Params.x;
    float soft_particle_distance = g_Params.z;
    float distortion_strength = g_Params.w;
    uint shading_mode = (uint)max(g_CameraParams.w, 0.0f);
    float fresnel_power = g_ShadingParams.x;
    float fresnel_strength = g_ShadingParams.y;
    float refraction_strength = g_ShadingParams.z;
    float interior_glow = g_ShadingParams.w;
#if defined(KARMA_PARTICLE_GLOBAL_MATERIALS)
    ParticleMaterialRecord material = g_ParticleMaterials[input.material_id];
    use_soft_mask = material.use_soft_mask;
    soft_particle_distance = material.soft_particle_distance;
    distortion_strength = material.distortion_strength;
    shading_mode = material.shading_mode;
    fresnel_power = material.fresnel_power;
    fresnel_strength = material.fresnel_strength;
    refraction_strength = material.refraction_strength;
    interior_glow = material.interior_glow;
    uint texture_index = min(material.texture_index,
                             (uint)(KARMA_PARTICLE_TEXTURE_TABLE_SIZE - 1));
    float4 texel =
        g_ParticleTextures[NonUniformResourceIndex(texture_index)].Sample(g_ParticleTextures_sampler, input.uv);
    float4 next_texel =
        g_ParticleTextures[NonUniformResourceIndex(texture_index)].Sample(g_ParticleTextures_sampler, input.uv_next);
#else
    float4 texel = g_Texture.Sample(g_Texture_sampler, input.uv);
    float4 next_texel = g_Texture.Sample(g_Texture_sampler, input.uv_next);
#endif
    texel = lerp(texel, next_texel, input.frame_blend);
    float alpha = texel.a * input.color.a;
    if (use_soft_mask > 0.5f)
    {
        float2 centered = input.local_uv * 2.0f - 1.0f;
        float radial = saturate(1.0f - dot(centered, centered));
        alpha *= radial * radial;
    }

    float2 screen_uv = input.pos.xy * g_ScreenParams.zw;
    if (soft_particle_distance > 0.0f || g_PresentationParams.w > 0.5f)
    {
        float scene_depth = g_SceneDepth.Sample(g_SceneDepth_sampler, screen_uv);
        float particle_depth = saturate(input.pos.z);
        float scene_linear_depth = linearizeDepth(scene_depth);
        float particle_linear_depth = linearizeDepth(particle_depth);
        if (g_PresentationParams.w > 0.5f &&
            particle_linear_depth > scene_linear_depth + 1.0e-3f)
        {
            discard;
        }
        if (soft_particle_distance > 0.0f)
        {
            float fade = saturate((scene_linear_depth - particle_linear_depth) /
                                  max(soft_particle_distance, 1.0e-4f));
            alpha *= fade;
        }
    }

    if (alpha <= 1.0e-4f)
    {
        discard;
    }

    if (g_Params.y > 1.5f)
    {
        float2 centered = input.local_uv * 2.0f - 1.0f;
        float radius = length(centered);
        float2 direction = radius > 1.0e-4f ? centered / radius : float2(0.0f, -1.0f);
        float2 tangent = float2(-direction.y, direction.x);
        float2 offset_pixels = (direction * (texel.r * 2.0f - 1.0f) +
                                tangent * (texel.g * 2.0f - 1.0f) * 0.45f) *
                               (distortion_strength * alpha);
        float2 distorted_uv = saturate(screen_uv + offset_pixels * g_ScreenParams.zw);
        float3 scene_color = g_SceneColor.Sample(g_SceneColor_sampler, distorted_uv).rgb;
        return float4(scene_color, alpha);
    }

    const float3 texture_color = texel.rgb * input.color.rgb;
    if (shading_mode > 0u)
    {
        float2 centered = input.local_uv * 2.0f - 1.0f;
        const float radial_sq = dot(centered, centered);
        if (radial_sq >= 1.0f)
        {
            discard;
        }

        const float3 sphere_normal = reconstructSphereNormal(centered);
        const float3 view_dir = normalize(g_CameraPosition.xyz - input.world_pos);
        const float body = pow(saturate(1.0f - radial_sq), 0.62f);
        const float radius = sqrt(radial_sq);
        const float outline =
            smoothstep(0.66f, 0.90f, radius) * (1.0f - smoothstep(0.93f, 0.995f, radius));
        const float fresnel =
            pow(saturate(1.0f - dot(view_dir, sphere_normal)), max(fresnel_power, 0.001f)) *
            fresnel_strength;
        const float3 light_dir = normalize(float3(-0.38f, 0.64f, 0.67f));
        const float3 reflected = reflect(-light_dir, sphere_normal);
        const float specular =
            pow(saturate(dot(reflected, view_dir)), 42.0f) * (0.18f + fresnel * 0.82f);
        const float2 turbulence = texel.rg * 2.0f - 1.0f;
        const float2 refract_pixels =
            (centered * 0.65f + turbulence * 0.35f) *
            (refraction_strength * (0.18f + body * 0.16f + fresnel * 0.36f) * alpha);
        const float2 refract_uv = saturate(screen_uv + refract_pixels * g_ScreenParams.zw);
        const float3 scene_color = g_SceneColor.Sample(g_SceneColor_sampler, refract_uv).rgb;

        const float3 glass_tint =
            lerp(float3(0.72f, 0.90f, 1.0f), texture_color, 0.55f);
        const float rim_light = saturate(fresnel * (0.52f + texel.a * 0.48f));
        const float inner_tint = body * (0.16f + texel.b * 0.10f);
        const float shell_alpha =
            saturate(alpha * (0.16f + body * 0.22f + outline * 0.58f +
                              rim_light * 0.34f + specular * 0.26f));
        const float3 refracted_color =
            lerp(scene_color,
                 scene_color * (0.88f + glass_tint * 0.12f) + glass_tint * body * 0.06f,
                 inner_tint);
        const float3 shell_rim =
            glass_tint * (outline * 0.72f + rim_light * 0.56f + specular * 1.25f + texel.r * 0.08f);
        const float3 interior =
            glass_tint * (interior_glow * body * (0.12f + texel.a * 0.18f));
        const float3 combined = refracted_color + shell_rim + interior;
        return float4(combined, shell_alpha);
    }

    return float4(texture_color * alpha, alpha);
}
)";

static constexpr const char* kParticleHalfResCompositeVS = R"(
struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vid : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    output.uv = uv;
    output.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
)";

static constexpr const char* kParticleHalfResCompositePS = R"(
Texture2D g_HalfResAlpha;
SamplerState g_HalfResAlpha_sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return g_HalfResAlpha.Sample(g_HalfResAlpha_sampler, input.uv);
}
)";

static constexpr const char* kParticleSimCS = R"(
cbuffer ParticleSimConstants
{
    float4 g_PositionTime;
    float4 g_Rotation;
    float4 g_ScaleSeed;
    float4 g_Playback;
    float4 g_Emission;
    float4 g_Lifetime;
    float4 g_Size;
    float4 g_RotationParams;
    float4 g_SpawnBox;
    float4 g_SpawnSphere;
    float4 g_SourceParams0;
    float4 g_SourceParams1;
    float4 g_SourceParams2;
    float4 g_SourceMesh;
    float4 g_SourcePath0;
    float4 g_SourcePath1;
    float4 g_SourcePath2;
    float4 g_SourcePath3;
    float4 g_SourcePath4;
    float4 g_SourcePath5;
    float4 g_SourcePath6;
    float4 g_SourcePath7;
    float4 g_VelocityMin;
    float4 g_VelocityMax;
    float4 g_AccelerationDrag;
    float4 g_Collision;
    float4 g_ColorStart;
    float4 g_ColorEnd;
    float4 g_Output;
};

struct ParticleInstance
{
    float4 position_age;
    float4 color_start;
    float4 color_end;
    float4 rotation_size;
    float4 uv_rect;
    float4 uv_rect_next;
    float4 params;
};

RWStructuredBuffer<ParticleInstance> g_OutParticles;

uint Hash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float Rand01(uint slot, uint salt)
{
    uint seed = (uint)max(g_ScaleSeed.w, 1.0);
    return (float)(Hash(slot ^ Hash(seed + salt * 747796405u)) & 0x00ffffffu) /
           16777215.0;
}

float Range(float a, float b, uint slot, uint salt)
{
    return lerp(a, b, Rand01(slot, salt));
}

float3 RotateByQuat(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float3 RandomUnitVector(uint slot)
{
    float z = Range(-1.0, 1.0, slot, 17u);
    float angle = Range(0.0, 6.28318530718, slot, 19u);
    float r = sqrt(max(1.0 - z * z, 0.0));
    return float3(cos(angle) * r, z, sin(angle) * r);
}

float2 RandomDisc(uint slot, float radius_min, float radius_max)
{
    float a = Range(0.0, 6.28318530718, slot, 61u);
    float min_r = max(radius_min, 0.0);
    float max_r = max(radius_max, min_r);
    float r2 = lerp(min_r * min_r, max_r * max_r, Rand01(slot, 67u));
    float r = sqrt(max(r2, 0.0));
    return float2(cos(a) * r, sin(a) * r);
}

float3 SourcePathPoint(uint index)
{
    if (index == 0u) return g_SourcePath0.xyz;
    if (index == 1u) return g_SourcePath1.xyz;
    if (index == 2u) return g_SourcePath2.xyz;
    if (index == 3u) return g_SourcePath3.xyz;
    if (index == 4u) return g_SourcePath4.xyz;
    if (index == 5u) return g_SourcePath5.xyz;
    if (index == 6u) return g_SourcePath6.xyz;
    return g_SourcePath7.xyz;
}

float3 SamplePath(uint slot, bool force_line)
{
    uint point_count = min((uint)max(g_SourceParams2.x, 0.0), 8u);
    if (force_line && point_count < 2u)
    {
        float half_length = max(max(g_SourceParams1.x, g_SpawnBox.x), 0.5);
        return float3(Range(-half_length, half_length, slot, 71u), 0.0, 0.0);
    }
    if (point_count == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }
    uint sampling = (uint)max(g_SourceParams2.y, 0.0);
    if (sampling == 2u || point_count == 1u)
    {
        return SourcePathPoint(slot % point_count);
    }
    bool closed_loop = g_SourceParams1.w > 0.5;
    uint segment_count = closed_loop ? point_count : max(point_count - 1u, 1u);
    float u = sampling == 1u
                  ? frac((float)slot * 0.61803398875)
                  : Rand01(slot, 73u);
    float scaled = u * (float)segment_count;
    uint segment = min((uint)floor(scaled), segment_count - 1u);
    float t = frac(scaled);
    uint next_index = segment + 1u;
    if (closed_loop && next_index >= point_count)
    {
        next_index = 0u;
    }
    else
    {
        next_index = min(next_index, point_count - 1u);
    }
    return lerp(SourcePathPoint(segment), SourcePathPoint(next_index), t);
}

float3 AddSourceJitter(float3 offset, uint slot)
{
    float jitter_radius = max(g_SourceParams0.w, 0.0);
    if (jitter_radius <= 0.0)
    {
        return offset;
    }
    return offset + RandomUnitVector(slot) * Range(0.0, jitter_radius, slot, 79u);
}

float3 RandomSpawnOffset(uint slot)
{
    uint shape = (uint)max(g_SpawnBox.w, 0.0);
    if (shape == 0u)
    {
        return AddSourceJitter(float3(Range(-g_SpawnBox.x, g_SpawnBox.x, slot, 1u),
                                      Range(-g_SpawnBox.y, g_SpawnBox.y, slot, 2u),
                                      Range(-g_SpawnBox.z, g_SpawnBox.z, slot, 3u)),
                               slot);
    }

    float radius = Range(max(g_SpawnSphere.x, 0.0),
                         max(g_SpawnSphere.y, 0.0),
                         slot,
                         5u);
    if (shape == 2u)
    {
        radius = max(g_SpawnSphere.y, g_SpawnSphere.x);
    }
    if (shape <= 2u)
    {
        return AddSourceJitter(RandomUnitVector(slot) * radius, slot);
    }
    if (shape == 3u || shape == 4u)
    {
        float min_r = shape == 4u ? max(g_SourceParams0.x, 0.0) : max(g_SpawnSphere.x, 0.0);
        float max_r = shape == 4u ? max(g_SourceParams0.y, min_r) : max(g_SpawnSphere.y, min_r);
        float2 disc = RandomDisc(slot, min_r, max_r);
        return AddSourceJitter(float3(disc.x, 0.0, disc.y), slot);
    }
    if (shape == 5u || shape == 6u || shape == 7u)
    {
        float height = max(g_SourceParams1.x, 0.0);
        float half_height = height * 0.5;
        float y = Range(-half_height, half_height, slot, 83u);
        float radius_max = max(g_SpawnSphere.y, max(g_SourceParams0.y, g_SourceParams1.y));
        if (shape == 7u && height > 1.0e-4)
        {
            float t = saturate((y + half_height) / height);
            radius_max *= 1.0 - t;
        }
        float2 disc = RandomDisc(slot, 0.0, radius_max);
        float3 offset = float3(disc.x, y, disc.y);
        if (shape == 6u && abs(y) > max(half_height - radius_max, 0.0))
        {
            float cap_sign = y >= 0.0 ? 1.0 : -1.0;
            offset += RandomUnitVector(slot) * radius_max;
            offset.y = cap_sign * half_height + offset.y * 0.35;
        }
        return AddSourceJitter(offset, slot);
    }
    if (shape == 8u)
    {
        return AddSourceJitter(SamplePath(slot, true), slot);
    }
    if (shape == 9u || shape == 10u)
    {
        return AddSourceJitter(SamplePath(slot, false), slot);
    }
    if (shape == 11u)
    {
        float mesh_radius = max(g_SourceMesh.w, max(g_SpawnSphere.y, 0.0));
        return AddSourceJitter(g_SourceMesh.xyz + RandomUnitVector(slot) * mesh_radius, slot);
    }
    return float3(0.0, 0.0, 0.0);
}

float ResolveLifetime(uint slot)
{
    return max(Range(g_Lifetime.x, g_Lifetime.y, slot, 7u), 0.01);
}

bool ResolveAge(uint slot, out float age, out float lifetime)
{
    age = 0.0;
    lifetime = ResolveLifetime(slot);

    uint flags = (uint)(g_Emission.w + 0.5);
    bool emit_burst = (flags & 1u) != 0u;
    bool loop = (flags & 2u) != 0u;
    bool enabled = (flags & 16u) != 0u;
    bool playing = (flags & 32u) != 0u;
    bool visible = (flags & 64u) != 0u;
    if (!enabled || !playing || !visible)
    {
        return false;
    }

    float active_time = max(g_Playback.x - max(g_Playback.z, 0.0), 0.0);
    if (active_time <= 0.0 && g_Playback.z > 0.0)
    {
        return false;
    }

    uint burst_count = (uint)max(g_Emission.y, 0.0);
    if (emit_burst && slot < burst_count)
    {
        age = active_time;
        return age < lifetime;
    }

    float spawn_rate = max(g_Emission.z, 0.0);
    if (spawn_rate <= 0.0)
    {
        return false;
    }

    uint continuous_slot = emit_burst && slot >= burst_count ? slot - burst_count : slot;
    float slot_offset = (float)continuous_slot / spawn_rate;
    if (loop)
    {
        uint max_particles = max((uint)g_Emission.x, 1u);
        uint continuous_capacity = max_particles - min(max_particles, burst_count);
        float cycle = max((float)max(continuous_capacity, 1u) / spawn_rate, 1.0 / spawn_rate);
        age = fmod(active_time - slot_offset, cycle);
        if (age < 0.0)
        {
            age += cycle;
        }
        return age < lifetime && active_time >= slot_offset;
    }

    float birth_time = slot_offset;
    float duration = max(g_Playback.w, 0.0);
    if (duration > 0.0 && birth_time > duration)
    {
        return false;
    }
    if (active_time < birth_time)
    {
        return false;
    }
    age = active_time - birth_time;
    return age < lifetime;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint slot = dispatch_id.x;
    uint particle_count = (uint)max(g_Output.y, 0.0);
    if (slot >= particle_count)
    {
        return;
    }

    uint output_index = (uint)max(g_Output.x, 0.0) + slot;
    ParticleInstance particle;
    particle.position_age = float4(0.0, 0.0, 0.0, 1.0);
    particle.color_start = float4(0.0, 0.0, 0.0, 0.0);
    particle.color_end = float4(0.0, 0.0, 0.0, 0.0);
    particle.rotation_size = float4(1.0, 0.0, 0.0, 0.0);
    particle.uv_rect = float4(0.0, 0.0, 1.0, 1.0);
    particle.uv_rect_next = float4(0.0, 0.0, 1.0, 1.0);
    particle.params = float4(0.0, 0.0, 0.0, 0.0);

    float age;
    float lifetime;
    if (!ResolveAge(slot, age, lifetime))
    {
        g_OutParticles[output_index] = particle;
        return;
    }

    uint flags = (uint)(g_Emission.w + 0.5);
    bool local_space = (flags & 4u) != 0u;
    bool collide_ground = (flags & 8u) != 0u;

    float3 local_offset = RandomSpawnOffset(slot);
    float3 random_velocity =
        float3(Range(g_VelocityMin.x, g_VelocityMax.x, slot, 23u),
               Range(g_VelocityMin.y, g_VelocityMax.y, slot, 29u),
               Range(g_VelocityMin.z, g_VelocityMax.z, slot, 31u));
    float3 radial_dir = length(local_offset) > 1.0e-4
                            ? normalize(local_offset)
                            : RandomUnitVector(slot);
    random_velocity += radial_dir * Range(g_SpawnSphere.z, g_SpawnSphere.w, slot, 37u);

    float drag = max(g_AccelerationDrag.w, 0.0);
    float drag_factor = drag > 0.0 ? exp(-drag * age) : 1.0;
    float3 moved =
        local_offset +
        random_velocity * (drag > 0.0 ? (1.0 - drag_factor) / max(drag, 1.0e-4) : age) +
        g_AccelerationDrag.xyz * (0.5 * age * age);

    float3 world_position;
    if (local_space)
    {
        float3 scaled = moved * max(g_ScaleSeed.xyz, float3(1.0e-4, 1.0e-4, 1.0e-4));
        world_position = g_PositionTime.xyz + RotateByQuat(g_Rotation, scaled);
    }
    else
    {
        world_position = g_PositionTime.xyz + RotateByQuat(g_Rotation, local_offset) +
                         RotateByQuat(g_Rotation, random_velocity) *
                             (drag > 0.0 ? (1.0 - drag_factor) / max(drag, 1.0e-4) : age) +
                         g_AccelerationDrag.xyz * (0.5 * age * age);
    }

    if (collide_ground && world_position.y < g_Collision.x)
    {
        world_position.y = g_Collision.x;
    }

    float normalized_age = saturate(age / max(lifetime, 0.01));
    float start_size = max(Range(g_Size.x, g_Size.y, slot, 41u), 0.0);
    float end_size = max(Range(g_Size.z, g_Size.w, slot, 43u), 0.0);
    float rotation = Range(g_RotationParams.x, g_RotationParams.y, slot, 47u) +
                     Range(g_RotationParams.z, g_RotationParams.w, slot, 53u) * age;

    particle.position_age = float4(world_position, normalized_age);
    particle.color_start = g_ColorStart;
    particle.color_end = g_ColorEnd;
    particle.rotation_size = float4(cos(rotation), sin(rotation), start_size, end_size);
    particle.params = float4(0.0, floor(Rand01(slot, 59u) * 1024.0), age, 0.0);
    g_OutParticles[output_index] = particle;
}
)";

static constexpr const char* kParticleGpuCommonHLSL = R"(
cbuffer ParticleGpuFrameConstants
{
    uint g_EmitterCount;
    uint g_ParticleCapacity;
    uint g_GroupCount;
    uint g_SortCapacity;
    uint g_GlobalSortActive;
    uint g_GroupedSortFallback;
    uint g_FramePad0;
    uint g_FramePad1;
    float4 g_CameraPosition;
    float4 g_CameraForward;
};

struct ParticleGpuEmitterDesc
{
    float4 position;
    float4 rotation;
    float4 scale_time;
    float4 playback;
    float4 emission;
    float4 lifetime;
    float4 size;
    float4 rotation_params;
    float4 spawn_box;
    float4 spawn_sphere;
    float4 source_params0;
    float4 source_params1;
    float4 source_params2;
    float4 source_mesh;
    float4 source_path0;
    float4 source_path1;
    float4 source_path2;
    float4 source_path3;
    float4 source_path4;
    float4 source_path5;
    float4 source_path6;
    float4 source_path7;
    float4 velocity_min;
    float4 velocity_max;
    float4 acceleration_drag;
    float4 collision;
    float4 color_start;
    float4 color_end;
    uint slot_offset;
    uint slot_capacity;
    uint group_index;
    uint restart_count;
    uint seed;
    uint flags;
    uint emitter_index;
    uint emitter_state_index;
    uint material_id;
    uint source_mesh_sample_offset;
    uint source_mesh_sample_count;
    uint pad0;
};

struct ParticleGpuEmitterState
{
    float elapsed_seconds;
    float previous_elapsed_seconds;
    float spawn_accumulator;
    float pad0;
    uint restart_count;
    uint burst_emitted;
    uint spawn_budget;
    uint spawned_cursor;
};

struct ParticleGpuState
{
    float4 position_lifetime;
    float4 velocity_age;
    float4 color_start;
    float4 color_end;
    float4 rotation_size;
    uint emitter_index;
    uint group_index;
    uint flags;
    uint frame_offset;
};

struct ParticleGpuMeshSample
{
    float4 p0;
    float4 p1;
    float4 p2;
};

StructuredBuffer<ParticleGpuMeshSample> g_MeshSamples;

struct ParticleGpuMaterialGroup
{
    uint instance_base;
    uint sort_base;
    uint max_particles;
    uint sort_capacity;
    uint flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct ParticleGpuSortItem
{
    uint key;
    uint state_index;
    uint group_index;
    uint material_id;
};

struct ParticleGpuIndirectArgs
{
    uint num_vertices;
    uint num_instances;
    uint start_vertex;
    uint first_instance;
};

struct ParticleGpuStats
{
    uint particle_capacity;
    uint alive_particles;
    uint dead_particles;
    uint spawned_particles;
    uint killed_particles;
    uint compacted_particles;
    uint indirect_draws;
    uint indirect_dispatches;
    uint sort_key_count;
    uint sort_overflow;
    uint fallback_active;
    uint culled_particles;
    uint culling_dispatches;
    uint global_sort_active;
    uint grouped_sort_fallback;
    uint pad;
};

struct ParticleInstance
{
    float4 position_age;
    float4 color_start;
    float4 color_end;
    float4 rotation_size;
    float4 uv_rect;
    float4 uv_rect_next;
    float4 params;
};

static const uint kEmitterFlagBurst = 1u;
static const uint kEmitterFlagLoop = 2u;
static const uint kEmitterFlagLocalSpace = 4u;
static const uint kEmitterFlagCollideGround = 8u;
static const uint kEmitterFlagEnabled = 16u;
static const uint kEmitterFlagPlaying = 32u;
static const uint kEmitterFlagVisible = 64u;
static const uint kEmitterFlagReset = 128u;
static const uint kGroupFlagSortable = 1u;
static const uint kParticleFlagAlive = 1u;
static const uint kParticleFlagResting = 2u;
static const uint kInvalidIndex = 0xffffffffu;

uint Hash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float Rand01(uint seed, uint slot, uint salt)
{
    return (float)(Hash(slot ^ Hash(max(seed, 1u) + salt * 747796405u)) & 0x00ffffffu) /
           16777215.0;
}

float Range(float a, float b, uint seed, uint slot, uint salt)
{
    return lerp(a, b, Rand01(seed, slot, salt));
}

float3 RotateByQuat(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float3 RandomUnitVector(uint seed, uint slot)
{
    float z = Range(-1.0, 1.0, seed, slot, 17u);
    float angle = Range(0.0, 6.28318530718, seed, slot, 19u);
    float r = sqrt(max(1.0 - z * z, 0.0));
    return float3(cos(angle) * r, z, sin(angle) * r);
}

float2 RandomDisc(uint seed, uint slot, float radius_min, float radius_max)
{
    float a = Range(0.0, 6.28318530718, seed, slot, 61u);
    float min_r = max(radius_min, 0.0);
    float max_r = max(radius_max, min_r);
    float r2 = lerp(min_r * min_r, max_r * max_r, Rand01(seed, slot, 67u));
    float r = sqrt(max(r2, 0.0));
    return float2(cos(a) * r, sin(a) * r);
}

float3 SourcePathPoint(ParticleGpuEmitterDesc emitter, uint index)
{
    if (index == 0u) return emitter.source_path0.xyz;
    if (index == 1u) return emitter.source_path1.xyz;
    if (index == 2u) return emitter.source_path2.xyz;
    if (index == 3u) return emitter.source_path3.xyz;
    if (index == 4u) return emitter.source_path4.xyz;
    if (index == 5u) return emitter.source_path5.xyz;
    if (index == 6u) return emitter.source_path6.xyz;
    return emitter.source_path7.xyz;
}

float3 SamplePath(ParticleGpuEmitterDesc emitter, uint slot, bool force_line)
{
    uint point_count = min((uint)max(emitter.source_params2.x, 0.0), 8u);
    if (force_line && point_count < 2u)
    {
        float half_length = max(max(emitter.source_params1.x, emitter.spawn_box.x), 0.5);
        return float3(Range(-half_length, half_length, emitter.seed, slot, 71u), 0.0, 0.0);
    }
    if (point_count == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }
    uint sampling = (uint)max(emitter.source_params2.y, 0.0);
    if (sampling == 2u || point_count == 1u)
    {
        return SourcePathPoint(emitter, slot % point_count);
    }
    bool closed_loop = emitter.source_params1.w > 0.5;
    uint segment_count = closed_loop ? point_count : max(point_count - 1u, 1u);
    float u = sampling == 1u
                  ? frac((float)slot * 0.61803398875)
                  : Rand01(emitter.seed, slot, 73u);
    float scaled = u * (float)segment_count;
    uint segment = min((uint)floor(scaled), segment_count - 1u);
    float t = frac(scaled);
    uint next_index = segment + 1u;
    if (closed_loop && next_index >= point_count)
    {
        next_index = 0u;
    }
    else
    {
        next_index = min(next_index, point_count - 1u);
    }
    return lerp(SourcePathPoint(emitter, segment), SourcePathPoint(emitter, next_index), t);
}

float3 AddSourceJitter(ParticleGpuEmitterDesc emitter, float3 offset, uint slot)
{
    float jitter_radius = max(emitter.source_params0.w, 0.0);
    if (jitter_radius <= 0.0)
    {
        return offset;
    }
    return offset + RandomUnitVector(emitter.seed, slot) *
        Range(0.0, jitter_radius, emitter.seed, slot, 79u);
}

float3 MeshFallbackOffset(ParticleGpuEmitterDesc emitter, uint slot)
{
    float mesh_radius = max(emitter.source_mesh.w, max(emitter.spawn_sphere.y, 0.0));
    return emitter.source_mesh.xyz + RandomUnitVector(emitter.seed, slot) * mesh_radius;
}

float3 SampleMeshSurface(ParticleGpuEmitterDesc emitter, uint slot)
{
    uint sample_count = emitter.source_mesh_sample_count;
    if (sample_count == 0u)
    {
        return MeshFallbackOffset(emitter, slot);
    }

    uint offset = emitter.source_mesh_sample_offset;
    uint last_index = offset + sample_count - 1u;
    float total_area = g_MeshSamples[last_index].p0.w;
    if (total_area <= 1.0e-7)
    {
        return MeshFallbackOffset(emitter, slot);
    }

    uint sampling = (uint)max(emitter.source_params2.y, 0.0);
    uint distribution = (uint)max(emitter.source_params2.z, 0.0);
    uint sample_index = offset;
    if (sampling == 1u)
    {
        sample_index = offset + (slot % sample_count);
    }
    else
    {
        float target = Rand01(emitter.seed, slot, 89u) * total_area;
        uint lo = 0u;
        uint hi = sample_count - 1u;
        [loop]
        while (lo < hi)
        {
            uint mid = (lo + hi) >> 1u;
            if (g_MeshSamples[offset + mid].p0.w < target)
            {
                lo = mid + 1u;
            }
            else
            {
                hi = mid;
            }
        }
        sample_index = offset + lo;
    }

    ParticleGpuMeshSample sample = g_MeshSamples[sample_index];
    float3 p0 = sample.p0.xyz;
    float3 p1 = sample.p1.xyz;
    float3 p2 = sample.p2.xyz;
    if (sampling == 2u)
    {
        uint vertex = slot % 3u;
        return vertex == 0u ? p0 : (vertex == 1u ? p1 : p2);
    }
    if (distribution == 2u)
    {
        uint edge = slot % 3u;
        float t = sampling == 1u
                      ? frac((float)slot * 0.754877666)
                      : Rand01(emitter.seed, slot, 97u);
        if (edge == 0u) return lerp(p0, p1, t);
        if (edge == 1u) return lerp(p1, p2, t);
        return lerp(p2, p0, t);
    }

    float r0 = sampling == 1u ? frac((float)slot * 0.61803398875)
                              : Rand01(emitter.seed, slot, 101u);
    float r1 = sampling == 1u ? frac((float)slot * 0.41421356237)
                              : Rand01(emitter.seed, slot, 103u);
    float sr0 = sqrt(max(r0, 0.0));
    float b0 = 1.0 - sr0;
    float b1 = sr0 * (1.0 - r1);
    float b2 = sr0 * r1;
    return p0 * b0 + p1 * b1 + p2 * b2;
}

float3 RandomSpawnOffset(ParticleGpuEmitterDesc emitter, uint slot)
{
    uint shape = (uint)max(emitter.spawn_box.w, 0.0);
    if (shape == 0u)
    {
        return AddSourceJitter(
            emitter,
            float3(Range(-emitter.spawn_box.x, emitter.spawn_box.x, emitter.seed, slot, 1u),
                   Range(-emitter.spawn_box.y, emitter.spawn_box.y, emitter.seed, slot, 2u),
                   Range(-emitter.spawn_box.z, emitter.spawn_box.z, emitter.seed, slot, 3u)),
            slot);
    }

    float radius = Range(max(emitter.spawn_sphere.x, 0.0),
                         max(emitter.spawn_sphere.y, 0.0),
                         emitter.seed,
                         slot,
                         5u);
    if (shape == 2u)
    {
        radius = max(emitter.spawn_sphere.y, emitter.spawn_sphere.x);
    }
    if (shape <= 2u)
    {
        return AddSourceJitter(emitter, RandomUnitVector(emitter.seed, slot) * radius, slot);
    }
    if (shape == 3u || shape == 4u)
    {
        float min_r = shape == 4u ? max(emitter.source_params0.x, 0.0)
                                  : max(emitter.spawn_sphere.x, 0.0);
        float max_r = shape == 4u ? max(emitter.source_params0.y, min_r)
                                  : max(emitter.spawn_sphere.y, min_r);
        float2 disc = RandomDisc(emitter.seed, slot, min_r, max_r);
        return AddSourceJitter(emitter, float3(disc.x, 0.0, disc.y), slot);
    }
    if (shape == 5u || shape == 6u || shape == 7u)
    {
        float height = max(emitter.source_params1.x, 0.0);
        float half_height = height * 0.5;
        float y = Range(-half_height, half_height, emitter.seed, slot, 83u);
        float radius_max = max(emitter.spawn_sphere.y,
                               max(emitter.source_params0.y, emitter.source_params1.y));
        if (shape == 7u && height > 1.0e-4)
        {
            float t = saturate((y + half_height) / height);
            radius_max *= 1.0 - t;
        }
        float2 disc = RandomDisc(emitter.seed, slot, 0.0, radius_max);
        float3 offset = float3(disc.x, y, disc.y);
        if (shape == 6u && abs(y) > max(half_height - radius_max, 0.0))
        {
            float cap_sign = y >= 0.0 ? 1.0 : -1.0;
            offset += RandomUnitVector(emitter.seed, slot) * radius_max;
            offset.y = cap_sign * half_height + offset.y * 0.35;
        }
        return AddSourceJitter(emitter, offset, slot);
    }
    if (shape == 8u)
    {
        return AddSourceJitter(emitter, SamplePath(emitter, slot, true), slot);
    }
    if (shape == 9u || shape == 10u)
    {
        return AddSourceJitter(emitter, SamplePath(emitter, slot, false), slot);
    }
    if (shape == 11u)
    {
        return AddSourceJitter(emitter, SampleMeshSurface(emitter, slot), slot);
    }
    return float3(0.0, 0.0, 0.0);
}

float ResolveLifetime(ParticleGpuEmitterDesc emitter, uint slot)
{
    return max(Range(emitter.lifetime.x, emitter.lifetime.y, emitter.seed, slot, 7u), 0.01);
}

ParticleInstance MakeInstance(ParticleGpuState state, uint material_id)
{
    float normalized_age = saturate(state.velocity_age.w / max(state.position_lifetime.w, 0.01));
    float rotation = state.rotation_size.x;
    ParticleInstance particle;
    particle.position_age = float4(state.position_lifetime.xyz, normalized_age);
    particle.color_start = state.color_start;
    particle.color_end = state.color_end;
    particle.rotation_size =
        float4(cos(rotation), sin(rotation), state.rotation_size.z, state.rotation_size.w);
    particle.uv_rect = float4(0.0, 0.0, 1.0, 1.0);
    particle.uv_rect_next = float4(0.0, 0.0, 1.0, 1.0);
    particle.params = float4(0.0,
                             (float)state.frame_offset,
                             state.velocity_age.w,
                             (float)material_id);
    return particle;
}

uint FloatToSortable(float value)
{
    uint bits = asuint(value);
    return (bits & 0x80000000u) != 0u ? ~bits : (bits ^ 0x80000000u);
}

bool ParticleContributesToDraw(ParticleGpuState state)
{
    float normalized_age = saturate(state.velocity_age.w / max(state.position_lifetime.w, 0.01));
    float alpha = lerp(state.color_start.a, state.color_end.a, normalized_age);
    float size = lerp(state.rotation_size.z, state.rotation_size.w, normalized_age);
    return alpha > 1.0e-4 && size > 1.0e-5;
}
)";

static constexpr const char* kParticleGpuClearCS = R"(
StructuredBuffer<ParticleGpuMaterialGroup> g_Groups;
RWStructuredBuffer<uint> g_GroupCounters;
RWStructuredBuffer<ParticleGpuSortItem> g_SortItems;
RWStructuredBuffer<ParticleGpuIndirectArgs> g_DrawArgs;
RWStructuredBuffer<uint4> g_DispatchArgs;
RWStructuredBuffer<ParticleGpuStats> g_Stats;

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint id = dispatch_id.x;
    if (id == 0u)
    {
        ParticleGpuStats stats;
        stats.particle_capacity = g_ParticleCapacity;
        stats.alive_particles = 0u;
        stats.dead_particles = 0u;
        stats.spawned_particles = 0u;
        stats.killed_particles = 0u;
        stats.compacted_particles = 0u;
        stats.indirect_draws = 0u;
        stats.indirect_dispatches = 0u;
        stats.sort_key_count = 0u;
        stats.sort_overflow = 0u;
        stats.fallback_active = 0u;
        stats.culled_particles = 0u;
        stats.culling_dispatches = 0u;
        stats.global_sort_active = g_GlobalSortActive;
        stats.grouped_sort_fallback = g_GroupedSortFallback;
        stats.pad = 0u;
        g_Stats[0] = stats;
        g_DispatchArgs[0] = uint4((g_ParticleCapacity + 63u) / 64u, 1u, 1u, 0u);
    }
    if (id < g_GroupCount)
    {
        g_GroupCounters[id] = 0u;
        ParticleGpuIndirectArgs args;
        args.num_vertices = 6u;
        args.num_instances = 0u;
        args.start_vertex = 0u;
        args.first_instance = 0u;
        g_DrawArgs[id] = args;
    }
    if (id < g_SortCapacity)
    {
        ParticleGpuSortItem item;
        item.key = kInvalidIndex;
        item.state_index = kInvalidIndex;
        item.group_index = 0u;
        item.material_id = 0u;
        g_SortItems[id] = item;
    }
}
)";

static constexpr const char* kParticleGpuUpdateEmittersCS = R"(
StructuredBuffer<ParticleGpuEmitterDesc> g_Emitters;
RWStructuredBuffer<ParticleGpuEmitterState> g_EmitterStates;

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint emitter_index = dispatch_id.x;
    if (emitter_index >= g_EmitterCount)
    {
        return;
    }

    ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
    ParticleGpuEmitterState state = g_EmitterStates[emitter.emitter_state_index];
    bool reset = (emitter.flags & kEmitterFlagReset) != 0u ||
                 state.restart_count != emitter.restart_count;
    if (reset)
    {
        state.elapsed_seconds = 0.0;
        state.previous_elapsed_seconds = 0.0;
        state.spawn_accumulator = 0.0;
        state.restart_count = emitter.restart_count;
        state.burst_emitted = 0u;
    }

    state.previous_elapsed_seconds = state.elapsed_seconds;
    bool enabled = (emitter.flags & kEmitterFlagEnabled) != 0u;
    bool playing = (emitter.flags & kEmitterFlagPlaying) != 0u;
    if (enabled && playing)
    {
        state.elapsed_seconds += max(emitter.playback.x, 0.0) * max(emitter.playback.y, 0.0);
    }

    uint spawn_budget = 0u;
    if (enabled && playing)
    {
        float start_delay = max(emitter.playback.z, 0.0);
        float active_prev = max(state.previous_elapsed_seconds - start_delay, 0.0);
        float active_now = max(state.elapsed_seconds - start_delay, 0.0);
        bool active = active_now > 0.0 || start_delay <= 0.0;
        if (active)
        {
            if ((emitter.flags & kEmitterFlagBurst) != 0u && state.burst_emitted == 0u)
            {
                spawn_budget += (uint)max(emitter.emission.y, 0.0);
                state.burst_emitted = 1u;
            }

            float duration = max(emitter.playback.w, 0.0);
            bool looping = (emitter.flags & kEmitterFlagLoop) != 0u;
            float emit_prev = active_prev;
            float emit_now = active_now;
            if (!looping && duration > 0.0)
            {
                emit_prev = min(emit_prev, duration);
                emit_now = min(emit_now, duration);
            }
            float emit_delta = max(emit_now - emit_prev, 0.0);
            float spawn_rate = max(emitter.emission.x, 0.0);
            state.spawn_accumulator += emit_delta * spawn_rate;
            uint continuous_budget = (uint)floor(state.spawn_accumulator);
            if (continuous_budget > 0u)
            {
                spawn_budget += continuous_budget;
                state.spawn_accumulator -= (float)continuous_budget;
            }
        }
    }

    state.spawn_budget = spawn_budget;
    state.spawned_cursor = 0u;
    g_EmitterStates[emitter.emitter_state_index] = state;
}
)";

static constexpr const char* kParticleGpuSimulateCS = R"(
StructuredBuffer<ParticleGpuEmitterDesc> g_Emitters;
RWStructuredBuffer<ParticleGpuEmitterState> g_EmitterStates;
RWStructuredBuffer<ParticleGpuState> g_ParticleStates;
RWStructuredBuffer<uint> g_AliveList;
RWStructuredBuffer<uint> g_DeadList;
RWStructuredBuffer<ParticleGpuStats> g_Stats;

uint FindEmitterForSlot(uint state_index)
{
    [loop]
    for (uint emitter_index = 0u; emitter_index < g_EmitterCount; ++emitter_index)
    {
        ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
        if (state_index >= emitter.slot_offset &&
            state_index < emitter.slot_offset + emitter.slot_capacity)
        {
            return emitter_index;
        }
    }
    return kInvalidIndex;
}

void RecordAlive(uint state_index)
{
    uint out_index = 0u;
    InterlockedAdd(g_Stats[0].alive_particles, 1u, out_index);
    if (out_index < g_ParticleCapacity)
    {
        g_AliveList[out_index] = state_index;
    }
}

void RecordDead(uint state_index)
{
    uint out_index = 0u;
    InterlockedAdd(g_Stats[0].dead_particles, 1u, out_index);
    if (out_index < g_ParticleCapacity)
    {
        g_DeadList[out_index] = state_index;
    }
}

ParticleGpuState SpawnParticle(ParticleGpuEmitterDesc emitter, uint emitter_index, uint state_index)
{
    ParticleGpuState state;
    uint local_slot = state_index - emitter.slot_offset;
    float3 local_offset = RandomSpawnOffset(emitter, state_index);
    float3 random_velocity =
        float3(Range(emitter.velocity_min.x, emitter.velocity_max.x, emitter.seed, state_index, 23u),
               Range(emitter.velocity_min.y, emitter.velocity_max.y, emitter.seed, state_index, 29u),
               Range(emitter.velocity_min.z, emitter.velocity_max.z, emitter.seed, state_index, 31u));
    float3 radial_dir = length(local_offset) > 1.0e-4
                            ? normalize(local_offset)
                            : RandomUnitVector(emitter.seed, state_index);
    random_velocity += radial_dir *
                       Range(emitter.spawn_sphere.z, emitter.spawn_sphere.w, emitter.seed, state_index, 37u);

    float3 world_offset = RotateByQuat(emitter.rotation, local_offset * max(emitter.scale_time.xyz, float3(1.0e-4, 1.0e-4, 1.0e-4)));
    float3 world_velocity = RotateByQuat(emitter.rotation, random_velocity);

    state.position_lifetime = float4(emitter.position.xyz + world_offset,
                                     ResolveLifetime(emitter, state_index));
    state.velocity_age = float4(world_velocity, 0.0);
    state.color_start = emitter.color_start;
    state.color_end = emitter.color_end;
    state.rotation_size = float4(
        Range(emitter.rotation_params.x, emitter.rotation_params.y, emitter.seed, state_index, 47u),
        Range(emitter.rotation_params.z, emitter.rotation_params.w, emitter.seed, state_index, 53u),
        max(Range(emitter.size.x, emitter.size.y, emitter.seed, state_index, 41u), 0.0),
        max(Range(emitter.size.z, emitter.size.w, emitter.seed, state_index, 43u), 0.0));
    state.emitter_index = emitter_index;
    state.group_index = emitter.group_index;
    state.flags = kParticleFlagAlive;
    state.frame_offset = (uint)floor(Rand01(emitter.seed, state_index, 59u) * 1024.0);
    return state;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint state_index = dispatch_id.x;
    if (state_index == 0u)
    {
        InterlockedAdd(g_Stats[0].indirect_dispatches, 1u);
    }
    if (state_index >= g_ParticleCapacity)
    {
        return;
    }

    uint emitter_index = FindEmitterForSlot(state_index);
    ParticleGpuState state = g_ParticleStates[state_index];
    if (emitter_index == kInvalidIndex)
    {
        if ((state.flags & kParticleFlagAlive) != 0u)
        {
            InterlockedAdd(g_Stats[0].killed_particles, 1u);
        }
        state.flags = 0u;
        g_ParticleStates[state_index] = state;
        RecordDead(state_index);
        return;
    }

    ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
    bool reset = (emitter.flags & kEmitterFlagReset) != 0u;
    bool enabled = (emitter.flags & kEmitterFlagEnabled) != 0u;
    bool alive = (state.flags & kParticleFlagAlive) != 0u;
    if (reset && alive)
    {
        alive = false;
        state.flags = 0u;
        InterlockedAdd(g_Stats[0].killed_particles, 1u);
    }
    if (!enabled && alive)
    {
        alive = false;
        state.flags = 0u;
        InterlockedAdd(g_Stats[0].killed_particles, 1u);
    }

    if (alive)
    {
        float dt = max(emitter.playback.x, 0.0) * max(emitter.playback.y, 0.0);
        bool playing = (emitter.flags & kEmitterFlagPlaying) != 0u;
        if (!playing)
        {
            dt = 0.0;
        }
        state.velocity_age.w += dt;
        if (state.velocity_age.w >= state.position_lifetime.w)
        {
            state.flags = 0u;
            g_ParticleStates[state_index] = state;
            InterlockedAdd(g_Stats[0].killed_particles, 1u);
            RecordDead(state_index);
            return;
        }

        float3 velocity = state.velocity_age.xyz;
        float drag = max(emitter.acceleration_drag.w, 0.0);
        if ((state.flags & kParticleFlagResting) == 0u)
        {
            velocity += emitter.acceleration_drag.xyz * dt;
            if (drag > 0.0)
            {
                velocity *= exp(-drag * dt);
            }
            state.position_lifetime.xyz += velocity * dt;
        }
        if ((emitter.flags & kEmitterFlagCollideGround) != 0u &&
            state.position_lifetime.y < emitter.collision.x)
        {
            state.position_lifetime.y = emitter.collision.x;
            velocity.y = -velocity.y * saturate(emitter.collision.y);
            velocity.xz *= 1.0 - saturate(emitter.collision.z);
            if (length(velocity) <= max(emitter.collision.w, 0.0))
            {
                velocity = float3(0.0, 0.0, 0.0);
                state.flags |= kParticleFlagResting;
            }
        }
        state.velocity_age.xyz = velocity;
        state.rotation_size.x += state.rotation_size.y * dt;
        state.group_index = emitter.group_index;
        g_ParticleStates[state_index] = state;
        RecordAlive(state_index);
        return;
    }

    ParticleGpuEmitterState emitter_state = g_EmitterStates[emitter.emitter_state_index];
    uint spawn_ordinal = 0u;
    InterlockedAdd(g_EmitterStates[emitter.emitter_state_index].spawned_cursor,
                   1u,
                   spawn_ordinal);
    if (enabled && spawn_ordinal < emitter_state.spawn_budget)
    {
        state = SpawnParticle(emitter, emitter_index, state_index);
        g_ParticleStates[state_index] = state;
        InterlockedAdd(g_Stats[0].spawned_particles, 1u);
        RecordAlive(state_index);
        return;
    }

    state.flags = 0u;
    g_ParticleStates[state_index] = state;
    RecordDead(state_index);
}
)";

static constexpr const char* kParticleGpuPrepareUnsortedCS = R"(
StructuredBuffer<ParticleGpuEmitterDesc> g_Emitters;
StructuredBuffer<ParticleGpuState> g_ParticleStates;
StructuredBuffer<ParticleGpuMaterialGroup> g_Groups;
RWStructuredBuffer<uint> g_GroupCounters;
RWStructuredBuffer<ParticleInstance> g_OutParticles;
RWStructuredBuffer<ParticleGpuStats> g_Stats;

uint FindEmitterForSlot(uint state_index)
{
    [loop]
    for (uint emitter_index = 0u; emitter_index < g_EmitterCount; ++emitter_index)
    {
        ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
        if (state_index >= emitter.slot_offset &&
            state_index < emitter.slot_offset + emitter.slot_capacity)
        {
            return emitter_index;
        }
    }
    return kInvalidIndex;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint state_index = dispatch_id.x;
    if (state_index == 0u)
    {
        InterlockedAdd(g_Stats[0].indirect_dispatches, 1u);
        InterlockedAdd(g_Stats[0].culling_dispatches, 1u);
    }
    if (state_index >= g_ParticleCapacity)
    {
        return;
    }
    ParticleGpuState state = g_ParticleStates[state_index];
    if ((state.flags & kParticleFlagAlive) == 0u)
    {
        return;
    }
    uint emitter_index = FindEmitterForSlot(state_index);
    if (emitter_index == kInvalidIndex)
    {
        return;
    }
    ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
    if ((emitter.flags & kEmitterFlagVisible) == 0u)
    {
        InterlockedAdd(g_Stats[0].culled_particles, 1u);
        return;
    }
    if (!ParticleContributesToDraw(state))
    {
        InterlockedAdd(g_Stats[0].culled_particles, 1u);
        return;
    }
    ParticleGpuMaterialGroup group = g_Groups[emitter.group_index];
    if ((group.flags & kGroupFlagSortable) != 0u)
    {
        return;
    }
    uint local_index = 0u;
    InterlockedAdd(g_GroupCounters[emitter.group_index], 1u, local_index);
    if (local_index >= group.max_particles)
    {
        g_Stats[0].sort_overflow = 1u;
        return;
    }
    g_OutParticles[group.instance_base + local_index] = MakeInstance(state, emitter.material_id);
    InterlockedAdd(g_Stats[0].compacted_particles, 1u);
}
)";

static constexpr const char* kParticleGpuGenerateSortCS = R"(
StructuredBuffer<ParticleGpuEmitterDesc> g_Emitters;
StructuredBuffer<ParticleGpuState> g_ParticleStates;
StructuredBuffer<ParticleGpuMaterialGroup> g_Groups;
RWStructuredBuffer<uint> g_GroupCounters;
RWStructuredBuffer<ParticleGpuSortItem> g_SortItems;
RWStructuredBuffer<ParticleGpuStats> g_Stats;

uint FindEmitterForSlot(uint state_index)
{
    [loop]
    for (uint emitter_index = 0u; emitter_index < g_EmitterCount; ++emitter_index)
    {
        ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
        if (state_index >= emitter.slot_offset &&
            state_index < emitter.slot_offset + emitter.slot_capacity)
        {
            return emitter_index;
        }
    }
    return kInvalidIndex;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint state_index = dispatch_id.x;
    if (state_index == 0u)
    {
        InterlockedAdd(g_Stats[0].indirect_dispatches, 1u);
        InterlockedAdd(g_Stats[0].culling_dispatches, 1u);
    }
    if (state_index >= g_ParticleCapacity)
    {
        return;
    }
    ParticleGpuState state = g_ParticleStates[state_index];
    if ((state.flags & kParticleFlagAlive) == 0u)
    {
        return;
    }
    uint emitter_index = FindEmitterForSlot(state_index);
    if (emitter_index == kInvalidIndex)
    {
        return;
    }
    ParticleGpuEmitterDesc emitter = g_Emitters[emitter_index];
    if ((emitter.flags & kEmitterFlagVisible) == 0u)
    {
        InterlockedAdd(g_Stats[0].culled_particles, 1u);
        return;
    }
    if (!ParticleContributesToDraw(state))
    {
        InterlockedAdd(g_Stats[0].culled_particles, 1u);
        return;
    }
    ParticleGpuMaterialGroup group = g_Groups[emitter.group_index];
    if ((group.flags & kGroupFlagSortable) == 0u)
    {
        return;
    }

    uint local_index = 0u;
    InterlockedAdd(g_GroupCounters[emitter.group_index], 1u, local_index);
    if (local_index >= group.sort_capacity || local_index >= group.max_particles)
    {
        g_Stats[0].sort_overflow = 1u;
        return;
    }

    float depth = dot(state.position_lifetime.xyz - g_CameraPosition.xyz,
                      normalize(g_CameraForward.xyz));
    uint sortable = FloatToSortable(depth);
    ParticleGpuSortItem item;
    item.key = 0xffffffffu - sortable;
    item.state_index = state_index;
    item.group_index = emitter.group_index;
    item.material_id = emitter.material_id;
    g_SortItems[group.sort_base + local_index] = item;
    InterlockedAdd(g_Stats[0].sort_key_count, 1u);
}
)";

static constexpr const char* kParticleGpuSortCS = R"(
cbuffer ParticleGpuSortConstants
{
    uint g_SortBase;
    uint g_SortCountPower2;
    uint g_SortK;
    uint g_SortJ;
};

RWStructuredBuffer<ParticleGpuSortItem> g_SortItems;

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint i = dispatch_id.x;
    if (i >= g_SortCountPower2)
    {
        return;
    }
    uint ixj = i ^ g_SortJ;
    if (ixj <= i || ixj >= g_SortCountPower2)
    {
        return;
    }

    uint ai = g_SortBase + i;
    uint bi = g_SortBase + ixj;
    ParticleGpuSortItem a = g_SortItems[ai];
    ParticleGpuSortItem b = g_SortItems[bi];
    bool ascending = (i & g_SortK) == 0u;
    bool swap_items = ascending ? (a.key > b.key) : (a.key < b.key);
    if (swap_items)
    {
        g_SortItems[ai] = b;
        g_SortItems[bi] = a;
    }
}
)";

static constexpr const char* kParticleGpuPrepareSortedCS = R"(
StructuredBuffer<ParticleGpuState> g_ParticleStates;
StructuredBuffer<ParticleGpuMaterialGroup> g_Groups;
StructuredBuffer<uint> g_GroupCounters;
StructuredBuffer<ParticleGpuSortItem> g_SortItems;
RWStructuredBuffer<ParticleInstance> g_OutParticles;
RWStructuredBuffer<ParticleGpuStats> g_Stats;

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint sort_index = dispatch_id.x;
    if (sort_index == 0u)
    {
        InterlockedAdd(g_Stats[0].indirect_dispatches, 1u);
    }
    if (sort_index >= g_SortCapacity)
    {
        return;
    }
    ParticleGpuSortItem item = g_SortItems[sort_index];
    if (item.state_index == kInvalidIndex)
    {
        return;
    }
    ParticleGpuMaterialGroup group = g_Groups[item.group_index];
    uint local_index = sort_index - group.sort_base;
    uint group_count = g_GroupCounters[item.group_index];
    if (local_index >= group_count || local_index >= group.max_particles)
    {
        return;
    }
    ParticleGpuState state = g_ParticleStates[item.state_index];
    if ((state.flags & kParticleFlagAlive) == 0u)
    {
        return;
    }
    g_OutParticles[group.instance_base + local_index] =
        MakeInstance(state, item.material_id);
    InterlockedAdd(g_Stats[0].compacted_particles, 1u);
}
)";

static constexpr const char* kParticleGpuIndirectArgsCS = R"(
StructuredBuffer<ParticleGpuMaterialGroup> g_Groups;
StructuredBuffer<uint> g_GroupCounters;
RWStructuredBuffer<ParticleGpuIndirectArgs> g_DrawArgs;
RWStructuredBuffer<ParticleGpuStats> g_Stats;

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint group_index = dispatch_id.x;
    if (group_index >= g_GroupCount)
    {
        return;
    }
    ParticleGpuMaterialGroup group = g_Groups[group_index];
    uint count = min(g_GroupCounters[group_index], group.max_particles);
    ParticleGpuIndirectArgs args;
    args.num_vertices = 6u;
    args.num_instances = count;
    args.start_vertex = 0u;
    args.first_instance = 0u;
    g_DrawArgs[group_index] = args;
    if (count > 0u)
    {
        InterlockedAdd(g_Stats[0].indirect_draws, 1u);
    }
}
)";

static const ParticleVertex kParticleQuadVertices[] = {
    {{-0.5f, -0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f}, {0.0f, 1.0f}},
    {{0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f}},
};
}  // namespace

void DiligentBackend::ensureParticleResources() {
  if (particle_pipeline_state_additive_depth_ &&
      particle_pipeline_state_additive_no_depth_ &&
      particle_pipeline_state_alpha_depth_ &&
      particle_pipeline_state_alpha_no_depth_ &&
      particle_pipeline_state_alpha_half_res_ &&
      particle_pipeline_state_distortion_depth_ &&
      particle_pipeline_state_distortion_no_depth_ &&
      particle_half_res_composite_pipeline_state_ &&
      particle_sim_compute_pso_ &&
      particle_gpu_clear_compute_pso_ &&
      particle_gpu_update_emitters_pso_ &&
      particle_gpu_simulate_pso_ &&
      particle_gpu_prepare_unsorted_pso_ &&
      particle_gpu_generate_sort_pso_ &&
      particle_gpu_sort_pso_ &&
      particle_gpu_prepare_sorted_pso_ &&
      particle_gpu_indirect_args_pso_) {
    return;
  }
  if (!device_) {
    return;
  }

  ensureParticleFallbackDepthResource();

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Particle VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kParticleVS;
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Particle PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kParticlePS;
  ps = device_with_cache_.CreateShader(shader_ci);

  const std::string global_particle_prefix =
      "#define KARMA_PARTICLE_GLOBAL_MATERIALS 1\n"
      "#define KARMA_PARTICLE_TEXTURE_TABLE_SIZE " +
      std::to_string(kParticleGpuTextureTableSize) + "\n";
  const std::string global_particle_vs_source = global_particle_prefix + kParticleVS;
  const std::string global_particle_ps_source = global_particle_prefix + kParticlePS;

  Diligent::RefCntAutoPtr<Diligent::IShader> global_vs;
  shader_ci.Desc.Name = "Karma Particle Global VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = global_particle_vs_source.c_str();
  shader_ci.ShaderCompiler = Diligent::SHADER_COMPILER_DXC;
  global_vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> global_ps;
  shader_ci.Desc.Name = "Karma Particle Global PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = global_particle_ps_source.c_str();
  shader_ci.ShaderCompiler = Diligent::SHADER_COMPILER_DXC;
  global_ps = device_with_cache_.CreateShader(shader_ci);
  shader_ci.ShaderCompiler = Diligent::SHADER_COMPILER_DEFAULT;

  Diligent::RefCntAutoPtr<Diligent::IShader> sim_cs;
  shader_ci.Desc.Name = "Karma Particle Sim CS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kParticleSimCS;
  sim_cs = device_with_cache_.CreateShader(shader_ci);

  if (!vs || !ps) {
    return;
  }

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleVertex, corner)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleVertex))},
      Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleVertex, uv)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleVertex))},
      Diligent::LayoutElement{2, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu,
                                                                     position_age)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{3, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu,
                                                                     color_start)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu,
                                                                     color_end)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu,
                                                                     rotation_size)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu,
                                                                     uv_rect)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu,
                                                                     uv_rect_next)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{8, 1, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(ParticleInstanceGpu, params)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu)),
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
  };

  static const Diligent::ShaderResourceVariableDesc kParticleVars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
  static const Diligent::SamplerDesc kParticleSamplerDesc{
      Diligent::FILTER_TYPE_LINEAR, Diligent::FILTER_TYPE_LINEAR, Diligent::FILTER_TYPE_LINEAR,
      Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP,
      Diligent::TEXTURE_ADDRESS_CLAMP};
  static const Diligent::SamplerDesc kParticleDepthSamplerDesc{
      Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT,
      Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP,
      Diligent::TEXTURE_ADDRESS_CLAMP};
  static const Diligent::ImmutableSamplerDesc kParticleSamplers[] = {
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler", kParticleSamplerDesc},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor_sampler", kParticleSamplerDesc},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth_sampler", kParticleDepthSamplerDesc}};
  static const Diligent::ShaderResourceVariableDesc kParticleGlobalVars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX,
       "g_ParticleMaterials",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL,
       "g_ParticleMaterials",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL,
       "g_ParticleTextures",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
  static const Diligent::ImmutableSamplerDesc kParticleGlobalSamplers[] = {
      {Diligent::SHADER_TYPE_PIXEL, "g_ParticleTextures_sampler", kParticleSamplerDesc},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneColor_sampler", kParticleSamplerDesc},
      {Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth_sampler", kParticleDepthSamplerDesc}};

  if (!particle_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Particle Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(ParticleConstants);
    device_->CreateBuffer(cb_desc, nullptr, &particle_cb_);
  }
  if (!particle_cb_) {
    return;
  }

  if (sim_cs && !particle_sim_compute_pso_) {
    Diligent::ComputePipelineStateCreateInfo sim_pso{};
    sim_pso.PSODesc.Name = "Karma Particle Sim Compute Pipeline";
    sim_pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    sim_pso.pCS = sim_cs;

    Diligent::ShaderResourceVariableDesc sim_vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE,
         "ParticleSimConstants",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {Diligent::SHADER_TYPE_COMPUTE,
         "g_OutParticles",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
    };
    sim_pso.PSODesc.ResourceLayout.Variables = sim_vars;
    sim_pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(sim_vars) / sizeof(sim_vars[0]));

    particle_sim_compute_pso_ = device_with_cache_.CreateComputePipelineState(sim_pso);
    if (particle_sim_compute_pso_) {
      if (!particle_sim_cb_) {
        Diligent::BufferDesc cb_desc{};
        cb_desc.Name = "Karma Particle Sim Constants";
        cb_desc.Usage = Diligent::USAGE_DYNAMIC;
        cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        cb_desc.Size = sizeof(ParticleSimComputeConstants);
        device_->CreateBuffer(cb_desc, nullptr, &particle_sim_cb_);
      }
      if (particle_sim_cb_) {
        if (auto* var = particle_sim_compute_pso_->GetStaticVariableByName(
                Diligent::SHADER_TYPE_COMPUTE, "ParticleSimConstants")) {
          var->Set(particle_sim_cb_);
        }
      }
      particle_sim_compute_pso_->CreateShaderResourceBinding(&particle_sim_compute_srb_, true);
      if (particle_sim_compute_srb_) {
        particle_sim_output_var_ = particle_sim_compute_srb_->GetVariableByName(
            Diligent::SHADER_TYPE_COMPUTE, "g_OutParticles");
      }
    }
  }

  if (!particle_gpu_frame_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Particle GPU Frame Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(ParticleGpuFrameConstants);
    device_->CreateBuffer(cb_desc, nullptr, &particle_gpu_frame_cb_);
  }
  if (!particle_gpu_sort_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Particle GPU Sort Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(ParticleGpuSortConstants);
    device_->CreateBuffer(cb_desc, nullptr, &particle_gpu_sort_cb_);
  }

  auto create_gpu_compute_pipeline =
      [&](const char* name,
          const char* kernel_source,
          const Diligent::ShaderResourceVariableDesc* variables,
          Diligent::Uint32 variable_count,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb) {
    if (out_pso) {
      return;
    }

    std::string source = std::string(kParticleGpuCommonHLSL) + kernel_source;
    Diligent::ShaderCreateInfo gpu_shader_ci{};
    gpu_shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    gpu_shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};
    gpu_shader_ci.Desc.Name = name;
    gpu_shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    gpu_shader_ci.EntryPoint = "main";
    gpu_shader_ci.Source = source.c_str();
    Diligent::RefCntAutoPtr<Diligent::IShader> shader =
        device_with_cache_.CreateShader(gpu_shader_ci);
    if (!shader) {
      return;
    }

    Diligent::ComputePipelineStateCreateInfo pso{};
    pso.PSODesc.Name = name;
    pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    pso.pCS = shader;
    pso.PSODesc.ResourceLayout.Variables = variables;
    pso.PSODesc.ResourceLayout.NumVariables = variable_count;
    out_pso = device_with_cache_.CreateComputePipelineState(pso);
    if (!out_pso) {
      return;
    }
    if (particle_gpu_frame_cb_) {
      if (auto* var = out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                       "ParticleGpuFrameConstants")) {
        var->Set(particle_gpu_frame_cb_);
      }
    }
    if (particle_gpu_sort_cb_) {
      if (auto* var = out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                       "ParticleGpuSortConstants")) {
        var->Set(particle_gpu_sort_cb_);
      }
    }
    out_pso->CreateShaderResourceBinding(&out_srb, true);
  };

  static const Diligent::ShaderResourceVariableDesc kParticleGpuClearVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Groups",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_GroupCounters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_SortItems",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DispatchArgs",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Stats",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_MeshSamples",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Clear CS",
                              kParticleGpuClearCS,
                              kParticleGpuClearVars,
                              static_cast<Diligent::Uint32>(std::size(kParticleGpuClearVars)),
                              particle_gpu_clear_compute_pso_,
                              particle_gpu_clear_compute_srb_);
  if (particle_gpu_clear_compute_srb_) {
    particle_gpu_clear_groups_var_ =
        particle_gpu_clear_compute_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Groups");
    particle_gpu_clear_counters_var_ =
        particle_gpu_clear_compute_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_GroupCounters");
    particle_gpu_clear_sort_items_var_ =
        particle_gpu_clear_compute_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_SortItems");
    particle_gpu_clear_draw_args_var_ =
        particle_gpu_clear_compute_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_DrawArgs");
    particle_gpu_clear_dispatch_args_var_ =
        particle_gpu_clear_compute_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_DispatchArgs");
    particle_gpu_clear_stats_var_ =
        particle_gpu_clear_compute_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Stats");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuUpdateEmittersVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Emitters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_EmitterStates",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Update Emitters CS",
                              kParticleGpuUpdateEmittersCS,
                              kParticleGpuUpdateEmittersVars,
                              static_cast<Diligent::Uint32>(
                                  std::size(kParticleGpuUpdateEmittersVars)),
                              particle_gpu_update_emitters_pso_,
                              particle_gpu_update_emitters_srb_);
  if (particle_gpu_update_emitters_srb_) {
    particle_gpu_update_emitters_descs_var_ =
        particle_gpu_update_emitters_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                             "g_Emitters");
    particle_gpu_update_emitters_states_var_ =
        particle_gpu_update_emitters_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                             "g_EmitterStates");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuSimulateVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Emitters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_EmitterStates",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_ParticleStates",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_AliveList",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DeadList",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Stats",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Simulate CS",
                              kParticleGpuSimulateCS,
                              kParticleGpuSimulateVars,
                              static_cast<Diligent::Uint32>(std::size(kParticleGpuSimulateVars)),
                              particle_gpu_simulate_pso_,
                              particle_gpu_simulate_srb_);
  if (particle_gpu_simulate_srb_) {
    particle_gpu_simulate_descs_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_Emitters");
    particle_gpu_simulate_emitters_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_EmitterStates");
    particle_gpu_simulate_states_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_ParticleStates");
    particle_gpu_simulate_alive_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_AliveList");
    particle_gpu_simulate_dead_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_DeadList");
    particle_gpu_simulate_stats_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_Stats");
    particle_gpu_simulate_mesh_samples_var_ =
        particle_gpu_simulate_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                      "g_MeshSamples");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuPrepareUnsortedVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Emitters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_ParticleStates",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Groups",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_GroupCounters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_OutParticles",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Stats",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Prepare Unsorted CS",
                              kParticleGpuPrepareUnsortedCS,
                              kParticleGpuPrepareUnsortedVars,
                              static_cast<Diligent::Uint32>(
                                  std::size(kParticleGpuPrepareUnsortedVars)),
                              particle_gpu_prepare_unsorted_pso_,
                              particle_gpu_prepare_unsorted_srb_);
  if (particle_gpu_prepare_unsorted_srb_) {
    particle_gpu_prepare_unsorted_descs_var_ =
        particle_gpu_prepare_unsorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                              "g_Emitters");
    particle_gpu_prepare_unsorted_states_var_ =
        particle_gpu_prepare_unsorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                              "g_ParticleStates");
    particle_gpu_prepare_unsorted_groups_var_ =
        particle_gpu_prepare_unsorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                              "g_Groups");
    particle_gpu_prepare_unsorted_counters_var_ =
        particle_gpu_prepare_unsorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                              "g_GroupCounters");
    particle_gpu_prepare_unsorted_instances_var_ =
        particle_gpu_prepare_unsorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                              "g_OutParticles");
    particle_gpu_prepare_unsorted_stats_var_ =
        particle_gpu_prepare_unsorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                              "g_Stats");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuGenerateSortVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Emitters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_ParticleStates",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Groups",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_GroupCounters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_SortItems",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Stats",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Generate Sort CS",
                              kParticleGpuGenerateSortCS,
                              kParticleGpuGenerateSortVars,
                              static_cast<Diligent::Uint32>(
                                  std::size(kParticleGpuGenerateSortVars)),
                              particle_gpu_generate_sort_pso_,
                              particle_gpu_generate_sort_srb_);
  if (particle_gpu_generate_sort_srb_) {
    particle_gpu_generate_sort_descs_var_ =
        particle_gpu_generate_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Emitters");
    particle_gpu_generate_sort_states_var_ =
        particle_gpu_generate_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_ParticleStates");
    particle_gpu_generate_sort_groups_var_ =
        particle_gpu_generate_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Groups");
    particle_gpu_generate_sort_counters_var_ =
        particle_gpu_generate_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_GroupCounters");
    particle_gpu_generate_sort_items_var_ =
        particle_gpu_generate_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_SortItems");
    particle_gpu_generate_sort_stats_var_ =
        particle_gpu_generate_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Stats");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuSortVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuSortConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_SortItems",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Sort CS",
                              kParticleGpuSortCS,
                              kParticleGpuSortVars,
                              static_cast<Diligent::Uint32>(std::size(kParticleGpuSortVars)),
                              particle_gpu_sort_pso_,
                              particle_gpu_sort_srb_);
  if (particle_gpu_sort_srb_) {
    particle_gpu_sort_items_var_ =
        particle_gpu_sort_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                  "g_SortItems");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuPrepareSortedVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_ParticleStates",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Groups",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_GroupCounters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_SortItems",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_OutParticles",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Stats",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Prepare Sorted CS",
                              kParticleGpuPrepareSortedCS,
                              kParticleGpuPrepareSortedVars,
                              static_cast<Diligent::Uint32>(
                                  std::size(kParticleGpuPrepareSortedVars)),
                              particle_gpu_prepare_sorted_pso_,
                              particle_gpu_prepare_sorted_srb_);
  if (particle_gpu_prepare_sorted_srb_) {
    particle_gpu_prepare_sorted_states_var_ =
        particle_gpu_prepare_sorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                            "g_ParticleStates");
    particle_gpu_prepare_sorted_groups_var_ =
        particle_gpu_prepare_sorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                            "g_Groups");
    particle_gpu_prepare_sorted_counters_var_ =
        particle_gpu_prepare_sorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                            "g_GroupCounters");
    particle_gpu_prepare_sorted_sort_items_var_ =
        particle_gpu_prepare_sorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                            "g_SortItems");
    particle_gpu_prepare_sorted_instances_var_ =
        particle_gpu_prepare_sorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                            "g_OutParticles");
    particle_gpu_prepare_sorted_stats_var_ =
        particle_gpu_prepare_sorted_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                            "g_Stats");
  }

  static const Diligent::ShaderResourceVariableDesc kParticleGpuIndirectArgsVars[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "ParticleGpuFrameConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Groups",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_GroupCounters",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_Stats",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
  };
  create_gpu_compute_pipeline("Karma Particle GPU Indirect Args CS",
                              kParticleGpuIndirectArgsCS,
                              kParticleGpuIndirectArgsVars,
                              static_cast<Diligent::Uint32>(
                                  std::size(kParticleGpuIndirectArgsVars)),
                              particle_gpu_indirect_args_pso_,
                              particle_gpu_indirect_args_srb_);
  if (particle_gpu_indirect_args_srb_) {
    particle_gpu_indirect_args_groups_var_ =
        particle_gpu_indirect_args_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Groups");
    particle_gpu_indirect_args_counters_var_ =
        particle_gpu_indirect_args_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_GroupCounters");
    particle_gpu_indirect_args_draw_args_var_ =
        particle_gpu_indirect_args_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_DrawArgs");
    particle_gpu_indirect_args_stats_var_ =
        particle_gpu_indirect_args_srb_->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                                           "g_Stats");
  }

  auto create_pipeline = [&](const char* name,
                             bool depth_test,
                             renderer::ParticleBlendMode blend_mode,
                             bool premultiplied_alpha,
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
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = depth_test;
    graphics.DepthStencilDesc.DepthWriteEnable = false;

    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    const bool alpha_like = blend_mode != renderer::ParticleBlendMode::Additive;
    blend.SrcBlend = premultiplied_alpha ? Diligent::BLEND_FACTOR_ONE
                                         : Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = alpha_like ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA
                                 : Diligent::BLEND_FACTOR_ONE;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = premultiplied_alpha || !alpha_like
                              ? Diligent::BLEND_FACTOR_ONE
                              : Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlendAlpha = alpha_like ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA
                                      : Diligent::BLEND_FACTOR_ONE;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements =
        static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));

    pso.PSODesc.ResourceLayout.Variables = kParticleVars;
    pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(kParticleVars) / sizeof(kParticleVars[0]));
    pso.PSODesc.ResourceLayout.ImmutableSamplers = kParticleSamplers;
    pso.PSODesc.ResourceLayout.NumImmutableSamplers =
        static_cast<Diligent::Uint32>(sizeof(kParticleSamplers) / sizeof(kParticleSamplers[0]));

    out_pso = device_with_cache_.CreateGraphicsPipelineState(pso);
    if (!out_pso) {
      return false;
    }
    if (auto* var =
            out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      var->Set(particle_cb_);
    }
    if (auto* var =
            out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) {
      var->Set(particle_cb_);
    }
    out_pso->CreateShaderResourceBinding(&out_srb, true);
    return true;
  };

  auto create_global_pipeline = [&](const char* name,
                                    bool depth_test,
                                    renderer::ParticleBlendMode blend_mode,
                                    bool premultiplied_alpha,
                                    ParticleGlobalPipeline& out_pipeline) {
    if (!global_vs || !global_ps) {
      return false;
    }

    Diligent::GraphicsPipelineStateCreateInfo pso{};
    pso.PSODesc.Name = name;
    pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pso.pVS = global_vs;
    pso.pPS = global_ps;

    auto& graphics = pso.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                         : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                     : Diligent::TEX_FORMAT_D32_FLOAT;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = depth_test;
    graphics.DepthStencilDesc.DepthWriteEnable = false;

    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    const bool alpha_like = blend_mode != renderer::ParticleBlendMode::Additive;
    blend.SrcBlend = premultiplied_alpha ? Diligent::BLEND_FACTOR_ONE
                                         : Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = alpha_like ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA
                                 : Diligent::BLEND_FACTOR_ONE;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = premultiplied_alpha || !alpha_like
                              ? Diligent::BLEND_FACTOR_ONE
                              : Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlendAlpha = alpha_like ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA
                                      : Diligent::BLEND_FACTOR_ONE;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements =
        static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));

    pso.PSODesc.ResourceLayout.Variables = kParticleGlobalVars;
    pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(kParticleGlobalVars) /
                                      sizeof(kParticleGlobalVars[0]));
    pso.PSODesc.ResourceLayout.ImmutableSamplers = kParticleGlobalSamplers;
    pso.PSODesc.ResourceLayout.NumImmutableSamplers =
        static_cast<Diligent::Uint32>(sizeof(kParticleGlobalSamplers) /
                                      sizeof(kParticleGlobalSamplers[0]));

    out_pipeline.pso = device_with_cache_.CreateGraphicsPipelineState(pso);
    if (!out_pipeline.pso) {
      return false;
    }
    if (auto* var =
            out_pipeline.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      var->Set(particle_cb_);
    }
    if (auto* var =
            out_pipeline.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) {
      var->Set(particle_cb_);
    }
    out_pipeline.pso->CreateShaderResourceBinding(&out_pipeline.srb, true);
    if (out_pipeline.srb) {
      out_pipeline.materials_vs_var =
          out_pipeline.srb->GetVariableByName(Diligent::SHADER_TYPE_VERTEX,
                                              "g_ParticleMaterials");
      out_pipeline.materials_ps_var =
          out_pipeline.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                              "g_ParticleMaterials");
      out_pipeline.textures_var =
          out_pipeline.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                              "g_ParticleTextures");
      out_pipeline.scene_color_var =
          out_pipeline.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor");
      out_pipeline.scene_depth_var =
          out_pipeline.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth");
    }
    return out_pipeline.srb != nullptr &&
           out_pipeline.materials_vs_var != nullptr &&
           out_pipeline.materials_ps_var != nullptr &&
           out_pipeline.textures_var != nullptr;
  };

  create_pipeline("Karma Particle Pipeline Additive (Depth)",
                  true,
                  renderer::ParticleBlendMode::Additive,
                  true,
                  particle_pipeline_state_additive_depth_,
                  particle_srb_additive_depth_);
  create_pipeline("Karma Particle Pipeline Additive (NoDepth)",
                  false,
                  renderer::ParticleBlendMode::Additive,
                  true,
                  particle_pipeline_state_additive_no_depth_,
                  particle_srb_additive_no_depth_);
  create_pipeline("Karma Particle Pipeline Alpha (Depth)",
                  true,
                  renderer::ParticleBlendMode::Alpha,
                  true,
                  particle_pipeline_state_alpha_depth_,
                  particle_srb_alpha_depth_);
  create_pipeline("Karma Particle Pipeline Alpha (NoDepth)",
                  false,
                  renderer::ParticleBlendMode::Alpha,
                  true,
                  particle_pipeline_state_alpha_no_depth_,
                  particle_srb_alpha_no_depth_);
  create_pipeline("Karma Particle Pipeline Alpha (HalfRes)",
                  false,
                  renderer::ParticleBlendMode::Alpha,
                  true,
                  particle_pipeline_state_alpha_half_res_,
                  particle_srb_alpha_half_res_);
  create_pipeline("Karma Particle Pipeline Distortion (Depth)",
                  true,
                  renderer::ParticleBlendMode::Distortion,
                  false,
                  particle_pipeline_state_distortion_depth_,
                  particle_srb_distortion_depth_);
  create_pipeline("Karma Particle Pipeline Distortion (NoDepth)",
                  false,
                  renderer::ParticleBlendMode::Distortion,
                  false,
                  particle_pipeline_state_distortion_no_depth_,
                  particle_srb_distortion_no_depth_);
  create_global_pipeline("Karma Particle Global Pipeline Alpha (Depth)",
                         true,
                         renderer::ParticleBlendMode::Alpha,
                         true,
                         particle_global_alpha_depth_);
  create_global_pipeline("Karma Particle Global Pipeline Alpha (NoDepth)",
                         false,
                         renderer::ParticleBlendMode::Alpha,
                         true,
                         particle_global_alpha_no_depth_);
  create_global_pipeline("Karma Particle Global Pipeline Alpha (HalfRes)",
                         false,
                         renderer::ParticleBlendMode::Alpha,
                         true,
                         particle_global_alpha_half_res_);
  create_global_pipeline("Karma Particle Global Pipeline Distortion (Depth)",
                         true,
                         renderer::ParticleBlendMode::Distortion,
                         false,
                         particle_global_distortion_depth_);
  create_global_pipeline("Karma Particle Global Pipeline Distortion (NoDepth)",
                         false,
                         renderer::ParticleBlendMode::Distortion,
                         false,
                         particle_global_distortion_no_depth_);

  auto initialize_particle_bindings = [&](Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb,
                                          Diligent::IShaderResourceVariable*& texture_var,
                                          Diligent::IShaderResourceVariable*& scene_color_var,
                                          Diligent::IShaderResourceVariable*& scene_depth_var) {
    if (!srb) {
      return;
    }
    if (!texture_var) {
      texture_var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
    }
    if (!scene_color_var) {
      scene_color_var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor");
    }
    if (!scene_depth_var) {
      scene_depth_var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth");
    }
    if (texture_var && default_base_color_) {
      texture_var->Set(default_base_color_);
    }
    if (scene_color_var && default_base_color_) {
      scene_color_var->Set(default_base_color_);
    }
    if (scene_depth_var && particle_fallback_depth_srv_) {
      scene_depth_var->Set(particle_fallback_depth_srv_);
    }
  };

  initialize_particle_bindings(particle_srb_additive_depth_,
                               particle_texture_var_additive_depth_,
                               particle_scene_color_var_additive_depth_,
                               particle_scene_depth_var_additive_depth_);
  initialize_particle_bindings(particle_srb_additive_no_depth_,
                               particle_texture_var_additive_no_depth_,
                               particle_scene_color_var_additive_no_depth_,
                               particle_scene_depth_var_additive_no_depth_);
  initialize_particle_bindings(particle_srb_alpha_depth_,
                               particle_texture_var_alpha_depth_,
                               particle_scene_color_var_alpha_depth_,
                               particle_scene_depth_var_alpha_depth_);
  initialize_particle_bindings(particle_srb_alpha_no_depth_,
                               particle_texture_var_alpha_no_depth_,
                               particle_scene_color_var_alpha_no_depth_,
                               particle_scene_depth_var_alpha_no_depth_);
  initialize_particle_bindings(particle_srb_alpha_half_res_,
                               particle_texture_var_alpha_half_res_,
                               particle_scene_color_var_alpha_half_res_,
                               particle_scene_depth_var_alpha_half_res_);
  initialize_particle_bindings(particle_srb_distortion_depth_,
                               particle_texture_var_distortion_depth_,
                               particle_scene_color_var_distortion_depth_,
                               particle_scene_depth_var_distortion_depth_);
  initialize_particle_bindings(particle_srb_distortion_no_depth_,
                               particle_texture_var_distortion_no_depth_,
                               particle_scene_color_var_distortion_no_depth_,
                               particle_scene_depth_var_distortion_no_depth_);

  if (!particle_vb_) {
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma Particle Quad VB";
    vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.Size = static_cast<Diligent::Uint32>(sizeof(kParticleQuadVertices));
    Diligent::BufferData vb_data{kParticleQuadVertices, vb_desc.Size};
    device_->CreateBuffer(vb_desc, &vb_data, &particle_vb_);
  }

  if (!particle_half_res_composite_pipeline_state_) {
    Diligent::ShaderCreateInfo composite_shader_ci{};
    composite_shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    composite_shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

    Diligent::RefCntAutoPtr<Diligent::IShader> composite_vs;
    composite_shader_ci.Desc.Name = "Karma Particle Half Res Composite VS";
    composite_shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    composite_shader_ci.EntryPoint = "main";
    composite_shader_ci.Source = kParticleHalfResCompositeVS;
    composite_vs = device_with_cache_.CreateShader(composite_shader_ci);

    Diligent::RefCntAutoPtr<Diligent::IShader> composite_ps;
    composite_shader_ci.Desc.Name = "Karma Particle Half Res Composite PS";
    composite_shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    composite_shader_ci.EntryPoint = "main";
    composite_shader_ci.Source = kParticleHalfResCompositePS;
    composite_ps = device_with_cache_.CreateShader(composite_shader_ci);

    if (composite_vs && composite_ps) {
      static const Diligent::ShaderResourceVariableDesc kCompositeVars[] = {
          {Diligent::SHADER_TYPE_PIXEL,
           "g_HalfResAlpha",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      };
      static const Diligent::SamplerDesc kCompositeSamplerDesc{
          Diligent::FILTER_TYPE_LINEAR,
          Diligent::FILTER_TYPE_LINEAR,
          Diligent::FILTER_TYPE_LINEAR,
          Diligent::TEXTURE_ADDRESS_CLAMP,
          Diligent::TEXTURE_ADDRESS_CLAMP,
          Diligent::TEXTURE_ADDRESS_CLAMP};
      static const Diligent::ImmutableSamplerDesc kCompositeSamplers[] = {
          {Diligent::SHADER_TYPE_PIXEL, "g_HalfResAlpha_sampler", kCompositeSamplerDesc}};

      Diligent::GraphicsPipelineStateCreateInfo composite_pso{};
      composite_pso.PSODesc.Name = "Karma Particle Half Res Composite";
      composite_pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      composite_pso.pVS = composite_vs;
      composite_pso.pPS = composite_ps;

      auto& graphics = composite_pso.GraphicsPipeline;
      graphics.NumRenderTargets = 1;
      graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                           : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
      graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                       : Diligent::TEX_FORMAT_D32_FLOAT;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      graphics.InputLayout.LayoutElements = nullptr;
      graphics.InputLayout.NumElements = 0;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.DepthStencilDesc.DepthEnable = false;
      graphics.DepthStencilDesc.DepthWriteEnable = false;

      auto& blend = graphics.BlendDesc.RenderTargets[0];
      blend.BlendEnable = true;
      blend.SrcBlend = Diligent::BLEND_FACTOR_ONE;
      blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
      blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
      blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
      blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
      blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
      blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

      composite_pso.PSODesc.ResourceLayout.Variables = kCompositeVars;
      composite_pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(sizeof(kCompositeVars) / sizeof(kCompositeVars[0]));
      composite_pso.PSODesc.ResourceLayout.ImmutableSamplers = kCompositeSamplers;
      composite_pso.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Diligent::Uint32>(
          sizeof(kCompositeSamplers) / sizeof(kCompositeSamplers[0]));

      particle_half_res_composite_pipeline_state_ =
          device_with_cache_.CreateGraphicsPipelineState(composite_pso);
      if (particle_half_res_composite_pipeline_state_) {
        particle_half_res_composite_pipeline_state_->CreateShaderResourceBinding(
            &particle_half_res_composite_srb_,
            true);
        if (particle_half_res_composite_srb_) {
          particle_half_res_alpha_var_ = particle_half_res_composite_srb_->GetVariableByName(
              Diligent::SHADER_TYPE_PIXEL,
              "g_HalfResAlpha");
          if (particle_half_res_alpha_var_ && default_base_color_) {
            particle_half_res_alpha_var_->Set(default_base_color_);
          }
        }
      }
    }
  }
}

}  // namespace karma::renderer_backend
