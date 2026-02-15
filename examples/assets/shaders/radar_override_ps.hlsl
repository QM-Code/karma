cbuffer CameraOverrideUser
{
    uint4 g_UserKeyHashes[32];
    float4 g_UserValues[32];
    float4 g_UserMeta;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    const uint KEY_HEIGHT_RANGE = 0x2AD2BDBCu;
    const uint KEY_LOW_COLOR = 0x194F1F4Du;
    const uint KEY_HIGH_COLOR = 0x75DB8769u;

    float4 height_range = float4(-2.0, 20.0, 0.0, 0.0);
    float4 low_color = float4(0.07, 0.30, 0.95, 1.0);
    float4 high_color = float4(1.0, 0.28, 0.08, 1.0);

    uint count = min((uint)g_UserMeta.x, 32u);
    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        uint key = g_UserKeyHashes[i].x;
        if (key == KEY_HEIGHT_RANGE)
        {
            height_range = g_UserValues[i];
        }
        else if (key == KEY_LOW_COLOR)
        {
            low_color = g_UserValues[i];
        }
        else if (key == KEY_HIGH_COLOR)
        {
            high_color = g_UserValues[i];
        }
    }

    const float min_y = height_range.x;
    const float max_y = height_range.y;
    const float t = saturate((input.WorldPos.y - min_y) / max(max_y - min_y, 1e-4));
    const float3 color = lerp(low_color.rgb, high_color.rgb, t);
    return float4(color, 1.0);
}
