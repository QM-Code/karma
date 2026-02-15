cbuffer Constants
{
    float4x4 g_MVP;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV : ATTRIB3;
    float4 ModelCol0 : ATTRIB4;
    float4 ModelCol1 : ATTRIB5;
    float4 ModelCol2 : ATTRIB6;
    float4 ModelCol3 : ATTRIB7;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = input.ModelCol0 * input.Pos.x +
                       input.ModelCol1 * input.Pos.y +
                       input.ModelCol2 * input.Pos.z +
                       input.ModelCol3;
    output.Pos = mul(g_MVP, world_pos);
    output.WorldPos = world_pos.xyz;
    return output;
}
