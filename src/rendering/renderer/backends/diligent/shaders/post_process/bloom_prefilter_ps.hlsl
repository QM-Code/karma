Texture2D<float4> g_Source;
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

float3 ThresholdBright(float3 color)
{
    float threshold = max(g_BloomParams.x, 0.0);
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    float knee = max(threshold * 0.35, 1.0e-3);
    float soft = clamp(luminance - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / max(4.0 * knee, 1.0e-4);
    float contribution = max(luminance - threshold, soft);
    contribution = saturate(contribution / max(luminance, 1.0e-4));
    return max(color, 0.0) * contribution;
}

float4 main(PSInput input) : SV_TARGET
{
    float2 texel = g_ScreenParams.zw;
    float3 color = ThresholdBright(g_Source.Sample(g_Sampler, ClampUV(input.UV)).rgb) * 0.5;
    color += ThresholdBright(g_Source.Sample(g_Sampler, ClampUV(input.UV + float2( texel.x, 0.0))).rgb) * 0.125;
    color += ThresholdBright(g_Source.Sample(g_Sampler, ClampUV(input.UV + float2(-texel.x, 0.0))).rgb) * 0.125;
    color += ThresholdBright(g_Source.Sample(g_Sampler, ClampUV(input.UV + float2(0.0,  texel.y))).rgb) * 0.125;
    color += ThresholdBright(g_Source.Sample(g_Sampler, ClampUV(input.UV + float2(0.0, -texel.y))).rgb) * 0.125;
    return float4(color, 1.0);
}
