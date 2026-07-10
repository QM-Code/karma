Texture2D<float4> g_Source;
Texture2D<float4> g_History;
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

float3 RGBToYCoCg(float3 color)
{
    return float3(dot(color, float3(0.25, 0.5, 0.25)),
                  color.r - color.b,
                  color.g - 0.5 * (color.r + color.b));
}

float3 YCoCgToRGB(float3 color)
{
    float t = color.x - 0.5 * color.z;
    return float3(t + 0.5 * color.y,
                  color.x + 0.5 * color.z,
                  t - 0.5 * color.y);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.UV;
    float4 source = g_Source.Sample(g_Sampler, ClampUV(uv));
    float3 current = max(source.rgb, 0.0);

    float2 texel = g_ScreenParams.zw;
    float3 n0 = SampleSource(uv + float2( texel.x, 0.0));
    float3 n1 = SampleSource(uv + float2(-texel.x, 0.0));
    float3 n2 = SampleSource(uv + float2(0.0,  texel.y));
    float3 n3 = SampleSource(uv + float2(0.0, -texel.y));
    float3 n4 = SampleSource(uv + float2( texel.x,  texel.y));
    float3 n5 = SampleSource(uv + float2(-texel.x,  texel.y));
    float3 n6 = SampleSource(uv + float2( texel.x, -texel.y));
    float3 n7 = SampleSource(uv + float2(-texel.x, -texel.y));

    float3 current_ycocg = RGBToYCoCg(current);
    float3 neighborhood_min = current_ycocg;
    float3 neighborhood_max = current_ycocg;
    float3 sample_ycocg = RGBToYCoCg(n0);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n1);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n2);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n3);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n4);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n5);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n6);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);
    sample_ycocg = RGBToYCoCg(n7);
    neighborhood_min = min(neighborhood_min, sample_ycocg);
    neighborhood_max = max(neighborhood_max, sample_ycocg);

    float3 history_ycocg = current_ycocg;
    float history_feedback = 0.0;
    if (g_TaaParams.z > 0.5)
    {
        float3 history = max(g_History.Sample(g_Sampler, ClampUV(uv)).rgb, 0.0);
        float3 unclamped_history_ycocg = RGBToYCoCg(history);
        history_ycocg = clamp(unclamped_history_ycocg,
                              neighborhood_min,
                              neighborhood_max);
        float luma_delta = abs(unclamped_history_ycocg.x - current_ycocg.x) /
                           max(max(unclamped_history_ycocg.x, current_ycocg.x), 0.10);
        float history_rejection = saturate((luma_delta - 0.05) * 2.5);
        history_feedback = saturate(g_TaaParams.x) * (1.0 - history_rejection);
    }

    float3 resolved = YCoCgToRGB(lerp(current_ycocg,
                                      history_ycocg,
                                      history_feedback));
    float3 blur = (n0 + n1 + n2 + n3) * 0.25;
    resolved += (resolved - blur) * g_TaaParams.y;
    return float4(max(resolved, 0.0), source.a);
}
