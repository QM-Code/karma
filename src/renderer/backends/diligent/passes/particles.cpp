#include "karma/renderer/backends/diligent/backend.hpp"

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

void ComputeFrameUv(uint frame_index, out float2 uv_min, out float2 uv_max)
{
    uint columns = max((uint)g_AtlasParams0.x, 1u);
    uint rows = max((uint)g_AtlasParams0.y, 1u);
    uint frame_count = max((uint)g_AtlasParams0.z, 1u);
    uint clamped_frame = min(frame_index, frame_count - 1u);
    uint column = clamped_frame % columns;
    uint row = clamped_frame / columns;

    float frame_width = g_AtlasParams1.x;
    float frame_height = g_AtlasParams1.y;
    float border_x = g_AtlasParams1.z;
    float border_y = g_AtlasParams1.w;
    float spacing_x = g_AtlasParams2.x;
    float spacing_y = g_AtlasParams2.y;

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
    float normalized_age = saturate(input.position_age.w);
    float size = input.rotation_size.z;
    float4 color = input.color_start;
    float2 uv_min = input.uv_rect.xy;
    float2 uv_max = input.uv_rect.zw;
    float2 uv_min_next = input.uv_rect_next.xy;
    float2 uv_max_next = input.uv_rect_next.zw;
    float frame_blend = saturate(input.params.x);
    if (g_PresentationParams.z > 0.5)
    {
        float size_t = ApplyCurveExponent(normalized_age, g_PresentationParams.x);
        float alpha_t = ApplyCurveExponent(normalized_age, g_PresentationParams.y);
        size = lerp(input.rotation_size.z, input.rotation_size.w, size_t);
        color.rgb = lerp(input.color_start.rgb, input.color_end.rgb, normalized_age);
        color.a = lerp(input.color_start.a, input.color_end.a, alpha_t);

        uint frame_count = max((uint)g_AtlasParams0.z, 1u);
        if (frame_count > 1u)
        {
            float frame_position = 0.0;
            if (g_AtlasParams0.w > 0.5)
            {
                frame_position = normalized_age * (float)(frame_count - 1u);
                uint current_frame = min((uint)floor(frame_position), frame_count - 1u);
                uint next_frame = min(current_frame + 1u, frame_count - 1u);
                frame_blend = saturate(frame_position - (float)current_frame);
                ComputeFrameUv(current_frame, uv_min, uv_max);
                ComputeFrameUv(next_frame, uv_min_next, uv_max_next);
            }
            else
            {
                frame_position = max(input.params.z, 0.0) * g_AtlasParams2.z + input.params.y;
                float wrapped_position = fmod(frame_position, (float)frame_count);
                float normalized_position =
                    wrapped_position >= 0.0 ? wrapped_position : wrapped_position + (float)frame_count;
                uint current_frame = ((uint)floor(normalized_position)) % frame_count;
                uint next_frame = (current_frame + 1u) % frame_count;
                frame_blend = saturate(normalized_position - (float)current_frame);
                ComputeFrameUv(current_frame, uv_min, uv_max);
                ComputeFrameUv(next_frame, uv_min_next, uv_max_next);
            }
        }
        else
        {
            ComputeFrameUv(0u, uv_min, uv_max);
            uv_min_next = uv_min;
            uv_max_next = uv_max;
            frame_blend = 0.0;
        }
    }

    float2 local = input.corner * size;
    float2 rotated = float2(local.x * input.rotation_size.x - local.y * input.rotation_size.y,
                            local.x * input.rotation_size.y + local.y * input.rotation_size.x);
    float3 world = input.position_age.xyz +
                   g_CameraRight.xyz * rotated.x +
                   g_CameraUp.xyz * rotated.y;

    PSInput output;
    output.pos = mul(float4(world, 1.0f), g_ViewProj);
    output.uv = lerp(uv_min, uv_max, input.uv);
    output.uv_next = lerp(uv_min_next, uv_max_next, input.uv);
    output.local_uv = input.uv;
    output.color = color;
    output.frame_blend = frame_blend;
    output.world_pos = world;
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

Texture2D g_Texture;
SamplerState g_Texture_sampler;
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
    float4 texel = g_Texture.Sample(g_Texture_sampler, input.uv);
    float4 next_texel = g_Texture.Sample(g_Texture_sampler, input.uv_next);
    texel = lerp(texel, next_texel, input.frame_blend);
    float alpha = texel.a * input.color.a;
    if (g_Params.x > 0.5f)
    {
        float2 centered = input.local_uv * 2.0f - 1.0f;
        float radial = saturate(1.0f - dot(centered, centered));
        alpha *= radial * radial;
    }

    float2 screen_uv = input.pos.xy * g_ScreenParams.zw;
    if (g_Params.z > 0.0f || g_PresentationParams.w > 0.5f)
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
        if (g_Params.z > 0.0f)
        {
            float fade = saturate((scene_linear_depth - particle_linear_depth) /
                                  max(g_Params.z, 1.0e-4f));
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
                               (g_Params.w * alpha);
        float2 distorted_uv = saturate(screen_uv + offset_pixels * g_ScreenParams.zw);
        float3 scene_color = g_SceneColor.Sample(g_SceneColor_sampler, distorted_uv).rgb;
        return float4(scene_color, alpha);
    }

    const float3 texture_color = texel.rgb * input.color.rgb;
    if (g_CameraParams.w > 0.5f)
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
            pow(saturate(1.0f - dot(view_dir, sphere_normal)), max(g_ShadingParams.x, 0.001f)) *
            g_ShadingParams.y;
        const float3 light_dir = normalize(float3(-0.38f, 0.64f, 0.67f));
        const float3 reflected = reflect(-light_dir, sphere_normal);
        const float specular =
            pow(saturate(dot(reflected, view_dir)), 42.0f) * (0.18f + fresnel * 0.82f);
        const float2 turbulence = texel.rg * 2.0f - 1.0f;
        const float2 refract_pixels =
            (centered * 0.65f + turbulence * 0.35f) *
            (g_ShadingParams.z * (0.18f + body * 0.16f + fresnel * 0.36f) * alpha);
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
            glass_tint * (g_ShadingParams.w * body * (0.12f + texel.a * 0.18f));
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
      particle_half_res_composite_pipeline_state_) {
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

  create_pipeline("Karma Particle Pipeline Additive (Depth)",
                  true,
                  renderer::ParticleBlendMode::Additive,
                  false,
                  particle_pipeline_state_additive_depth_,
                  particle_srb_additive_depth_);
  create_pipeline("Karma Particle Pipeline Additive (NoDepth)",
                  false,
                  renderer::ParticleBlendMode::Additive,
                  false,
                  particle_pipeline_state_additive_no_depth_,
                  particle_srb_additive_no_depth_);
  create_pipeline("Karma Particle Pipeline Alpha (Depth)",
                  true,
                  renderer::ParticleBlendMode::Alpha,
                  false,
                  particle_pipeline_state_alpha_depth_,
                  particle_srb_alpha_depth_);
  create_pipeline("Karma Particle Pipeline Alpha (NoDepth)",
                  false,
                  renderer::ParticleBlendMode::Alpha,
                  false,
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
