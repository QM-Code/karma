Texture2D<float4> g_Source;
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

float3 SampleLow(float2 uv)
{
    return max(g_Source.Sample(g_Sampler, ClampUV(uv)).rgb, 0.0);
}

float4 main(PSInput input) : SV_TARGET
{
    float radius = max(g_BloomParams.z, 0.25);
    float2 texel = g_ScreenParams.zw * min(radius, 8.0) * 0.35;

    float3 low = SampleLow(input.UV) * 0.38;
    low += SampleLow(input.UV + float2( texel.x, 0.0)) * 0.12;
    low += SampleLow(input.UV + float2(-texel.x, 0.0)) * 0.12;
    low += SampleLow(input.UV + float2(0.0,  texel.y)) * 0.12;
    low += SampleLow(input.UV + float2(0.0, -texel.y)) * 0.12;
    low += SampleLow(input.UV + float2( texel.x,  texel.y)) * 0.07;
    low += SampleLow(input.UV + float2(-texel.x,  texel.y)) * 0.07;
    low += SampleLow(input.UV + float2( texel.x, -texel.y)) * 0.07;
    low += SampleLow(input.UV + float2(-texel.x, -texel.y)) * 0.07;

    float3 high = max(g_Bloom.Sample(g_Sampler, ClampUV(input.UV)).rgb, 0.0);
    return float4(high + low * 0.72, 1.0);
}
