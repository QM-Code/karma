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

float3 SampleBloom(float2 uv)
{
    return max(g_Source.Sample(g_Sampler, ClampUV(uv)).rgb, 0.0);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 texel = g_ScreenParams.zw;
    float3 color = SampleBloom(input.UV) * 0.40;
    color += SampleBloom(input.UV + float2( texel.x, 0.0)) * 0.15;
    color += SampleBloom(input.UV + float2(-texel.x, 0.0)) * 0.15;
    color += SampleBloom(input.UV + float2(0.0,  texel.y)) * 0.15;
    color += SampleBloom(input.UV + float2(0.0, -texel.y)) * 0.15;
    color += SampleBloom(input.UV + float2( texel.x,  texel.y)) * 0.05;
    color += SampleBloom(input.UV + float2(-texel.x,  texel.y)) * 0.05;
    color += SampleBloom(input.UV + float2( texel.x, -texel.y)) * 0.05;
    color += SampleBloom(input.UV + float2(-texel.x, -texel.y)) * 0.05;
    return float4(color / 1.20, 1.0);
}
