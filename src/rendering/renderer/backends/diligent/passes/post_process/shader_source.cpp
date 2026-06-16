#include "shader_source.h"

#include "../../backend_internal.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace karma::renderer_backend::post_process {
namespace {

static constexpr const char* kFallbackFullscreenVS = R"(
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
)";

static constexpr const char* kFallbackCopyPS = R"(
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

float4 main(PSInput input) : SV_TARGET
{
    return g_Source.Sample(g_Sampler, input.UV);
}
)";

static constexpr const char* kFallbackCompositePS = R"(
Texture2D<float4> g_Source;
Texture2D<float> g_Depth;
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

float3 FilmicCurve(float3 color)
{
    color = max(color, 0.0);
    return saturate((color * (2.51 * color + 0.03)) /
                    (color * (2.43 * color + 0.59) + 0.14));
}

float4 main(PSInput input) : SV_TARGET
{
    float4 source = g_Source.Sample(g_Sampler, input.UV);
    float3 color = max(source.rgb, 0.0);
    if (g_BloomParams.w > 0.5)
    {
        color += max(g_Bloom.Sample(g_Sampler, input.UV).rgb, 0.0) * g_BloomParams.y;
    }
    if (g_ToneParams.w > 0.5)
    {
        color = FilmicCurve(color * max(g_ToneParams.x, 0.01));
        color = (color - 0.5) * max(g_ToneParams.y, 0.01) + 0.5;
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = lerp(float3(luma, luma, luma), color, max(g_ToneParams.z, 0.0));
        color = saturate(color);
    }
    return float4(color, source.a);
}
)";

static constexpr const char* kFallbackBloomCombinePS = R"(
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

float4 main(PSInput input) : SV_TARGET
{
    return float4(max(g_Bloom.Sample(g_Sampler, input.UV).rgb, 0.0) +
                  max(g_Source.Sample(g_Sampler, input.UV).rgb, 0.0), 1.0);
}
)";

static constexpr const char* kFallbackTemporalPS = R"(
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

float4 main(PSInput input) : SV_TARGET
{
    float4 source = g_Source.Sample(g_Sampler, input.UV);
    if (g_TaaParams.z < 0.5)
    {
        return source;
    }
    float4 history = g_History.Sample(g_Sampler, input.UV);
    return float4(lerp(source.rgb, history.rgb, saturate(g_TaaParams.x)), source.a);
}
)";

const char* fallbackSource(ShaderFallback fallback) {
  switch (fallback) {
    case ShaderFallback::FullscreenTriangle:
      return kFallbackFullscreenVS;
    case ShaderFallback::Copy:
      return kFallbackCopyPS;
    case ShaderFallback::Composite:
      return kFallbackCompositePS;
    case ShaderFallback::BloomCombine:
      return kFallbackBloomCombinePS;
    case ShaderFallback::Temporal:
      return kFallbackTemporalPS;
  }
  return kFallbackCopyPS;
}

std::string readTextFile(const std::filesystem::path& path) {
  const std::vector<unsigned char> bytes = readFileBytes(path);
  if (bytes.empty()) {
    return {};
  }
  return std::string(bytes.begin(), bytes.end());
}

std::filesystem::path shaderAssetRelativePath() {
  return std::filesystem::path("src") / "rendering" / "renderer" / "backends" /
         "diligent" / "shaders" / "post_process";
}

std::filesystem::path installedShaderAssetRelativePath() {
  return std::filesystem::path("share") / "karma" / "shaders" / "diligent" /
         "post_process";
}

std::vector<std::filesystem::path> shaderCandidates(const char* filename) {
  std::vector<std::filesystem::path> candidates;
#if defined(KARMA_DILIGENT_SHADER_SOURCE_DIR)
  candidates.emplace_back(std::filesystem::path(KARMA_DILIGENT_SHADER_SOURCE_DIR) / filename);
#endif
  if (const char* env_dir = std::getenv("KARMA_DILIGENT_SHADER_DIR")) {
    if (env_dir[0] != '\0') {
      candidates.emplace_back(std::filesystem::path(env_dir) / filename);
    }
  }

  std::error_code ec;
  std::filesystem::path current = std::filesystem::current_path(ec);
  if (!ec) {
    for (int depth = 0; depth < 8; ++depth) {
      candidates.emplace_back(current / shaderAssetRelativePath() / filename);
      candidates.emplace_back(current / installedShaderAssetRelativePath() / filename);
      const std::filesystem::path parent = current.parent_path();
      if (parent.empty() || parent == current) {
        break;
      }
      current = parent;
    }
  }

  candidates.emplace_back(installedShaderAssetRelativePath() / filename);
  return candidates;
}

}  // namespace

std::string loadShader(const char* filename, ShaderFallback fallback) {
  for (const auto& candidate : shaderCandidates(filename)) {
    std::string source = readTextFile(candidate);
    if (!source.empty()) {
      return source;
    }
  }
  return fallbackSource(fallback);
}

}  // namespace karma::renderer_backend::post_process
