#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace karma::rendering::backend {
namespace {

struct alignas(16) TerrainConstants {
  float view_proj[16];
  float model[16];
  float light_dir[4];
  float light_color[4];
  float camera_pos[4];
  float tile_info[4];
  float render_params[4];
  float material_scales[4];
};

struct TerrainVertex {
  float position[3] = {0.0f, 0.0f, 0.0f};
  float uv[2] = {0.0f, 0.0f};
};

struct FrustumPlane {
  glm::vec3 normal{0.0f};
  float distance = 0.0f;
};

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

static constexpr const char* kTerrainTessVS = R"(
struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
};

struct VSOutput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Pos = input.Pos;
    output.UV = input.UV;
    return output;
}
)";

static constexpr const char* kTerrainTessHS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
};

struct HSInput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

struct HSOutput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

struct HSConst
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

float2 projectToPixels(float3 local_pos)
{
    float4 world_pos = mul(g_Model, float4(local_pos, 1.0));
    float4 clip = mul(g_ViewProj, world_pos);
    float inv_w = 1.0 / max(abs(clip.w), 1.0e-5);
    float2 ndc = clip.xy * inv_w;
    return (ndc * 0.5 + 0.5) * g_RenderParams.yz;
}

float edgeTess(float3 a, float3 b)
{
    float pixels = length(projectToPixels(a) - projectToPixels(b));
    float target = max(g_RenderParams.x, 1.0);
    return clamp(pixels / target, 1.0, clamp(g_TileInfo.w, 1.0, 64.0));
}

HSConst patchConstants(InputPatch<HSInput, 3> patch, uint patch_id : SV_PrimitiveID)
{
    HSConst output;
    output.EdgeTess[0] = edgeTess(patch[1].Pos, patch[2].Pos);
    output.EdgeTess[1] = edgeTess(patch[2].Pos, patch[0].Pos);
    output.EdgeTess[2] = edgeTess(patch[0].Pos, patch[1].Pos);
    output.InsideTess = clamp((output.EdgeTess[0] + output.EdgeTess[1] + output.EdgeTess[2]) / 3.0,
                              1.0,
                              clamp(g_TileInfo.w, 1.0, 64.0));
    return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patchConstants")]
HSOutput main(InputPatch<HSInput, 3> patch,
              uint control_point_id : SV_OutputControlPointID,
              uint patch_id : SV_PrimitiveID)
{
    HSOutput output;
    output.Pos = patch[control_point_id].Pos;
    output.UV = patch[control_point_id].UV;
    return output;
}
)";

static constexpr const char* kTerrainTessDS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
};

Texture2D<float> g_HeightTexture;
SamplerState g_HeightSampler;

struct HSOutput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD0;
};

struct HSConst
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

struct DSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

[domain("tri")]
DSOutput main(HSConst input,
              const OutputPatch<HSOutput, 3> patch,
              float3 bary : SV_DomainLocation)
{
    float3 local_pos = patch[0].Pos * bary.x + patch[1].Pos * bary.y + patch[2].Pos * bary.z;
    float2 uv = patch[0].UV * bary.x + patch[1].UV * bary.y + patch[2].UV * bary.z;
    float h = g_HeightTexture.SampleLevel(g_HeightSampler, uv, 0.0);
    local_pos.y = h * g_TileInfo.y + g_TileInfo.z;
    float4 world_pos = mul(g_Model, float4(local_pos, 1.0));
    DSOutput output;
    output.Pos = mul(g_ViewProj, world_pos);
    output.UV = uv;
    output.WorldPos = world_pos.xyz;
    return output;
}
)";

static constexpr const char* kTerrainCpuVS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    float4 world_pos = mul(g_Model, float4(input.Pos, 1.0));
    VSOutput output;
    output.Pos = mul(g_ViewProj, world_pos);
    output.UV = input.UV;
    output.WorldPos = world_pos.xyz;
    return output;
}
)";

static constexpr const char* kTerrainPS = R"(
cbuffer TerrainConstants
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_TileInfo;
    float4 g_RenderParams;
    float4 g_MaterialScales;
};

Texture2D<float4> g_ColorTexture;
Texture2D<float4> g_ControlTexture;
Texture2D<float4> g_MaterialAlbedo0;
Texture2D<float4> g_MaterialAlbedo1;
Texture2D<float4> g_MaterialAlbedo2;
Texture2D<float4> g_MaterialAlbedo3;
Texture2D<float4> g_MaterialNormal0;
Texture2D<float4> g_MaterialNormal1;
Texture2D<float4> g_MaterialNormal2;
Texture2D<float4> g_MaterialNormal3;
Texture2D<float4> g_MaterialRoughness0;
Texture2D<float4> g_MaterialRoughness1;
Texture2D<float4> g_MaterialRoughness2;
Texture2D<float4> g_MaterialRoughness3;
SamplerState g_ColorSampler;
SamplerState g_MaterialSampler;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 macro = g_ColorTexture.Sample(g_ColorSampler, input.UV);
    float4 albedo = macro;
    float roughness = 1.0;
    float3 normal = normalize(cross(ddy(input.WorldPos), ddx(input.WorldPos)));
    if (normal.y < 0.0)
    {
        normal = -normal;
    }

    float layer_count = floor(g_RenderParams.w + 0.5);
    if (layer_count > 0.5)
    {
        float4 weights = g_ControlTexture.Sample(g_ColorSampler, input.UV);
        if (layer_count < 3.5) weights.a = 0.0;
        if (layer_count < 2.5) weights.b = 0.0;
        if (layer_count < 1.5) weights.g = 0.0;
        float weight_sum = max(weights.r + weights.g + weights.b + weights.a, 0.0001);
        weights /= weight_sum;

        float2 detail_uv = (input.WorldPos.xz / max(g_TileInfo.x, 0.001));
        float2 uv0 = detail_uv * max(g_MaterialScales.x, 0.001);
        float2 uv1 = detail_uv * max(g_MaterialScales.y, 0.001);
        float2 uv2 = detail_uv * max(g_MaterialScales.z, 0.001);
        float2 uv3 = detail_uv * max(g_MaterialScales.w, 0.001);

        float4 a0 = g_MaterialAlbedo0.Sample(g_MaterialSampler, uv0);
        float4 a1 = g_MaterialAlbedo1.Sample(g_MaterialSampler, uv1);
        float4 a2 = g_MaterialAlbedo2.Sample(g_MaterialSampler, uv2);
        float4 a3 = g_MaterialAlbedo3.Sample(g_MaterialSampler, uv3);
        albedo = a0 * weights.r + a1 * weights.g + a2 * weights.b + a3 * weights.a;
        albedo.rgb *= macro.rgb;
        albedo.a *= macro.a;

        float3 n0 = g_MaterialNormal0.Sample(g_MaterialSampler, uv0).xyz * 2.0 - 1.0;
        float3 n1 = g_MaterialNormal1.Sample(g_MaterialSampler, uv1).xyz * 2.0 - 1.0;
        float3 n2 = g_MaterialNormal2.Sample(g_MaterialSampler, uv2).xyz * 2.0 - 1.0;
        float3 n3 = g_MaterialNormal3.Sample(g_MaterialSampler, uv3).xyz * 2.0 - 1.0;
        float3 blended_tangent_normal =
            normalize(n0 * weights.r + n1 * weights.g + n2 * weights.b + n3 * weights.a);
        normal = normalize(float3(blended_tangent_normal.x,
                                  blended_tangent_normal.z,
                                  blended_tangent_normal.y));
        if (normal.y < 0.0)
        {
            normal = -normal;
        }

        float r0 = g_MaterialRoughness0.Sample(g_MaterialSampler, uv0).r;
        float r1 = g_MaterialRoughness1.Sample(g_MaterialSampler, uv1).r;
        float r2 = g_MaterialRoughness2.Sample(g_MaterialSampler, uv2).r;
        float r3 = g_MaterialRoughness3.Sample(g_MaterialSampler, uv3).r;
        roughness = saturate(r0 * weights.r + r1 * weights.g + r2 * weights.b + r3 * weights.a);
    }

    float3 light_dir = normalize(-g_LightDir.xyz);
    float light = saturate(dot(normal, light_dir));
    float3 view_dir = normalize(g_CameraPos.xyz - input.WorldPos);
    float3 half_dir = normalize(light_dir + view_dir);
    float spec_power = lerp(96.0, 8.0, roughness);
    float specular = pow(saturate(dot(normal, half_dir)), spec_power) *
                     (1.0 - roughness) * 0.18;
    float3 color = albedo.rgb * (0.30 + light * g_LightColor.rgb * 0.70) +
                   specular * g_LightColor.rgb;
    return float4(color, albedo.a);
}
)";

bool terrainTessellationEnabled(Diligent::IRenderDevice* device) {
  return device != nullptr &&
         device->GetDeviceInfo().Features.Tessellation ==
             Diligent::DEVICE_FEATURE_STATE_ENABLED;
}

glm::vec4 matrixRow(const glm::mat4& matrix, int row) {
  return {
      matrix[0][row],
      matrix[1][row],
      matrix[2][row],
      matrix[3][row],
  };
}

FrustumPlane normalizePlane(const glm::vec4& plane) {
  const glm::vec3 normal{plane.x, plane.y, plane.z};
  const float len = glm::length(normal);
  if (len <= 1.0e-6f) {
    return {};
  }
  return {.normal = normal / len, .distance = plane.w / len};
}

std::array<FrustumPlane, 6> extractFrustumPlanes(const glm::mat4& clip_from_world,
                                                 bool is_gl_ndc) {
  const glm::vec4 row0 = matrixRow(clip_from_world, 0);
  const glm::vec4 row1 = matrixRow(clip_from_world, 1);
  const glm::vec4 row2 = matrixRow(clip_from_world, 2);
  const glm::vec4 row3 = matrixRow(clip_from_world, 3);
  return {
      normalizePlane(row3 + row0),
      normalizePlane(row3 - row0),
      normalizePlane(row3 + row1),
      normalizePlane(row3 - row1),
      normalizePlane(is_gl_ndc ? row3 + row2 : row2),
      normalizePlane(row3 - row2),
  };
}

bool sphereIntersectsFrustum(const std::array<FrustumPlane, 6>& planes,
                             const glm::vec4& sphere,
                             float guard_band) {
  if (sphere.w <= 0.0f) {
    return true;
  }
  const glm::vec3 center{sphere.x, sphere.y, sphere.z};
  const float radius = sphere.w + std::max(guard_band, 0.0f);
  for (const FrustumPlane& plane : planes) {
    if (glm::dot(plane.normal, center) + plane.distance < -radius) {
      return false;
    }
  }
  return true;
}

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max({sx, sy, sz, 1.0f});
}

std::vector<TerrainVertex> buildPatchVertices(
    const rendering::TerrainDesc& desc,
    uint32_t resolution) {
  const uint32_t step = std::max(desc.base_patch_size, 1u);
  const float inv = 1.0f / static_cast<float>(std::max(resolution - 1u, 1u));
  std::vector<TerrainVertex> vertices;
  const uint32_t patch_count_per_axis = (resolution + step - 2u) / step;
  vertices.reserve(static_cast<std::size_t>(patch_count_per_axis) *
                   static_cast<std::size_t>(patch_count_per_axis) * 6u);

  auto make_vertex = [&](uint32_t x, uint32_t z) {
    const float u = static_cast<float>(x) * inv;
    const float v = static_cast<float>(z) * inv;
    TerrainVertex vertex{};
    vertex.position[0] = u * desc.tile_size;
    vertex.position[1] = 0.0f;
    vertex.position[2] = v * desc.tile_size;
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    return vertex;
  };

  for (uint32_t z = 0u; z + 1u < resolution; z += step) {
    const uint32_t z1 = std::min(z + step, resolution - 1u);
    for (uint32_t x = 0u; x + 1u < resolution; x += step) {
      const uint32_t x1 = std::min(x + step, resolution - 1u);
      const auto v00 = make_vertex(x, z);
      const auto v10 = make_vertex(x1, z);
      const auto v11 = make_vertex(x1, z1);
      const auto v01 = make_vertex(x, z1);
      vertices.insert(vertices.end(), {v00, v11, v10, v00, v01, v11});
    }
  }
  return vertices;
}

void buildCpuMesh(const rendering::TerrainDesc& desc,
                  const rendering::TerrainTileData& tile,
                  std::vector<TerrainVertex>& vertices,
                  std::vector<uint32_t>& indices,
                  float& out_min_height,
                  float& out_max_height) {
  const uint32_t resolution = tile.resolution;
  const float inv = 1.0f / static_cast<float>(std::max(resolution - 1u, 1u));
  vertices.resize(static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution));
  out_min_height = std::numeric_limits<float>::max();
  out_max_height = -std::numeric_limits<float>::max();
  for (uint32_t z = 0u; z < resolution; ++z) {
    for (uint32_t x = 0u; x < resolution; ++x) {
      const float u = static_cast<float>(x) * inv;
      const float v = static_cast<float>(z) * inv;
      const float normalized =
          tile.heights[static_cast<std::size_t>(z) * resolution + x];
      const float height = normalized * desc.height_scale + desc.height_offset;
      out_min_height = std::min(out_min_height, height);
      out_max_height = std::max(out_max_height, height);
      auto& vertex = vertices[static_cast<std::size_t>(z) * resolution + x];
      vertex.position[0] = u * desc.tile_size;
      vertex.position[1] = height;
      vertex.position[2] = v * desc.tile_size;
      vertex.uv[0] = u;
      vertex.uv[1] = v;
    }
  }
  indices.clear();
  indices.reserve(static_cast<std::size_t>(resolution - 1u) *
                  static_cast<std::size_t>(resolution - 1u) * 6u);
  for (uint32_t z = 0u; z + 1u < resolution; ++z) {
    for (uint32_t x = 0u; x + 1u < resolution; ++x) {
      const uint32_t base = z * resolution + x;
      indices.insert(indices.end(),
                     {base, base + resolution + 1u, base + 1u,
                      base, base + resolution, base + resolution + 1u});
    }
  }
  if (out_min_height == std::numeric_limits<float>::max()) {
    out_min_height = desc.height_offset;
    out_max_height = desc.height_offset;
  }
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> createStaticBuffer(
    Diligent::IRenderDevice* device,
    const void* data,
    std::size_t byte_count,
    Diligent::BIND_FLAGS bind_flags,
    const char* name) {
  Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
  if (device == nullptr || data == nullptr || byte_count == 0u) {
    return buffer;
  }
  Diligent::BufferDesc desc{};
  desc.Name = name;
  desc.Usage = Diligent::USAGE_IMMUTABLE;
  desc.BindFlags = bind_flags;
  desc.Size = static_cast<Diligent::Uint64>(byte_count);
  Diligent::BufferData buffer_data{};
  buffer_data.pData = data;
  buffer_data.DataSize = byte_count;
  device->CreateBuffer(desc, &buffer_data, &buffer);
  return buffer;
}

uint64_t terrainPipelineKey(Diligent::TEXTURE_FORMAT rtv_format,
                            Diligent::TEXTURE_FORMAT dsv_format,
                            uint32_t sample_count) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(rtv_format)) << 32u) |
         (static_cast<uint64_t>(static_cast<uint32_t>(dsv_format) & 0xffffu) << 8u) |
         static_cast<uint64_t>(std::max(sample_count, 1u));
}

Diligent::TEXTURE_FORMAT textureViewFormat(Diligent::ITextureView* view,
                                           Diligent::TEXTURE_FORMAT fallback) {
  if (view == nullptr) {
    return fallback;
  }
  const Diligent::TEXTURE_FORMAT format = view->GetDesc().Format;
  return format == Diligent::TEX_FORMAT_UNKNOWN ? fallback : format;
}

}  // namespace

rendering::TerrainId DiligentBackend::createTerrain(const rendering::TerrainDesc& desc) {
  const rendering::TerrainId id = nextTerrainId_++;
  TerrainRecord record{};
  record.desc = desc;
  record.desc.tile_size = std::max(record.desc.tile_size, 0.001f);
  record.desc.tile_resolution = std::max(record.desc.tile_resolution, 2u);
  record.desc.base_patch_size = std::max(record.desc.base_patch_size, 1u);
  record.desc.max_tessellation_factor =
      std::clamp(record.desc.max_tessellation_factor, 1.0f, 64.0f);
  record.desc.target_tessellated_edge_size =
      std::max(record.desc.target_tessellated_edge_size, 1.0f);
  terrains_.emplace(id, std::move(record));
  return id;
}

void DiligentBackend::destroyTerrain(rendering::TerrainId terrain) {
  if (terrain == rendering::kInvalidTerrain) {
    return;
  }
  terrains_.erase(terrain);
  terrain_submissions_.erase(
      std::remove_if(terrain_submissions_.begin(),
                     terrain_submissions_.end(),
                     [&](const TerrainSubmission& submission) {
                       return submission.item.terrain == terrain;
                     }),
      terrain_submissions_.end());
}

void DiligentBackend::uploadTerrainTile(rendering::TerrainId terrain,
                                        const rendering::TerrainTileData& tile) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end() || !tile.valid()) {
    return;
  }

  TerrainTileRecord record{};
  record.coord = tile.coord;

  if (device_) {
    Diligent::TextureSubResData height_subres{};
    height_subres.pData = tile.heights.data();
    height_subres.Stride = static_cast<Diligent::Uint32>(tile.resolution * sizeof(float));
    Diligent::TextureData height_data{};
    height_data.pSubResources = &height_subres;
    height_data.NumSubresources = 1u;
    Diligent::TextureDesc height_desc{};
    height_desc.Name = "Karma Terrain Height Tile";
    height_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    height_desc.Width = tile.resolution;
    height_desc.Height = tile.resolution;
    height_desc.MipLevels = 1u;
    height_desc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    height_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(height_desc, &height_data, &record.height_texture);
    if (record.height_texture) {
      record.height_srv =
          record.height_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }

    Diligent::TextureSubResData color_subres{};
    color_subres.pData = tile.color_rgba8.data();
    color_subres.Stride = static_cast<Diligent::Uint32>(tile.color_width * 4u);
    Diligent::TextureData color_data{};
    color_data.pSubResources = &color_subres;
    color_data.NumSubresources = 1u;
    Diligent::TextureDesc color_desc{};
    color_desc.Name = "Karma Terrain Color Tile";
    color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    color_desc.Width = tile.color_width;
    color_desc.Height = tile.color_height;
    color_desc.MipLevels = 1u;
    color_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    color_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    device_->CreateTexture(color_desc, &color_data, &record.color_texture);
    if (record.color_texture) {
      record.color_srv =
          record.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }

    if (!tile.control_rgba8.empty() && tile.control_width > 0u && tile.control_height > 0u) {
      record.control_srv =
          createTextureSRV(tile.control_rgba8.data(),
                           static_cast<int>(tile.control_width),
                           static_cast<int>(tile.control_height),
                           false,
                           false,
                           "Karma Terrain Control Tile",
                           record.control_texture);
    }

    const auto patch_vertices = buildPatchVertices(terrain_it->second.desc, tile.resolution);
    record.patch_vertex_count = static_cast<Diligent::Uint32>(std::min<std::size_t>(
        patch_vertices.size(), std::numeric_limits<Diligent::Uint32>::max()));
    record.patch_vertex_buffer =
        createStaticBuffer(device_,
                           patch_vertices.data(),
                           patch_vertices.size() * sizeof(TerrainVertex),
                           Diligent::BIND_VERTEX_BUFFER,
                           "Karma Terrain Patch VB");

    std::vector<TerrainVertex> cpu_vertices;
    std::vector<uint32_t> cpu_indices;
    buildCpuMesh(terrain_it->second.desc,
                 tile,
                 cpu_vertices,
                 cpu_indices,
                 record.min_height,
                 record.max_height);
    record.cpu_index_count = static_cast<Diligent::Uint32>(std::min<std::size_t>(
        cpu_indices.size(), std::numeric_limits<Diligent::Uint32>::max()));
    record.cpu_vertex_buffer =
        createStaticBuffer(device_,
                           cpu_vertices.data(),
                           cpu_vertices.size() * sizeof(TerrainVertex),
                           Diligent::BIND_VERTEX_BUFFER,
                           "Karma Terrain CPU VB");
    record.cpu_index_buffer =
        createStaticBuffer(device_,
                           cpu_indices.data(),
                           cpu_indices.size() * sizeof(uint32_t),
                           Diligent::BIND_INDEX_BUFFER,
                           "Karma Terrain CPU IB");
  } else {
    auto minmax = std::minmax_element(tile.heights.begin(), tile.heights.end());
    record.min_height = *minmax.first * terrain_it->second.desc.height_scale +
                        terrain_it->second.desc.height_offset;
    record.max_height = *minmax.second * terrain_it->second.desc.height_scale +
                        terrain_it->second.desc.height_offset;
  }

  terrain_it->second.tiles[tile.coord] = std::move(record);
  terrain_stats_.upload_count += 1u;
}

void DiligentBackend::uploadTerrainMaterialLayer(
    rendering::TerrainId terrain,
    const rendering::TerrainMaterialLayerData& layer) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end() || !layer.valid()) {
    return;
  }
  TerrainRecord& terrain_record = terrain_it->second;
  TerrainMaterialLayerRecord record{};
  record.name = layer.name;
  record.uv_scale = std::max(layer.uv_scale, 0.001f);
  record.enabled = true;

  if (device_) {
    record.albedo_srv =
        createTextureSRV(layer.albedo.rgba8.data(),
                         static_cast<int>(layer.albedo.width),
                         static_cast<int>(layer.albedo.height),
                         true,
                         true,
                         "Karma Terrain Material Albedo",
                         record.albedo_texture);
    if (layer.normal.valid()) {
      record.normal_srv =
          createTextureSRV(layer.normal.rgba8.data(),
                           static_cast<int>(layer.normal.width),
                           static_cast<int>(layer.normal.height),
                           false,
                           true,
                           "Karma Terrain Material Normal",
                           record.normal_texture);
    }
    if (layer.roughness.valid()) {
      record.roughness_srv =
          createTextureSRV(layer.roughness.rgba8.data(),
                           static_cast<int>(layer.roughness.width),
                           static_cast<int>(layer.roughness.height),
                           false,
                           true,
                           "Karma Terrain Material Roughness",
                           record.roughness_texture);
    }
  }

  terrain_record.material_layers[layer.layer] = std::move(record);
  terrain_record.material_layer_count =
      std::max(terrain_record.material_layer_count, layer.layer + 1u);
  for (auto& [coord, tile] : terrain_record.tiles) {
    (void)coord;
    tile.tess_srbs.clear();
    tile.cpu_srbs.clear();
  }
}

void DiligentBackend::clearTerrainMaterialLayers(rendering::TerrainId terrain) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end()) {
    return;
  }
  TerrainRecord& terrain_record = terrain_it->second;
  terrain_record.material_layers = {};
  terrain_record.material_layer_count = 0u;
  for (auto& [coord, tile] : terrain_record.tiles) {
    (void)coord;
    tile.tess_srbs.clear();
    tile.cpu_srbs.clear();
  }
}

void DiligentBackend::evictTerrainTile(rendering::TerrainId terrain,
                                       rendering::TerrainTileCoord coord) {
  auto terrain_it = terrains_.find(terrain);
  if (terrain_it == terrains_.end()) {
    return;
  }
  terrain_it->second.tiles.erase(coord);
  terrain_stats_.eviction_count += 1u;
}

void DiligentBackend::submitTerrain(const rendering::TerrainDrawItem& item) {
  if (item.terrain == rendering::kInvalidTerrain ||
      item.instance == rendering::kInvalidInstance ||
      terrains_.find(item.terrain) == terrains_.end()) {
    return;
  }
  terrain_submissions_.push_back(TerrainSubmission{.item = item});
  terrain_stats_.submitted_tiles += 1u;
}

rendering::TerrainCapabilities DiligentBackend::getTerrainCapabilities() const {
  rendering::TerrainCapabilities caps{};
  caps.supported = true;
  caps.hardware_tessellation = terrainTessellationEnabled(device_);
  caps.cpu_fallback = true;
  caps.max_tessellation_factor = 64u;
  return caps;
}

rendering::TerrainStats DiligentBackend::getTerrainStats() const {
  rendering::TerrainStats stats = terrain_stats_;
  stats.terrain_count = static_cast<uint32_t>(std::min<std::size_t>(
      terrains_.size(), std::numeric_limits<uint32_t>::max()));
  uint32_t resident_tiles = 0u;
  for (const auto& [id, terrain] : terrains_) {
    (void)id;
    resident_tiles += static_cast<uint32_t>(std::min<std::size_t>(
        terrain.tiles.size(), std::numeric_limits<uint32_t>::max() - resident_tiles));
  }
  stats.resident_tiles = resident_tiles;
  return stats;
}

DiligentBackend::TerrainPipelineSet* DiligentBackend::ensureTerrainResources(
    Diligent::TEXTURE_FORMAT rtv_format,
    Diligent::TEXTURE_FORMAT dsv_format) {
  if (!device_ || rtv_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return nullptr;
  }

  if (!terrain_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Terrain Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(TerrainConstants);
    device_->CreateBuffer(cb_desc, nullptr, &terrain_cb_);
  }
  if (!terrain_cb_) {
    return nullptr;
  }

  if (!terrain_color_sampler_) {
    Diligent::SamplerDesc sampler{};
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    device_->CreateSampler(sampler, &terrain_color_sampler_);
  }
  if (!terrain_height_sampler_) {
    Diligent::SamplerDesc sampler{};
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    device_->CreateSampler(sampler, &terrain_height_sampler_);
  }
  if (!terrain_material_sampler_) {
    Diligent::SamplerDesc sampler{};
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    device_->CreateSampler(sampler, &terrain_material_sampler_);
  }
  if (!terrain_default_control_) {
    terrain_default_control_ = createSolidTextureSRV(255,
                                                     0,
                                                     0,
                                                     255,
                                                     false,
                                                     "Karma Terrain Default Control",
                                                     terrain_default_control_tex_);
  }

  const uint64_t pipeline_key =
      terrainPipelineKey(rtv_format, dsv_format, activeRasterSampleCount());
  TerrainPipelineSet& pipelines = terrain_pipeline_sets_[pipeline_key];
  pipelines.rtv_format = rtv_format;
  pipelines.dsv_format = dsv_format;

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(TerrainVertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(TerrainVertex))},
      Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(TerrainVertex, uv)),
                              static_cast<Diligent::Uint32>(sizeof(TerrainVertex))},
  };
  const bool depth_enabled = dsv_format != Diligent::TEX_FORMAT_UNKNOWN;

  auto bind_static_constants = [&](Diligent::IPipelineState* pso,
                                   std::initializer_list<Diligent::SHADER_TYPE> stages) {
    if (!pso || !terrain_cb_) {
      return;
    }
    for (Diligent::SHADER_TYPE stage : stages) {
      if (auto* var = pso->GetStaticVariableByName(stage, "TerrainConstants")) {
        var->Set(terrain_cb_);
      }
    }
  };

  if (!pipelines.cpu_pipeline_state) {
    Diligent::ShaderCreateInfo shader_ci{};
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.EntryPoint = "main";

    shader_ci.Desc.Name = "Karma Terrain CPU VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.Source = kTerrainCpuVS;
    Diligent::RefCntAutoPtr<Diligent::IShader> vs = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.Source = kTerrainPS;
    Diligent::RefCntAutoPtr<Diligent::IShader> ps = device_with_cache_.CreateShader(shader_ci);

    if (vs && ps) {
      Diligent::GraphicsPipelineStateCreateInfo pso{};
      pso.PSODesc.Name = "Karma Terrain CPU Pipeline";
      pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      pso.pVS = vs;
      pso.pPS = ps;
      auto& graphics = pso.GraphicsPipeline;
      graphics.NumRenderTargets = 1u;
      graphics.SmplDesc.Count = static_cast<Diligent::Uint8>(activeRasterSampleCount());
      graphics.RTVFormats[0] = rtv_format;
      graphics.DSVFormat = dsv_format;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.RasterizerDesc.FrontCounterClockwise = true;
      graphics.DepthStencilDesc.DepthEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthWriteEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
      graphics.InputLayout.LayoutElements = layout;
      graphics.InputLayout.NumElements =
          static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));
      Diligent::ShaderResourceVariableDesc vars[] = {
          {Diligent::SHADER_TYPE_VERTEX, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ControlTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      };
      pso.PSODesc.ResourceLayout.Variables = vars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
      const auto pso_start = core::SteadyClock::now();
      pipelines.cpu_pipeline_state = createGraphicsPipelineState(pso);
      logRenderPipelineDiag("terrain",
                            "Karma Terrain CPU Pipeline",
                            pso_start,
                            core::SteadyClock::now());
      bind_static_constants(pipelines.cpu_pipeline_state.RawPtr(),
                            {Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL});
    }
  }

  if (!pipelines.tess_pipeline_state && terrainTessellationEnabled(device_)) {
    Diligent::ShaderCreateInfo shader_ci{};
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.EntryPoint = "main";

    shader_ci.Desc.Name = "Karma Terrain Tess VS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.Source = kTerrainTessVS;
    Diligent::RefCntAutoPtr<Diligent::IShader> vs = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain Tess HS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_HULL;
    shader_ci.Source = kTerrainTessHS;
    Diligent::RefCntAutoPtr<Diligent::IShader> hs = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain Tess DS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_DOMAIN;
    shader_ci.Source = kTerrainTessDS;
    Diligent::RefCntAutoPtr<Diligent::IShader> ds = device_with_cache_.CreateShader(shader_ci);

    shader_ci.Desc.Name = "Karma Terrain Tess PS";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.Source = kTerrainPS;
    Diligent::RefCntAutoPtr<Diligent::IShader> ps = device_with_cache_.CreateShader(shader_ci);

    if (vs && hs && ds && ps) {
      Diligent::GraphicsPipelineStateCreateInfo pso{};
      pso.PSODesc.Name = "Karma Terrain Tessellation Pipeline";
      pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      pso.pVS = vs;
      pso.pHS = hs;
      pso.pDS = ds;
      pso.pPS = ps;
      auto& graphics = pso.GraphicsPipeline;
      graphics.NumRenderTargets = 1u;
      graphics.SmplDesc.Count = static_cast<Diligent::Uint8>(activeRasterSampleCount());
      graphics.RTVFormats[0] = rtv_format;
      graphics.DSVFormat = dsv_format;
      graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
      graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
      graphics.RasterizerDesc.FrontCounterClockwise = true;
      graphics.DepthStencilDesc.DepthEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthWriteEnable = depth_enabled;
      graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
      graphics.InputLayout.LayoutElements = layout;
      graphics.InputLayout.NumElements =
          static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));
      Diligent::ShaderResourceVariableDesc vars[] = {
          {Diligent::SHADER_TYPE_VERTEX, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_HULL, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_DOMAIN, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_PIXEL, "TerrainConstants",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
          {Diligent::SHADER_TYPE_DOMAIN, "g_HeightTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_DOMAIN, "g_HeightSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ControlTexture",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialAlbedo3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialNormal3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness0",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness1",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness2",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialRoughness3",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_ColorSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
          {Diligent::SHADER_TYPE_PIXEL, "g_MaterialSampler",
           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      };
      pso.PSODesc.ResourceLayout.Variables = vars;
      pso.PSODesc.ResourceLayout.NumVariables =
          static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));
      const auto pso_start = core::SteadyClock::now();
      pipelines.tess_pipeline_state = createGraphicsPipelineState(pso);
      logRenderPipelineDiag("terrain",
                            "Karma Terrain Tessellation Pipeline",
                            pso_start,
                            core::SteadyClock::now());
      bind_static_constants(pipelines.tess_pipeline_state.RawPtr(),
                            {Diligent::SHADER_TYPE_VERTEX,
                             Diligent::SHADER_TYPE_HULL,
                             Diligent::SHADER_TYPE_DOMAIN,
                             Diligent::SHADER_TYPE_PIXEL});
    }
  }

  auto bind_pixel_resources = [&](Diligent::IShaderResourceBinding* srb,
                                  TerrainRecord& terrain,
                                  TerrainTileRecord& tile) {
    if (!srb) {
      return;
    }
    auto set_texture = [&](const char* name, Diligent::ITextureView* srv) {
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, name)) {
        var->Set(srv);
      }
    };
    set_texture("g_ColorTexture", tile.color_srv);
    set_texture("g_ControlTexture",
                tile.control_srv ? tile.control_srv.RawPtr()
                                 : terrain_default_control_.RawPtr());

    const auto layer_srv = [&](uint32_t index,
                               Diligent::RefCntAutoPtr<Diligent::ITextureView>
                                   TerrainMaterialLayerRecord::*member,
                               Diligent::ITextureView* fallback) -> Diligent::ITextureView* {
      if (index >= terrain.material_layers.size()) {
        return fallback;
      }
      const TerrainMaterialLayerRecord& layer = terrain.material_layers[index];
      Diligent::ITextureView* const value = (layer.*member).RawPtr();
      return layer.enabled && value != nullptr ? value : fallback;
    };
    set_texture("g_MaterialAlbedo0",
                layer_srv(0u, &TerrainMaterialLayerRecord::albedo_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialAlbedo1",
                layer_srv(1u, &TerrainMaterialLayerRecord::albedo_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialAlbedo2",
                layer_srv(2u, &TerrainMaterialLayerRecord::albedo_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialAlbedo3",
                layer_srv(3u, &TerrainMaterialLayerRecord::albedo_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialNormal0",
                layer_srv(0u, &TerrainMaterialLayerRecord::normal_srv,
                          default_normal_.RawPtr()));
    set_texture("g_MaterialNormal1",
                layer_srv(1u, &TerrainMaterialLayerRecord::normal_srv,
                          default_normal_.RawPtr()));
    set_texture("g_MaterialNormal2",
                layer_srv(2u, &TerrainMaterialLayerRecord::normal_srv,
                          default_normal_.RawPtr()));
    set_texture("g_MaterialNormal3",
                layer_srv(3u, &TerrainMaterialLayerRecord::normal_srv,
                          default_normal_.RawPtr()));
    set_texture("g_MaterialRoughness0",
                layer_srv(0u, &TerrainMaterialLayerRecord::roughness_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialRoughness1",
                layer_srv(1u, &TerrainMaterialLayerRecord::roughness_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialRoughness2",
                layer_srv(2u, &TerrainMaterialLayerRecord::roughness_srv,
                          default_base_color_.RawPtr()));
    set_texture("g_MaterialRoughness3",
                layer_srv(3u, &TerrainMaterialLayerRecord::roughness_srv,
                          default_base_color_.RawPtr()));

    if (auto* var =
            srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ColorSampler")) {
      var->Set(terrain_color_sampler_ ? terrain_color_sampler_.RawPtr()
                                      : sampler_color_.RawPtr());
    }
    if (auto* var =
            srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MaterialSampler")) {
      var->Set(terrain_material_sampler_ ? terrain_material_sampler_.RawPtr()
                                         : sampler_color_.RawPtr());
    }
  };

  auto initialize_tile_srb = [&](TerrainRecord& terrain, TerrainTileRecord& tile) {
    auto& cpu_srb = tile.cpu_srbs[pipeline_key];
    if (!cpu_srb && pipelines.cpu_pipeline_state) {
      pipelines.cpu_pipeline_state->CreateShaderResourceBinding(&cpu_srb, true);
      if (cpu_srb) {
        bind_pixel_resources(cpu_srb.RawPtr(), terrain, tile);
      }
    }

    auto& tess_srb = tile.tess_srbs[pipeline_key];
    if (!tess_srb && pipelines.tess_pipeline_state) {
      pipelines.tess_pipeline_state->CreateShaderResourceBinding(&tess_srb, true);
      if (tess_srb) {
        if (auto* var = tess_srb->GetVariableByName(Diligent::SHADER_TYPE_DOMAIN,
                                                    "g_HeightTexture")) {
          var->Set(tile.height_srv);
        }
        if (auto* var = tess_srb->GetVariableByName(Diligent::SHADER_TYPE_DOMAIN,
                                                    "g_HeightSampler")) {
          var->Set(terrain_height_sampler_ ? terrain_height_sampler_.RawPtr()
                                           : sampler_data_.RawPtr());
        }
        bind_pixel_resources(tess_srb.RawPtr(), terrain, tile);
      }
    }
  };

  for (auto& [terrain_id, terrain] : terrains_) {
    (void)terrain_id;
    for (auto& [coord, tile] : terrain.tiles) {
      (void)coord;
      initialize_tile_srb(terrain, tile);
    }
  }
  return &pipelines;
}

Diligent::Uint32 DiligentBackend::renderTerrainLayer(rendering::LayerId layer,
                                                     const DrawConstants& base_constants,
                                                     const glm::mat4& view_proj,
                                                     bool is_gl,
                                                     Diligent::ITextureView* active_rtv,
                                                     Diligent::ITextureView* active_dsv,
                                                     int render_width,
                                                     int render_height) {
  if (!context_ || !active_rtv || terrain_submissions_.empty()) {
    return 0u;
  }
  const Diligent::TEXTURE_FORMAT fallback_rtv_format =
      swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                  : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
  const Diligent::TEXTURE_FORMAT fallback_dsv_format =
      swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                  : Diligent::TEX_FORMAT_D32_FLOAT;
  const Diligent::TEXTURE_FORMAT rtv_format =
      textureViewFormat(active_rtv, fallback_rtv_format);
  const Diligent::TEXTURE_FORMAT dsv_format =
      active_dsv ? textureViewFormat(active_dsv, fallback_dsv_format)
                 : Diligent::TEX_FORMAT_UNKNOWN;
  TerrainPipelineSet* pipelines = ensureTerrainResources(rtv_format, dsv_format);
  if (!pipelines ||
      (!pipelines->cpu_pipeline_state && !pipelines->tess_pipeline_state)) {
    return 0u;
  }
  const uint64_t pipeline_key =
      terrainPipelineKey(rtv_format, dsv_format, activeRasterSampleCount());

  auto* rtv = active_rtv;
  auto* dsv = active_dsv;
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(render_width);
  viewport.Height = static_cast<float>(render_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(render_width),
                         static_cast<Diligent::Uint32>(render_height));

  const auto frustum_planes = extractFrustumPlanes(view_proj, is_gl);
  Diligent::Uint32 draw_count = 0u;
  for (const TerrainSubmission& submission : terrain_submissions_) {
    const auto& item = submission.item;
    if (!item.visible || item.layer != layer) {
      continue;
    }
    auto terrain_it = terrains_.find(item.terrain);
    if (terrain_it == terrains_.end()) {
      continue;
    }
    TerrainRecord& terrain = terrain_it->second;
    auto tile_it = terrain.tiles.find(item.coord);
    if (tile_it == terrain.tiles.end()) {
      continue;
    }
    TerrainTileRecord& tile = tile_it->second;

    const float tile_origin_x =
        static_cast<float>(item.coord.x - terrain.desc.origin_tile_x) * terrain.desc.tile_size;
    const float tile_origin_z =
        static_cast<float>(item.coord.z - terrain.desc.origin_tile_z) * terrain.desc.tile_size;
    glm::mat4 model = item.transform;
    model = glm::translate(model, glm::vec3(tile_origin_x, 0.0f, tile_origin_z));

    const float mid_y = (tile.min_height + tile.max_height) * 0.5f;
    const float half_y = std::max((tile.max_height - tile.min_height) * 0.5f, 1.0f);
    const glm::vec3 local_center{terrain.desc.tile_size * 0.5f,
                                 mid_y,
                                 terrain.desc.tile_size * 0.5f};
    const float local_radius =
        std::sqrt(terrain.desc.tile_size * terrain.desc.tile_size * 0.5f +
                  half_y * half_y);
    const glm::vec3 world_center = glm::vec3(model * glm::vec4(local_center, 1.0f));
    const glm::vec4 sphere(world_center, local_radius * maxTransformScale(model));
    const float cull_guard_band = sphere.w * 0.12f;
    if (!sphereIntersectsFrustum(frustum_planes, sphere, cull_guard_band)) {
      terrain_stats_.culled_tiles += 1u;
      continue;
    }

    auto tess_srb_it = tile.tess_srbs.find(pipeline_key);
    Diligent::IShaderResourceBinding* tess_srb =
        tess_srb_it != tile.tess_srbs.end() ? tess_srb_it->second.RawPtr() : nullptr;
    auto cpu_srb_it = tile.cpu_srbs.find(pipeline_key);
    Diligent::IShaderResourceBinding* cpu_srb =
        cpu_srb_it != tile.cpu_srbs.end() ? cpu_srb_it->second.RawPtr() : nullptr;

    const bool use_tessellation =
        pipelines->tess_pipeline_state &&
        terrainTessellationEnabled(device_) &&
        tess_srb &&
        tile.patch_vertex_buffer &&
        tile.patch_vertex_count > 0u;
    const bool use_cpu =
        !use_tessellation &&
        terrain.desc.cpu_fallback_enabled &&
        pipelines->cpu_pipeline_state &&
        cpu_srb &&
        tile.cpu_vertex_buffer &&
        tile.cpu_index_buffer &&
        tile.cpu_index_count > 0u;
    if (!use_tessellation && !use_cpu) {
      continue;
    }

    TerrainConstants constants{};
    copyMat4(constants.view_proj, view_proj);
    copyMat4(constants.model, model);
    std::memcpy(constants.light_dir, base_constants.light_dir, sizeof(constants.light_dir));
    std::memcpy(constants.light_color, base_constants.light_color, sizeof(constants.light_color));
    std::memcpy(constants.camera_pos, base_constants.camera_pos, sizeof(constants.camera_pos));
    constants.tile_info[0] = terrain.desc.tile_size;
    constants.tile_info[1] = terrain.desc.height_scale;
    constants.tile_info[2] = terrain.desc.height_offset;
    constants.tile_info[3] = terrain.desc.max_tessellation_factor;
    constants.render_params[0] = terrain.desc.target_tessellated_edge_size;
    constants.render_params[1] = static_cast<float>(std::max(render_width, 1));
    constants.render_params[2] = static_cast<float>(std::max(render_height, 1));
    constants.render_params[3] = static_cast<float>(std::min<uint32_t>(
        terrain.material_layer_count, static_cast<uint32_t>(terrain.material_layers.size())));
    for (uint32_t i = 0u; i < terrain.material_layers.size(); ++i) {
      constants.material_scales[i] =
          terrain.material_layers[i].enabled
              ? std::max(terrain.material_layers[i].uv_scale, 0.001f)
              : 1.0f;
    }
    {
      Diligent::MapHelper<TerrainConstants> mapped(
          context_, terrain_cb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (!mapped_constants) {
        continue;
      }
      *mapped_constants = constants;
    }

    if (use_tessellation) {
      context_->SetPipelineState(pipelines->tess_pipeline_state);
      context_->CommitShaderResources(tess_srb,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::IBuffer* vbs[] = {tile.patch_vertex_buffer};
      Diligent::Uint64 offsets[] = {0u};
      context_->SetVertexBuffers(0,
                                 1,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      Diligent::DrawAttribs draw{};
      draw.NumVertices = tile.patch_vertex_count;
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->Draw(draw);
      terrain_stats_.tessellated_tiles += 1u;
    } else {
      context_->SetPipelineState(pipelines->cpu_pipeline_state);
      context_->CommitShaderResources(cpu_srb,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::IBuffer* vbs[] = {tile.cpu_vertex_buffer};
      Diligent::Uint64 offsets[] = {0u};
      context_->SetVertexBuffers(0,
                                 1,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      context_->SetIndexBuffer(tile.cpu_index_buffer,
                               0u,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::DrawIndexedAttribs draw{};
      draw.IndexType = Diligent::VT_UINT32;
      draw.NumIndices = tile.cpu_index_count;
      draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->DrawIndexed(draw);
      terrain_stats_.cpu_fallback_tiles += 1u;
    }
    terrain_stats_.drawn_tiles += 1u;
    draw_count += 1u;
  }
  return draw_count;
}

}  // namespace karma::rendering::backend
