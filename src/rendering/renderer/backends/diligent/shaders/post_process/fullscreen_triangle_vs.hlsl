struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput main(uint VertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((VertexId << 1) & 2, VertexId & 2);
    output.UV = uv;
    output.Pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}
