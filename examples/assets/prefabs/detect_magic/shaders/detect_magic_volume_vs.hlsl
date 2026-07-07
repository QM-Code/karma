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
    float2 UV1 : ATTRIB10;
    float4 ModelCol0 : ATTRIB4;
    float4 ModelCol1 : ATTRIB5;
    float4 ModelCol2 : ATTRIB6;
    float4 ModelCol3 : ATTRIB7;
    float4 InstanceParams : ATTRIB11;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    float2 UV : TEXCOORD0;
    float2 UV1 : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float3 WorldPos : TEXCOORD3;
    float4 InstanceParams : TEXCOORD4;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 world_pos = input.ModelCol0 * input.Pos.x +
                       input.ModelCol1 * input.Pos.y +
                       input.ModelCol2 * input.Pos.z +
                       input.ModelCol3;
    output.Pos = mul(g_MVP, world_pos);
    output.Normal = input.Normal;
    output.UV = input.UV;
    output.UV1 = input.UV1;
    output.Tangent = input.Tangent;
    output.WorldPos = world_pos.xyz;
    output.InstanceParams = input.InstanceParams;
    return output;
}
