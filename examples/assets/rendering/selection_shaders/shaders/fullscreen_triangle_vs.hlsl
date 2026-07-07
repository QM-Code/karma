struct VSOut {
  float4 Position : SV_POSITION;
  float2 UV : TEXCOORD0;
};

VSOut main(uint vertex_id : SV_VertexID) {
  VSOut output;
  float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
  output.UV = uv;
  output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  return output;
}
