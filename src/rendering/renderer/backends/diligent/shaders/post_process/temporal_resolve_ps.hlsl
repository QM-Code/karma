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
    float3 neighborhood_min = min(current, min(min(n0, n1), min(n2, n3)));
    float3 neighborhood_max = max(current, max(max(n0, n1), max(n2, n3)));

    float3 history = current;
    if (g_TaaParams.z > 0.5)
    {
        history = g_History.Sample(g_Sampler, ClampUV(uv)).rgb;
        history = clamp(history, neighborhood_min, neighborhood_max);
    }

    float3 resolved = lerp(current, history, saturate(g_TaaParams.x));
    float3 blur = (n0 + n1 + n2 + n3) * 0.25;
    resolved += (resolved - blur) * g_TaaParams.y;
    return float4(saturate(resolved), source.a);
}
