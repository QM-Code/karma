Texture2D<float4> g_Source;
Texture2D<float> g_Depth;
Texture2D<float4> g_Bloom;
SamplerState g_Sampler;

cbuffer PostConstants
{
    float4 g_ScreenParams;
    float4 g_BloomParams;
    float4 g_ToneParams;
    float4 g_SsaoParams;
    float4 g_SsrParams;
    float4 g_TaaParams;
    float4 g_DofParams;
    float4 g_CameraParams;
    float4 g_ModeParams;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float2 ClampUV(float2 uv)
{
    float2 pad = g_ScreenParams.zw * 0.5;
    return clamp(uv, pad, 1.0 - pad);
}

float3 SampleSource(float2 uv)
{
    return max(g_Source.Sample(g_Sampler, ClampUV(uv)).rgb, 0.0);
}

float SafeDepth(float2 uv)
{
    return saturate(g_Depth.Sample(g_Sampler, ClampUV(uv)));
}

float LinearEyeDepth(float depth)
{
    float near_clip = max(g_CameraParams.x, 1.0e-4);
    float far_clip = max(g_CameraParams.y, near_clip + 1.0e-3);
    if (g_CameraParams.z < 0.5)
    {
        return lerp(near_clip, far_clip, depth);
    }
    return (near_clip * far_clip) / max(far_clip - depth * (far_clip - near_clip), 1.0e-4);
}

float3 EstimateViewNormal(float2 uv)
{
    float depth_l = SafeDepth(uv - float2(g_ScreenParams.z, 0.0));
    float depth_r = SafeDepth(uv + float2(g_ScreenParams.z, 0.0));
    float depth_u = SafeDepth(uv - float2(0.0, g_ScreenParams.w));
    float depth_d = SafeDepth(uv + float2(0.0, g_ScreenParams.w));
    float2 gradient = float2(depth_r - depth_l, depth_d - depth_u);
    return normalize(float3(-gradient.x * 96.0, gradient.y * 96.0, 1.0));
}

float ComputeAmbientOcclusion(float2 uv, float depth)
{
    if (g_SsaoParams.w < 0.5)
    {
        return 1.0;
    }

    float base_depth = LinearEyeDepth(depth);
    float pixel_radius = max(g_SsaoParams.x, 0.1);
    float occlusion = 0.0;
    float sample_count = 0.0;

    float2 offsets[8] = {
        float2( 1.0,  0.0), float2(-1.0,  0.0),
        float2( 0.0,  1.0), float2( 0.0, -1.0),
        float2( 0.707,  0.707), float2(-0.707,  0.707),
        float2( 0.707, -0.707), float2(-0.707, -0.707)
    };

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float2 sample_uv = uv + offsets[i] * g_ScreenParams.zw * pixel_radius;
        float sample_depth = LinearEyeDepth(SafeDepth(sample_uv));
        float delta = base_depth - sample_depth;
        float range_check = saturate(1.0 - abs(delta) / max(pixel_radius * 0.35, 0.05));
        occlusion += saturate(delta / max(pixel_radius * 0.08, 0.02)) * range_check;
        sample_count += 1.0;
    }

    float ao = occlusion / max(sample_count, 1.0);
    ao = pow(saturate(ao), max(g_SsaoParams.z, 0.25)) * g_SsaoParams.y;
    return saturate(1.0 - ao);
}

float3 ComputeScreenSpaceReflection(float2 uv, float depth, float3 base_color)
{
    if (g_SsrParams.w < 0.5)
    {
        return base_color;
    }

    float3 normal = EstimateViewNormal(uv);
    float2 direction = normal.xy;
    float direction_len = max(length(direction), 1.0e-4);
    direction /= direction_len;
    float fresnel = pow(saturate(1.0 - normal.z), 2.0);
    float roughness_gate = saturate(1.0 - g_SsrParams.y);
    float reflection_weight = saturate((fresnel + direction_len * 0.35) * (0.25 + roughness_gate));

    float3 reflected = 0.0;
    float hit_weight = 0.0;
    [unroll]
    for (int i = 1; i <= 8; ++i)
    {
        float step_len = (float)i * (2.0 + g_SsrParams.z * 30.0);
        float2 sample_uv = uv + direction * g_ScreenParams.zw * step_len;
        float sample_depth = SafeDepth(sample_uv);
        float depth_gate = saturate((depth - sample_depth + g_SsrParams.z) * 24.0);
        float fade = saturate(min(min(sample_uv.x, sample_uv.y), min(1.0 - sample_uv.x, 1.0 - sample_uv.y)) * 8.0);
        float w = depth_gate * fade / (float)i;
        reflected += SampleSource(sample_uv) * w;
        hit_weight += w;
    }

    reflected = hit_weight > 1.0e-4 ? reflected / hit_weight : base_color;
    return lerp(base_color, reflected, saturate(g_SsrParams.x * reflection_weight));
}

float3 ApplyDepthOfField(float2 uv, float depth, float3 color)
{
    if (g_DofParams.w < 0.5)
    {
        return color;
    }

    float linear_depth = LinearEyeDepth(depth);
    float coc = saturate(abs(linear_depth - g_DofParams.x) / max(g_DofParams.y, 1.0e-3));
    coc = saturate(coc * g_DofParams.z);
    if (coc <= 1.0e-3)
    {
        return color;
    }

    float radius = coc * 10.0;
    float2 texel = g_ScreenParams.zw * radius;
    float3 blur = color * 0.18;
    float weight = 0.18;

    float2 offsets[8] = {
        float2( 1.0,  0.0), float2(-1.0,  0.0),
        float2( 0.0,  1.0), float2( 0.0, -1.0),
        float2( 0.707,  0.707), float2(-0.707,  0.707),
        float2( 0.707, -0.707), float2(-0.707, -0.707)
    };

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        blur += SampleSource(uv + offsets[i] * texel) * 0.1025;
        weight += 0.1025;
    }
    return lerp(color, blur / max(weight, 1.0e-3), coc);
}

float3 FilmicCurve(float3 color)
{
    color = max(color, 0.0);
    return saturate((color * (2.51 * color + 0.03)) /
                    (color * (2.43 * color + 0.59) + 0.14));
}

float3 ApplyToneControls(float3 color)
{
    if (g_ToneParams.w < 0.5)
    {
        return color;
    }

    color = FilmicCurve(color * max(g_ToneParams.x, 0.01));
    color = (color - 0.5) * max(g_ToneParams.y, 0.01) + 0.5;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luminance, luminance, luminance), color, max(g_ToneParams.z, 0.0));
    return saturate(color);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.UV;
    float depth = SafeDepth(uv);
    float4 source = g_Source.Sample(g_Sampler, ClampUV(uv));
    float3 color = max(source.rgb, 0.0);

    if (g_BloomParams.w > 0.5)
    {
        color += max(g_Bloom.Sample(g_Sampler, ClampUV(uv)).rgb, 0.0) * g_BloomParams.y;
    }
    color *= ComputeAmbientOcclusion(uv, depth);
    color = ComputeScreenSpaceReflection(uv, depth, color);
    color = ApplyDepthOfField(uv, depth, color);
    color = ApplyToneControls(color);
    return float4(color, source.a);
}
