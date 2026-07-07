Texture2D<float4> g_Source;
Texture2D<float> g_SelectedMask;
Texture2D<float> g_SelectedDepth;
Texture2D<float> g_SceneDepth;
SamplerState g_Sampler;

struct PSIn {
  float4 Position : SV_POSITION;
  float2 UV : TEXCOORD0;
};

int2 clampPixel(int2 pixel, uint2 dimensions) {
  pixel = clamp(pixel, int2(0, 0), int2(dimensions) - int2(1, 1));
  return pixel;
}

float loadMask(int2 pixel, uint2 dimensions) {
  return g_SelectedMask.Load(int3(clampPixel(pixel, dimensions), 0));
}

float loadSceneDepth(int2 pixel, uint2 dimensions) {
  return g_SceneDepth.Load(int3(clampPixel(pixel, dimensions), 0));
}

float loadSelectedDepth(int2 pixel, uint2 dimensions) {
  return g_SelectedDepth.Load(int3(clampPixel(pixel, dimensions), 0));
}

float4 main(PSIn input) : SV_Target {
  uint width = 0;
  uint height = 0;
  g_Source.GetDimensions(width, height);
  const uint2 dimensions = uint2(width, height);
  const int2 pixel = int2(input.Position.xy);

  const float4 source = g_Source.SampleLevel(g_Sampler, input.UV, 0.0);
  const float center_mask = loadMask(pixel, dimensions);

  float neighbor_mask = 0.0;
  [unroll]
  for (int y = -3; y <= 3; ++y) {
    [unroll]
    for (int x = -3; x <= 3; ++x) {
      if (x * x + y * y <= 10) {
        neighbor_mask = max(neighbor_mask, loadMask(pixel + int2(x, y), dimensions));
      }
    }
  }

  const float outline = saturate(neighbor_mask - center_mask);
  const float scene_depth = loadSceneDepth(pixel, dimensions);
  const float selected_depth = loadSelectedDepth(pixel, dimensions);
  const float selected_here = saturate(center_mask);
  const float occluded = selected_here * step(scene_depth + 0.00015, selected_depth);

  float3 color = source.rgb;
  color = lerp(color, float3(0.05, 0.56, 1.0), occluded * 0.38);
  color = lerp(color, float3(1.0, 0.76, 0.16), outline * 0.95);
  return float4(color, source.a);
}
