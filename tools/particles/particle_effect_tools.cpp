#include "particle_effect_tools.h"

#include "karma/assets.h"
#include "karma/visual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace karma::tools::particles {

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr float kFeetToMeters = 0.3048f;
constexpr float kFireballDefaultBlastRadius = 30.0f * kFeetToMeters;
constexpr float kFireballMaxBlastRadius = 50.0f * kFeetToMeters;
constexpr float kFireballOrbRadius = 0.18f;
constexpr float kDetectMagicRadius = 30.0f * kFeetToMeters;

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

std::optional<Json> readJson(const std::filesystem::path& path, std::string* diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    fail(diagnostic, "failed to open JSON file: " + path.string());
    return std::nullopt;
  }
  Json json;
  try {
    stream >> json;
  } catch (const std::exception& e) {
    fail(diagnostic, std::string("failed to parse JSON: ") + e.what());
    return std::nullopt;
  }
  return json;
}

bool writeJson(const std::filesystem::path& path, const Json& json, std::string* diagnostic) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return fail(diagnostic, "failed to create directory: " + path.parent_path().string());
    }
  }
  std::ofstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to write JSON file: " + path.string());
  }
  stream << json.dump(2) << '\n';
  return static_cast<bool>(stream);
}

bool writeTextFile(const std::filesystem::path& path,
                   std::string_view text,
                   std::string* diagnostic) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return fail(diagnostic, "failed to create directory: " + path.parent_path().string());
    }
  }
  std::ofstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to write text file: " + path.string());
  }
  stream << text;
  if (text.empty() || text.back() != '\n') {
    stream << '\n';
  }
  return static_cast<bool>(stream);
}

std::optional<OrderedJson> readOrderedJson(const std::filesystem::path& path,
                                           std::string* diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    fail(diagnostic, "failed to open JSON file: " + path.string());
    return std::nullopt;
  }
  OrderedJson json;
  try {
    stream >> json;
  } catch (const std::exception& e) {
    fail(diagnostic, std::string("failed to parse JSON: ") + e.what());
    return std::nullopt;
  }
  return json;
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::filesystem::path findRepoRoot(const std::filesystem::path& start) {
  if (const char* value = std::getenv("KARMA_REPO_ROOT")) {
    std::filesystem::path root(value);
    if (std::filesystem::exists(root / "examples/assets/prefabs/explosion/textures/glow_atlas.png")) {
      return root;
    }
  }

  std::error_code ec;
  std::filesystem::path cursor = std::filesystem::absolute(start, ec);
  if (ec) {
    cursor = start;
  }
  if (!std::filesystem::is_directory(cursor, ec)) {
    cursor = cursor.parent_path();
  }
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "examples/assets/prefabs/explosion/textures/glow_atlas.png",
                                ec)) {
      return cursor;
    }
    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }
    cursor = parent;
  }
  if (start != std::filesystem::current_path(ec)) {
    return findRepoRoot(std::filesystem::current_path(ec));
  }
  return {};
}

std::string presetDefaultNamespace(std::string_view preset) {
  return "generated/" + std::string(preset);
}

std::string presetDefaultName(std::string_view preset) {
  if (preset == "fire_ray") {
    return "Generated Fire Ray";
  }
  if (preset == "magic_missile") {
    return "Generated Magic Missile";
  }
  if (preset == "arcane_barrage") {
    return "Generated Arcane Barrage";
  }
  if (preset == "blade_barrier") {
    return "Generated Blade Barrier";
  }
  if (preset == "chromatic_ray") {
    return "Generated Chromatic Ray";
  }
  if (preset == "daze") {
    return "Generated Daze";
  }
  if (preset == "heal") {
    return "Generated Heal";
  }
  if (preset == "haste") {
    return "Generated Haste";
  }
  if (preset == "detect_magic") {
    return "Generated Detect Magic";
  }
  if (preset == "breathe_fire") {
    return "Generated Breathe Fire";
  }
  if (preset == "fireball") {
    return "Generated Fireball";
  }
  if (preset == "impact_burst") {
    return "Generated Impact Burst";
  }
  if (preset == "energy_orb") {
    return "Generated Energy Orb";
  }
  return "Generated Particle Effect";
}

float readFloat(const Json& json, std::string_view key, float fallback) {
  const auto it = json.find(std::string(key));
  return it != json.end() && it->is_number() ? it->get<float>() : fallback;
}

std::string readString(const Json& json, std::string_view key, std::string fallback) {
  const auto it = json.find(std::string(key));
  return it != json.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
}

std::vector<Json> defaultPathPoints(float length) {
  const float safe_length = std::max(length, 0.25f);
  return {
      Json::array({0.0f, 1.0f, 0.0f}),
      Json::array({safe_length * 0.45f, 1.12f, 0.0f}),
      Json::array({safe_length, 1.0f, 0.0f}),
  };
}

bool readPathPoints(const Json& spec,
                    float length,
                    std::vector<Json>& out_points,
                    std::string* diagnostic) {
  const auto it = spec.find("path_points");
  if (it == spec.end()) {
    out_points = defaultPathPoints(length);
    return true;
  }
  if (!it->is_array() || it->size() < 2u) {
    return fail(diagnostic, "path_points must be an array with at least two [x,y,z] points");
  }
  std::vector<Json> points;
  points.reserve(it->size());
  for (const Json& point : *it) {
    if (!point.is_array() || point.size() != 3u ||
        !point[0].is_number() ||
        !point[1].is_number() ||
        !point[2].is_number()) {
      return fail(diagnostic, "each path point must be a 3-number array");
    }
    points.push_back(Json::array({point[0].get<float>(), point[1].get<float>(), point[2].get<float>()}));
  }
  out_points = std::move(points);
  return true;
}

Json color(float r, float g, float b, float a) {
  return Json::array({r, g, b, a});
}

Json vec3(float x, float y, float z) {
  return Json::array({x, y, z});
}

Json vec3(const math::Vec3& value) {
  return vec3(value.x, value.y, value.z);
}

std::string numberExpr(float value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(6) << value;
  return stream.str();
}

Json expr(std::string expression) {
  return Json{{"$expr", std::move(expression)}};
}

Json var(std::string name) {
  return Json{{"$var", std::move(name)}};
}

Json scaledVar(std::string_view name, float scale) {
  return expr(std::string(name) + " * (" + numberExpr(scale) + ")");
}

Json variableVec3(float radius_x, float height_y, float radius_z) {
  return Json::array({scaledVar("radius", radius_x),
                      scaledVar("height", height_y),
                      scaledVar("radius", radius_z)});
}

Json makeEmitter(std::string name,
                 std::string texture,
                 std::string blend_mode,
                 std::string source_shape,
                 uint32_t max_particles,
                 uint32_t burst_count,
                 float spawn_rate,
                 float lifetime_min,
                 float lifetime_max,
                 float start_size_min,
                 float start_size_max,
                 float end_size_min,
                 float end_size_max,
                 Json start_color,
                 Json end_color,
                 Json source_points = Json::array(),
                 uint32_t seed = 1u) {
  Json source{
      {"shape", std::move(source_shape)},
      {"box_extents", vec3(0.0f, 0.0f, 0.0f)},
      {"dimensions", vec3(0.0f, 0.0f, 0.0f)},
      {"radius_min", 0.0f},
      {"radius_max", 0.5f},
      {"inner_radius", 0.0f},
      {"outer_radius", 0.5f},
      {"height", 0.0f},
      {"angle", 0.0f},
      {"points", std::move(source_points)},
      {"closed_loop", false},
      {"sampling", "random"},
      {"jitter_radius", 0.04f},
      {"mesh_asset_key", ""},
      {"distribution", "uniform"},
      {"radial_speed_min", 0.0f},
      {"radial_speed_max", 0.0f},
  };

  if (source["shape"] == "path") {
    source["sampling"] = "sequential";
    source["jitter_radius"] = 0.08f;
    source["radial_speed_min"] = 0.02f;
    source["radial_speed_max"] = 0.12f;
  } else if (source["shape"] == "sphere" || source["shape"] == "sphere_surface") {
    source["radius_min"] = 0.0f;
    source["radius_max"] = 0.4f;
    source["radial_speed_min"] = 0.35f;
    source["radial_speed_max"] = 1.6f;
  } else if (source["shape"] == "disc" || source["shape"] == "ring") {
    source["inner_radius"] = 0.05f;
    source["outer_radius"] = 0.8f;
    source["radial_speed_min"] = 0.15f;
    source["radial_speed_max"] = 1.0f;
  }

  return Json{
      {"name", std::move(name)},
      {"texture", std::move(texture)},
      {"playback",
       {
           {"enabled", true},
           {"playing", true},
           {"loop", true},
           {"emit_burst_on_start", burst_count > 0u},
           {"local_space", false},
           {"time_scale", 1.0f},
           {"start_delay", 0.0f},
           {"duration", 0.0f},
       }},
      {"render",
       {
           {"layer", 0},
           {"depth_test", true},
           {"blend_mode", std::move(blend_mode)},
           {"alignment", "billboard"},
           {"shading_mode", "standard"},
           {"use_soft_mask", true},
           {"soft_particle_distance", 0.0f},
           {"distortion_strength", 0.0f},
           {"fresnel_power", 4.0f},
           {"fresnel_strength", 1.0f},
           {"refraction_strength", 0.0f},
           {"interior_glow", 0.0f},
       }},
      {"atlas",
       {
           {"columns", 4},
           {"rows", 1},
           {"frame_count", 4},
           {"frame_width", 0},
           {"frame_height", 0},
           {"border_x", 0},
           {"border_y", 0},
           {"spacing_x", 0},
           {"spacing_y", 0},
           {"animation_fps", 12.0f},
           {"animate_over_lifetime", true},
           {"random_start_frame", true},
       }},
      {"emission",
       {
           {"max_particles", max_particles},
           {"burst_count", burst_count},
           {"seed", seed},
           {"spawn_rate", spawn_rate},
       }},
      {"lifetime", {{"min", lifetime_min}, {"max", lifetime_max}}},
      {"size",
       {
           {"start_min", start_size_min},
           {"start_max", start_size_max},
           {"end_min", end_size_min},
           {"end_max", end_size_max},
           {"curve_exponent", 1.0f},
       }},
      {"rotation",
       {
           {"initial_min", 0.0f},
           {"initial_max", 6.2831853f},
           {"angular_velocity_min", -2.2f},
           {"angular_velocity_max", 2.2f},
       }},
      {"source", std::move(source)},
      {"motion",
       {
           {"velocity_min", vec3(-0.15f, 0.2f, -0.15f)},
           {"velocity_max", vec3(0.15f, 0.8f, 0.15f)},
           {"acceleration", vec3(0.0f, -0.15f, 0.0f)},
           {"drag", 0.1f},
           {"orbit_axis", vec3(0.0f, 1.0f, 0.0f)},
           {"orbit_speed", 0.0f},
       }},
      {"collision",
       {
           {"ground", false},
           {"ground_height", 0.0f},
           {"bounce_damping", 0.25f},
           {"friction", 0.25f},
           {"rest_speed_threshold", 0.25f},
       }},
      {"color",
       {
           {"start", std::move(start_color)},
           {"end", std::move(end_color)},
           {"alpha_curve_exponent", 1.0f},
       }},
  };
}

Json effect(Json emitter) {
  Json emitters = Json::array();
  emitters.push_back(std::move(emitter));
  return Json{{"version", 3}, {"emitters", std::move(emitters)}};
}

Json effect(std::vector<Json> emitters) {
  Json json_emitters = Json::array();
  for (Json& emitter : emitters) {
    json_emitters.push_back(std::move(emitter));
  }
  return Json{{"version", 3}, {"emitters", std::move(json_emitters)}};
}

struct GeneratedAsset {
  std::string type;
  std::string key;
  std::string path;
};

void addTexture(std::vector<GeneratedAsset>& assets,
                std::string key,
                std::string path) {
  assets.push_back(GeneratedAsset{
      .type = "texture_rgba8",
      .key = std::move(key),
      .path = std::move(path),
  });
}

void addMesh(std::vector<GeneratedAsset>& assets,
             std::string key,
             std::string path) {
  assets.push_back(GeneratedAsset{
      .type = "mesh",
      .key = std::move(key),
      .path = std::move(path),
  });
}

void addEffect(std::vector<GeneratedAsset>& assets,
               std::string key,
               std::string path) {
  assets.push_back(GeneratedAsset{
      .type = "particle_effect",
      .key = std::move(key),
      .path = std::move(path),
  });
}

void addMaterial(std::vector<GeneratedAsset>& assets,
                 std::string key,
                 std::string path) {
  assets.push_back(GeneratedAsset{
      .type = "material",
      .key = std::move(key),
      .path = std::move(path),
  });
}

Json makeParticleNode(uint32_t id,
                      uint32_t parent,
                      std::string name,
                      std::string effect_key,
                      Json position = vec3(0.0f, 0.0f, 0.0f)) {
  return Json{
      {"id", id},
      {"name", name},
      {"parent", parent},
      {"components",
       {
           {"TagComponent", {{"name", name}}},
           {"TransformComponent",
            {
                {"position", std::move(position)},
                {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                {"scale", vec3(1.0f, 1.0f, 1.0f)},
            }},
           {"ParticleEffectComponent",
            {
                {"effect_key", std::move(effect_key)},
                {"auto_apply", true},
                {"preserve_enabled", true},
                {"preserve_playing", true},
                {"preserve_start_delay", false},
            }},
           {"ParticleEmitterComponent",
            {
                {"enabled", true},
                {"playing", true},
                {"start_delay", 0.0f},
                {"texture_key", ""},
            }},
       }},
  };
}

Json makeParticleNodeWithOverride(uint32_t id,
                                  uint32_t parent,
                                  std::string name,
                                  std::string effect_key,
                                  Json effect_override,
                                  Json position = vec3(0.0f, 0.0f, 0.0f)) {
  Json node = makeParticleNode(id,
                               parent,
                               std::move(name),
                               std::move(effect_key),
                               std::move(position));
  node["components"]["ParticleEffectOverrideComponent"] = std::move(effect_override);
  return node;
}

Json makeVolumetricNode(uint32_t id,
                        uint32_t parent,
                        std::string name,
                        Json position,
                        Json radius,
                        std::string interior_material_key,
                        std::string surface_material_key,
                        bool surface_double_sided = false) {
  return Json{
      {"id", id},
      {"name", name},
      {"parent", parent},
      {"components",
       {
           {"TagComponent", {{"name", name}}},
           {"TransformComponent",
            {
                {"position", std::move(position)},
                {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                {"scale", vec3(1.0f, 1.0f, 1.0f)},
            }},
           {"VolumetricComponent",
            {
                {"shape", "sphere"},
                {"radius", std::move(radius)},
                {"capsule_half_length", 0.0f},
                {"scale_with_transform", false},
                {"visible", true},
                {"overlay_depth", 0.10f},
                {"surface_double_sided", surface_double_sided},
                {"interior_material_key", std::move(interior_material_key)},
                {"surface_material_key", std::move(surface_material_key)},
            }},
       }},
  };
}

constexpr std::string_view kDetectMagicVolumeVertexShader = R"(cbuffer Constants
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
)";

constexpr std::string_view kDetectMagicVolumePixelHeader = R"(cbuffer Constants
{
    float4x4 g_MVP;
    float4x4 g_Model;
    float4x4 g_LightViewProj;
    float4x4 g_ShadowUVProj;
    float4x4 g_ShadowCascadeUVProj[4];
    float4x4 g_PointShadowUVProj[96];
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_PointShadowParams;
    float4 g_LocalLightParams;
    float4 g_PointShadowTuning;
    float4 g_ShadowBiasParams;
    float4 g_ShadowCascadeSplits;
    float4 g_ShadowCascadeWorldTexel;
    float4 g_ShadowCascadeParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_CameraForward;
    float4 g_ScreenParams;
    float4 g_CameraClipParams;
    float4 g_ForwardPlusParams;
    float4 g_LocalLightPositionRange[64];
    float4 g_LocalLightDirectionType[64];
    float4 g_LocalLightColorIntensity[64];
    float4 g_LocalLightSpotParams[64];
    float4 g_LocalLightMeta;
    float4 g_InstanceParams;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
    float4 g_MaterialParams3;
    float4 g_MaterialParams4;
    float4 g_MaterialParams5;
    float4 g_MaterialParams6;
    float4 g_VolumeParams0;
    float4 g_VolumeParams1;
    float4 g_VolumeParams2;
    float4 g_VolumeParams3;
    float4 g_VolumeParams4;
    float4 g_TexCoordRow0[12];
    float4 g_TexCoordRow1[12];
};

Texture2D g_SceneColor;
Texture2D<float> g_SceneDepth;
SamplerState g_SamplerColor;
SamplerState g_SamplerData;

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

float3 SafeNormalize(float3 v, float3 fallback)
{
    float len_sq = dot(v, v);
    return len_sq > 1.0e-8 ? v * rsqrt(len_sq) : fallback;
}

float LinearizeSceneDepth(float depth)
{
    float near_clip = max(g_CameraClipParams.x, 0.001);
    float far_clip = max(g_CameraClipParams.y, near_clip + 0.001);
    if (g_CameraClipParams.z > 0.5)
    {
        return (near_clip * far_clip) /
               max(far_clip - depth * (far_clip - near_clip), 1.0e-4);
    }
    return near_clip + depth * (far_clip - near_clip);
}

bool IntersectSphere(float3 ro, float3 rd, float radius, out float t0, out float t1)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float h = b * b - c;
    if (h < 0.0)
    {
        t0 = 0.0;
        t1 = 0.0;
        return false;
    }
    h = sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
    return true;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
)";

constexpr std::string_view kDetectMagicInteriorPixelShader = R"(float4 PSMain(PSInput input) : SV_TARGET
{
    float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
    float3 center = g_VolumeParams0.xyz;
    float radius = max(g_VolumeParams0.w, 1.0e-4);
    float3 axis_x = SafeNormalize(g_VolumeParams1.xyz, float3(1.0, 0.0, 0.0));
    float3 axis_y = SafeNormalize(g_VolumeParams2.xyz, float3(0.0, 1.0, 0.0));
    float3 axis_z = SafeNormalize(g_VolumeParams3.xyz, float3(0.0, 0.0, 1.0));
    float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
    float3 ro = g_CameraPos.xyz - center;

    float t0;
    float t1;
    if (!IntersectSphere(ro, ray_dir, radius, t0, t1) || t1 <= 0.0)
    {
        discard;
    }
    float t_enter = max(t0, 0.0);
    float t_exit = max(t1, t_enter + 1.0e-4);
    float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
    float raw_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
    if (raw_depth >= 0.9999)
    {
        discard;
    }
    float scene_t = LinearizeSceneDepth(raw_depth) / ray_forward;
    if (scene_t < t_enter || scene_t > t_exit)
    {
        discard;
    }

    float time = g_LocalLightMeta.w;
    float3 scene_pos = g_CameraPos.xyz + ray_dir * scene_t;
    float3 scene_offset = scene_pos - center;
    float3 scene_local3 = float3(dot(scene_offset, axis_x),
                                 dot(scene_offset, axis_y),
                                 dot(scene_offset, axis_z)) / radius;
    float volume_radius = length(scene_local3);
    if (volume_radius > 1.0)
    {
        discard;
    }

    float boundary_fade = 1.0 - smoothstep(0.82, 1.0, volume_radius);
    float surface_depth = min(scene_t - t_enter, t_exit - scene_t);
    float surface_fade = smoothstep(0.0, radius * 0.08, surface_depth);
    float mask = saturate(boundary_fade * surface_fade);
    if (mask <= 0.001)
    {
        discard;
    }

    float2 local_plane = scene_local3.xz;
    float radial = length(local_plane);
    float angle = atan2(local_plane.y, local_plane.x);
    float2 tangent = SafeNormalize(float3(-local_plane.y, local_plane.x, 0.0),
                                   float3(1.0, 0.0, 0.0)).xy;
    float2 radial_dir = SafeNormalize(float3(local_plane, 0.0),
                                      float3(0.0, 1.0, 0.0)).xy;
    float inner_gate = smoothstep(0.03, 0.24, radial) *
                       (1.0 - smoothstep(0.88, 1.0, volume_radius));

    float3 heat_p = scene_local3 * 8.0;
    float plume_a = sin(heat_p.x * 1.91 + heat_p.y * 3.37 + time * 2.10);
    float plume_b = sin(heat_p.z * 2.47 - heat_p.y * 2.81 - time * 1.73);
    float plume_c = sin(dot(heat_p, float3(1.43, -2.11, 1.67)) + time * 2.87);
    float thread_a = sin(scene_local3.y * 42.0 + scene_local3.x * 12.0 + time * 4.10);
    float thread_b = cos(scene_local3.y * 31.0 - scene_local3.z * 15.0 - time * 3.30);
    float shimmer_a = sin(angle * 4.0 + scene_local3.y * 16.0 + time * 3.10);
    float shimmer_b = cos((scene_local3.x - scene_local3.z) * 21.0 - time * 2.45);
    float shimmer_c = sin(dot(scene_local3, float3(17.0, 11.0, -13.0)) + time * 4.70);
    float turbulence = saturate(0.50 + 0.18 * plume_a + 0.18 * plume_b + 0.14 * plume_c);
    float threads = saturate(0.50 + 0.28 * thread_a + 0.22 * thread_b);
    float shimmer = smoothstep(0.24,
                               0.88,
                               0.50 + 0.20 * shimmer_a + 0.18 * shimmer_b +
                                   0.16 * shimmer_c);
    float flicker_cell = Hash21(floor((local_plane + radial_dir * time * 0.035) * 10.0) +
                                floor(time * 6.0));
    float flicker = saturate(0.82 +
                             0.10 * sin(time * 10.5 + shimmer * 6.2831853) +
                             0.08 * step(0.62, flicker_cell));
    float2 drift = float2(plume_a + plume_c * 0.65 + thread_a * 0.45,
                          plume_b - plume_c * 0.55 + thread_b * 0.40);
    float2 rise = float2(sin(scene_local3.y * 24.0 + time * 2.60),
                         cos((scene_local3.x - scene_local3.z) * 18.0 - time * 2.20));
    float heat_strength = (0.0025 + turbulence * 0.0030 + threads * 0.0020) * mask;
    float rainbow_strength = (0.0008 + shimmer * 0.0019) * inner_gate * mask * flicker;
    float2 rainbow_axis =
        SafeNormalize(float3(tangent * (0.72 + 0.28 * shimmer_a) +
                                 radial_dir * (0.24 * shimmer_b),
                             0.0),
                      float3(1.0, 0.0, 0.0)).xy;
    float2 warp = (drift * 0.62 + rise * 0.38) * heat_strength;
    float2 chroma = rainbow_axis * rainbow_strength;

    float3 scene_base = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;
    float2 uv_mid = clamp(screen_uv + warp, 0.001, 0.999);
    float3 scene_mid = g_SceneColor.Sample(g_SamplerColor, uv_mid).rgb;
    float red = g_SceneColor.Sample(g_SamplerColor,
                                    clamp(uv_mid + chroma * 1.25, 0.001, 0.999)).r;
    float green = g_SceneColor.Sample(g_SamplerColor,
                                      clamp(uv_mid - chroma * 0.20, 0.001, 0.999)).g;
    float blue = g_SceneColor.Sample(g_SamplerColor,
                                     clamp(uv_mid - chroma * 1.05, 0.001, 0.999)).b;
    float3 chromatic = float3(red, green, blue);
    float3 rainbow_wave = 0.5 + 0.5 * sin(float3(0.0, 2.0944, 4.1888) +
                                          angle * 2.0 +
                                          scene_local3.y * 8.0 +
                                          time * 2.6);
    float heat_lift = (turbulence - 0.5) * 0.028 + threads * 0.012;
    float3 refracted = lerp(scene_mid, chromatic, 0.18 + shimmer * 0.08);
    refracted = lerp(scene_base, refracted, 0.42);
    float3 shimmer_tint = (rainbow_wave - 0.42) * shimmer * flicker * 0.020 * mask;
    float3 color = refracted * (1.0 + heat_lift * mask + (flicker - 0.86) * 0.04 * mask) +
                   shimmer_tint;
    float alpha = saturate(mask * g_BaseColorFactor.a);
    return float4(color, alpha);
}
)";

constexpr std::string_view kDetectMagicSurfacePixelShader = R"(float4 PSMain(PSInput input) : SV_TARGET
{
    float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
    float3 center = g_VolumeParams0.xyz;
    float radius = max(g_VolumeParams0.w, 1.0e-4);
    float3 axis_y = SafeNormalize(g_VolumeParams2.xyz, float3(0.0, 1.0, 0.0));
    float3 axis_z = SafeNormalize(g_VolumeParams3.xyz, float3(0.0, 0.0, 1.0));
    float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
    float3 ro = g_CameraPos.xyz - center;

    float t0;
    float t1;
    if (!IntersectSphere(ro, ray_dir, radius, t0, t1) || t1 <= 0.0)
    {
        discard;
    }

    float t_surface = t0 > 0.0 ? t0 : t1;
    if (t_surface <= 0.0)
    {
        discard;
    }
    float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
    float raw_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
    float scene_t = 1.0e20;
    if (raw_depth < 0.9999)
    {
        scene_t = LinearizeSceneDepth(raw_depth) / ray_forward;
    }
    float shell_depth_bias = radius * 0.006;
    bool front_visible = scene_t >= t_surface - shell_depth_bias;
    bool surface_double_sided = g_VolumeParams4.w > 0.5;
    bool back_visible = surface_double_sided &&
                        t0 > 0.0 &&
                        t1 > t_surface + radius * 0.01 &&
                        scene_t >= t1 - shell_depth_bias;
    if (!front_visible && !back_visible)
    {
        discard;
    }
    float back_layer = back_visible ? 1.0 : 0.0;

    float3 surface_pos = g_CameraPos.xyz + ray_dir * t_surface;
    float3 normal = SafeNormalize(surface_pos - center, -ray_dir);
    float3 view_dir = SafeNormalize(g_CameraPos.xyz - surface_pos, -ray_dir);
    float ndv = saturate(abs(dot(normal, view_dir)));
    float fresnel = pow(1.0 - ndv, 3.0);

    float2 normal_uv = float2(dot(normal, axis_y), dot(normal, axis_z));
    float edge = smoothstep(0.30, 0.92, fresnel);
    float time = g_LocalLightMeta.w;
    float glint = smoothstep(0.86,
                             1.0,
                             0.5 + 0.5 * sin(dot(normal, float3(17.0, 9.0, -13.0)) +
                                             time * 2.4));
    float2 reflect_uv_offset =
        normal_uv * (0.018 + fresnel * 0.045) +
        float2(sin(time * 1.7 + normal.y * 8.0),
               cos(time * 1.3 + normal.z * 7.0)) * 0.003;

    float3 scene = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;
    float3 reflected = g_SceneColor.Sample(g_SamplerColor,
                                           clamp(screen_uv + reflect_uv_offset,
                                                 0.001,
                                                 0.999)).rgb;
    float3 reflected_opposite =
        g_SceneColor.Sample(g_SamplerColor,
                            clamp(screen_uv - reflect_uv_offset * 0.42,
                                  0.001,
                                  0.999)).rgb;

    float3 shell_tint = float3(0.82, 0.92, 1.0);
    float3 highlight =
        shell_tint * (0.10 + edge * 0.42 + glint * fresnel * 0.20 + back_layer * 0.08);
    float3 reflection = lerp(reflected, reflected_opposite, 0.22);
    reflection = max(reflection, scene * 0.82);
    float3 color = lerp(scene * 1.02,
                        reflection + highlight,
                        0.25 + fresnel * 0.30 + back_layer * 0.08);
    float alpha = saturate((0.18 + fresnel * 0.36 + glint * edge * 0.08 +
                            back_layer * 0.12) *
                           g_BaseColorFactor.a);
    if (alpha <= 0.002)
    {
        discard;
    }
    return float4(color, alpha);
}
)";

Json makeDetectMagicVolumeMaterial(std::string fragment_shader, float base_alpha) {
  return Json{
      {"version", 2},
      {"pipeline",
       {
           {"name", "custom"},
           {"vertex", "../shaders/detect_magic_volume_vs.hlsl"},
           {"fragment", std::move(fragment_shader)},
           {"vertex_entry", "VSMain"},
           {"fragment_entry", "PSMain"},
       }},
      {"surface",
       {
           {"base_color", Json::array({1.0f, 1.0f, 1.0f, base_alpha})},
           {"emissive_color", Json::array({0.94f, 0.98f, 1.0f, 1.0f})},
           {"metallic", 0.0f},
           {"roughness", 1.0f},
           {"unlit", true},
       }},
      {"render_state",
       {
           {"transparent", true},
           {"alpha_mode", "blend"},
           {"blend_mode", "alpha"},
           {"depth_test", false},
           {"depth_write", false},
           {"double_sided", true},
       }},
  };
}

Json makeMeshNode(uint32_t id,
                  uint32_t parent,
                  std::string name,
                  std::string mesh_key,
                  float scale) {
  return Json{
      {"id", id},
      {"name", name},
      {"parent", parent},
      {"components",
       {
           {"TagComponent", {{"name", name}}},
           {"TransformComponent",
            {
                {"position", vec3(0.0f, 0.0f, 0.0f)},
                {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                {"scale", vec3(scale, scale, scale)},
            }},
           {"MeshComponent",
            {
                {"mesh_asset_key", std::move(mesh_key)},
                {"materials", Json::array()},
                {"visible", true},
                {"shadow_visible", false},
            }},
       }},
  };
}

Json makePointLightNode(uint32_t id,
                        uint32_t parent,
                        std::string name,
                        Json light_color,
                        Json intensity,
                        Json range,
                        Json position = vec3(0.0f, 0.0f, 0.0f)) {
  return Json{
      {"id", id},
      {"name", name},
      {"parent", parent},
      {"components",
       {
           {"TagComponent", {{"name", name}}},
           {"TransformComponent",
            {
                {"position", std::move(position)},
                {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                {"scale", vec3(1.0f, 1.0f, 1.0f)},
            }},
           {"LightComponent",
            {
                {"type", "point"},
                {"color", std::move(light_color)},
                {"intensity", std::move(intensity)},
                {"range", std::move(range)},
                {"inner_cone_degrees", 15.0f},
                {"outer_cone_degrees", 30.0f},
                {"casts_shadows", false},
                {"shadow_extent", 0.0f},
            }},
       }},
  };
}

Json makeRootNode(std::string name,
                  std::optional<Json> beam_component = std::nullopt) {
  Json components{
      {"TagComponent", {{"name", name}}},
      {"TransformComponent",
       {
           {"position", vec3(0.0f, 0.0f, 0.0f)},
           {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
           {"scale", vec3(1.0f, 1.0f, 1.0f)},
       }},
  };
  if (beam_component.has_value()) {
    components["ParticleBeamComponent"] = std::move(*beam_component);
  }
  return Json{
      {"id", 0},
      {"name", name},
      {"parent", nullptr},
      {"components", std::move(components)},
  };
}

Json makeBeamNode(uint32_t id,
                  uint32_t parent,
                  std::string name,
                  Json beam_component) {
  return Json{
      {"id", id},
      {"name", name},
      {"parent", parent},
      {"components",
       {
           {"TagComponent", {{"name", name}}},
           {"TransformComponent",
            {
                {"position", vec3(0.0f, 0.0f, 0.0f)},
                {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                {"scale", vec3(1.0f, 1.0f, 1.0f)},
            }},
           {"ParticleBeamComponent", std::move(beam_component)},
       }},
  };
}

void configureOrbEmitter(Json& emitter, bool random_start_frame = true) {
  emitter["playback"]["local_space"] = true;
  emitter["render"]["use_soft_mask"] = false;
  emitter["atlas"]["columns"] = 6;
  emitter["atlas"]["rows"] = 1;
  emitter["atlas"]["frame_count"] = 6;
  emitter["atlas"]["animation_fps"] = 0.0f;
  emitter["atlas"]["random_start_frame"] = random_start_frame;
}

Json makeOrbCoreEmitter(std::string texture, float radius) {
  Json emitter = makeEmitter("orb_core",
                             std::move(texture),
                             "additive",
                             "sphere",
                             256,
                             72,
                             72.0f,
                             1.1f,
                             2.4f,
                             1.10f * radius,
                             1.74f * radius,
                             0.52f * radius,
                             0.92f * radius,
                             color(0.92f, 0.98f, 1.0f, 0.96f),
                             color(0.28f, 0.72f, 1.0f, 0.0f),
                             Json::array(),
                             5203u);
  configureOrbEmitter(emitter);
  emitter["size"]["curve_exponent"] = 0.62f;
  emitter["rotation"]["angular_velocity_min"] = -0.35f;
  emitter["rotation"]["angular_velocity_max"] = 0.35f;
  emitter["source"]["radius_min"] = 0.18f * radius;
  emitter["source"]["radius_max"] = 0.42f * radius;
  emitter["source"]["radial_speed_min"] = 0.0f;
  emitter["source"]["radial_speed_max"] = 0.012f * radius;
  emitter["motion"]["velocity_min"] = vec3(-0.01f * radius, -0.008f * radius, -0.01f * radius);
  emitter["motion"]["velocity_max"] = vec3(0.01f * radius, 0.012f * radius, 0.01f * radius);
  emitter["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["drag"] = 3.4f;
  return emitter;
}

Json makeOrbArcEmitter(std::string texture, float radius) {
  Json emitter = makeEmitter("orb_arcs",
                             std::move(texture),
                             "additive",
                             "sphere",
                             520,
                             120,
                             128.0f,
                             0.28f,
                             0.68f,
                             0.18f * radius,
                             0.36f * radius,
                             0.055f * radius,
                             0.13f * radius,
                             color(0.58f, 0.94f, 1.0f, 1.0f),
                             color(0.40f, 0.32f, 1.0f, 0.0f),
                             Json::array(),
                             5309u);
  configureOrbEmitter(emitter);
  emitter["size"]["curve_exponent"] = 0.52f;
  emitter["rotation"]["angular_velocity_min"] = -2.6f;
  emitter["rotation"]["angular_velocity_max"] = 2.6f;
  emitter["source"]["radius_min"] = 0.66f * radius;
  emitter["source"]["radius_max"] = 1.02f * radius;
  emitter["source"]["radial_speed_min"] = 0.004f * radius;
  emitter["source"]["radial_speed_max"] = 0.04f * radius;
  emitter["motion"]["velocity_min"] = vec3(-0.02f * radius, -0.012f * radius, -0.02f * radius);
  emitter["motion"]["velocity_max"] = vec3(0.02f * radius, 0.012f * radius, 0.02f * radius);
  emitter["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["drag"] = 3.1f;
  return emitter;
}

Json makeOrbHaloEmitter(std::string texture, float radius) {
  Json emitter = makeEmitter("orb_halo",
                             std::move(texture),
                             "additive",
                             "box",
                             8,
                             4,
                             1.8f,
                             1.6f,
                             2.7f,
                             2.45f * radius,
                             2.85f * radius,
                             3.0f * radius,
                             3.45f * radius,
                             color(0.10f, 0.46f, 1.0f, 0.15f),
                             color(0.02f, 0.12f, 0.78f, 0.0f),
                             Json::array(),
                             5413u);
  configureOrbEmitter(emitter);
  emitter["size"]["curve_exponent"] = 0.78f;
  emitter["color"]["alpha_curve_exponent"] = 1.18f;
  emitter["rotation"]["angular_velocity_min"] = -0.16f;
  emitter["rotation"]["angular_velocity_max"] = 0.16f;
  emitter["motion"]["velocity_min"] = vec3(-0.02f * radius, -0.015f * radius, -0.02f * radius);
  emitter["motion"]["velocity_max"] = vec3(0.02f * radius, 0.015f * radius, 0.02f * radius);
  emitter["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["drag"] = 2.4f;
  return emitter;
}

Json makeOrbDistortionEmitter(std::string texture, float radius) {
  Json emitter = makeEmitter("orb_distortion",
                             std::move(texture),
                             "distortion",
                             "box",
                             4,
                             2,
                             0.75f,
                             1.9f,
                             2.7f,
                             2.7f * radius,
                             3.05f * radius,
                             3.05f * radius,
                             3.55f * radius,
                             color(1.0f, 1.0f, 1.0f, 0.62f),
                             color(1.0f, 1.0f, 1.0f, 0.0f),
                             Json::array(),
                             5527u);
  configureOrbEmitter(emitter, false);
  emitter["render"]["soft_particle_distance"] = 1.0f;
  emitter["render"]["distortion_strength"] = 22.0f;
  emitter["size"]["curve_exponent"] = 0.8f;
  emitter["rotation"]["angular_velocity_min"] = -0.03f;
  emitter["rotation"]["angular_velocity_max"] = 0.03f;
  emitter["motion"]["velocity_min"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["velocity_max"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["drag"] = 5.0f;
  return emitter;
}

void configureBladeAtlas(Json& emitter) {
  emitter["atlas"]["columns"] = 8;
  emitter["atlas"]["rows"] = 1;
  emitter["atlas"]["frame_count"] = 8;
  emitter["atlas"]["animation_fps"] = 0.0f;
  emitter["atlas"]["animate_over_lifetime"] = false;
  emitter["atlas"]["random_start_frame"] = true;
  emitter["render"]["use_soft_mask"] = false;
}

void configureBladeOrbit(Json& emitter, float orbit_speed) {
  emitter["motion"]["velocity_min"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["velocity_max"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  emitter["motion"]["drag"] = 0.0f;
  emitter["motion"]["orbit_axis"] = vec3(0.0f, 1.0f, 0.0f);
  emitter["motion"]["orbit_speed"] = orbit_speed;
  emitter["source"]["radial_speed_min"] = 0.0f;
  emitter["source"]["radial_speed_max"] = 0.0f;
}

void configureDazeAtlas(Json& emitter, bool random_start_frame = true) {
  emitter["playback"]["local_space"] = true;
  emitter["render"]["use_soft_mask"] = false;
  emitter["atlas"]["columns"] = 4;
  emitter["atlas"]["rows"] = 1;
  emitter["atlas"]["frame_count"] = 4;
  emitter["atlas"]["animation_fps"] = 0.0f;
  emitter["atlas"]["animate_over_lifetime"] = false;
  emitter["atlas"]["random_start_frame"] = random_start_frame;
}

void configurePixieDustAtlas(Json& emitter) {
  emitter["playback"]["local_space"] = true;
  emitter["render"]["use_soft_mask"] = false;
  emitter["atlas"]["columns"] = 4;
  emitter["atlas"]["rows"] = 4;
  emitter["atlas"]["frame_count"] = 16;
  emitter["atlas"]["animation_fps"] = 0.0f;
  emitter["atlas"]["animate_over_lifetime"] = false;
  emitter["atlas"]["random_start_frame"] = true;
}

void configureDazeRingSource(Json& emitter,
                             float radius,
                             float inner_scale,
                             float outer_scale,
                             float radial_speed_min,
                             float radial_speed_max) {
  emitter["playback"]["local_space"] = true;
  emitter["source"]["inner_radius"] = inner_scale * radius;
  emitter["source"]["outer_radius"] = outer_scale * radius;
  emitter["source"]["height"] = 0.0f;
  emitter["source"]["jitter_radius"] = 0.035f * radius;
  emitter["source"]["radial_speed_min"] = radial_speed_min * radius;
  emitter["source"]["radial_speed_max"] = radial_speed_max * radius;
  emitter["motion"]["orbit_axis"] = vec3(0.0f, 1.0f, 0.0f);
}

void configureHealAtlas(Json& emitter, bool random_start_frame = true) {
  configureDazeAtlas(emitter, random_start_frame);
}

void configureHasteAtlas(Json& emitter, bool random_start_frame = true) {
  configureDazeAtlas(emitter, random_start_frame);
}

void configureChromaticAtlas(Json& emitter, bool random_start_frame = true) {
  emitter["playback"]["local_space"] = true;
  emitter["render"]["use_soft_mask"] = false;
  emitter["atlas"]["columns"] = 8;
  emitter["atlas"]["rows"] = 1;
  emitter["atlas"]["frame_count"] = 8;
  emitter["atlas"]["animation_fps"] = 0.0f;
  emitter["atlas"]["animate_over_lifetime"] = false;
  emitter["atlas"]["random_start_frame"] = random_start_frame;
}

void configureBreatheFireAtlas(Json& emitter, bool random_start_frame = true) {
  emitter["playback"]["local_space"] = true;
  emitter["render"]["use_soft_mask"] = false;
  emitter["atlas"]["columns"] = 8;
  emitter["atlas"]["rows"] = 1;
  emitter["atlas"]["frame_count"] = 8;
  emitter["atlas"]["animation_fps"] = 0.0f;
  emitter["atlas"]["animate_over_lifetime"] = false;
  emitter["atlas"]["random_start_frame"] = random_start_frame;
}

void configureFireballBurstEmitter(Json& emitter,
                                   float duration,
                                   float start_delay = 0.0f,
                                   bool random_start_frame = true) {
  emitter["playback"]["loop"] = false;
  emitter["playback"]["local_space"] = true;
  emitter["playback"]["start_delay"] = start_delay;
  emitter["playback"]["duration"] = duration;
  emitter["render"]["use_soft_mask"] = false;
  emitter["render"]["soft_particle_distance"] = 0.55f;
  emitter["atlas"]["columns"] = 4;
  emitter["atlas"]["rows"] = 1;
  emitter["atlas"]["frame_count"] = 4;
  emitter["atlas"]["animation_fps"] = 18.0f;
  emitter["atlas"]["animate_over_lifetime"] = true;
  emitter["atlas"]["random_start_frame"] = random_start_frame;
}

void configureFireballRealismAtlas(Json& emitter, bool random_start_frame = false) {
  emitter["atlas"]["columns"] = 4;
  emitter["atlas"]["rows"] = 2;
  emitter["atlas"]["frame_count"] = 8;
  emitter["atlas"]["frame_width"] = 0;
  emitter["atlas"]["frame_height"] = 0;
  emitter["atlas"]["animation_fps"] = 18.0f;
  emitter["atlas"]["animate_over_lifetime"] = true;
  emitter["atlas"]["random_start_frame"] = random_start_frame;
}

Json makeBreatheFireRibbonPath(float length,
                               float start_y,
                               float mid_y,
                               float end_y,
                               float start_z,
                               float mid_z,
                               float end_z,
                               float lift = 0.0f,
                               float end_x_scale = 0.52f) {
  const float safe_length = std::max(length, 0.25f);
  const float clamped_end_x = std::clamp(end_x_scale, 0.18f, 0.68f);
  const auto mix = [](float a, float b, float t) {
    return a + (b - a) * t;
  };
  const float lifted_mid_y = mid_y + lift;
  const float tail_x_1 = mix(0.04f, clamped_end_x, 0.30f);
  const float tail_x_2 = mix(0.04f, clamped_end_x, 0.55f);
  const float tail_x_3 = mix(0.04f, clamped_end_x, 0.78f);
  Json points = Json::array();
  points.push_back(vec3(-0.48f * safe_length, start_y, start_z));
  points.push_back(vec3(-0.42f * safe_length, mix(start_y, lifted_mid_y, 0.20f), mix(start_z, mid_z, 0.20f)));
  points.push_back(vec3(-0.30f * safe_length, mix(start_y, lifted_mid_y, 0.42f), mix(start_z, mid_z, 0.42f)));
  points.push_back(vec3(-0.12f * safe_length, mix(start_y, lifted_mid_y, 0.68f), mix(start_z, mid_z, 0.68f)));
  points.push_back(vec3(0.04f * safe_length, lifted_mid_y, mid_z));
  points.push_back(vec3(tail_x_1 * safe_length, mix(lifted_mid_y, end_y, 0.30f), mix(mid_z, end_z, 0.30f)));
  points.push_back(vec3(tail_x_2 * safe_length, mix(lifted_mid_y, end_y, 0.55f), mix(mid_z, end_z, 0.55f)));
  points.push_back(vec3(tail_x_3 * safe_length, mix(lifted_mid_y, end_y, 0.78f), mix(mid_z, end_z, 0.78f)));
  points.push_back(vec3(clamped_end_x * safe_length, end_y, end_z));
  return points;
}

Json makeBreatheFireEmitterPath(float length, float radius) {
  const float safe_length = std::max(length, 0.25f);
  const float cone_half_width = 1.22f * safe_length;
  const float cone_lift = 0.24f * safe_length;
  const float cone_drop = 0.20f * safe_length;
  Json points = Json::array();
  points.push_back(vec3(-0.48f * safe_length, 1.08f * radius, 0.0f));
  points.push_back(vec3(-0.34f * safe_length, 1.10f * radius + cone_lift * 0.02f, -cone_half_width * 0.08f));
  points.push_back(vec3(-0.16f * safe_length, 1.11f * radius - cone_drop * 0.02f, cone_half_width * 0.14f));
  points.push_back(vec3(0.04f * safe_length, 1.12f * radius + cone_lift * 0.08f, -cone_half_width * 0.24f));
  points.push_back(vec3(0.20f * safe_length, 1.14f * radius - cone_drop * 0.10f, cone_half_width * 0.32f));
  points.push_back(vec3(0.38f * safe_length, 1.13f * radius + cone_lift * 0.12f, -cone_half_width * 0.56f));
  points.push_back(vec3(0.54f * safe_length, 1.12f * radius - cone_drop * 0.14f, cone_half_width));
  points.push_back(vec3(0.60f * safe_length, 1.13f * radius + cone_lift * 0.06f, -cone_half_width * 0.20f));
  return points;
}

Json makeBeam(std::string texture_key,
              Json path_points,
              Json start_width,
              Json end_width,
              Json start_color,
              Json end_color,
              float uv_repeat,
              float uv_scroll_speed,
              std::string blend_mode = "additive",
              float edge_softness = 0.18f,
              uint32_t layer = 0u) {
  return Json{
      {"enabled", true},
      {"visible", true},
      {"layer", layer},
      {"depth_test", true},
      {"blend_mode", std::move(blend_mode)},
      {"texture_key", std::move(texture_key)},
      {"local_path_points", std::move(path_points)},
      {"start_width", start_width},
      {"end_width", end_width},
      {"start_color", std::move(start_color)},
      {"end_color", std::move(end_color)},
      {"edge_softness", edge_softness},
      {"uv_repeat", uv_repeat},
      {"uv_scroll_speed", uv_scroll_speed},
      {"time_scale", 1.0f},
      {"restart_count", 0},
  };
}

Json makeBladeBarrierRingPath(float radius,
                              const std::array<float, 3>& u,
                              const std::array<float, 3>& v,
                              std::size_t segments,
                              float phase) {
  Json path = Json::array();
  constexpr float kPi = 3.14159265358979323846f;
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float t = (static_cast<float>(i) / static_cast<float>(segments)) * kPi * 2.0f + phase;
    const float c = std::cos(t);
    const float s = std::sin(t);
    path.push_back(vec3(radius * (u[0] * c + v[0] * s),
                        radius * (u[1] * c + v[1] * s),
                        radius * (u[2] * c + v[2] * s)));
  }
  return path;
}

std::vector<Json> makeBladeBarrierRingPaths(float radius) {
  constexpr std::size_t kSegments = 28u;
  return {
      makeBladeBarrierRingPath(radius * 1.02f, {1.0f, 0.0f, 0.0f}, {0.0f, 0.34f, 0.92f}, kSegments, 0.10f),
      makeBladeBarrierRingPath(radius * 0.92f, {0.0f, 1.0f, 0.0f}, {0.86f, 0.0f, 0.50f}, kSegments, 0.95f),
      makeBladeBarrierRingPath(radius * 1.08f, {0.92f, 0.26f, 0.0f}, {-0.18f, 0.58f, 0.80f}, kSegments, 1.70f),
      makeBladeBarrierRingPath(radius * 0.86f, {0.74f, -0.34f, 0.48f}, {0.15f, 0.90f, 0.30f}, kSegments, 2.40f),
      makeBladeBarrierRingPath(radius * 1.16f, {0.56f, 0.48f, -0.66f}, {0.78f, -0.08f, 0.36f}, kSegments, 3.05f),
      makeBladeBarrierRingPath(radius * 0.78f, {0.18f, 0.88f, 0.44f}, {0.98f, -0.10f, -0.10f}, kSegments, 4.05f),
      makeBladeBarrierRingPath(radius * 1.22f, {0.84f, -0.12f, 0.52f}, {-0.30f, 0.82f, 0.48f}, kSegments, 5.10f),
      makeBladeBarrierRingPath(radius * 0.66f, {0.98f, 0.12f, 0.08f}, {0.05f, 0.70f, -0.71f}, kSegments, 5.85f),
  };
}

Json makeDazeHaloPath(float radius_x,
                      float radius_z,
                      float height,
                      std::size_t segments,
                      float phase,
                      float wobble) {
  Json path = Json::array();
  constexpr float kPi = 3.14159265358979323846f;
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float t = (static_cast<float>(i) / static_cast<float>(segments)) * kPi * 2.0f + phase;
    path.push_back(vec3(radius_x * std::cos(t),
                        height + wobble * std::sin(t * 2.0f + phase),
                        radius_z * std::sin(t)));
  }
  return path;
}

Json makeDazeArcPath(float radius_x,
                     float radius_z,
                     float height,
                     float start_angle,
                     float sweep,
                     std::size_t segments,
                     float wobble) {
  Json path = Json::array();
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float t = start_angle +
                    (static_cast<float>(i) / static_cast<float>(segments)) * sweep;
    path.push_back(vec3(radius_x * std::cos(t),
                        height + wobble * std::sin(t * 2.0f + start_angle),
                        radius_z * std::sin(t)));
  }
  return path;
}

Json makeHealSpiralPath(float radius_x,
                        float radius_z,
                        float min_height,
                        float max_height,
                        float turns,
                        std::size_t segments,
                        float phase) {
  Json path = Json::array();
  constexpr float kPi = 3.14159265358979323846f;
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(segments);
    const float t = phase + u * turns * kPi * 2.0f;
    const float pulse = 0.94f + 0.10f * std::sin(u * kPi * 4.0f + phase);
    path.push_back(vec3(radius_x * pulse * std::cos(t),
                        min_height + (max_height - min_height) * u,
                        radius_z * pulse * std::sin(t)));
  }
  return path;
}

Json makeHasteRingPath(float radius_scale,
                       float height_scale,
                       std::size_t segments,
                       float phase,
                       float ripple = 0.0f) {
  Json path = Json::array();
  constexpr float kPi = 3.14159265358979323846f;
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(segments);
    const float t = phase + u * kPi * 2.0f;
    const float pulse = 1.0f + ripple * std::sin(u * kPi * 6.0f + phase);
    path.push_back(variableVec3(radius_scale * pulse * std::cos(t),
                                height_scale,
                                radius_scale * pulse * std::sin(t)));
  }
  return path;
}

Json makeHasteStreakPath(float phase,
                         float radius_scale,
                         float y_start,
                         float y_mid,
                         float y_end,
                         float lean = 0.0f) {
  const float c = std::cos(phase);
  const float s = std::sin(phase);
  return Json::array({
      variableVec3(radius_scale * c - lean, y_start, radius_scale * s),
      variableVec3(radius_scale * c, y_mid, radius_scale * s),
      variableVec3(radius_scale * c + lean, y_end, radius_scale * s),
  });
}

Json makeHasteAfterimagePath(float side,
                             float y_start,
                             float y_mid,
                             float y_end,
                             float depth_scale) {
  return Json::array({
      variableVec3(side * 0.52f, y_start, -depth_scale),
      variableVec3(side * 0.32f, y_mid, -depth_scale * 0.42f),
      variableVec3(side * 0.10f, y_end, 0.04f),
  });
}

constexpr float kDefaultChromaticLength = 6.4f;
constexpr float kChromaticRayAxisY = 1.30f;
constexpr float kDefaultChromaticHelixTurns = 2.45f;
constexpr float kChromaticHelixRadius = 0.25f;

float chromaticHelixTurnsForLength(float length) {
  return (std::max(length, 0.25f) / kDefaultChromaticLength) *
         kDefaultChromaticHelixTurns;
}

std::pair<math::Vec3, math::Vec3> chromaticRayEndpoints(float length) {
  const float scale = std::max(length, 0.25f) / kDefaultChromaticLength;
  return {
      {-3.20f * scale, kChromaticRayAxisY, 0.0f},
      {3.20f * scale, kChromaticRayAxisY, 0.0f},
  };
}

Json makeChromaticRayPath(float length,
                          float y_offset = 0.0f,
                          float z_offset = 0.0f) {
  auto [start, end] = chromaticRayEndpoints(length);
  start.y += y_offset;
  start.z += z_offset;
  end.y += y_offset;
  end.z += z_offset;

  return Json::array({
      vec3(math::lerp(start, end, 0.0f)),
      vec3(math::lerp(start, end, 1.0f / 3.0f)),
      vec3(math::lerp(start, end, 2.0f / 3.0f)),
      vec3(math::lerp(start, end, 1.0f)),
  });
}

Json makeChromaticHelixPath(float length,
                            float strand_radius,
                            float phase,
                            std::size_t segments = 22u) {
  constexpr float kPi = 3.14159265358979323846f;
  auto [start, end] = chromaticRayEndpoints(length);
  const math::Vec3 axis = math::normalize(math::subtract(end, start));
  const math::Vec3 normal = math::normalize(math::cross(axis, {0.0f, 1.0f, 0.0f}));
  const math::Vec3 binormal = math::normalize(math::cross(normal, axis));
  const float turns = chromaticHelixTurnsForLength(length);
  Json path = Json::array();
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(segments);
    const float angle = phase + u * turns * kPi * 2.0f;
    const math::Vec3 center = math::lerp(start, end, u);
    const math::Vec3 offset =
        math::add(math::scale(normal, std::cos(angle) * strand_radius),
                  math::scale(binormal, std::sin(angle) * strand_radius));
    path.push_back(vec3(math::add(center, offset)));
  }
  return path;
}

std::vector<Json> makeArcaneBarragePaths(float length) {
  constexpr float kDefaultArcaneLength = 5.4f;
  const float scale = std::max(length, 0.25f) / kDefaultArcaneLength;
  auto point = [scale](float x, float y, float z) {
    return vec3(x * scale, y * scale, z * scale);
  };

  return {
      Json::array({
          point(0.66f, 1.18f, 0.0f),
          point(0.05f, 1.28f, -0.06f),
          point(-2.35f, 1.58f, -0.42f),
          point(-4.75f, 1.78f, -0.82f),
      }),
      Json::array({
          point(0.66f, 1.18f, 0.0f),
          point(0.02f, 1.16f, -0.02f),
          point(-2.15f, 1.17f, -0.24f),
          point(-4.20f, 1.20f, -0.38f),
      }),
      Json::array({
          point(0.66f, 1.18f, 0.0f),
          point(-0.04f, 1.01f, 0.02f),
          point(-1.95f, 0.78f, -0.08f),
          point(-3.35f, 0.58f, -0.14f),
      }),
      Json::array({
          point(0.66f, 1.18f, 0.0f),
          point(-0.08f, 0.84f, 0.04f),
          point(-1.78f, 0.32f, 0.12f),
          point(-2.95f, 0.04f, 0.20f),
      }),
      Json::array({
          point(0.66f, 1.18f, 0.0f),
          point(-0.02f, 0.71f, 0.08f),
          point(-2.10f, -0.18f, 0.36f),
          point(-3.80f, -0.52f, 0.52f),
      }),
      Json::array({
          point(0.66f, 1.18f, 0.0f),
          point(0.04f, 0.58f, 0.12f),
          point(-2.62f, -0.80f, 0.62f),
          point(-4.90f, -1.10f, 0.86f),
      }),
  };
}

Json pathPoint(const Json& path, std::size_t index) {
  if (!path.is_array() || index >= path.size()) {
    return vec3(0.0f, 0.0f, 0.0f);
  }
  return path[index];
}

Json pathEndpoint(const Json& path) {
  return pathPoint(path, path.empty() ? 0u : path.size() - 1u);
}

Json pathMidpoint(const Json& path) {
  return pathPoint(path, std::min<std::size_t>(2u, path.empty() ? 0u : path.size() - 1u));
}

Json shiftedPoint(const Json& point, float x, float y, float z) {
  if (!point.is_array() || point.size() < 3u) {
    return vec3(x, y, z);
  }
  return vec3(point[0].get<float>() + x,
              point[1].get<float>() + y,
              point[2].get<float>() + z);
}

Json pointsArray(std::initializer_list<Json> points) {
  Json result = Json::array();
  for (const Json& point : points) {
    result.push_back(point);
  }
  return result;
}

bool copyTexture(const std::filesystem::path& repo_root,
                 const std::filesystem::path& output_dir,
                 const std::string& source_relative,
                 const std::string& output_relative,
                 std::string* diagnostic) {
  const std::filesystem::path source = repo_root / source_relative;
  const std::filesystem::path destination = output_dir / output_relative;
  std::error_code ec;
  std::filesystem::create_directories(destination.parent_path(), ec);
  if (ec) {
    return fail(diagnostic, "failed to create texture directory: " + destination.parent_path().string());
  }
  std::error_code equivalent_ec;
  if (std::filesystem::equivalent(source, destination, equivalent_ec)) {
    return true;
  }
  std::filesystem::copy_file(source,
                             destination,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    return fail(diagnostic, "failed to copy texture '" + source.string() + "': " + ec.message());
  }
  return true;
}

bool copyAssetFile(const std::filesystem::path& repo_root,
                   const std::filesystem::path& output_dir,
                   const std::string& source_relative,
                   const std::string& output_relative,
                   std::string_view label,
                   std::string* diagnostic) {
  const std::filesystem::path source = repo_root / source_relative;
  const std::filesystem::path destination = output_dir / output_relative;
  std::error_code ec;
  std::filesystem::create_directories(destination.parent_path(), ec);
  if (ec) {
    return fail(diagnostic, "failed to create " + std::string(label) +
                                " directory: " + destination.parent_path().string());
  }
  std::filesystem::copy_file(source,
                             destination,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    return fail(diagnostic,
                "failed to copy " + std::string(label) + " '" +
                    source.string() + "': " + ec.message());
  }
  return true;
}

bool validateGeneratedEffects(const std::filesystem::path& output_dir,
                              const std::vector<std::string>& effect_paths,
                              std::string* diagnostic) {
  for (const std::string& relative : effect_paths) {
    if (!validateEffectFile(output_dir / relative, diagnostic)) {
      return false;
    }
  }
  return true;
}

Json packageManifest(const std::vector<GeneratedAsset>& assets) {
  Json entries = Json::array();
  for (const GeneratedAsset& asset : assets) {
    entries.push_back(Json{
        {"type", asset.type},
        {"key", asset.key},
        {"path", asset.path},
    });
  }
  return Json{{"version", 1}, {"assets", std::move(entries)}};
}

bool prepareGeneratedPackageDir(const std::filesystem::path& package_dir,
                                std::string* diagnostic) {
  std::error_code ec;
  std::filesystem::create_directories(package_dir / "particles", ec);
  if (ec) {
    return fail(diagnostic, "failed to create output particles directory: " + ec.message());
  }
  std::filesystem::create_directories(package_dir / "textures", ec);
  if (ec) {
    return fail(diagnostic, "failed to create output textures directory: " + ec.message());
  }
  return true;
}

bool writeGeneratedPrefabPackage(const std::filesystem::path& package_dir,
                                 std::vector<GeneratedAsset>& assets,
                                 std::vector<std::string>& effect_paths,
                                 Json nodes,
                                 Json variables,
                                 std::string* diagnostic) {
  if (!writeJson(package_dir / "assets.package.json", packageManifest(assets), diagnostic)) {
    return false;
  }
  Json prefab{{"version", 2}, {"root", 0}, {"nodes", std::move(nodes)}};
  if (!variables.empty()) {
    prefab["variables"] = std::move(variables);
  }
  if (!writeJson(package_dir / "prefab.json", prefab, diagnostic)) {
    return false;
  }
  return validateGeneratedEffects(package_dir, effect_paths, diagnostic);
}

bool copyFireballTexture(const std::filesystem::path& repo_root,
                         const std::filesystem::path& package_dir,
                         const std::string& file_name,
                         std::string* diagnostic) {
  return copyTexture(repo_root,
                     package_dir,
                     "examples/assets/prefabs/fireball/textures/" + file_name,
                     "textures/" + file_name,
                     diagnostic);
}

bool addFireballTextureSet(const std::filesystem::path& repo_root,
                           const std::filesystem::path& package_dir,
                           const std::string& asset_namespace,
                           std::vector<GeneratedAsset>& assets,
                           std::initializer_list<std::pair<std::string_view, std::string_view>> textures,
                           std::string* diagnostic) {
  for (const auto& [suffix, file_name] : textures) {
    const std::string file{file_name};
    if (!copyFireballTexture(repo_root, package_dir, file, diagnostic)) {
      return false;
    }
    addTexture(assets,
               asset_namespace + "/" + std::string{suffix},
               "textures/" + file);
  }
  return true;
}

bool generateFireballEffectPackages(const std::filesystem::path& repo_root,
                                    const std::filesystem::path& output_dir,
                                    const std::string& asset_namespace,
                                    const std::string& name,
                                    float radius,
                                    std::string* diagnostic) {
  const std::filesystem::path projectile_dir = output_dir / "projectile";
  const std::filesystem::path explosion_dir = output_dir / "explosion";
  const std::string projectile_namespace = asset_namespace + "/projectile";
  const std::string explosion_namespace = asset_namespace + "/explosion";
  std::error_code ec;
  std::filesystem::remove(projectile_dir / "prefab.json", ec);
  ec.clear();
  std::filesystem::remove(projectile_dir / "assets.package.json", ec);
  ec.clear();
  std::filesystem::remove_all(projectile_dir / "particles", ec);
  ec.clear();
  std::filesystem::remove_all(projectile_dir / "textures", ec);
  ec.clear();
  std::filesystem::remove(explosion_dir / "prefab.json", ec);
  ec.clear();
  std::filesystem::remove(explosion_dir / "assets.package.json", ec);
  ec.clear();
  std::filesystem::remove_all(explosion_dir / "particles", ec);
  ec.clear();
  std::filesystem::remove_all(explosion_dir / "textures", ec);
  ec.clear();
  if (!prepareGeneratedPackageDir(projectile_dir, diagnostic) ||
      !prepareGeneratedPackageDir(explosion_dir, diagnostic)) {
    return false;
  }

  std::filesystem::remove(output_dir / "prefab.json", ec);
  ec.clear();
  std::filesystem::remove(output_dir / "assets.package.json", ec);
  ec.clear();
  std::filesystem::remove_all(output_dir / "particles", ec);

  auto projectile_texture_key = [&](std::string_view suffix) {
    return projectile_namespace + "/" + std::string{suffix};
  };
  auto projectile_effect_key = [&](std::string_view suffix) {
    return projectile_namespace + "/" + std::string{suffix};
  };
  auto explosion_texture_key = [&](std::string_view suffix) {
    return explosion_namespace + "/" + std::string{suffix};
  };
  auto explosion_effect_key = [&](std::string_view suffix) {
    return explosion_namespace + "/" + std::string{suffix};
  };

  std::vector<GeneratedAsset> projectile_assets;
  std::vector<std::string> projectile_effect_paths;
  auto write_projectile_effect = [&](std::string file_name, std::string key, Json effect_json) {
    const std::string relative = "particles/" + file_name;
    if (!writeJson(projectile_dir / relative, effect_json, diagnostic)) {
      return false;
    }
    addEffect(projectile_assets, std::move(key), relative);
    projectile_effect_paths.push_back(relative);
    return true;
  };

  if (!addFireballTextureSet(repo_root,
                             projectile_dir,
                             projectile_namespace,
                             projectile_assets,
                             {
                                 {"fireball_core_atlas", "fireball_core_atlas.png"},
                                 {"fireball_flame_atlas", "fireball_flame_atlas.png"},
                                 {"fireball_tongue_atlas", "fireball_tongue_atlas.png"},
                                 {"fireball_ember_atlas", "fireball_ember_atlas.png"},
                                 {"fireball_ember_burst_atlas", "fireball_ember_burst_atlas.png"},
                                 {"fireball_smoke_atlas", "fireball_smoke_atlas.png"},
                                 {"fireball_heat_atlas", "fireball_heat_atlas.png"},
                             },
                             diagnostic)) {
    return false;
  }

  constexpr float projectile_radius = kFireballOrbRadius;
  Json projectile_core = makeEmitter("fireball_projectile_core",
                                     projectile_texture_key("fireball_core_atlas"),
                                     "additive",
                                     "sphere",
                                     180,
                                     36,
                                     128.0f,
                                     0.12f,
                                     0.28f,
                                     0.42f * projectile_radius,
                                     1.12f * projectile_radius,
                                     0.12f * projectile_radius,
                                     0.38f * projectile_radius,
                                     color(3.4f, 2.55f, 1.05f, 0.95f),
                                     color(1.15f, 0.16f, 0.02f, 0.0f),
                                     Json::array(),
                                     8401u);
  projectile_core["playback"]["local_space"] = false;
  projectile_core["source"]["radius_min"] = 0.0f;
  projectile_core["source"]["radius_max"] = 0.72f * projectile_radius;
  projectile_core["source"]["jitter_radius"] = 0.04f * projectile_radius;
  projectile_core["source"]["radial_speed_min"] = 0.24f * projectile_radius;
  projectile_core["source"]["radial_speed_max"] = 1.25f * projectile_radius;
  projectile_core["motion"]["velocity_min"] = vec3(-0.20f, -0.05f, -0.12f);
  projectile_core["motion"]["velocity_max"] = vec3(0.12f, 0.18f, 0.12f);
  projectile_core["motion"]["acceleration"] = vec3(0.0f, 0.08f, 0.0f);
  projectile_core["motion"]["drag"] = 0.30f;
  projectile_core["size"]["curve_exponent"] = 0.52f;
  projectile_core["color"]["alpha_curve_exponent"] = 0.95f;

  Json projectile_flames = makeEmitter("fireball_projectile_flames",
                                       projectile_texture_key("fireball_flame_atlas"),
                                       "additive",
                                       "sphere_surface",
                                       260,
                                       18,
                                       160.0f,
                                       0.18f,
                                       0.42f,
                                       0.34f * projectile_radius,
                                       0.92f * projectile_radius,
                                       0.04f * projectile_radius,
                                       0.22f * projectile_radius,
                                       color(2.75f, 1.08f, 0.18f, 0.84f),
                                       color(0.92f, 0.08f, 0.01f, 0.0f),
                                       Json::array(),
                                       8411u);
  projectile_flames["playback"]["local_space"] = false;
  projectile_flames["source"]["radius_min"] = 0.48f * projectile_radius;
  projectile_flames["source"]["radius_max"] = 1.08f * projectile_radius;
  projectile_flames["source"]["jitter_radius"] = 0.16f * projectile_radius;
  projectile_flames["source"]["radial_speed_min"] = 0.30f * projectile_radius;
  projectile_flames["source"]["radial_speed_max"] = 1.75f * projectile_radius;
  projectile_flames["motion"]["velocity_min"] = vec3(-0.34f, -0.06f, -0.16f);
  projectile_flames["motion"]["velocity_max"] = vec3(0.08f, 0.22f, 0.16f);
  projectile_flames["motion"]["acceleration"] = vec3(0.0f, 0.18f, 0.0f);
  projectile_flames["motion"]["drag"] = 0.42f;
  projectile_flames["rotation"]["angular_velocity_min"] = -4.8f;
  projectile_flames["rotation"]["angular_velocity_max"] = 4.8f;
  projectile_flames["size"]["curve_exponent"] = 0.66f;
  projectile_flames["color"]["alpha_curve_exponent"] = 0.92f;

  Json projectile_smoke = makeEmitter("fireball_projectile_smoke",
                                      projectile_texture_key("fireball_smoke_atlas"),
                                      "alpha",
                                      "sphere",
                                      360,
                                      0,
                                      96.0f,
                                      0.92f,
                                      1.95f,
                                      0.68f * projectile_radius,
                                      1.48f * projectile_radius,
                                      2.70f * projectile_radius,
                                      5.90f * projectile_radius,
                                      color(0.42f, 0.31f, 0.23f, 0.34f),
                                      color(0.055f, 0.050f, 0.046f, 0.0f),
                                      Json::array(),
                                      8421u);
  projectile_smoke["playback"]["local_space"] = false;
  projectile_smoke["render"]["layer"] = 0u;
  projectile_smoke["render"]["soft_particle_distance"] = 0.55f;
  projectile_smoke["source"]["radius_min"] = 0.10f * projectile_radius;
  projectile_smoke["source"]["radius_max"] = 0.82f * projectile_radius;
  projectile_smoke["source"]["jitter_radius"] = 0.24f * projectile_radius;
  projectile_smoke["source"]["radial_speed_min"] = 0.02f * projectile_radius;
  projectile_smoke["source"]["radial_speed_max"] = 0.40f * projectile_radius;
  projectile_smoke["motion"]["velocity_min"] = vec3(-0.28f, 0.04f, -0.12f);
  projectile_smoke["motion"]["velocity_max"] = vec3(-0.04f, 0.34f, 0.12f);
  projectile_smoke["motion"]["acceleration"] = vec3(0.0f, 0.18f, 0.0f);
  projectile_smoke["motion"]["drag"] = 0.72f;
  projectile_smoke["size"]["curve_exponent"] = 0.78f;
  projectile_smoke["color"]["alpha_curve_exponent"] = 0.88f;

  Json projectile_embers = makeEmitter("fireball_projectile_embers",
                                       projectile_texture_key("fireball_ember_atlas"),
                                       "additive",
                                       "sphere_surface",
                                       320,
                                       12,
                                       72.0f,
                                       0.48f,
                                       1.12f,
                                       0.07f * projectile_radius,
                                       0.22f * projectile_radius,
                                       0.012f * projectile_radius,
                                       0.045f * projectile_radius,
                                       color(1.0f, 0.64f, 0.18f, 1.0f),
                                       color(1.0f, 0.04f, 0.0f, 0.0f),
                                       Json::array(),
                                       8431u);
  projectile_embers["playback"]["local_space"] = false;
  projectile_embers["source"]["radius_min"] = 0.40f * projectile_radius;
  projectile_embers["source"]["radius_max"] = 1.05f * projectile_radius;
  projectile_embers["source"]["jitter_radius"] = 0.10f * projectile_radius;
  projectile_embers["source"]["radial_speed_min"] = 0.32f * projectile_radius;
  projectile_embers["source"]["radial_speed_max"] = 1.40f * projectile_radius;
  projectile_embers["motion"]["velocity_min"] = vec3(-0.46f, -0.10f, -0.18f);
  projectile_embers["motion"]["velocity_max"] = vec3(0.06f, 0.24f, 0.18f);
  projectile_embers["motion"]["acceleration"] = vec3(0.0f, -0.42f, 0.0f);
  projectile_embers["motion"]["drag"] = 0.24f;
  projectile_embers["size"]["curve_exponent"] = 0.72f;
  projectile_embers["color"]["alpha_curve_exponent"] = 1.18f;

  Json projectile_ember_sparks = makeEmitter("fireball_projectile_ember_sparks",
                                             projectile_texture_key("fireball_ember_burst_atlas"),
                                             "additive",
                                             "sphere_surface",
                                             420,
                                             18,
                                             54.0f,
                                             0.40f,
                                             1.18f,
                                             0.09f * projectile_radius,
                                             0.24f * projectile_radius,
                                             0.010f * projectile_radius,
                                             0.036f * projectile_radius,
                                             color(1.0f, 0.58f, 0.12f, 0.78f),
                                             color(1.0f, 0.035f, 0.0f, 0.0f),
                                             Json::array(),
                                             8437u);
  projectile_ember_sparks["playback"]["local_space"] = false;
  projectile_ember_sparks["source"]["radius_min"] = 0.36f * projectile_radius;
  projectile_ember_sparks["source"]["radius_max"] = 1.18f * projectile_radius;
  projectile_ember_sparks["source"]["jitter_radius"] = 0.16f * projectile_radius;
  projectile_ember_sparks["source"]["radial_speed_min"] = 0.44f * projectile_radius;
  projectile_ember_sparks["source"]["radial_speed_max"] = 2.10f * projectile_radius;
  projectile_ember_sparks["motion"]["velocity_min"] = vec3(-0.66f, -0.14f, -0.24f);
  projectile_ember_sparks["motion"]["velocity_max"] = vec3(0.04f, 0.34f, 0.24f);
  projectile_ember_sparks["motion"]["acceleration"] = vec3(0.0f, -0.58f, 0.0f);
  projectile_ember_sparks["motion"]["drag"] = 0.20f;
  projectile_ember_sparks["rotation"]["angular_velocity_min"] = -8.0f;
  projectile_ember_sparks["rotation"]["angular_velocity_max"] = 8.0f;
  projectile_ember_sparks["size"]["curve_exponent"] = 0.76f;
  projectile_ember_sparks["color"]["alpha_curve_exponent"] = 1.32f;
  configureFireballRealismAtlas(projectile_ember_sparks, true);

  Json projectile_heat = makeEmitter("fireball_projectile_heat",
                                     projectile_texture_key("fireball_heat_atlas"),
                                     "distortion",
                                     "sphere",
                                     36,
                                     0,
                                     28.0f,
                                     0.22f,
                                     0.48f,
                                     0.82f * projectile_radius,
                                     1.60f * projectile_radius,
                                     1.20f * projectile_radius,
                                     2.40f * projectile_radius,
                                     color(1.0f, 1.0f, 1.0f, 0.22f),
                                     color(1.0f, 1.0f, 1.0f, 0.0f),
                                     Json::array(),
                                     8441u);
  projectile_heat["playback"]["local_space"] = false;
  projectile_heat["render"]["soft_particle_distance"] = 0.55f;
  projectile_heat["render"]["distortion_strength"] = 5.8f;
  projectile_heat["source"]["radius_max"] = 0.95f * projectile_radius;
  projectile_heat["source"]["jitter_radius"] = 0.08f * projectile_radius;
  projectile_heat["motion"]["drag"] = 1.2f;
  projectile_heat["size"]["curve_exponent"] = 0.58f;

  if (!write_projectile_effect("fireball_projectile_core.kpeffect",
                               projectile_effect_key("core"),
                               effect(std::move(projectile_core))) ||
      !write_projectile_effect("fireball_projectile_flames.kpeffect",
                               projectile_effect_key("flames"),
                               effect(std::move(projectile_flames))) ||
      !write_projectile_effect("fireball_projectile_smoke.kpeffect",
                               projectile_effect_key("smoke_trail"),
                               effect(std::move(projectile_smoke))) ||
      !write_projectile_effect("fireball_projectile_embers.kpeffect",
                               projectile_effect_key("embers"),
                               effect(std::move(projectile_embers))) ||
      !write_projectile_effect("fireball_projectile_ember_sparks.kpeffect",
                               projectile_effect_key("ember_sparks"),
                               effect(std::move(projectile_ember_sparks))) ||
      !write_projectile_effect("fireball_projectile_heat.kpeffect",
                               projectile_effect_key("heat"),
                               effect(std::move(projectile_heat)))) {
    return false;
  }

  Json projectile_nodes = Json::array();
  projectile_nodes.push_back(makeRootNode(name + " Projectile"));
  uint32_t projectile_node_id = 1u;
  projectile_nodes.push_back(makeParticleNode(projectile_node_id++,
                                              0u,
                                              "projectile_core",
                                              projectile_effect_key("core")));
  projectile_nodes.push_back(makeParticleNode(projectile_node_id++,
                                              0u,
                                              "projectile_flames",
                                              projectile_effect_key("flames")));
  projectile_nodes.push_back(makeParticleNode(projectile_node_id++,
                                              0u,
                                              "projectile_smoke_trail",
                                              projectile_effect_key("smoke_trail")));
  projectile_nodes.push_back(makeParticleNode(projectile_node_id++,
                                              0u,
                                              "projectile_embers",
                                              projectile_effect_key("embers")));
  projectile_nodes.push_back(makeParticleNode(projectile_node_id++,
                                              0u,
                                              "projectile_ember_sparks",
                                              projectile_effect_key("ember_sparks")));
  projectile_nodes.push_back(makeParticleNode(projectile_node_id++,
                                              0u,
                                              "projectile_heat",
                                              projectile_effect_key("heat")));
  projectile_nodes.push_back(makePointLightNode(projectile_node_id++,
                                                0u,
                                                "projectile_light",
                                                color(1.0f, 0.42f, 0.08f, 1.0f),
                                                7.0f,
                                                2.6f));
  if (!writeGeneratedPrefabPackage(projectile_dir,
                                   projectile_assets,
                                   projectile_effect_paths,
                                   std::move(projectile_nodes),
                                   Json::object(),
                                   diagnostic)) {
    return false;
  }

  std::vector<GeneratedAsset> explosion_assets;
  std::vector<std::string> explosion_effect_paths;
  auto write_explosion_effect = [&](std::string file_name, std::string key, Json effect_json) {
    const std::string relative = "particles/" + file_name;
    if (!writeJson(explosion_dir / relative, effect_json, diagnostic)) {
      return false;
    }
    addEffect(explosion_assets, std::move(key), relative);
    explosion_effect_paths.push_back(relative);
    return true;
  };

  if (!addFireballTextureSet(repo_root,
                             explosion_dir,
                             explosion_namespace,
                             explosion_assets,
                             {
                                 {"fireball_core_atlas", "fireball_core_atlas.png"},
                                 {"fireball_flame_atlas", "fireball_flame_atlas.png"},
                                 {"fireball_tongue_atlas", "fireball_tongue_atlas.png"},
                                 {"fireball_ember_atlas", "fireball_ember_atlas.png"},
                                 {"fireball_ember_burst_atlas", "fireball_ember_burst_atlas.png"},
                                 {"fireball_smoke_atlas", "fireball_smoke_atlas.png"},
                                 {"fireball_smoke_plumes_atlas", "fireball_smoke_plumes_atlas.png"},
                                 {"fireball_heat_atlas", "fireball_heat_atlas.png"},
                                 {"fireball_core_flipbook_atlas", "fireball_core_flipbook_atlas.png"},
                                 {"fireball_smoke_flipbook_atlas", "fireball_smoke_flipbook_atlas.png"},
                                 {"fireball_flame_lobes_atlas", "fireball_flame_lobes_atlas.png"},
                             },
                             diagnostic)) {
    return false;
  }

  constexpr float kFlameParticleSizeScale = 0.5f;
  constexpr uint32_t kFlameParticleQuantityScale = 4u;
  const std::string radius_scale_expression =
      "radius / (" + numberExpr(radius) + ")";
  auto blast_radius_override = [&]() {
    return Json{
        {"radius_scale", expr(radius_scale_expression)},
        {"size_scale", expr(radius_scale_expression)},
        {"velocity_scale", expr(radius_scale_expression)},
    };
  };
  Json explosion_variables{
      {"radius", Json{{"type", "float"}, {"default", radius}}},
  };

  Json core_flash = makeEmitter("fireball_core_flash",
                                explosion_texture_key("fireball_core_atlas"),
                                "additive",
                                "sphere",
                                128,
                                104,
                                0.0f,
                                0.22f,
                                0.48f,
                                0.18f,
                                0.34f,
                                2.30f * radius,
                                3.05f * radius,
                                color(3.20f, 2.70f, 1.35f, 1.0f),
                                color(1.05f, 0.26f, 0.02f, 0.0f),
                                Json::array(),
                                8011u);
  configureFireballBurstEmitter(core_flash, 0.56f, 0.0f);
  core_flash["source"]["radius_min"] = 0.0f;
  core_flash["source"]["radius_max"] = 1.10f * kFireballOrbRadius;
  core_flash["source"]["radial_speed_min"] = 1.10f * radius;
  core_flash["source"]["radial_speed_max"] = 3.40f * radius;
  core_flash["motion"]["velocity_min"] = vec3(-0.04f * radius, -0.02f * radius, -0.04f * radius);
  core_flash["motion"]["velocity_max"] = vec3(0.04f * radius, 0.04f * radius, 0.04f * radius);
  core_flash["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  core_flash["motion"]["drag"] = 0.25f;
  core_flash["size"]["curve_exponent"] = 2.60f;
  core_flash["color"]["alpha_curve_exponent"] = 2.05f;

  Json core_flipbook = makeEmitter("fireball_core_flipbook",
                                   explosion_texture_key("fireball_core_flipbook_atlas"),
                                   "additive",
                                   "sphere",
                                   8,
                                   2,
                                   0.0f,
                                   0.72f,
                                   1.05f,
                                   0.20f,
                                   0.36f,
                                   2.45f * radius,
                                   2.95f * radius,
                                   color(1.55f, 1.05f, 0.54f, 0.58f),
                                   color(0.55f, 0.08f, 0.02f, 0.0f),
                                   Json::array(),
                                   8017u);
  configureFireballBurstEmitter(core_flipbook, 1.08f, 0.01f, false);
  configureFireballRealismAtlas(core_flipbook, false);
  core_flipbook["source"]["radius_min"] = 0.0f;
  core_flipbook["source"]["radius_max"] = 0.55f * kFireballOrbRadius;
  core_flipbook["source"]["radial_speed_min"] = 0.24f * radius;
  core_flipbook["source"]["radial_speed_max"] = 1.35f * radius;
  core_flipbook["source"]["jitter_radius"] = 0.0f;
  core_flipbook["motion"]["velocity_min"] = vec3(0.0f, 0.0f, 0.0f);
  core_flipbook["motion"]["velocity_max"] = vec3(0.0f, 0.0f, 0.0f);
  core_flipbook["motion"]["acceleration"] = vec3(0.0f, 0.03f * radius, 0.0f);
  core_flipbook["motion"]["drag"] = 0.45f;
  core_flipbook["rotation"]["angular_velocity_min"] = -0.22f;
  core_flipbook["rotation"]["angular_velocity_max"] = 0.22f;
  core_flipbook["size"]["curve_exponent"] = 2.35f;
  core_flipbook["color"]["alpha_curve_exponent"] = 1.38f;

  Json flame_shell = makeEmitter("fireball_flame_shell",
                                 explosion_texture_key("fireball_flame_lobes_atlas"),
                                 "additive",
                                 "sphere_surface",
                                 560u * kFlameParticleQuantityScale,
                                 380u * kFlameParticleQuantityScale,
                                 0.0f,
                                 0.34f,
                                 0.74f,
                                 0.12f,
                                 0.28f,
                                 1.34f * radius * kFlameParticleSizeScale,
                                 2.28f * radius * kFlameParticleSizeScale,
                                 color(2.15f, 0.92f, 0.16f, 0.92f),
                                 color(0.95f, 0.08f, 0.01f, 0.0f),
                                 Json::array(),
                                 8023u);
  configureFireballBurstEmitter(flame_shell, 0.80f, 0.03f);
  flame_shell["source"]["radius_min"] = 0.35f * kFireballOrbRadius;
  flame_shell["source"]["radius_max"] = 1.45f * kFireballOrbRadius;
  flame_shell["source"]["radial_speed_min"] = 1.65f * radius;
  flame_shell["source"]["radial_speed_max"] = 4.20f * radius;
  flame_shell["source"]["jitter_radius"] = 0.25f * kFireballOrbRadius;
  flame_shell["motion"]["velocity_min"] = vec3(-0.08f * radius, -0.02f * radius, -0.08f * radius);
  flame_shell["motion"]["velocity_max"] = vec3(0.08f * radius, 0.10f * radius, 0.08f * radius);
  flame_shell["motion"]["acceleration"] = vec3(0.0f, 0.16f * radius, 0.0f);
  flame_shell["motion"]["drag"] = 0.10f;
  flame_shell["rotation"]["angular_velocity_min"] = -3.2f;
  flame_shell["rotation"]["angular_velocity_max"] = 3.2f;
  flame_shell["size"]["curve_exponent"] = 2.20f;
  flame_shell["color"]["alpha_curve_exponent"] = 1.18f;
  configureFireballRealismAtlas(flame_shell, true);

  Json flame_tongues = makeEmitter("fireball_flame_tongues",
                                   explosion_texture_key("fireball_flame_lobes_atlas"),
                                   "additive",
                                   "sphere_surface",
                                   460u * kFlameParticleQuantityScale,
                                   260u * kFlameParticleQuantityScale,
                                   0.0f,
                                   0.42f,
                                   0.92f,
                                   0.10f,
                                   0.24f,
                                   1.08f * radius * kFlameParticleSizeScale,
                                   2.02f * radius * kFlameParticleSizeScale,
                                   color(2.45f, 1.10f, 0.22f, 0.88f),
                                   color(0.86f, 0.06f, 0.01f, 0.0f),
                                   Json::array(),
                                   8039u);
  configureFireballBurstEmitter(flame_tongues, 0.96f, 0.05f);
  flame_tongues["source"]["radius_min"] = 0.45f * kFireballOrbRadius;
  flame_tongues["source"]["radius_max"] = 1.75f * kFireballOrbRadius;
  flame_tongues["source"]["radial_speed_min"] = 1.45f * radius;
  flame_tongues["source"]["radial_speed_max"] = 3.70f * radius;
  flame_tongues["source"]["jitter_radius"] = 0.35f * kFireballOrbRadius;
  flame_tongues["motion"]["velocity_min"] = vec3(-0.14f * radius, -0.06f * radius, -0.14f * radius);
  flame_tongues["motion"]["velocity_max"] = vec3(0.14f * radius, 0.22f * radius, 0.14f * radius);
  flame_tongues["motion"]["acceleration"] = vec3(0.0f, 0.26f * radius, 0.0f);
  flame_tongues["motion"]["drag"] = 0.14f;
  flame_tongues["rotation"]["angular_velocity_min"] = -5.0f;
  flame_tongues["rotation"]["angular_velocity_max"] = 5.0f;
  flame_tongues["size"]["curve_exponent"] = 2.10f;
  flame_tongues["color"]["alpha_curve_exponent"] = 1.10f;
  configureFireballRealismAtlas(flame_tongues, true);

  Json embers = makeEmitter("fireball_embers",
                            explosion_texture_key("fireball_ember_atlas"),
                            "additive",
                            "sphere_surface",
                            760,
                            260,
                            0.0f,
                            0.55f,
                            1.35f,
                            0.032f * radius,
                            0.085f * radius,
                            0.006f * radius,
                            0.018f * radius,
                            color(1.0f, 0.68f, 0.20f, 1.0f),
                            color(1.0f, 0.05f, 0.0f, 0.0f),
                            Json::array(),
                            8053u);
  configureFireballBurstEmitter(embers, 1.36f, 0.06f);
  embers["source"]["radius_min"] = 0.90f * kFireballOrbRadius;
  embers["source"]["radius_max"] = 3.40f * kFireballOrbRadius;
  embers["source"]["radial_speed_min"] = 2.30f * radius;
  embers["source"]["radial_speed_max"] = 5.10f * radius;
  embers["source"]["jitter_radius"] = 0.10f * radius;
  embers["motion"]["velocity_min"] = vec3(-0.18f * radius, -0.18f * radius, -0.18f * radius);
  embers["motion"]["velocity_max"] = vec3(0.18f * radius, 0.34f * radius, 0.18f * radius);
  embers["motion"]["acceleration"] = vec3(0.0f, -0.72f * radius, 0.0f);
  embers["motion"]["drag"] = 0.22f;
  embers["rotation"]["angular_velocity_min"] = -7.0f;
  embers["rotation"]["angular_velocity_max"] = 7.0f;
  embers["size"]["curve_exponent"] = 0.72f;
  embers["color"]["alpha_curve_exponent"] = 1.45f;

  Json ember_storm = makeEmitter("fireball_ember_storm",
                                 explosion_texture_key("fireball_ember_burst_atlas"),
                                 "additive",
                                 "sphere_surface",
                                 900,
                                 360,
                                 0.0f,
                                 0.44f,
                                 1.62f,
                                 0.10f,
                                 0.24f,
                                 0.014f * radius,
                                 0.050f * radius,
                                 color(1.0f, 0.58f, 0.12f, 0.86f),
                                 color(1.0f, 0.035f, 0.0f, 0.0f),
                                 Json::array(),
                                 8061u);
  configureFireballBurstEmitter(ember_storm, 1.72f, 0.04f);
  configureFireballRealismAtlas(ember_storm, true);
  ember_storm["source"]["radius_min"] = 0.55f * kFireballOrbRadius;
  ember_storm["source"]["radius_max"] = 2.30f * kFireballOrbRadius;
  ember_storm["source"]["radial_speed_min"] = 2.80f * radius;
  ember_storm["source"]["radial_speed_max"] = 6.40f * radius;
  ember_storm["source"]["jitter_radius"] = 0.25f * kFireballOrbRadius;
  ember_storm["motion"]["velocity_min"] = vec3(-0.20f * radius, -0.24f * radius, -0.20f * radius);
  ember_storm["motion"]["velocity_max"] = vec3(0.20f * radius, 0.42f * radius, 0.20f * radius);
  ember_storm["motion"]["acceleration"] = vec3(0.0f, -0.82f * radius, 0.0f);
  ember_storm["motion"]["drag"] = 0.16f;
  ember_storm["rotation"]["angular_velocity_min"] = -9.5f;
  ember_storm["rotation"]["angular_velocity_max"] = 9.5f;
  ember_storm["size"]["curve_exponent"] = 0.72f;
  ember_storm["color"]["alpha_curve_exponent"] = 1.45f;

  Json hot_ash_embers = makeEmitter("fireball_hot_ash_embers",
                                    explosion_texture_key("fireball_ember_atlas"),
                                    "alpha",
                                    "sphere",
                                    2200,
                                    1400,
                                    0.0f,
                                    4.20f,
                                    7.20f,
                                    0.110f,
                                    0.240f,
                                    0.045f,
                                    0.100f,
                                    color(1.0f, 0.72f, 0.20f, 1.0f),
                                    color(0.22f, 0.20f, 0.18f, 0.0f),
                                    Json::array(),
                                    8065u);
  configureFireballBurstEmitter(hot_ash_embers, 7.40f, 0.02f);
  hot_ash_embers["source"]["radius_min"] = 0.0f;
  hot_ash_embers["source"]["radius_max"] = 0.85f * kFireballOrbRadius;
  hot_ash_embers["source"]["radial_speed_min"] = 0.45f * radius;
  hot_ash_embers["source"]["radial_speed_max"] = 1.35f * radius;
  hot_ash_embers["source"]["jitter_radius"] = 0.12f * kFireballOrbRadius;
  hot_ash_embers["motion"]["velocity_min"] = vec3(-0.06f * radius, -0.04f * radius, -0.06f * radius);
  hot_ash_embers["motion"]["velocity_max"] = vec3(0.06f * radius, 0.12f * radius, 0.06f * radius);
  hot_ash_embers["motion"]["acceleration"] = vec3(0.0f, -0.12f * radius, 0.0f);
  hot_ash_embers["motion"]["drag"] = 0.18f;
  hot_ash_embers["rotation"]["angular_velocity_min"] = -3.6f;
  hot_ash_embers["rotation"]["angular_velocity_max"] = 3.6f;
  hot_ash_embers["size"]["curve_exponent"] = 0.68f;
  hot_ash_embers["color"]["alpha_curve_exponent"] = 2.20f;

  Json smoke = makeEmitter("fireball_smoke_edge",
                           explosion_texture_key("fireball_smoke_atlas"),
                           "alpha",
                           "sphere_surface",
                           420,
                           210,
                           0.0f,
                           0.86f,
                           1.70f,
                           0.32f,
                           0.70f,
                           1.48f * radius,
                           2.45f * radius,
                           color(0.34f, 0.22f, 0.14f, 0.38f),
                           color(0.05f, 0.045f, 0.04f, 0.0f),
                           Json::array(),
                           8069u);
  configureFireballBurstEmitter(smoke, 1.72f, 0.22f);
  smoke["source"]["radius_min"] = 0.75f * kFireballOrbRadius;
  smoke["source"]["radius_max"] = 2.20f * kFireballOrbRadius;
  smoke["source"]["radial_speed_min"] = 0.85f * radius;
  smoke["source"]["radial_speed_max"] = 1.65f * radius;
  smoke["source"]["jitter_radius"] = 0.45f * kFireballOrbRadius;
  smoke["motion"]["velocity_min"] = vec3(-0.12f * radius, 0.06f * radius, -0.12f * radius);
  smoke["motion"]["velocity_max"] = vec3(0.12f * radius, 0.46f * radius, 0.12f * radius);
  smoke["motion"]["acceleration"] = vec3(0.0f, 0.18f * radius, 0.0f);
  smoke["motion"]["drag"] = 0.55f;
  smoke["size"]["curve_exponent"] = 2.15f;
  smoke["color"]["alpha_curve_exponent"] = 1.20f;

  Json smoke_plumes = makeEmitter("fireball_smoke_plumes",
                                  explosion_texture_key("fireball_smoke_plumes_atlas"),
                                  "alpha",
                                  "sphere",
                                  320,
                                  150,
                                  0.0f,
                                  1.25f,
                                  2.80f,
                                  0.38f,
                                  0.85f,
                                  2.10f * radius,
                                  3.40f * radius,
                                  color(0.42f, 0.30f, 0.22f, 0.42f),
                                  color(0.055f, 0.050f, 0.046f, 0.0f),
                                  Json::array(),
                                  8071u);
  configureFireballBurstEmitter(smoke_plumes, 2.65f, 0.30f, false);
  configureFireballRealismAtlas(smoke_plumes, false);
  smoke_plumes["source"]["radius_min"] = 0.80f * kFireballOrbRadius;
  smoke_plumes["source"]["radius_max"] = 3.80f * kFireballOrbRadius;
  smoke_plumes["source"]["radial_speed_min"] = 0.72f * radius;
  smoke_plumes["source"]["radial_speed_max"] = 1.90f * radius;
  smoke_plumes["source"]["jitter_radius"] = 0.58f * kFireballOrbRadius;
  smoke_plumes["motion"]["velocity_min"] = vec3(-0.10f * radius, 0.05f * radius, -0.10f * radius);
  smoke_plumes["motion"]["velocity_max"] = vec3(0.10f * radius, 0.50f * radius, 0.10f * radius);
  smoke_plumes["motion"]["acceleration"] = vec3(0.0f, 0.16f * radius, 0.0f);
  smoke_plumes["motion"]["drag"] = 0.62f;
  smoke_plumes["rotation"]["angular_velocity_min"] = -0.45f;
  smoke_plumes["rotation"]["angular_velocity_max"] = 0.45f;
  smoke_plumes["size"]["curve_exponent"] = 2.20f;
  smoke_plumes["color"]["alpha_curve_exponent"] = 1.10f;

  Json smoke_roll = makeEmitter("fireball_smoke_roll",
                                explosion_texture_key("fireball_smoke_flipbook_atlas"),
                                "alpha",
                                "sphere",
                                24,
                                5,
                                0.0f,
                                1.10f,
                                2.05f,
                                0.48f,
                                1.00f,
                                2.45f * radius,
                                3.10f * radius,
                                color(0.56f, 0.38f, 0.25f, 0.52f),
                                color(0.055f, 0.050f, 0.045f, 0.0f),
                                Json::array(),
                                8075u);
  configureFireballBurstEmitter(smoke_roll, 2.08f, 0.58f, false);
  configureFireballRealismAtlas(smoke_roll, false);
  smoke_roll["source"]["radius_min"] = 0.0f;
  smoke_roll["source"]["radius_max"] = 1.20f * kFireballOrbRadius;
  smoke_roll["source"]["radial_speed_min"] = 0.08f * radius;
  smoke_roll["source"]["radial_speed_max"] = 0.34f * radius;
  smoke_roll["source"]["jitter_radius"] = 0.30f * kFireballOrbRadius;
  smoke_roll["motion"]["velocity_min"] = vec3(-0.035f * radius, 0.03f * radius, -0.035f * radius);
  smoke_roll["motion"]["velocity_max"] = vec3(0.035f * radius, 0.18f * radius, 0.035f * radius);
  smoke_roll["motion"]["acceleration"] = vec3(0.0f, 0.12f * radius, 0.0f);
  smoke_roll["motion"]["drag"] = 0.75f;
  smoke_roll["rotation"]["angular_velocity_min"] = -0.30f;
  smoke_roll["rotation"]["angular_velocity_max"] = 0.30f;
  smoke_roll["size"]["curve_exponent"] = 2.05f;
  smoke_roll["color"]["alpha_curve_exponent"] = 1.05f;

  Json heat = makeEmitter("fireball_heat_shimmer",
                          explosion_texture_key("fireball_heat_atlas"),
                          "distortion",
                          "sphere",
                          12,
                          8,
                          0.0f,
                          0.30f,
                          0.72f,
                          0.22f,
                          0.44f,
                          2.85f * radius,
                          3.85f * radius,
                          color(1.0f, 1.0f, 1.0f, 0.44f),
                          color(1.0f, 1.0f, 1.0f, 0.0f),
                          Json::array(),
                          8081u);
  configureFireballBurstEmitter(heat, 0.78f, 0.04f, false);
  heat["render"]["soft_particle_distance"] = 1.0f;
  heat["render"]["distortion_strength"] = 24.0f;
  heat["source"]["radius_min"] = 0.0f;
  heat["source"]["radius_max"] = 0.85f * kFireballOrbRadius;
  heat["source"]["radial_speed_min"] = 0.65f * radius;
  heat["source"]["radial_speed_max"] = 2.10f * radius;
  heat["motion"]["velocity_min"] = vec3(0.0f, 0.0f, 0.0f);
  heat["motion"]["velocity_max"] = vec3(0.0f, 0.0f, 0.0f);
  heat["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
  heat["motion"]["drag"] = 1.7f;
  heat["size"]["curve_exponent"] = 2.45f;

  if (!write_explosion_effect("fireball_core_flash.kpeffect",
                              explosion_effect_key("core_flash"),
                              effect(std::move(core_flash))) ||
      !write_explosion_effect("fireball_core_flipbook.kpeffect",
                              explosion_effect_key("core_flipbook"),
                              effect(std::move(core_flipbook))) ||
      !write_explosion_effect("fireball_flame_shell.kpeffect",
                              explosion_effect_key("flame_shell"),
                              effect(std::move(flame_shell))) ||
      !write_explosion_effect("fireball_flame_tongues.kpeffect",
                              explosion_effect_key("flame_tongues"),
                              effect(std::move(flame_tongues))) ||
      !write_explosion_effect("fireball_embers.kpeffect",
                              explosion_effect_key("embers"),
                              effect(std::move(embers))) ||
      !write_explosion_effect("fireball_ember_storm.kpeffect",
                              explosion_effect_key("ember_storm"),
                              effect(std::move(ember_storm))) ||
      !write_explosion_effect("fireball_hot_ash_embers.kpeffect",
                              explosion_effect_key("hot_ash_embers"),
                              effect(std::move(hot_ash_embers))) ||
      !write_explosion_effect("fireball_smoke_edge.kpeffect",
                              explosion_effect_key("smoke_edge"),
                              effect(std::move(smoke))) ||
      !write_explosion_effect("fireball_smoke_plumes.kpeffect",
                              explosion_effect_key("smoke_plumes"),
                              effect(std::move(smoke_plumes))) ||
      !write_explosion_effect("fireball_smoke_roll.kpeffect",
                              explosion_effect_key("smoke_roll"),
                              effect(std::move(smoke_roll))) ||
      !write_explosion_effect("fireball_heat_shimmer.kpeffect",
                              explosion_effect_key("heat_shimmer"),
                              effect(std::move(heat)))) {
    return false;
  }

  Json explosion_nodes = Json::array();
  explosion_nodes.push_back(makeRootNode(name + " Explosion"));
  uint32_t explosion_node_id = 1u;
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "core_flash",
                                                         explosion_effect_key("core_flash"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "core_flipbook",
                                                         explosion_effect_key("core_flipbook"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "flame_shell",
                                                         explosion_effect_key("flame_shell"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "flame_tongues",
                                                         explosion_effect_key("flame_tongues"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "embers",
                                                         explosion_effect_key("embers"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "ember_storm",
                                                         explosion_effect_key("ember_storm"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "hot_ash_embers",
                                                         explosion_effect_key("hot_ash_embers"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "smoke_edge",
                                                         explosion_effect_key("smoke_edge"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "smoke_plumes",
                                                         explosion_effect_key("smoke_plumes"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "smoke_roll",
                                                         explosion_effect_key("smoke_roll"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makeParticleNodeWithOverride(explosion_node_id++,
                                                         0u,
                                                         "heat_shimmer",
                                                         explosion_effect_key("heat_shimmer"),
                                                         blast_radius_override()));
  explosion_nodes.push_back(makePointLightNode(explosion_node_id++,
                                               0u,
                                               "fireball_light",
                                               color(1.0f, 0.46f, 0.08f, 1.0f),
                                               expr("radius * 2.9"),
                                               expr("radius * 1.4")));
  return writeGeneratedPrefabPackage(explosion_dir,
                                     explosion_assets,
                                     explosion_effect_paths,
                                     std::move(explosion_nodes),
                                     std::move(explosion_variables),
                                     diagnostic);
}

}  // namespace

bool validateEffectFile(const std::filesystem::path& path, std::string* diagnostic) {
  visual::particles::ParticleEffectAsset effect_asset{};
  if (!visual::particles::loadParticleEffectAsset(path, effect_asset)) {
    return fail(diagnostic, "invalid particle effect: " + path.string());
  }
  return true;
}

bool formatEffectFile(const std::filesystem::path& path,
                      bool check_only,
                      std::string* diagnostic) {
  std::optional<OrderedJson> ordered = readOrderedJson(path, diagnostic);
  if (!ordered.has_value()) {
    return false;
  }
  if (!validateEffectFile(path, diagnostic)) {
    return false;
  }
  const std::string formatted = ordered->dump(2) + '\n';
  if (check_only) {
    const std::string existing = readText(path);
    if (existing != formatted) {
      return fail(diagnostic, "file is not formatted: " + path.string());
    }
    return true;
  }
  std::ofstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to write formatted file: " + path.string());
  }
  stream << formatted;
  return static_cast<bool>(stream);
}

bool generateParticleEffectPackage(const std::filesystem::path& spec_path,
                                   const std::filesystem::path& output_dir,
                                   std::string* diagnostic) {
  std::optional<Json> spec = readJson(spec_path, diagnostic);
  if (!spec.has_value() || !spec->is_object()) {
    return fail(diagnostic, "generation spec root must be an object");
  }
  const auto version_it = spec->find("version");
  if (version_it == spec->end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 1) {
    return fail(diagnostic, "generation spec must declare version 1");
  }
  const std::string preset = readString(*spec, "preset", "");
  if (preset != "fire_ray" && preset != "magic_missile" &&
      preset != "arcane_barrage" &&
      preset != "blade_barrier" &&
      preset != "chromatic_ray" &&
      preset != "daze" &&
      preset != "heal" &&
      preset != "haste" &&
      preset != "detect_magic" &&
      preset != "breathe_fire" &&
      preset != "fireball" &&
      preset != "impact_burst" && preset != "energy_orb") {
    return fail(diagnostic,
                "preset must be one of: fire_ray, magic_missile, "
                "arcane_barrage, blade_barrier, chromatic_ray, daze, "
                "heal, haste, detect_magic, breathe_fire, fireball, "
                "impact_burst, energy_orb");
  }
  const std::string asset_namespace =
      readString(*spec, "namespace", presetDefaultNamespace(preset));
  if (!assets::AssetRegistry::isValidAssetKey(asset_namespace)) {
    return fail(diagnostic,
                "invalid namespace '" + asset_namespace + "': " +
                    assets::AssetRegistry::assetKeyValidationError(asset_namespace));
  }
  const std::string name = readString(*spec, "name", presetDefaultName(preset));
  const float default_length =
      preset == "arcane_barrage" ? 5.4f
                                  : (preset == "chromatic_ray"
                                         ? 6.4f
                                         : (preset == "breathe_fire" ? 4.8f : 8.0f));
  const float length = std::max(readFloat(*spec, "length", default_length), 0.25f);
  const float default_radius = [&]() {
    if (preset == "blade_barrier") {
      return 1.65f;
    }
    if (preset == "daze" || preset == "heal" || preset == "haste") {
      return 1.55f;
    }
    if (preset == "breathe_fire") {
      return 1.35f;
    }
    if (preset == "fireball") {
      return kFireballDefaultBlastRadius;
    }
    if (preset == "detect_magic") {
      return kDetectMagicRadius;
    }
    return 1.0f;
  }();
  const float max_radius =
      preset == "fireball" ? kFireballMaxBlastRadius
                            : (preset == "detect_magic" ? kDetectMagicRadius : 6.0f);
  const float radius =
      std::clamp(readFloat(*spec, "radius", default_radius), 0.2f, max_radius);
  std::vector<Json> path_points;
  if (!readPathPoints(*spec, length, path_points, diagnostic)) {
    return false;
  }
  Json path_json = Json::array();
  for (const Json& point : path_points) {
    path_json.push_back(point);
  }

  const std::filesystem::path repo_root = findRepoRoot(spec_path.parent_path());
  if (repo_root.empty()) {
    return fail(diagnostic, "failed to locate Karma repo root for curated atlas assets");
  }
  if (preset == "fireball") {
    return generateFireballEffectPackages(repo_root,
                                          output_dir,
                                          asset_namespace,
                                          name,
                                          radius,
                                          diagnostic);
  }
  std::error_code ec;
  std::filesystem::create_directories(output_dir / "particles", ec);
  if (ec) {
    return fail(diagnostic, "failed to create output particles directory: " + ec.message());
  }
  std::filesystem::create_directories(output_dir / "textures", ec);
  if (ec) {
    return fail(diagnostic, "failed to create output textures directory: " + ec.message());
  }

  std::vector<GeneratedAsset> package_assets;
  std::vector<std::string> effect_paths;
  auto texture_key = [&](std::string_view suffix) {
    return asset_namespace + "/" + std::string(suffix);
  };
  auto effect_key = [&](std::string_view suffix) {
    return asset_namespace + "/" + std::string(suffix);
  };

  auto write_effect = [&](std::string file_name, std::string key, Json effect_json) {
    const std::string relative = "particles/" + file_name;
    if (!writeJson(output_dir / relative, effect_json, diagnostic)) {
      return false;
    }
    addEffect(package_assets, std::move(key), relative);
    effect_paths.push_back(relative);
    return true;
  };

  Json nodes = Json::array();
  Json prefab_variables = Json::object();
  uint32_t next_node_id = 1u;

  if (preset == "fire_ray") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/glow_atlas.png",
                     "textures/glow_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                     "textures/spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/smoke_atlas.png",
                     "textures/smoke_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("glow_atlas"), "textures/glow_atlas.png");
    addTexture(package_assets, texture_key("spark_atlas"), "textures/spark_atlas.png");
    addTexture(package_assets, texture_key("smoke_atlas"), "textures/smoke_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");

    nodes.push_back(makeRootNode(
        name,
        makeBeam(texture_key("glow_atlas"),
                 path_json,
                 0.42f,
                 0.28f,
                 color(1.0f, 0.45f, 0.10f, 0.95f),
                 color(1.0f, 0.08f, 0.02f, 0.45f),
                 3.0f,
                 -1.2f)));
    if (!write_effect("fire_sparks.kpeffect",
                      effect_key("sparks"),
                      effect(makeEmitter("fire_sparks",
                                         texture_key("spark_atlas"),
                                         "additive",
                                         "path",
                                         160,
                                         12,
                                         56.0f,
                                         0.22f,
                                         0.72f,
                                         0.045f,
                                         0.12f,
                                         0.01f,
                                         0.03f,
                                         color(1.0f, 0.78f, 0.22f, 0.95f),
                                         color(1.0f, 0.05f, 0.0f, 0.0f),
                                         path_json,
                                         11u))) ||
        !write_effect("fire_smoke.kpeffect",
                      effect_key("smoke"),
                      effect(makeEmitter("fire_smoke",
                                         texture_key("smoke_atlas"),
                                         "alpha",
                                         "path",
                                         96,
                                         0,
                                         16.0f,
                                         0.8f,
                                         1.55f,
                                         0.16f,
                                         0.32f,
                                         0.36f,
                                         0.72f,
                                         color(0.42f, 0.34f, 0.28f, 0.32f),
                                         color(0.18f, 0.17f, 0.16f, 0.0f),
                                         path_json,
                                         12u))) ||
        !write_effect("fire_heat.kpeffect",
                      effect_key("heat"),
                      effect(makeEmitter("fire_heat",
                                         texture_key("heat_atlas"),
                                         "distortion",
                                         "path",
                                         64,
                                         0,
                                         10.0f,
                                         0.35f,
                                         0.8f,
                                         0.22f,
                                         0.48f,
                                         0.5f,
                                         0.9f,
                                         color(1.0f, 1.0f, 1.0f, 0.16f),
                                         color(1.0f, 1.0f, 1.0f, 0.0f),
                                         path_json,
                                         13u)))) {
      return false;
    }
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "sparks", effect_key("sparks")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "smoke", effect_key("smoke")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "heat", effect_key("heat")));
  } else if (preset == "breathe_fire") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/breathe_fire/textures/breathe_fire_ribbon_atlas.png",
                     "textures/breathe_fire_ribbon_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/breathe_fire/textures/breathe_fire_flame_atlas.png",
                     "textures/breathe_fire_flame_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/breathe_fire/textures/breathe_fire_ember_atlas.png",
                     "textures/breathe_fire_ember_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/breathe_fire/textures/breathe_fire_smoke_atlas.png",
                     "textures/breathe_fire_smoke_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/breathe_fire/textures/breathe_fire_heat_atlas.png",
                     "textures/breathe_fire_heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets,
               texture_key("breathe_fire_ribbon_atlas"),
               "textures/breathe_fire_ribbon_atlas.png");
    addTexture(package_assets,
               texture_key("breathe_fire_flame_atlas"),
               "textures/breathe_fire_flame_atlas.png");
    addTexture(package_assets,
               texture_key("breathe_fire_ember_atlas"),
               "textures/breathe_fire_ember_atlas.png");
    addTexture(package_assets,
               texture_key("breathe_fire_smoke_atlas"),
               "textures/breathe_fire_smoke_atlas.png");
    addTexture(package_assets,
               texture_key("breathe_fire_heat_atlas"),
               "textures/breathe_fire_heat_atlas.png");

    const float mouth_y = 1.08f * radius;
    const float cone_y = 1.20f * radius;
    const float cone_inner_z = 0.68f * length;
    const float cone_outer_z = 1.24f * length;
    const float cone_top_y = mouth_y + 0.34f * length;
    const float cone_bottom_y = std::max(0.05f * radius, mouth_y - 0.28f * length);
    const float cone_mid_high_y = mouth_y + 0.17f * length;
    const float cone_mid_low_y = mouth_y - 0.13f * length;
    const Json breath_path = makeBreatheFireEmitterPath(length, radius);
    nodes.push_back(makeRootNode(name));

    auto add_breath_beam = [&](std::string node_name,
                               Json path,
                               float start_width,
                               float end_width,
                               Json start_color,
                               Json end_color,
                               float uv_repeat,
                               float uv_scroll_speed,
                               std::string blend_mode,
                               float edge_softness,
                               uint32_t layer,
                               std::string texture_suffix) {
      nodes.push_back(makeBeamNode(next_node_id++,
                                   0u,
                                   std::move(node_name),
                                   makeBeam(texture_key(texture_suffix),
                                            std::move(path),
                                            start_width,
                                            end_width,
                                            std::move(start_color),
                                            std::move(end_color),
                                            uv_repeat,
                                            uv_scroll_speed,
                                            std::move(blend_mode),
                                            edge_softness,
                                            layer)));
    };

    add_breath_beam("white_hot_breath_core",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.13f * radius,
                                              cone_y,
                                              0.0f,
                                              0.0f,
                                              0.0f,
                                              0.0f,
                                              0.44f),
                    0.12f * radius,
                    0.72f * radius,
                    color(2.95f, 2.58f, 1.82f, 1.0f),
                    color(1.65f, 0.42f, 0.04f, 0.02f),
                    1.25f,
                    -2.4f,
                    "additive",
                    0.16f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("central_flame_cone",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.17f * radius,
                                              cone_y,
                                              0.0f,
                                              0.0f,
                                              0.0f,
                                              0.05f * radius,
                                              0.50f),
                    0.30f * radius,
                    1.82f * radius,
                    color(2.05f, 0.90f, 0.18f, 0.42f),
                    color(1.05f, 0.08f, 0.02f, 0.02f),
                    1.05f,
                    -1.55f,
                    "additive",
                    0.26f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("left_inner_flame_sheet",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.16f * radius,
                                              1.22f * radius,
                                              0.0f,
                                              cone_inner_z * 0.45f,
                                              cone_inner_z,
                                              0.02f * radius,
                                              0.54f),
                    0.22f * radius,
                    1.62f * radius,
                    color(2.10f, 0.78f, 0.12f, 0.32f),
                    color(1.08f, 0.07f, 0.01f, 0.01f),
                    0.95f,
                    -1.25f,
                    "additive",
                    0.32f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("right_inner_flame_sheet",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.16f * radius,
                                              1.20f * radius,
                                              0.0f,
                                              -cone_inner_z * 0.45f,
                                              -cone_inner_z,
                                              0.00f,
                                              0.48f),
                    0.22f * radius,
                    1.62f * radius,
                    color(2.10f, 0.78f, 0.12f, 0.32f),
                    color(1.08f, 0.07f, 0.01f, 0.01f),
                    0.95f,
                    -1.25f,
                    "additive",
                    0.32f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("front_flame_billow",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.18f * radius,
                                              1.24f * radius,
                                              0.0f,
                                              0.0f,
                                              0.0f,
                                              0.08f * radius,
                                              0.58f),
                    0.34f * radius,
                    2.30f * radius,
                    color(1.64f, 0.50f, 0.08f, 0.22f),
                    color(0.70f, 0.05f, 0.01f, 0.0f),
                    0.70f,
                    -0.72f,
                    "additive",
                    0.40f,
                    0u,
                    "breathe_fire_ribbon_atlas");

    constexpr uint32_t kDenseFlameFanBeamCount = 96u;
    constexpr uint32_t kDenseFlameFanColumns = 24u;
    constexpr uint32_t kDenseFlameFanRows = kDenseFlameFanBeamCount / kDenseFlameFanColumns;
    for (uint32_t i = 0; i < kDenseFlameFanBeamCount; ++i) {
      const uint32_t column = i % kDenseFlameFanColumns;
      const uint32_t row = i / kDenseFlameFanColumns;
      const float u = (static_cast<float>(column) + 0.5f) /
                      static_cast<float>(kDenseFlameFanColumns);
      const float row_u =
          (static_cast<float>(row) + 0.5f) / static_cast<float>(kDenseFlameFanRows);
      const float side = u * 2.0f - 1.0f;
      const float vertical_side = row_u * 2.0f - 1.0f;
      const float edge = std::abs(side);
      const float ribbon_noise = std::sin(static_cast<float>(i) * 2.3999631f);
      const float width_noise = std::sin(static_cast<float>(i) * 5.0832038f);
      const float end_x_scale = 0.50f + 0.12f * (0.5f + 0.5f * ribbon_noise);
      const float end_z = side * cone_outer_z * (0.96f + 0.05f * width_noise);
      const float mid_z = end_z * (0.56f + 0.05f * ribbon_noise);
      const float vertical_scale = (0.34f + (1.0f - edge) * 0.15f) * length;
      const float y_lattice = vertical_side * vertical_scale;
      const float end_y = mouth_y + 0.02f * length + y_lattice +
                          std::sin(static_cast<float>(i) * 1.173f) * 0.025f * length;
      const float mid_y = mouth_y + (end_y - mouth_y) * 0.52f +
                          std::sin(static_cast<float>(i) * 1.719f) * 0.018f * length;
      const float start_alpha = 0.064f + (1.0f - edge) * 0.056f;
      const float end_width = (0.58f + (1.0f - edge) * 0.62f +
                               (0.5f + 0.5f * width_noise) * 0.18f) *
                              radius;
      add_breath_beam("dense_flame_fan_" + std::to_string(i),
                      makeBreatheFireRibbonPath(length,
                                                mouth_y,
                                                mid_y,
                                                end_y,
                                                0.0f,
                                                mid_z,
                                                end_z,
                                                0.0f,
                                                end_x_scale),
                      (0.062f + (1.0f - edge) * 0.054f) * radius,
                      end_width,
                      color(2.30f, 0.90f, 0.13f, start_alpha),
                      color(0.82f, 0.05f, 0.01f, 0.0f),
                      1.05f + 0.25f * (0.5f + 0.5f * ribbon_noise),
                      -1.10f - 0.85f * (0.5f + 0.5f * width_noise),
                      "additive",
                      0.38f,
                      0u,
                      "breathe_fire_ribbon_atlas");
    }

    constexpr uint32_t kOuterSmokeFanBeamCount = 12u;
    for (uint32_t i = 0; i < kOuterSmokeFanBeamCount; ++i) {
      const float side =
          -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(kOuterSmokeFanBeamCount - 1u);
      const float edge = std::abs(side);
      const float end_z = side * cone_outer_z * 1.05f;
      const float y_offset = (static_cast<float>(i % 4u) - 1.5f) * 0.05f * length;
      add_breath_beam("outer_smoke_fan_" + std::to_string(i),
                      makeBreatheFireRibbonPath(length,
                                                mouth_y,
                                                mouth_y + y_offset * 0.45f,
                                                mouth_y + y_offset,
                                                0.0f,
                                                end_z * 0.38f,
                                                end_z,
                                                0.02f * radius,
                                                0.60f),
                      0.14f * radius,
                      (1.32f + (1.0f - edge) * 0.66f) * radius,
                      color(0.70f, 0.32f, 0.16f, 0.06f),
                      color(0.16f, 0.09f, 0.06f, 0.0f),
                      0.82f,
                      -0.34f,
                      "alpha",
                      0.54f,
                      0u,
                      "breathe_fire_smoke_atlas");
    }
    add_breath_beam("upper_outer_flame_shear",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              cone_mid_high_y,
                                              cone_top_y,
                                              0.0f,
                                              cone_outer_z * 0.18f,
                                              cone_outer_z * 0.42f,
                                              0.03f * radius,
                                              0.53f),
                    0.18f * radius,
                    1.76f * radius,
                    color(2.10f, 0.66f, 0.08f, 0.24f),
                    color(0.70f, 0.04f, 0.01f, 0.0f),
                    1.20f,
                    -1.36f,
                    "additive",
                    0.42f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("lower_outer_flame_shear",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              cone_mid_low_y,
                                              cone_bottom_y,
                                              0.0f,
                                              -cone_outer_z * 0.16f,
                                              -cone_outer_z * 0.34f,
                                              -0.02f * radius,
                                              0.57f),
                    0.18f * radius,
                    1.64f * radius,
                    color(1.95f, 0.54f, 0.07f, 0.22f),
                    color(0.58f, 0.03f, 0.01f, 0.0f),
                    1.16f,
                    -1.20f,
                    "additive",
                    0.44f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("upper_flame_tongue",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              cone_mid_high_y,
                                              cone_top_y,
                                              0.0f,
                                              cone_inner_z * 0.12f,
                                              cone_inner_z * 0.24f,
                                              0.12f * radius,
                                              0.46f),
                    0.24f * radius,
                    1.92f * radius,
                    color(2.65f, 1.16f, 0.18f, 0.72f),
                    color(1.20f, 0.10f, 0.01f, 0.02f),
                    1.35f,
                    -2.05f,
                    "additive",
                    0.28f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("lower_flame_tongue",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              cone_mid_low_y,
                                              cone_bottom_y,
                                              0.0f,
                                              -cone_inner_z * 0.10f,
                                              -cone_inner_z * 0.22f,
                                              -0.08f * radius,
                                              0.49f),
                    0.23f * radius,
                    1.82f * radius,
                    color(2.45f, 0.88f, 0.14f, 0.66f),
                    color(0.98f, 0.05f, 0.01f, 0.02f),
                    1.45f,
                    -1.85f,
                    "additive",
                    0.30f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("left_flame_tongue",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.12f * radius,
                                              1.10f * radius,
                                              0.0f,
                                              cone_outer_z * 0.40f,
                                              cone_outer_z,
                                              0.03f * radius,
                                              0.60f),
                    0.22f * radius,
                    2.10f * radius,
                    color(2.20f, 0.72f, 0.10f, 0.52f),
                    color(0.92f, 0.04f, 0.01f, 0.0f),
                    1.35f,
                    -1.75f,
                    "additive",
                    0.34f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("right_flame_tongue",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.12f * radius,
                                              1.08f * radius,
                                              0.0f,
                                              -cone_outer_z * 0.40f,
                                              -cone_outer_z,
                                              -0.02f * radius,
                                              0.55f),
                    0.22f * radius,
                    2.10f * radius,
                    color(2.20f, 0.72f, 0.10f, 0.52f),
                    color(0.92f, 0.04f, 0.01f, 0.0f),
                    1.35f,
                    -1.75f,
                    "additive",
                    0.34f,
                    0u,
                    "breathe_fire_ribbon_atlas");
    add_breath_beam("smoke_edge_haze",
                    makeBreatheFireRibbonPath(length,
                                              mouth_y,
                                              1.20f * radius,
                                              1.28f * radius,
                                              0.0f,
                                              0.0f,
                                              0.0f,
                                              0.06f * radius,
                                              0.62f),
                    0.78f * radius,
                    4.10f * radius,
                    color(0.75f, 0.34f, 0.16f, 0.20f),
                    color(0.18f, 0.12f, 0.10f, 0.0f),
                    0.9f,
                    -0.45f,
                    "alpha",
                    0.50f,
                    0u,
                    "breathe_fire_smoke_atlas");

    Json mouth_flash = makeEmitter("breathe_fire_mouth_flash",
                                   texture_key("breathe_fire_flame_atlas"),
                                   "additive",
                                   "sphere",
                                   128,
                                   96,
                                   24.0f,
                                   0.08f,
                                   0.22f,
                                   0.18f * radius,
                                   0.42f * radius,
                                   0.05f * radius,
                                   0.12f * radius,
                                   color(3.00f, 2.45f, 1.48f, 1.0f),
                                   color(1.00f, 0.18f, 0.02f, 0.0f),
                                   Json::array(),
                                   7103u);
    configureBreatheFireAtlas(mouth_flash);
    mouth_flash["source"]["radius_min"] = 0.02f * radius;
    mouth_flash["source"]["radius_max"] = 0.18f * radius;
    mouth_flash["source"]["radial_speed_min"] = 0.28f * radius;
    mouth_flash["source"]["radial_speed_max"] = 0.92f * radius;
    mouth_flash["motion"]["velocity_min"] = vec3(0.35f * length, -0.06f * radius, -0.16f * radius);
    mouth_flash["motion"]["velocity_max"] = vec3(0.70f * length, 0.16f * radius, 0.16f * radius);
    mouth_flash["motion"]["acceleration"] = vec3(0.0f, 0.08f * radius, 0.0f);
    mouth_flash["motion"]["drag"] = 1.2f;
    mouth_flash["size"]["curve_exponent"] = 0.58f;
    mouth_flash["color"]["alpha_curve_exponent"] = 1.35f;

    Json flame_plumes = makeEmitter("breathe_fire_flame_plumes",
                                    texture_key("breathe_fire_flame_atlas"),
                                    "additive",
                                    "path",
                                    1520,
                                    340,
                                    440.0f,
                                    0.20f,
                                    0.70f,
                                    0.34f * radius,
                                    0.82f * radius,
                                    0.70f * radius,
                                    1.62f * radius,
                                    color(2.95f, 1.54f, 0.32f, 0.98f),
                                    color(1.15f, 0.08f, 0.01f, 0.0f),
                                    breath_path,
                                    7117u);
    configureBreatheFireAtlas(flame_plumes);
    flame_plumes["render"]["layer"] = 1;
    flame_plumes["source"]["sampling"] = "random";
    flame_plumes["source"]["jitter_radius"] = 0.82f * radius;
    flame_plumes["source"]["radial_speed_min"] = 0.38f * radius;
    flame_plumes["source"]["radial_speed_max"] = 2.10f * radius;
    flame_plumes["motion"]["velocity_min"] = vec3(0.10f * length, -0.32f * length, -0.48f * length);
    flame_plumes["motion"]["velocity_max"] = vec3(0.62f * length, 0.42f * length, 0.48f * length);
    flame_plumes["motion"]["acceleration"] = vec3(-0.20f * length, 0.12f * radius, 0.0f);
    flame_plumes["motion"]["drag"] = 0.62f;
    flame_plumes["size"]["curve_exponent"] = 0.74f;
    flame_plumes["color"]["alpha_curve_exponent"] = 0.92f;

    Json embers = makeEmitter("breathe_fire_embers",
                              texture_key("breathe_fire_ember_atlas"),
                              "additive",
                              "path",
                              620,
                              190,
                              165.0f,
                              0.34f,
                              1.12f,
                              0.045f * radius,
                              0.13f * radius,
                              0.01f * radius,
                              0.035f * radius,
                              color(2.85f, 1.55f, 0.28f, 0.98f),
                              color(1.00f, 0.05f, 0.0f, 0.0f),
                              breath_path,
                              7129u);
    configureBreatheFireAtlas(embers);
    embers["source"]["sampling"] = "random";
    embers["source"]["jitter_radius"] = 0.72f * radius;
    embers["source"]["radial_speed_min"] = 0.45f * radius;
    embers["source"]["radial_speed_max"] = 2.20f * radius;
    embers["motion"]["velocity_min"] = vec3(0.28f * length, -0.22f * length, -0.52f * length);
    embers["motion"]["velocity_max"] = vec3(0.86f * length, 0.34f * length, 0.52f * length);
    embers["motion"]["acceleration"] = vec3(-0.18f * length, 0.32f * radius, 0.0f);
    embers["motion"]["drag"] = 0.72f;
    embers["rotation"]["angular_velocity_min"] = -4.8f;
    embers["rotation"]["angular_velocity_max"] = 4.8f;

    Json smoke = makeEmitter("breathe_fire_smoke_wisps",
                             texture_key("breathe_fire_smoke_atlas"),
                             "alpha",
                             "path",
                             460,
                             96,
                             96.0f,
                             0.72f,
                             1.70f,
                             0.62f * radius,
                             1.28f * radius,
                             1.36f * radius,
                             2.72f * radius,
                             color(0.62f, 0.33f, 0.16f, 0.44f),
                             color(0.10f, 0.09f, 0.08f, 0.0f),
                             breath_path,
                             7141u);
    configureBreatheFireAtlas(smoke);
    smoke["source"]["sampling"] = "random";
    smoke["source"]["jitter_radius"] = 1.22f * radius;
    smoke["source"]["radial_speed_min"] = 0.10f * radius;
    smoke["source"]["radial_speed_max"] = 0.56f * radius;
    smoke["motion"]["velocity_min"] = vec3(0.00f * length, -0.05f * length, -0.36f * length);
    smoke["motion"]["velocity_max"] = vec3(0.22f * length, 0.18f * length, 0.36f * length);
    smoke["motion"]["acceleration"] = vec3(-0.04f * length, 0.12f * radius, 0.0f);
    smoke["motion"]["drag"] = 0.56f;
    smoke["size"]["curve_exponent"] = 0.84f;
    smoke["color"]["alpha_curve_exponent"] = 1.12f;

    Json heat = makeEmitter("breathe_fire_heat_distortion",
                            texture_key("breathe_fire_heat_atlas"),
                            "distortion",
                            "path",
                            220,
                            24,
                            42.0f,
                            0.34f,
                            0.94f,
                            0.34f * radius,
                            0.86f * radius,
                            0.72f * radius,
                            1.38f * radius,
                            color(1.0f, 1.0f, 1.0f, 0.34f),
                            color(1.0f, 1.0f, 1.0f, 0.0f),
                            breath_path,
                            7159u);
    configureBreatheFireAtlas(heat);
    heat["render"]["soft_particle_distance"] = 0.8f;
    heat["render"]["distortion_strength"] = 7.0f;
    heat["source"]["sampling"] = "random";
    heat["source"]["jitter_radius"] = 0.86f * radius;
    heat["source"]["radial_speed_min"] = 0.02f * radius;
    heat["source"]["radial_speed_max"] = 0.22f * radius;
    heat["motion"]["velocity_min"] = vec3(0.08f * length, -0.07f * length, -0.24f * length);
    heat["motion"]["velocity_max"] = vec3(0.34f * length, 0.12f * length, 0.24f * length);
    heat["motion"]["acceleration"] = vec3(-0.03f * length, 0.10f * radius, 0.0f);
    heat["motion"]["drag"] = 1.1f;

    if (!write_effect("breathe_fire_mouth_flash.kpeffect",
                      effect_key("mouth_flash"),
                      effect(std::move(mouth_flash))) ||
        !write_effect("breathe_fire_flame_plumes.kpeffect",
                      effect_key("flame_plumes"),
                      effect(std::move(flame_plumes))) ||
        !write_effect("breathe_fire_embers.kpeffect",
                      effect_key("embers"),
                      effect(std::move(embers))) ||
        !write_effect("breathe_fire_smoke.kpeffect",
                      effect_key("smoke"),
                      effect(std::move(smoke))) ||
        !write_effect("breathe_fire_heat.kpeffect",
                      effect_key("heat"),
                      effect(std::move(heat)))) {
      return false;
    }
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "mouth_flash",
                                     effect_key("mouth_flash"),
                                     vec3(-0.48f * length, mouth_y, 0.0f)));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "flame_plumes", effect_key("flame_plumes")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "embers", effect_key("embers")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "smoke", effect_key("smoke")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "heat", effect_key("heat")));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "breathe_fire_glow",
                                       color(1.0f, 0.42f, 0.10f, 1.0f),
                                       9.0f * radius,
                                       5.0f * radius,
                                       vec3(-0.05f * length, 1.15f * radius, 0.0f)));
  } else if (preset == "magic_missile") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_core_atlas.png",
                     "textures/orb_core_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_halo_atlas.png",
                     "textures/orb_halo_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                     "textures/spark_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("orb_core_atlas"), "textures/orb_core_atlas.png");
    addTexture(package_assets, texture_key("orb_halo_atlas"), "textures/orb_halo_atlas.png");
    addTexture(package_assets, texture_key("spark_atlas"), "textures/spark_atlas.png");

    nodes.push_back(makeRootNode(
        name,
        makeBeam(texture_key("orb_core_atlas"),
                 path_json,
                 0.22f,
                 0.06f,
                 color(0.30f, 0.84f, 1.0f, 0.9f),
                 color(0.72f, 0.34f, 1.0f, 0.18f),
                 2.2f,
                 -1.8f)));
    if (!write_effect("magic_trail_sparks.kpeffect",
                      effect_key("trail_sparks"),
                      effect(makeEmitter("magic_trail_sparks",
                                         texture_key("spark_atlas"),
                                         "additive",
                                         "path",
                                         128,
                                         8,
                                         44.0f,
                                         0.24f,
                                         0.68f,
                                         0.035f,
                                         0.08f,
                                         0.005f,
                                         0.02f,
                                         color(0.58f, 0.92f, 1.0f, 1.0f),
                                         color(0.50f, 0.16f, 1.0f, 0.0f),
                                         path_json,
                                         21u))) ||
        !write_effect("magic_head_halo.kpeffect",
                      effect_key("head_halo"),
                      effect(makeEmitter("magic_head_halo",
                                         texture_key("orb_halo_atlas"),
                                         "additive",
                                         "sphere",
                                         48,
                                         12,
                                         10.0f,
                                         0.32f,
                                         0.72f,
                                         0.18f,
                                         0.34f,
                                         0.05f,
                                         0.10f,
                                         color(0.45f, 0.88f, 1.0f, 0.75f),
                                         color(0.74f, 0.32f, 1.0f, 0.0f),
                                         Json::array(),
                                         22u)))) {
      return false;
    }
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "trail_sparks", effect_key("trail_sparks")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "head_halo", effect_key("head_halo")));
  } else if (preset == "arcane_barrage") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_core_atlas.png",
                     "textures/orb_core_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_halo_atlas.png",
                     "textures/orb_halo_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                     "textures/spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/smoke_atlas.png",
                     "textures/smoke_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/arcane_barrage/textures/arcane_ribbon_atlas.png",
                     "textures/arcane_ribbon_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("orb_core_atlas"), "textures/orb_core_atlas.png");
    addTexture(package_assets, texture_key("orb_halo_atlas"), "textures/orb_halo_atlas.png");
    addTexture(package_assets, texture_key("spark_atlas"), "textures/spark_atlas.png");
    addTexture(package_assets, texture_key("smoke_atlas"), "textures/smoke_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");
    addTexture(package_assets, texture_key("arcane_ribbon_atlas"),
               "textures/arcane_ribbon_atlas.png");

    const std::vector<Json> missile_paths = makeArcaneBarragePaths(length);
    constexpr float kDepthScales[] = {0.78f, 0.88f, 0.98f, 1.08f, 1.22f, 1.36f};
    nodes.push_back(makeRootNode(name));
    for (std::size_t i = 0u; i < missile_paths.size(); ++i) {
      const std::string index = std::to_string(i + 1u);
      const float depth_scale = kDepthScales[std::min<std::size_t>(
          i, (sizeof(kDepthScales) / sizeof(kDepthScales[0])) - 1u)];
      nodes.push_back(makeBeamNode(
          next_node_id++,
          0u,
          "missile_" + index + "_core",
          makeBeam(texture_key("arcane_ribbon_atlas"),
                   missile_paths[i],
                   0.32f * depth_scale,
                   0.18f * depth_scale,
                   color(1.55f, 1.85f, 1.70f, 0.96f),
                   color(0.85f, 1.65f, 1.42f, 0.76f),
                   1.35f,
                   -0.72f,
                   "additive",
                   0.16f,
                   0u)));
      nodes.push_back(makeBeamNode(
          next_node_id++,
          0u,
          "missile_" + index + "_haze",
          makeBeam(texture_key("smoke_atlas"),
                   missile_paths[i],
                   1.05f * depth_scale,
                   0.62f * depth_scale,
                   color(0.592f, 0.992f, 0.910f, 0.72f),
                   color(0.388f, 0.718f, 0.627f, 0.24f),
                   0.70f,
                   -0.22f,
                   "alpha",
                   0.70f,
                   0u)));
    }

    Json caster_flare = makeEmitter("arcane_caster_flare",
                                    texture_key("orb_core_atlas"),
                                    "additive",
                                    "path",
                                    220,
                                    96,
                                    90.0f,
                                    0.18f,
                                    0.62f,
                                    0.55f,
                                    1.25f,
                                    0.18f,
                                    0.42f,
                                    color(1.0f, 1.0f, 1.0f, 0.98f),
                                    color(0.835f, 0.988f, 0.957f, 0.0f),
                                    pointsArray({pathPoint(missile_paths.front(), 0u)}),
                                    4101u);
    configureOrbEmitter(caster_flare);
    caster_flare["render"]["layer"] = 0u;
    caster_flare["source"]["sampling"] = "vertices";
    caster_flare["source"]["jitter_radius"] = 0.14f;
    caster_flare["source"]["radial_speed_min"] = 0.04f;
    caster_flare["source"]["radial_speed_max"] = 0.22f;
    caster_flare["motion"]["velocity_min"] = vec3(-0.14f, -0.08f, -0.08f);
    caster_flare["motion"]["velocity_max"] = vec3(0.12f, 0.16f, 0.08f);
    caster_flare["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    caster_flare["motion"]["drag"] = 1.0f;
    caster_flare["size"]["curve_exponent"] = 0.50f;

    Json caster_halo = makeEmitter("arcane_caster_halo",
                                   texture_key("orb_halo_atlas"),
                                   "additive",
                                   "path",
                                   96,
                                   38,
                                   30.0f,
                                   0.32f,
                                   0.92f,
                                   1.05f,
                                   1.80f,
                                   0.42f,
                                   0.82f,
                                   color(0.835f, 0.988f, 0.957f, 0.46f),
                                   color(0.388f, 0.718f, 0.627f, 0.0f),
                                   pointsArray({pathPoint(missile_paths.front(), 0u)}),
                                   4102u);
    configureOrbEmitter(caster_halo);
    caster_halo["render"]["layer"] = 0u;
    caster_halo["source"]["sampling"] = "vertices";
    caster_halo["source"]["jitter_radius"] = 0.10f;
    caster_halo["source"]["radial_speed_min"] = 0.01f;
    caster_halo["source"]["radial_speed_max"] = 0.08f;
    caster_halo["motion"]["velocity_min"] = vec3(-0.08f, -0.04f, -0.06f);
    caster_halo["motion"]["velocity_max"] = vec3(0.08f, 0.08f, 0.06f);
    caster_halo["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    caster_halo["motion"]["drag"] = 1.35f;
    caster_halo["size"]["curve_exponent"] = 0.68f;
    caster_halo["color"]["alpha_curve_exponent"] = 1.35f;

    std::vector<Json> head_emitters;
    std::vector<Json> spark_emitters;
    std::vector<Json> mist_emitters;
    std::vector<Json> distortion_emitters;
    head_emitters.reserve(missile_paths.size() * 2u);
    spark_emitters.reserve(missile_paths.size());
    mist_emitters.reserve(missile_paths.size() * 2u);
    distortion_emitters.reserve(missile_paths.size());

    Json caster_plume = makeEmitter("arcane_caster_plume",
                                    texture_key("smoke_atlas"),
                                    "alpha",
                                    "path",
                                    180,
                                    24,
                                    18.0f,
                                    0.85f,
                                    1.90f,
                                    0.62f,
                                    1.25f,
                                    2.10f,
                                    3.30f,
                                    color(0.592f, 0.992f, 0.910f, 0.46f),
                                    color(0.388f, 0.718f, 0.627f, 0.0f),
                                    pointsArray({pathPoint(missile_paths.front(), 0u),
                                                 shiftedPoint(pathPoint(missile_paths.front(), 1u),
                                                              -0.18f,
                                                              0.10f,
                                                              -0.02f),
                                                 shiftedPoint(pathPoint(missile_paths.back(), 1u),
                                                              -0.24f,
                                                              -0.14f,
                                                              0.08f)}),
                                    4390u);
    caster_plume["render"]["layer"] = 0u;
    caster_plume["render"]["soft_particle_distance"] = 0.85f;
    caster_plume["source"]["jitter_radius"] = 0.58f;
    caster_plume["source"]["radial_speed_min"] = 0.06f;
    caster_plume["source"]["radial_speed_max"] = 0.30f;
    caster_plume["motion"]["velocity_min"] = vec3(-0.22f, -0.10f, -0.10f);
    caster_plume["motion"]["velocity_max"] = vec3(0.14f, 0.32f, 0.12f);
    caster_plume["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    caster_plume["motion"]["drag"] = 0.70f;
    caster_plume["color"]["alpha_curve_exponent"] = 1.42f;
    mist_emitters.push_back(std::move(caster_plume));

    for (std::size_t i = 0u; i < missile_paths.size(); ++i) {
      const std::string index = std::to_string(i + 1u);
      const float depth_scale = kDepthScales[std::min<std::size_t>(
          i, (sizeof(kDepthScales) / sizeof(kDepthScales[0])) - 1u)];
      Json head = makeEmitter("arcane_head_" + index,
                              texture_key("orb_core_atlas"),
                              "additive",
                              "path",
                              96,
                              24,
                              22.0f,
                              0.20f,
                              0.54f,
                              0.34f * depth_scale,
                              0.66f * depth_scale,
                              0.16f * depth_scale,
                              0.30f * depth_scale,
                              color(0.969f, 0.992f, 0.992f, 0.95f),
                              color(0.447f, 0.988f, 0.969f, 0.0f),
                              pointsArray({pathEndpoint(missile_paths[i])}),
                              static_cast<uint32_t>(4200u + i));
      configureOrbEmitter(head);
      head["render"]["layer"] = 0u;
      head["source"]["sampling"] = "vertices";
      head["source"]["jitter_radius"] = 0.12f * depth_scale;
      head["source"]["radial_speed_min"] = 0.02f;
      head["source"]["radial_speed_max"] = 0.12f;
      head["motion"]["velocity_min"] = vec3(-0.08f, -0.04f, -0.05f);
      head["motion"]["velocity_max"] = vec3(0.05f, 0.08f, 0.05f);
      head["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
      head["motion"]["drag"] = 1.4f;
      head["size"]["curve_exponent"] = 0.50f;
      head_emitters.push_back(std::move(head));

      Json head_halo = makeEmitter("arcane_head_halo_" + index,
                                   texture_key("orb_halo_atlas"),
                                   "additive",
                                   "path",
                                   48,
                                   12,
                                   9.0f,
                                   0.25f,
                                   0.68f,
                                   0.78f * depth_scale,
                                   1.38f * depth_scale,
                                   0.34f * depth_scale,
                                   0.62f * depth_scale,
                                   color(0.651f, 0.996f, 1.0f, 0.70f),
                                   color(0.388f, 0.718f, 0.627f, 0.0f),
                                   pointsArray({pathEndpoint(missile_paths[i])}),
                                   static_cast<uint32_t>(4250u + i));
      configureOrbEmitter(head_halo);
      head_halo["render"]["layer"] = 0u;
      head_halo["source"]["sampling"] = "vertices";
      head_halo["source"]["jitter_radius"] = 0.10f * depth_scale;
      head_halo["source"]["radial_speed_min"] = 0.01f;
      head_halo["source"]["radial_speed_max"] = 0.06f;
      head_halo["motion"]["velocity_min"] = vec3(-0.04f, -0.03f, -0.04f);
      head_halo["motion"]["velocity_max"] = vec3(0.04f, 0.05f, 0.04f);
      head_halo["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
      head_halo["motion"]["drag"] = 1.6f;
      head_halo["size"]["curve_exponent"] = 0.66f;
      head_halo["color"]["alpha_curve_exponent"] = 1.3f;
      head_emitters.push_back(std::move(head_halo));

      Json sparks = makeEmitter("arcane_trail_sparks_" + index,
                                texture_key("spark_atlas"),
                                "additive",
                                "path",
                                112,
                                12,
                                26.0f,
                                0.20f,
                                0.64f,
                                0.030f * depth_scale,
                                0.090f * depth_scale,
                                0.004f,
                                0.018f,
                                color(0.651f, 0.996f, 1.0f, 0.96f),
                                color(0.447f, 0.988f, 0.969f, 0.0f),
                                missile_paths[i],
                                static_cast<uint32_t>(4300u + i));
      sparks["render"]["layer"] = 0u;
      sparks["source"]["jitter_radius"] = 0.12f * depth_scale;
      sparks["source"]["radial_speed_min"] = 0.06f;
      sparks["source"]["radial_speed_max"] = 0.34f;
      sparks["motion"]["velocity_min"] = vec3(-0.34f, -0.08f, -0.14f);
      sparks["motion"]["velocity_max"] = vec3(0.10f, 0.26f, 0.14f);
      sparks["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
      sparks["motion"]["drag"] = 0.55f;
      sparks["atlas"]["animation_fps"] = 16.0f;
      sparks["size"]["curve_exponent"] = 0.74f;
      spark_emitters.push_back(std::move(sparks));

      Json mist = makeEmitter("arcane_trail_mist_" + index,
                              texture_key("smoke_atlas"),
                              "alpha",
                              "path",
                              170,
                              0,
                              20.0f,
                              0.78f,
                              1.70f,
                              0.42f * depth_scale,
                              0.85f * depth_scale,
                              1.35f * depth_scale,
                              2.35f * depth_scale,
                              color(0.592f, 0.992f, 0.910f, 0.58f),
                              color(0.388f, 0.718f, 0.627f, 0.0f),
                              pointsArray({pathPoint(missile_paths[i], 0u),
                                           pathPoint(missile_paths[i], 1u),
                                           pathMidpoint(missile_paths[i]),
                                           pathEndpoint(missile_paths[i])}),
                              static_cast<uint32_t>(4400u + i));
      mist["render"]["layer"] = 0u;
      mist["render"]["soft_particle_distance"] = 0.72f;
      mist["source"]["jitter_radius"] = 0.46f * depth_scale;
      mist["source"]["radial_speed_min"] = 0.06f;
      mist["source"]["radial_speed_max"] = 0.38f;
      mist["motion"]["velocity_min"] = vec3(-0.24f, -0.02f, -0.10f);
      mist["motion"]["velocity_max"] = vec3(0.18f, 0.42f, 0.12f);
      mist["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
      mist["motion"]["drag"] = 0.65f;
      mist["color"]["alpha_curve_exponent"] = 1.38f;
      mist_emitters.push_back(std::move(mist));

      const float wisp_sign = (i % 2u == 0u) ? 1.0f : -1.0f;
      Json wisp = makeEmitter("arcane_loose_wisp_" + index,
                              texture_key("smoke_atlas"),
                              "alpha",
                              "path",
                              42,
                              4,
                              4.2f,
                              0.80f,
                              1.75f,
                              0.18f * depth_scale,
                              0.34f * depth_scale,
                              1.00f * depth_scale,
                              1.80f * depth_scale,
                              color(0.592f, 0.992f, 0.910f, 0.42f),
                              color(0.388f, 0.718f, 0.627f, 0.0f),
                              pointsArray({shiftedPoint(pathPoint(missile_paths[i], 0u),
                                                        0.05f * wisp_sign,
                                                        0.05f,
                                                        0.03f * wisp_sign),
                                           shiftedPoint(pathPoint(missile_paths[i], 1u),
                                                        0.12f * wisp_sign,
                                                        0.18f,
                                                        0.08f * wisp_sign),
                                           shiftedPoint(pathMidpoint(missile_paths[i]),
                                                        0.22f * wisp_sign,
                                                        0.30f,
                                                        0.14f * wisp_sign)}),
                              static_cast<uint32_t>(4450u + i));
      wisp["render"]["layer"] = 0u;
      wisp["render"]["soft_particle_distance"] = 0.65f;
      wisp["source"]["jitter_radius"] = 0.26f * depth_scale;
      wisp["source"]["radial_speed_min"] = 0.02f;
      wisp["source"]["radial_speed_max"] = 0.12f;
      wisp["motion"]["velocity_min"] = vec3(-0.08f, 0.02f, -0.06f);
      wisp["motion"]["velocity_max"] = vec3(0.10f, 0.22f, 0.06f);
      wisp["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
      wisp["motion"]["drag"] = 0.82f;
      wisp["color"]["alpha_curve_exponent"] = 1.5f;
      mist_emitters.push_back(std::move(wisp));

      Json distortion = makeEmitter("arcane_trail_distortion_" + index,
                                    texture_key("heat_atlas"),
                                    "distortion",
                                    "path",
                                    56,
                                    0,
                                    6.0f,
                                    0.28f,
                                    0.72f,
                                    0.26f * depth_scale,
                                    0.50f * depth_scale,
                                    0.60f * depth_scale,
                                    1.00f * depth_scale,
                                    color(1.0f, 1.0f, 1.0f, 0.14f),
                                    color(1.0f, 1.0f, 1.0f, 0.0f),
                                    pointsArray({pathPoint(missile_paths[i], 1u),
                                                 pathMidpoint(missile_paths[i]),
                                                 pathEndpoint(missile_paths[i])}),
                                    static_cast<uint32_t>(4500u + i));
      distortion["render"]["layer"] = 0u;
      distortion["render"]["soft_particle_distance"] = 0.75f;
      distortion["render"]["distortion_strength"] = 6.0f;
      distortion["source"]["jitter_radius"] = 0.22f * depth_scale;
      distortion["source"]["radial_speed_min"] = 0.02f;
      distortion["source"]["radial_speed_max"] = 0.14f;
      distortion["motion"]["velocity_min"] = vec3(-0.06f, -0.03f, -0.04f);
      distortion["motion"]["velocity_max"] = vec3(0.08f, 0.14f, 0.05f);
      distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
      distortion["motion"]["drag"] = 1.0f;
      distortion["color"]["alpha_curve_exponent"] = 1.25f;
      distortion_emitters.push_back(std::move(distortion));
    }

    if (!write_effect("arcane_caster_flare.kpeffect",
                      effect_key("caster_flare"),
                      effect(std::vector<Json>{std::move(caster_flare),
                                               std::move(caster_halo)})) ||
        !write_effect("arcane_missile_heads.kpeffect",
                      effect_key("missile_heads"),
                      effect(std::move(head_emitters))) ||
        !write_effect("arcane_trail_sparks.kpeffect",
                      effect_key("trail_sparks"),
                      effect(std::move(spark_emitters))) ||
        !write_effect("arcane_trail_mist.kpeffect",
                      effect_key("trail_mist"),
                      effect(std::move(mist_emitters))) ||
        !write_effect("arcane_trail_distortion.kpeffect",
                      effect_key("trail_distortion"),
                      effect(std::move(distortion_emitters)))) {
      return false;
    }
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "caster_flare",
                                     effect_key("caster_flare")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "missile_heads",
                                     effect_key("missile_heads")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "trail_sparks",
                                     effect_key("trail_sparks")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "trail_mist",
                                     effect_key("trail_mist")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "trail_distortion",
                                     effect_key("trail_distortion")));
  } else if (preset == "chromatic_ray") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/chromatic_ray/textures/chromatic_ribbon_atlas.png",
                     "textures/chromatic_ribbon_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/chromatic_ray/textures/chromatic_spark_atlas.png",
                     "textures/chromatic_spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/chromatic_ray/textures/chromatic_haze_atlas.png",
                     "textures/chromatic_haze_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("chromatic_ribbon_atlas"),
               "textures/chromatic_ribbon_atlas.png");
    addTexture(package_assets, texture_key("chromatic_spark_atlas"),
               "textures/chromatic_spark_atlas.png");
    addTexture(package_assets, texture_key("chromatic_haze_atlas"),
               "textures/chromatic_haze_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");

    const Json ray_path = makeChromaticRayPath(length);
    const Json origin_path = pointsArray({pathPoint(ray_path, 0u)});
    const Json impact_path = pointsArray({pathEndpoint(ray_path)});
    nodes.push_back(makeRootNode(name));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "chromatic_core",
        makeBeam(texture_key("chromatic_ribbon_atlas"),
                 ray_path,
                 0.20f * radius,
                 0.28f * radius,
                 color(2.05f, 2.05f, 2.12f, 0.84f),
                 color(1.30f, 1.52f, 2.02f, 0.60f),
                 1.0f,
                 -2.60f,
                 "additive",
                 0.10f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "chromatic_ribbon",
        makeBeam(texture_key("chromatic_ribbon_atlas"),
                 ray_path,
                 0.98f * radius,
                 1.26f * radius,
                 color(1.28f, 1.22f, 1.18f, 0.88f),
                 color(1.18f, 1.08f, 1.38f, 0.76f),
                 1.0f,
                 -1.20f,
                 "additive",
                 0.34f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "chromatic_haze",
        makeBeam(texture_key("chromatic_haze_atlas"),
                 ray_path,
                 1.34f * radius,
                 1.72f * radius,
                 color(0.74f, 0.80f, 1.12f, 0.40f),
                 color(1.02f, 0.26f, 1.36f, 0.30f),
                 1.0f,
                 -0.34f,
                 "alpha",
                 0.72f,
                 0u)));

    constexpr const char* kThreadNames[] = {
        "red_fire_thread",
        "orange_acid_thread",
        "yellow_electric_thread",
        "green_poison_thread",
        "blue_stone_thread",
        "indigo_mind_thread",
        "violet_shift_thread",
    };
    constexpr float kThreadColors[][4] = {
        {2.45f, 0.16f, 0.09f, 0.92f},
        {2.35f, 0.78f, 0.08f, 0.84f},
        {2.15f, 1.96f, 0.12f, 0.88f},
        {0.20f, 2.15f, 0.22f, 0.82f},
        {0.12f, 1.88f, 2.45f, 0.90f},
        {0.22f, 0.32f, 2.50f, 0.84f},
        {1.70f, 0.20f, 2.55f, 0.94f},
    };
    constexpr float kPi = 3.14159265358979323846f;
    for (std::size_t i = 0u; i < 7u; ++i) {
      const float phase = (static_cast<float>(i) / 7.0f) * kPi * 2.0f;
      const Json thread_path =
          makeChromaticHelixPath(length, kChromaticHelixRadius, phase, 24u);
      nodes.push_back(makeBeamNode(
          next_node_id++,
          0u,
          kThreadNames[i],
          makeBeam(texture_key("chromatic_ribbon_atlas"),
                   thread_path,
                   (0.095f + 0.014f * static_cast<float>(i % 3u)) * radius,
                   (0.140f + 0.018f * static_cast<float>((i + 1u) % 3u)) * radius,
                   color(kThreadColors[i][0],
                         kThreadColors[i][1],
                         kThreadColors[i][2],
                         kThreadColors[i][3]),
                   color(kThreadColors[i][0],
                         kThreadColors[i][1],
                         kThreadColors[i][2],
                         kThreadColors[i][3] * 0.48f),
                   1.32f,
                   -1.62f - static_cast<float>(i) * 0.13f,
                   "additive",
                   0.20f,
                   0u)));
    }

    std::vector<Json> flare_emitters;
    flare_emitters.reserve(2u);
    Json origin_flare = makeEmitter("chromatic_origin_flare",
                                    texture_key("chromatic_spark_atlas"),
                                    "additive",
                                    "path",
                                    130,
                                    84,
                                    36.0f,
                                    0.18f,
                                    0.52f,
                                    0.18f * radius,
                                    0.48f * radius,
                                    0.030f * radius,
                                    0.090f * radius,
                                    color(2.55f, 2.30f, 2.65f, 0.96f),
                                    color(1.00f, 0.30f, 1.80f, 0.0f),
                                    origin_path,
                                    7301u);
    configureChromaticAtlas(origin_flare);
    origin_flare["source"]["sampling"] = "vertices";
    origin_flare["source"]["jitter_radius"] = 0.16f * radius;
    origin_flare["source"]["radial_speed_min"] = 0.08f * radius;
    origin_flare["source"]["radial_speed_max"] = 0.46f * radius;
    origin_flare["motion"]["velocity_min"] = vec3(-0.22f * radius,
                                                  -0.12f * radius,
                                                  -0.16f * radius);
    origin_flare["motion"]["velocity_max"] = vec3(0.46f * radius,
                                                  0.30f * radius,
                                                  0.16f * radius);
    origin_flare["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    origin_flare["motion"]["drag"] = 0.70f;
    origin_flare["size"]["curve_exponent"] = 0.50f;
    origin_flare["color"]["alpha_curve_exponent"] = 1.45f;
    flare_emitters.push_back(std::move(origin_flare));

    Json impact_flare = makeEmitter("chromatic_impact_flare",
                                    texture_key("chromatic_spark_atlas"),
                                    "additive",
                                    "path",
                                    170,
                                    112,
                                    42.0f,
                                    0.16f,
                                    0.62f,
                                    0.22f * radius,
                                    0.62f * radius,
                                    0.035f * radius,
                                    0.12f * radius,
                                    color(2.75f, 2.44f, 2.90f, 0.98f),
                                    color(1.30f, 0.26f, 2.20f, 0.0f),
                                    impact_path,
                                    7302u);
    configureChromaticAtlas(impact_flare);
    impact_flare["source"]["sampling"] = "vertices";
    impact_flare["source"]["jitter_radius"] = 0.22f * radius;
    impact_flare["source"]["radial_speed_min"] = 0.12f * radius;
    impact_flare["source"]["radial_speed_max"] = 0.62f * radius;
    impact_flare["motion"]["velocity_min"] = vec3(-0.28f * radius,
                                                  -0.16f * radius,
                                                  -0.22f * radius);
    impact_flare["motion"]["velocity_max"] = vec3(0.36f * radius,
                                                  0.42f * radius,
                                                  0.22f * radius);
    impact_flare["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    impact_flare["motion"]["drag"] = 0.64f;
    impact_flare["size"]["curve_exponent"] = 0.46f;
    impact_flare["color"]["alpha_curve_exponent"] = 1.38f;
    flare_emitters.push_back(std::move(impact_flare));

    Json sparks = makeEmitter("chromatic_color_sparks",
                              texture_key("chromatic_spark_atlas"),
                              "additive",
                              "path",
                              620,
                              148,
                              180.0f,
                              0.28f,
                              1.05f,
                              0.045f * radius,
                              0.155f * radius,
                              0.006f * radius,
                              0.036f * radius,
                              color(1.95f, 1.60f, 1.18f, 0.94f),
                              color(0.45f, 0.18f, 1.40f, 0.0f),
                              ray_path,
                              7311u);
    configureChromaticAtlas(sparks);
    sparks["source"]["jitter_radius"] = 0.46f * radius;
    sparks["source"]["radial_speed_min"] = 0.06f * radius;
    sparks["source"]["radial_speed_max"] = 0.46f * radius;
    sparks["motion"]["velocity_min"] = vec3(-0.20f * radius,
                                            -0.08f * radius,
                                            -0.20f * radius);
    sparks["motion"]["velocity_max"] = vec3(0.52f * radius,
                                            0.36f * radius,
                                            0.20f * radius);
    sparks["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    sparks["motion"]["drag"] = 0.42f;
    sparks["rotation"]["angular_velocity_min"] = -3.6f;
    sparks["rotation"]["angular_velocity_max"] = 3.6f;
    sparks["size"]["curve_exponent"] = 0.62f;
    sparks["color"]["alpha_curve_exponent"] = 1.42f;

    Json wisps = makeEmitter("chromatic_color_wisps",
                             texture_key("chromatic_haze_atlas"),
                             "alpha",
                             "path",
                             340,
                             82,
                             58.0f,
                             0.72f,
                             1.65f,
                             0.28f * radius,
                             0.70f * radius,
                             0.86f * radius,
                             1.65f * radius,
                             color(0.58f, 1.02f, 1.28f, 0.38f),
                             color(0.45f, 0.08f, 0.80f, 0.0f),
                             ray_path,
                             7312u);
    configureChromaticAtlas(wisps);
    wisps["render"]["soft_particle_distance"] = 0.86f;
    wisps["source"]["jitter_radius"] = 0.68f * radius;
    wisps["source"]["radial_speed_min"] = 0.01f * radius;
    wisps["source"]["radial_speed_max"] = 0.20f * radius;
    wisps["motion"]["velocity_min"] = vec3(-0.14f * radius,
                                           -0.04f * radius,
                                           -0.18f * radius);
    wisps["motion"]["velocity_max"] = vec3(0.28f * radius,
                                           0.32f * radius,
                                           0.18f * radius);
    wisps["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    wisps["motion"]["drag"] = 0.58f;
    wisps["motion"]["orbit_speed"] = 0.34f;
    wisps["rotation"]["angular_velocity_min"] = -0.85f;
    wisps["rotation"]["angular_velocity_max"] = 0.85f;
    wisps["size"]["curve_exponent"] = 0.78f;
    wisps["color"]["alpha_curve_exponent"] = 1.74f;

    Json distortion = makeEmitter("chromatic_ray_distortion",
                                  texture_key("heat_atlas"),
                                  "distortion",
                                  "path",
                                  110,
                                  18,
                                  14.0f,
                                  0.28f,
                                  0.78f,
                                  0.24f * radius,
                                  0.52f * radius,
                                  0.66f * radius,
                                  1.05f * radius,
                                  color(1.0f, 1.0f, 1.0f, 0.10f),
                                  color(1.0f, 1.0f, 1.0f, 0.0f),
                                  ray_path,
                                  7321u);
    distortion["playback"]["local_space"] = true;
    distortion["render"]["soft_particle_distance"] = 0.92f;
    distortion["render"]["distortion_strength"] = 3.2f;
    distortion["source"]["jitter_radius"] = 0.32f * radius;
    distortion["source"]["radial_speed_min"] = 0.00f;
    distortion["source"]["radial_speed_max"] = 0.10f * radius;
    distortion["motion"]["velocity_min"] = vec3(-0.06f * radius,
                                                -0.04f * radius,
                                                -0.08f * radius);
    distortion["motion"]["velocity_max"] = vec3(0.18f * radius,
                                                0.16f * radius,
                                                0.08f * radius);
    distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    distortion["motion"]["drag"] = 0.66f;
    distortion["rotation"]["angular_velocity_min"] = -1.3f;
    distortion["rotation"]["angular_velocity_max"] = 1.3f;
    distortion["size"]["curve_exponent"] = 0.66f;
    distortion["color"]["alpha_curve_exponent"] = 1.50f;

    if (!write_effect("chromatic_flares.kpeffect",
                      effect_key("flares"),
                      effect(std::move(flare_emitters))) ||
        !write_effect("chromatic_sparks.kpeffect",
                      effect_key("sparks"),
                      effect(std::move(sparks))) ||
        !write_effect("chromatic_wisps.kpeffect",
                      effect_key("wisps"),
                      effect(std::move(wisps))) ||
        !write_effect("chromatic_distortion.kpeffect",
                      effect_key("distortion"),
                      effect(std::move(distortion)))) {
      return false;
    }

    nodes.push_back(makeParticleNode(next_node_id++, 0u, "flares", effect_key("flares")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "sparks", effect_key("sparks")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "wisps", effect_key("wisps")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "distortion",
                                     effect_key("distortion")));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "chromatic_glow",
                                       color(1.0f, 0.60f, 1.0f, 1.0f),
                                       3.4f * radius,
                                       4.0f * radius,
                                       pathEndpoint(ray_path)));
  } else if (preset == "blade_barrier") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/blade_barrier/textures/blade_shard_atlas.png",
                     "textures/blade_shard_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/blade_barrier/textures/blade_wind_atlas.png",
                     "textures/blade_wind_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/blade_barrier/textures/blade_swish_atlas.png",
                     "textures/blade_swish_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/blade_barrier/textures/blade_dust_atlas.png",
                     "textures/blade_dust_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("blade_shard_atlas"),
               "textures/blade_shard_atlas.png");
    addTexture(package_assets, texture_key("blade_wind_atlas"),
               "textures/blade_wind_atlas.png");
    addTexture(package_assets, texture_key("blade_swish_atlas"),
               "textures/blade_swish_atlas.png");
    addTexture(package_assets, texture_key("blade_dust_atlas"),
               "textures/blade_dust_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");

    nodes.push_back(makeRootNode(name));
    const std::vector<Json> wind_rings = makeBladeBarrierRingPaths(radius);
    for (std::size_t i = 0u; i < wind_rings.size(); ++i) {
      const float depth_scale = 0.72f + static_cast<float>(i % 4u) * 0.10f;
      nodes.push_back(makeBeamNode(
          next_node_id++,
          0u,
          "wind_ring_" + std::to_string(i + 1u),
          makeBeam(texture_key("blade_wind_atlas"),
                   wind_rings[i],
                   0.075f * radius * depth_scale,
                   0.060f * radius * depth_scale,
                   color(1.35f, 1.55f, 1.65f, 0.78f),
                   color(0.64f, 0.78f, 0.76f, 0.34f),
                   2.8f,
                   (i % 2u == 0u ? 1.10f : -1.35f),
                   "additive",
                   0.28f,
                   0u)));
    }

    std::vector<Json> blade_emitters;
    blade_emitters.reserve(2u);
    Json outer_blades = makeEmitter("blade_outer_shell",
                                    texture_key("blade_shard_atlas"),
                                    "alpha",
                                    "sphere_surface",
                                    660,
                                    430,
                                    285.0f,
                                    0.86f,
                                    1.55f,
                                    0.13f * radius,
                                    0.25f * radius,
                                    0.10f * radius,
                                    0.20f * radius,
                                    color(0.78f, 0.88f, 0.96f, 0.86f),
                                    color(0.40f, 0.48f, 0.52f, 0.30f),
                                    Json::array(),
                                    6101u);
    configureBladeAtlas(outer_blades);
    outer_blades["playback"]["local_space"] = true;
    outer_blades["source"]["radius_min"] = 0.84f * radius;
    outer_blades["source"]["radius_max"] = 1.06f * radius;
    outer_blades["source"]["distribution"] = "surface";
    outer_blades["source"]["jitter_radius"] = 0.02f * radius;
    configureBladeOrbit(outer_blades, 2.85f);
    outer_blades["rotation"]["angular_velocity_min"] = -11.0f;
    outer_blades["rotation"]["angular_velocity_max"] = 11.0f;
    outer_blades["size"]["curve_exponent"] = 0.92f;
    outer_blades["color"]["alpha_curve_exponent"] = 0.82f;
    blade_emitters.push_back(std::move(outer_blades));

    Json inner_blades = makeEmitter("blade_inner_shell",
                                    texture_key("blade_shard_atlas"),
                                    "alpha",
                                    "sphere_surface",
                                    400,
                                    240,
                                    150.0f,
                                    0.68f,
                                    1.18f,
                                    0.09f * radius,
                                    0.17f * radius,
                                    0.065f * radius,
                                    0.13f * radius,
                                    color(0.70f, 0.80f, 0.88f, 0.68f),
                                    color(0.32f, 0.40f, 0.44f, 0.20f),
                                    Json::array(),
                                    6102u);
    configureBladeAtlas(inner_blades);
    inner_blades["playback"]["local_space"] = true;
    inner_blades["source"]["radius_min"] = 0.48f * radius;
    inner_blades["source"]["radius_max"] = 0.78f * radius;
    inner_blades["source"]["distribution"] = "surface";
    inner_blades["source"]["jitter_radius"] = 0.08f * radius;
    configureBladeOrbit(inner_blades, 3.45f);
    inner_blades["rotation"]["angular_velocity_min"] = -8.5f;
    inner_blades["rotation"]["angular_velocity_max"] = 8.5f;
    inner_blades["size"]["curve_exponent"] = 0.95f;
    inner_blades["color"]["alpha_curve_exponent"] = 0.90f;
    blade_emitters.push_back(std::move(inner_blades));

    Json blade_glints = makeEmitter("blade_glints",
                                    texture_key("blade_shard_atlas"),
                                    "additive",
                                    "sphere_surface",
                                    180,
                                    58,
                                    86.0f,
                                    0.14f,
                                    0.38f,
                                    0.06f * radius,
                                    0.13f * radius,
                                    0.014f * radius,
                                    0.040f * radius,
                                    color(1.7f, 2.0f, 2.2f, 0.72f),
                                    color(0.56f, 0.74f, 0.86f, 0.0f),
                                    Json::array(),
                                    6111u);
    configureBladeAtlas(blade_glints);
    blade_glints["playback"]["local_space"] = true;
    blade_glints["source"]["radius_min"] = 0.72f * radius;
    blade_glints["source"]["radius_max"] = 1.10f * radius;
    blade_glints["source"]["distribution"] = "surface";
    blade_glints["source"]["jitter_radius"] = 0.02f * radius;
    configureBladeOrbit(blade_glints, 3.2f);
    blade_glints["rotation"]["angular_velocity_min"] = -14.0f;
    blade_glints["rotation"]["angular_velocity_max"] = 14.0f;
    blade_glints["size"]["curve_exponent"] = 0.50f;
    blade_glints["color"]["alpha_curve_exponent"] = 1.8f;

    Json dust = makeEmitter("blade_dust_shroud",
                            texture_key("blade_dust_atlas"),
                            "alpha",
                            "sphere",
                            220,
                            68,
                            46.0f,
                            1.10f,
                            2.05f,
                            0.16f * radius,
                            0.32f * radius,
                            0.54f * radius,
                            0.90f * radius,
                            color(0.52f, 0.58f, 0.58f, 0.20f),
                            color(0.18f, 0.20f, 0.19f, 0.0f),
                            Json::array(),
                            6121u);
    dust["playback"]["local_space"] = true;
    dust["render"]["layer"] = 0u;
    dust["render"]["soft_particle_distance"] = 0.82f;
    dust["source"]["radius_min"] = 0.42f * radius;
    dust["source"]["radius_max"] = 1.02f * radius;
    dust["source"]["jitter_radius"] = 0.10f * radius;
    dust["source"]["radial_speed_min"] = 0.00f;
    dust["source"]["radial_speed_max"] = 0.07f * radius;
    dust["motion"]["velocity_min"] = vec3(-0.08f * radius, -0.03f * radius, -0.08f * radius);
    dust["motion"]["velocity_max"] = vec3(0.08f * radius, 0.12f * radius, 0.08f * radius);
    dust["motion"]["acceleration"] = vec3(0.0f, 0.015f * radius, 0.0f);
    dust["motion"]["drag"] = 0.82f;
    dust["motion"]["orbit_axis"] = vec3(0.0f, 1.0f, 0.0f);
    dust["motion"]["orbit_speed"] = 0.92f;
    dust["rotation"]["angular_velocity_min"] = -0.7f;
    dust["rotation"]["angular_velocity_max"] = 0.7f;
    dust["size"]["curve_exponent"] = 0.84f;
    dust["color"]["alpha_curve_exponent"] = 1.85f;

    Json dust_swirl = makeEmitter("blade_dust_swirl",
                                  texture_key("blade_dust_atlas"),
                                  "alpha",
                                  "sphere_surface",
                                  118,
                                  34,
                                  38.0f,
                                  0.62f,
                                  1.24f,
                                  0.12f * radius,
                                  0.24f * radius,
                                  0.34f * radius,
                                  0.66f * radius,
                                  color(0.56f, 0.64f, 0.64f, 0.15f),
                                  color(0.20f, 0.24f, 0.22f, 0.0f),
                                  Json::array(),
                                  6124u);
    dust_swirl["playback"]["local_space"] = true;
    dust_swirl["render"]["layer"] = 0u;
    dust_swirl["render"]["soft_particle_distance"] = 0.88f;
    dust_swirl["source"]["radius_min"] = 0.86f * radius;
    dust_swirl["source"]["radius_max"] = 1.18f * radius;
    dust_swirl["source"]["distribution"] = "surface";
    dust_swirl["source"]["jitter_radius"] = 0.05f * radius;
    dust_swirl["source"]["radial_speed_min"] = 0.00f;
    dust_swirl["source"]["radial_speed_max"] = 0.08f * radius;
    dust_swirl["motion"]["velocity_min"] = vec3(-0.14f * radius, -0.04f * radius, -0.12f * radius);
    dust_swirl["motion"]["velocity_max"] = vec3(0.16f * radius, 0.08f * radius, 0.14f * radius);
    dust_swirl["motion"]["acceleration"] = vec3(0.0f, 0.01f * radius, 0.0f);
    dust_swirl["motion"]["drag"] = 0.70f;
    dust_swirl["motion"]["orbit_axis"] = vec3(0.0f, 1.0f, 0.0f);
    dust_swirl["motion"]["orbit_speed"] = 1.55f;
    dust_swirl["rotation"]["angular_velocity_min"] = -1.8f;
    dust_swirl["rotation"]["angular_velocity_max"] = 1.8f;
    dust_swirl["size"]["curve_exponent"] = 0.82f;
    dust_swirl["color"]["alpha_curve_exponent"] = 1.82f;

    Json wind = makeEmitter("blade_wind_shroud",
                            texture_key("blade_dust_atlas"),
                            "alpha",
                            "sphere_surface",
                            150,
                            34,
                            36.0f,
                            0.42f,
                            0.96f,
                            0.09f * radius,
                            0.21f * radius,
                            0.32f * radius,
                            0.60f * radius,
                            color(0.70f, 0.84f, 0.86f, 0.14f),
                            color(0.30f, 0.38f, 0.36f, 0.0f),
                            Json::array(),
                            6122u);
    wind["playback"]["local_space"] = true;
    wind["render"]["soft_particle_distance"] = 0.78f;
    wind["source"]["radius_min"] = 0.82f * radius;
    wind["source"]["radius_max"] = 1.16f * radius;
    wind["source"]["distribution"] = "surface";
    wind["source"]["jitter_radius"] = 0.04f * radius;
    wind["source"]["radial_speed_min"] = 0.00f;
    wind["source"]["radial_speed_max"] = 0.06f * radius;
    wind["motion"]["velocity_min"] = vec3(-0.16f * radius, -0.06f * radius, -0.16f * radius);
    wind["motion"]["velocity_max"] = vec3(0.18f * radius, 0.08f * radius, 0.18f * radius);
    wind["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    wind["motion"]["drag"] = 0.58f;
    wind["motion"]["orbit_axis"] = vec3(0.0f, 1.0f, 0.0f);
    wind["motion"]["orbit_speed"] = 2.15f;
    wind["rotation"]["angular_velocity_min"] = -2.6f;
    wind["rotation"]["angular_velocity_max"] = 2.6f;
    wind["size"]["curve_exponent"] = 0.76f;
    wind["color"]["alpha_curve_exponent"] = 1.70f;

    Json swishes = makeEmitter("blade_swishes",
                               texture_key("blade_swish_atlas"),
                               "additive",
                               "sphere_surface",
                               220,
                               90,
                               170.0f,
                               0.18f,
                               0.46f,
                               0.30f * radius,
                               0.58f * radius,
                               0.08f * radius,
                               0.22f * radius,
                               color(1.42f, 1.70f, 1.82f, 0.52f),
                               color(0.32f, 0.50f, 0.54f, 0.0f),
                               Json::array(),
                               6123u);
    swishes["playback"]["local_space"] = true;
    swishes["playback"]["time_scale"] = 1.35f;
    swishes["render"]["use_soft_mask"] = false;
    swishes["source"]["radius_min"] = 0.74f * radius;
    swishes["source"]["radius_max"] = 1.14f * radius;
    swishes["source"]["distribution"] = "surface";
    swishes["source"]["jitter_radius"] = 0.025f * radius;
    configureBladeOrbit(swishes, 3.05f);
    swishes["rotation"]["angular_velocity_min"] = -8.0f;
    swishes["rotation"]["angular_velocity_max"] = 8.0f;
    swishes["size"]["curve_exponent"] = 0.70f;
    swishes["color"]["alpha_curve_exponent"] = 1.25f;

    Json distortion = makeEmitter("blade_barrier_distortion",
                                  texture_key("heat_atlas"),
                                  "distortion",
                                  "sphere_surface",
                                  96,
                                  20,
                                  18.0f,
                                  0.32f,
                                  0.82f,
                                  0.20f * radius,
                                  0.38f * radius,
                                  0.52f * radius,
                                  0.92f * radius,
                                  color(1.0f, 1.0f, 1.0f, 0.10f),
                                  color(1.0f, 1.0f, 1.0f, 0.0f),
                                  Json::array(),
                                  6131u);
    distortion["playback"]["local_space"] = true;
    distortion["render"]["soft_particle_distance"] = 0.85f;
    distortion["render"]["distortion_strength"] = 3.8f;
    distortion["source"]["radius_min"] = 0.72f * radius;
    distortion["source"]["radius_max"] = 1.08f * radius;
    distortion["source"]["distribution"] = "surface";
    distortion["source"]["jitter_radius"] = 0.08f * radius;
    distortion["source"]["radial_speed_min"] = 0.03f * radius;
    distortion["source"]["radial_speed_max"] = 0.14f * radius;
    distortion["motion"]["velocity_min"] = vec3(-0.16f * radius, -0.08f * radius, -0.16f * radius);
    distortion["motion"]["velocity_max"] = vec3(0.18f * radius, 0.12f * radius, 0.18f * radius);
    distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    distortion["motion"]["drag"] = 0.42f;
    distortion["rotation"]["angular_velocity_min"] = -2.2f;
    distortion["rotation"]["angular_velocity_max"] = 2.2f;
    distortion["color"]["alpha_curve_exponent"] = 1.55f;

    if (!write_effect("blade_shell.kpeffect",
                      effect_key("blade_shell"),
                      effect(std::move(blade_emitters))) ||
        !write_effect("blade_glints.kpeffect",
                      effect_key("blade_glints"),
                      effect(std::move(blade_glints))) ||
        !write_effect("blade_dust.kpeffect",
                      effect_key("dust_shroud"),
                      effect(std::vector<Json>{std::move(dust),
                                               std::move(dust_swirl)})) ||
        !write_effect("blade_wind.kpeffect",
                      effect_key("wind_shroud"),
                      effect(std::move(wind))) ||
        !write_effect("blade_swishes.kpeffect",
                      effect_key("blade_swishes"),
                      effect(std::move(swishes))) ||
        !write_effect("blade_distortion.kpeffect",
                      effect_key("blade_distortion"),
                      effect(std::move(distortion)))) {
      return false;
    }

    nodes.push_back(makeParticleNode(next_node_id++, 0u, "blade_shell", effect_key("blade_shell")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "blade_glints", effect_key("blade_glints")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "dust_shroud", effect_key("dust_shroud")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "wind_shroud", effect_key("wind_shroud")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "blade_swishes", effect_key("blade_swishes")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "blade_distortion",
                                     effect_key("blade_distortion")));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "steel_glow",
                                       color(0.78f, 0.88f, 1.0f, 1.0f),
                                       3.2f * radius,
                                       3.4f * radius));
  } else if (preset == "daze") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/daze/textures/daze_ribbon_atlas.png",
                     "textures/daze_ribbon_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/daze/textures/daze_star_atlas.png",
                     "textures/daze_star_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/daze/textures/daze_haze_atlas.png",
                     "textures/daze_haze_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("daze_ribbon_atlas"),
               "textures/daze_ribbon_atlas.png");
    addTexture(package_assets, texture_key("daze_star_atlas"),
               "textures/daze_star_atlas.png");
    addTexture(package_assets, texture_key("daze_haze_atlas"),
               "textures/daze_haze_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");

    constexpr float kPi = 3.14159265358979323846f;
    const float head_height = 1.24f * radius;
    const Json head_position = vec3(0.0f, head_height, 0.0f);
    nodes.push_back(makeRootNode(name));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "halo_ring",
        makeBeam(texture_key("daze_ribbon_atlas"),
                 makeDazeHaloPath(1.34f * radius,
                                  0.56f * radius,
                                  head_height,
                                  72u,
                                  0.08f,
                                  0.020f * radius),
                 0.140f * radius,
                 0.098f * radius,
                 color(1.12f, 0.92f, 2.70f, 0.94f),
                 color(0.36f, 0.72f, 2.10f, 0.36f),
                 2.4f,
                 -1.35f,
                 "additive",
                 0.26f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "halo_glow",
        makeBeam(texture_key("daze_ribbon_atlas"),
                 makeDazeHaloPath(1.48f * radius,
                                  0.66f * radius,
                                  head_height,
                                  72u,
                                  0.44f,
                                  0.032f * radius),
                 0.380f * radius,
                 0.280f * radius,
                 color(0.42f, 0.28f, 1.40f, 0.42f),
                 color(0.12f, 0.30f, 1.10f, 0.16f),
                 1.2f,
                 0.32f,
                 "additive",
                 0.82f,
                 0u)));

    const std::vector<Json> daze_arcs = {
        makeDazeArcPath(1.46f * radius,
                        0.62f * radius,
                        head_height + 0.05f * radius,
                        -0.22f * kPi,
                        0.56f * kPi,
                        18u,
                        0.050f * radius),
        makeDazeArcPath(1.24f * radius,
                        0.48f * radius,
                        head_height - 0.10f * radius,
                        0.46f * kPi,
                        0.42f * kPi,
                        16u,
                        0.036f * radius),
        makeDazeArcPath(1.62f * radius,
                        0.70f * radius,
                        head_height + 0.13f * radius,
                        1.06f * kPi,
                        0.36f * kPi,
                        14u,
                        0.044f * radius),
        makeDazeArcPath(1.08f * radius,
                        0.42f * radius,
                        head_height + 0.25f * radius,
                        1.58f * kPi,
                        0.31f * kPi,
                        12u,
                        0.030f * radius),
        makeDazeArcPath(1.72f * radius,
                        0.76f * radius,
                        head_height - 0.02f * radius,
                        0.88f * kPi,
                        0.28f * kPi,
                        12u,
                        0.034f * radius),
    };
    constexpr float kArcWidths[] = {0.095f, 0.074f, 0.082f, 0.062f, 0.070f};
    constexpr float kArcScroll[] = {-1.70f, 1.25f, -1.05f, 1.85f, -1.45f};
    for (std::size_t i = 0u; i < daze_arcs.size(); ++i) {
      nodes.push_back(makeBeamNode(
          next_node_id++,
          0u,
          "crescent_arc_" + std::to_string(i + 1u),
          makeBeam(texture_key("daze_ribbon_atlas"),
                   daze_arcs[i],
                   kArcWidths[i] * radius,
                   kArcWidths[i] * radius * 0.34f,
                   color(1.25f, 1.02f, 3.05f, 0.86f),
                   color(0.42f, 0.76f, 2.35f, 0.0f),
                   0.78f,
                   kArcScroll[i],
                   "additive",
                   0.24f,
                   0u)));
    }

    std::vector<Json> haze_emitters;
    haze_emitters.reserve(2u);
    Json haze_ring = makeEmitter("daze_haze_ring",
                                 texture_key("daze_haze_atlas"),
                                 "alpha",
                                 "ring",
                                 190,
                                 76,
                                 24.0f,
                                 0.78f,
                                 1.65f,
                                 0.30f * radius,
                                 0.62f * radius,
                                 0.92f * radius,
                                 1.46f * radius,
                                 color(0.42f, 0.30f, 1.08f, 0.32f),
                                 color(0.10f, 0.12f, 0.46f, 0.0f),
                                 Json::array(),
                                 7101u);
    configureDazeAtlas(haze_ring);
    configureDazeRingSource(haze_ring, radius, 0.72f, 1.52f, 0.00f, 0.055f);
    haze_ring["render"]["soft_particle_distance"] = 0.80f;
    haze_ring["motion"]["velocity_min"] = vec3(-0.035f * radius,
                                               -0.020f * radius,
                                               -0.035f * radius);
    haze_ring["motion"]["velocity_max"] = vec3(0.035f * radius,
                                               0.100f * radius,
                                               0.035f * radius);
    haze_ring["motion"]["acceleration"] = vec3(0.0f, 0.015f * radius, 0.0f);
    haze_ring["motion"]["drag"] = 0.72f;
    haze_ring["motion"]["orbit_speed"] = 0.68f;
    haze_ring["rotation"]["angular_velocity_min"] = -0.55f;
    haze_ring["rotation"]["angular_velocity_max"] = 0.55f;
    haze_ring["size"]["curve_exponent"] = 0.82f;
    haze_ring["color"]["alpha_curve_exponent"] = 1.78f;
    haze_emitters.push_back(std::move(haze_ring));

    Json lifted_haze = makeEmitter("daze_lifted_haze",
                                   texture_key("daze_haze_atlas"),
                                   "alpha",
                                   "ring",
                                   96,
                                   28,
                                   14.0f,
                                   0.95f,
                                   1.95f,
                                   0.22f * radius,
                                   0.44f * radius,
                                   0.70f * radius,
                                   1.12f * radius,
                                   color(0.30f, 0.44f, 1.16f, 0.22f),
                                   color(0.08f, 0.10f, 0.42f, 0.0f),
                                   Json::array(),
                                   7102u);
    configureDazeAtlas(lifted_haze);
    configureDazeRingSource(lifted_haze, radius, 0.82f, 1.34f, 0.00f, 0.035f);
    lifted_haze["render"]["soft_particle_distance"] = 0.95f;
    lifted_haze["motion"]["velocity_min"] = vec3(-0.030f * radius,
                                                 0.045f * radius,
                                                 -0.030f * radius);
    lifted_haze["motion"]["velocity_max"] = vec3(0.030f * radius,
                                                 0.180f * radius,
                                                 0.030f * radius);
    lifted_haze["motion"]["acceleration"] = vec3(0.0f, 0.025f * radius, 0.0f);
    lifted_haze["motion"]["drag"] = 0.58f;
    lifted_haze["motion"]["orbit_speed"] = -0.45f;
    lifted_haze["rotation"]["angular_velocity_min"] = -0.72f;
    lifted_haze["rotation"]["angular_velocity_max"] = 0.72f;
    lifted_haze["size"]["curve_exponent"] = 0.74f;
    lifted_haze["color"]["alpha_curve_exponent"] = 1.92f;
    haze_emitters.push_back(std::move(lifted_haze));

    Json stars = makeEmitter("daze_stars",
                             texture_key("daze_star_atlas"),
                             "additive",
                             "ring",
                             280,
                             112,
                             42.0f,
                             0.36f,
                             1.05f,
                             0.070f * radius,
                             0.205f * radius,
                             0.012f * radius,
                             0.045f * radius,
                             color(1.55f, 1.60f, 2.70f, 0.98f),
                             color(0.54f, 0.28f, 1.36f, 0.0f),
                             Json::array(),
                             7111u);
    configureDazeAtlas(stars);
    configureDazeRingSource(stars, radius, 0.78f, 1.58f, 0.020f, 0.22f);
    stars["motion"]["velocity_min"] = vec3(-0.10f * radius,
                                           -0.035f * radius,
                                           -0.10f * radius);
    stars["motion"]["velocity_max"] = vec3(0.10f * radius,
                                           0.140f * radius,
                                           0.10f * radius);
    stars["motion"]["acceleration"] = vec3(0.0f, -0.020f * radius, 0.0f);
    stars["motion"]["drag"] = 0.50f;
    stars["motion"]["orbit_speed"] = 1.12f;
    stars["rotation"]["angular_velocity_min"] = -2.8f;
    stars["rotation"]["angular_velocity_max"] = 2.8f;
    stars["size"]["curve_exponent"] = 0.58f;
    stars["color"]["alpha_curve_exponent"] = 1.55f;

    Json streaks = makeEmitter("daze_crescent_streaks",
                               texture_key("daze_star_atlas"),
                               "additive",
                               "ring",
                               156,
                               52,
                               30.0f,
                               0.24f,
                               0.62f,
                               0.145f * radius,
                               0.340f * radius,
                               0.024f * radius,
                               0.090f * radius,
                               color(0.98f, 1.14f, 2.85f, 0.82f),
                               color(0.26f, 0.26f, 1.30f, 0.0f),
                               Json::array(),
                               7112u);
    configureDazeAtlas(streaks);
    configureDazeRingSource(streaks, radius, 0.82f, 1.48f, 0.035f, 0.32f);
    streaks["motion"]["velocity_min"] = vec3(-0.16f * radius,
                                             -0.030f * radius,
                                             -0.16f * radius);
    streaks["motion"]["velocity_max"] = vec3(0.16f * radius,
                                             0.100f * radius,
                                             0.16f * radius);
    streaks["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    streaks["motion"]["drag"] = 0.38f;
    streaks["motion"]["orbit_speed"] = 1.85f;
    streaks["rotation"]["angular_velocity_min"] = -4.5f;
    streaks["rotation"]["angular_velocity_max"] = 4.5f;
    streaks["size"]["curve_exponent"] = 0.52f;
    streaks["color"]["alpha_curve_exponent"] = 1.35f;

    Json pulse = makeEmitter("daze_pulse_glints",
                             texture_key("daze_star_atlas"),
                             "additive",
                             "ring",
                             80,
                             44,
                             10.0f,
                             0.16f,
                             0.42f,
                             0.130f * radius,
                             0.300f * radius,
                             0.018f * radius,
                             0.055f * radius,
                             color(1.75f, 1.72f, 3.00f, 0.96f),
                             color(0.62f, 0.32f, 1.45f, 0.0f),
                             Json::array(),
                             7113u);
    configureDazeAtlas(pulse);
    configureDazeRingSource(pulse, radius, 0.86f, 1.36f, 0.020f, 0.18f);
    pulse["motion"]["velocity_min"] = vec3(-0.08f * radius,
                                           -0.020f * radius,
                                           -0.08f * radius);
    pulse["motion"]["velocity_max"] = vec3(0.08f * radius,
                                           0.090f * radius,
                                           0.08f * radius);
    pulse["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    pulse["motion"]["drag"] = 0.72f;
    pulse["motion"]["orbit_speed"] = -0.92f;
    pulse["rotation"]["angular_velocity_min"] = -3.0f;
    pulse["rotation"]["angular_velocity_max"] = 3.0f;
    pulse["size"]["curve_exponent"] = 0.42f;
    pulse["color"]["alpha_curve_exponent"] = 1.92f;

    Json distortion = makeEmitter("daze_halo_distortion",
                                  texture_key("heat_atlas"),
                                  "distortion",
                                  "ring",
                                  58,
                                  18,
                                  8.0f,
                                  0.30f,
                                  0.82f,
                                  0.20f * radius,
                                  0.42f * radius,
                                  0.56f * radius,
                                  0.98f * radius,
                                  color(1.0f, 1.0f, 1.0f, 0.10f),
                                  color(1.0f, 1.0f, 1.0f, 0.0f),
                                  Json::array(),
                                  7121u);
    distortion["playback"]["local_space"] = true;
    distortion["render"]["soft_particle_distance"] = 0.92f;
    distortion["render"]["distortion_strength"] = 2.7f;
    configureDazeRingSource(distortion, radius, 0.78f, 1.48f, 0.005f, 0.10f);
    distortion["motion"]["velocity_min"] = vec3(-0.055f * radius,
                                                -0.020f * radius,
                                                -0.055f * radius);
    distortion["motion"]["velocity_max"] = vec3(0.055f * radius,
                                                0.070f * radius,
                                                0.055f * radius);
    distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    distortion["motion"]["drag"] = 0.62f;
    distortion["motion"]["orbit_speed"] = 0.72f;
    distortion["rotation"]["angular_velocity_min"] = -1.2f;
    distortion["rotation"]["angular_velocity_max"] = 1.2f;
    distortion["size"]["curve_exponent"] = 0.68f;
    distortion["color"]["alpha_curve_exponent"] = 1.62f;

    if (!write_effect("daze_haze.kpeffect",
                      effect_key("haze"),
                      effect(std::move(haze_emitters))) ||
        !write_effect("daze_stars.kpeffect",
                      effect_key("stars"),
                      effect(std::move(stars))) ||
        !write_effect("daze_streaks.kpeffect",
                      effect_key("streaks"),
                      effect(std::move(streaks))) ||
        !write_effect("daze_pulse.kpeffect",
                      effect_key("pulse"),
                      effect(std::move(pulse))) ||
        !write_effect("daze_distortion.kpeffect",
                      effect_key("distortion"),
                      effect(std::move(distortion)))) {
      return false;
    }

    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "haze",
                                     effect_key("haze"),
                                     head_position));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "stars",
                                     effect_key("stars"),
                                     head_position));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "streaks",
                                     effect_key("streaks"),
                                     head_position));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "pulse",
                                     effect_key("pulse"),
                                     head_position));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "distortion",
                                     effect_key("distortion"),
                                     head_position));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "daze_glow",
                                       color(0.48f, 0.36f, 1.0f, 1.0f),
                                       2.6f * radius,
                                       3.0f * radius,
                                       head_position));
  } else if (preset == "heal") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/heal/textures/heal_ribbon_atlas.png",
                     "textures/heal_ribbon_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/heal/textures/heal_spark_atlas.png",
                     "textures/heal_spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/heal/textures/heal_haze_atlas.png",
                     "textures/heal_haze_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("heal_ribbon_atlas"),
               "textures/heal_ribbon_atlas.png");
    addTexture(package_assets, texture_key("heal_spark_atlas"),
               "textures/heal_spark_atlas.png");
    addTexture(package_assets, texture_key("heal_haze_atlas"),
               "textures/heal_haze_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");

    constexpr float kPi = 3.14159265358979323846f;
    const float base_height = 0.10f * radius;
    const float waist_height = 0.82f * radius;
    const float chest_height = 1.30f * radius;
    const float head_height = 2.08f * radius;
    const float body_height = 2.55f * radius;
    const Json body_center = vec3(0.0f, body_height * 0.5f, 0.0f);
    nodes.push_back(makeRootNode(name));

    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "base_healing_ring",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 makeDazeHaloPath(1.26f * radius,
                                  1.26f * radius,
                                  base_height,
                                  72u,
                                  0.00f,
                                  0.020f * radius),
                 0.220f * radius,
                 0.145f * radius,
                 color(0.82f, 1.76f, 2.75f, 0.86f),
                 color(0.18f, 0.70f, 1.90f, 0.24f),
                 2.1f,
                 -0.62f,
                 "additive",
                 0.42f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "waist_healing_ring",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 makeDazeHaloPath(1.04f * radius,
                                  1.04f * radius,
                                  waist_height,
                                  64u,
                                  0.36f,
                                  0.026f * radius),
                 0.090f * radius,
                 0.056f * radius,
                 color(0.64f, 1.45f, 2.35f, 0.58f),
                 color(0.18f, 0.54f, 1.55f, 0.10f),
                 1.8f,
                 0.88f,
                 "additive",
                 0.32f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "chest_healing_ring",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 makeDazeHaloPath(1.18f * radius,
                                  1.18f * radius,
                                  chest_height,
                                  72u,
                                  0.74f,
                                  0.030f * radius),
                 0.170f * radius,
                 0.110f * radius,
                 color(1.00f, 1.98f, 3.05f, 0.92f),
                 color(0.26f, 0.86f, 2.16f, 0.26f),
                 2.3f,
                 -1.05f,
                 "additive",
                 0.34f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "head_shimmer_ring",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 makeDazeHaloPath(0.78f * radius,
                                  0.78f * radius,
                                  head_height,
                                  56u,
                                  1.18f,
                                  0.020f * radius),
                 0.072f * radius,
                 0.042f * radius,
                 color(1.05f, 1.92f, 2.95f, 0.64f),
                 color(0.24f, 0.82f, 2.10f, 0.05f),
                 1.5f,
                 1.22f,
                 "additive",
                 0.30f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "healing_spiral_1",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 makeHealSpiralPath(0.92f * radius,
                                    0.92f * radius,
                                    base_height,
                                    body_height,
                                    1.62f,
                                    58u,
                                    0.15f),
                 0.072f * radius,
                 0.038f * radius,
                 color(0.82f, 1.85f, 2.80f, 0.70f),
                 color(0.18f, 0.68f, 1.85f, 0.0f),
                 2.6f,
                 -1.35f,
                 "additive",
                 0.24f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "healing_spiral_2",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 makeHealSpiralPath(1.12f * radius,
                                    1.12f * radius,
                                    base_height + 0.08f * radius,
                                    body_height * 0.88f,
                                    1.28f,
                                    52u,
                                    kPi),
                 0.052f * radius,
                 0.028f * radius,
                 color(0.45f, 1.32f, 2.40f, 0.46f),
                 color(0.12f, 0.46f, 1.48f, 0.0f),
                 2.0f,
                 0.92f,
                 "additive",
                 0.32f,
                 0u)));
    nodes.push_back(makeBeamNode(
        next_node_id++,
        0u,
        "healing_column",
        makeBeam(texture_key("heal_ribbon_atlas"),
                 Json::array({vec3(0.0f, base_height, 0.0f),
                              vec3(0.0f, chest_height, 0.0f),
                              vec3(0.0f, body_height + 0.18f * radius, 0.0f)}),
                 0.62f * radius,
                 0.42f * radius,
                 color(0.34f, 1.02f, 2.20f, 0.52f),
                 color(0.08f, 0.42f, 1.36f, 0.08f),
                 1.1f,
                 -0.22f,
                 "additive",
                 0.94f,
                 0u)));

    Json mist = makeEmitter("heal_column_mist",
                            texture_key("heal_haze_atlas"),
                            "alpha",
                            "cylinder",
                            300,
                            112,
                            46.0f,
                            1.05f,
                            2.35f,
                            0.32f * radius,
                            0.72f * radius,
                            1.05f * radius,
                            1.85f * radius,
                            color(0.24f, 0.82f, 1.55f, 0.42f),
                            color(0.05f, 0.14f, 0.58f, 0.0f),
                            Json::array(),
                            7201u);
    configureHealAtlas(mist);
    mist["render"]["soft_particle_distance"] = 0.95f;
    mist["source"]["height"] = body_height;
    mist["source"]["outer_radius"] = 0.80f * radius;
    mist["source"]["jitter_radius"] = 0.06f * radius;
    mist["source"]["radial_speed_min"] = 0.00f;
    mist["source"]["radial_speed_max"] = 0.035f * radius;
    mist["motion"]["velocity_min"] = vec3(-0.04f * radius, 0.10f * radius, -0.04f * radius);
    mist["motion"]["velocity_max"] = vec3(0.04f * radius, 0.42f * radius, 0.04f * radius);
    mist["motion"]["acceleration"] = vec3(0.0f, 0.035f * radius, 0.0f);
    mist["motion"]["drag"] = 0.66f;
    mist["motion"]["orbit_speed"] = 0.36f;
    mist["rotation"]["angular_velocity_min"] = -0.55f;
    mist["rotation"]["angular_velocity_max"] = 0.55f;
    mist["size"]["curve_exponent"] = 0.78f;
    mist["color"]["alpha_curve_exponent"] = 1.86f;

    Json shimmer = makeEmitter("heal_shimmer_motes",
                               texture_key("heal_spark_atlas"),
                               "additive",
                               "cylinder",
                               340,
                               124,
                               68.0f,
                               0.54f,
                               1.42f,
                               0.040f * radius,
                               0.135f * radius,
                               0.008f * radius,
                               0.040f * radius,
                               color(1.25f, 1.85f, 2.60f, 0.88f),
                               color(0.28f, 0.76f, 1.55f, 0.0f),
                               Json::array(),
                               7211u);
    configureHealAtlas(shimmer);
    shimmer["source"]["height"] = body_height * 0.94f;
    shimmer["source"]["outer_radius"] = 1.06f * radius;
    shimmer["source"]["jitter_radius"] = 0.025f * radius;
    shimmer["source"]["radial_speed_min"] = 0.005f * radius;
    shimmer["source"]["radial_speed_max"] = 0.09f * radius;
    shimmer["motion"]["velocity_min"] = vec3(-0.10f * radius, 0.18f * radius, -0.10f * radius);
    shimmer["motion"]["velocity_max"] = vec3(0.10f * radius, 0.74f * radius, 0.10f * radius);
    shimmer["motion"]["acceleration"] = vec3(0.0f, 0.040f * radius, 0.0f);
    shimmer["motion"]["drag"] = 0.42f;
    shimmer["motion"]["orbit_speed"] = 0.92f;
    shimmer["rotation"]["angular_velocity_min"] = -2.4f;
    shimmer["rotation"]["angular_velocity_max"] = 2.4f;
    shimmer["size"]["curve_exponent"] = 0.54f;
    shimmer["color"]["alpha_curve_exponent"] = 1.44f;

    Json glints = makeEmitter("heal_star_glints",
                              texture_key("heal_spark_atlas"),
                              "additive",
                              "cylinder",
                              150,
                              42,
                              24.0f,
                              0.32f,
                              0.82f,
                              0.105f * radius,
                              0.300f * radius,
                              0.020f * radius,
                              0.075f * radius,
                              color(1.65f, 2.10f, 2.95f, 0.88f),
                              color(0.34f, 0.88f, 1.85f, 0.0f),
                              Json::array(),
                              7212u);
    configureHealAtlas(glints);
    glints["source"]["height"] = body_height * 0.86f;
    glints["source"]["outer_radius"] = 1.22f * radius;
    glints["source"]["jitter_radius"] = 0.02f * radius;
    glints["source"]["radial_speed_min"] = 0.01f * radius;
    glints["source"]["radial_speed_max"] = 0.13f * radius;
    glints["motion"]["velocity_min"] = vec3(-0.12f * radius, 0.08f * radius, -0.12f * radius);
    glints["motion"]["velocity_max"] = vec3(0.12f * radius, 0.42f * radius, 0.12f * radius);
    glints["motion"]["acceleration"] = vec3(0.0f, 0.015f * radius, 0.0f);
    glints["motion"]["drag"] = 0.52f;
    glints["motion"]["orbit_speed"] = -0.75f;
    glints["rotation"]["angular_velocity_min"] = -2.8f;
    glints["rotation"]["angular_velocity_max"] = 2.8f;
    glints["size"]["curve_exponent"] = 0.48f;
    glints["color"]["alpha_curve_exponent"] = 1.70f;

    Json pulse = makeEmitter("heal_ring_pulse",
                             texture_key("heal_spark_atlas"),
                             "additive",
                             "ring",
                             110,
                             48,
                             12.0f,
                             0.22f,
                             0.58f,
                             0.125f * radius,
                             0.360f * radius,
                             0.016f * radius,
                             0.060f * radius,
                             color(1.55f, 2.05f, 2.90f, 0.82f),
                             color(0.30f, 0.90f, 2.00f, 0.0f),
                             Json::array(),
                             7213u);
    configureHealAtlas(pulse);
    configureDazeRingSource(pulse, radius, 0.74f, 1.45f, 0.020f, 0.16f);
    pulse["motion"]["velocity_min"] = vec3(-0.05f * radius, -0.015f * radius, -0.05f * radius);
    pulse["motion"]["velocity_max"] = vec3(0.05f * radius, 0.12f * radius, 0.05f * radius);
    pulse["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    pulse["motion"]["drag"] = 0.70f;
    pulse["motion"]["orbit_speed"] = 1.18f;
    pulse["rotation"]["angular_velocity_min"] = -2.2f;
    pulse["rotation"]["angular_velocity_max"] = 2.2f;
    pulse["size"]["curve_exponent"] = 0.44f;
    pulse["color"]["alpha_curve_exponent"] = 1.74f;

    Json distortion = makeEmitter("heal_column_distortion",
                                  texture_key("heat_atlas"),
                                  "distortion",
                                  "cylinder",
                                  68,
                                  18,
                                  8.0f,
                                  0.40f,
                                  0.95f,
                                  0.26f * radius,
                                  0.52f * radius,
                                  0.72f * radius,
                                  1.20f * radius,
                                  color(1.0f, 1.0f, 1.0f, 0.09f),
                                  color(1.0f, 1.0f, 1.0f, 0.0f),
                                  Json::array(),
                                  7221u);
    distortion["playback"]["local_space"] = true;
    distortion["render"]["soft_particle_distance"] = 0.92f;
    distortion["render"]["distortion_strength"] = 2.4f;
    distortion["source"]["height"] = body_height * 0.90f;
    distortion["source"]["outer_radius"] = 0.95f * radius;
    distortion["source"]["jitter_radius"] = 0.06f * radius;
    distortion["source"]["radial_speed_min"] = 0.00f;
    distortion["source"]["radial_speed_max"] = 0.055f * radius;
    distortion["motion"]["velocity_min"] = vec3(-0.05f * radius, 0.02f * radius, -0.05f * radius);
    distortion["motion"]["velocity_max"] = vec3(0.05f * radius, 0.18f * radius, 0.05f * radius);
    distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    distortion["motion"]["drag"] = 0.58f;
    distortion["motion"]["orbit_speed"] = 0.32f;
    distortion["rotation"]["angular_velocity_min"] = -0.9f;
    distortion["rotation"]["angular_velocity_max"] = 0.9f;
    distortion["size"]["curve_exponent"] = 0.66f;
    distortion["color"]["alpha_curve_exponent"] = 1.62f;

    if (!write_effect("heal_mist.kpeffect",
                      effect_key("mist"),
                      effect(std::move(mist))) ||
        !write_effect("heal_shimmer.kpeffect",
                      effect_key("shimmer"),
                      effect(std::move(shimmer))) ||
        !write_effect("heal_glints.kpeffect",
                      effect_key("glints"),
                      effect(std::move(glints))) ||
        !write_effect("heal_pulse.kpeffect",
                      effect_key("pulse"),
                      effect(std::move(pulse))) ||
        !write_effect("heal_distortion.kpeffect",
                      effect_key("distortion"),
                      effect(std::move(distortion)))) {
      return false;
    }

    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "mist",
                                     effect_key("mist"),
                                     body_center));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "shimmer",
                                     effect_key("shimmer"),
                                     body_center));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "glints",
                                     effect_key("glints"),
                                     body_center));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "pulse",
                                     effect_key("pulse"),
                                     vec3(0.0f, chest_height, 0.0f)));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "distortion",
                                     effect_key("distortion"),
                                     body_center));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "heal_glow",
                                       color(0.32f, 0.82f, 1.0f, 1.0f),
                                       3.2f * radius,
                                       3.5f * radius,
                                       vec3(0.0f, chest_height, 0.0f)));
  } else if (preset == "haste") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/haste/textures/haste_streak_atlas.png",
                     "textures/haste_streak_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/haste/textures/haste_spark_atlas.png",
                     "textures/haste_spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/haste/textures/haste_haze_atlas.png",
                     "textures/haste_haze_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("haste_streak_atlas"),
               "textures/haste_streak_atlas.png");
    addTexture(package_assets, texture_key("haste_spark_atlas"),
               "textures/haste_spark_atlas.png");
    addTexture(package_assets, texture_key("haste_haze_atlas"),
               "textures/haste_haze_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");

    constexpr float kPi = 3.14159265358979323846f;
    const float body_height = 2.55f * radius;
    const Json body_center = variableVec3(0.0f, 0.50f, 0.0f);
    const std::string radius_scale_expression =
        "radius / (" + numberExpr(radius) + ")";
    prefab_variables = Json{
        {"height", Json{{"type", "float"}, {"default", body_height}}},
        {"radius", Json{{"type", "float"}, {"default", radius}}},
        {"duration", Json{{"type", "float"}, {"default", 1.0f}}},
        {"intensity", Json{{"type", "float"}, {"default", 1.0f}}},
        {"color",
         Json{{"type", "color"},
              {"default", Json::array({1.8f, 1.8f, 1.8f, 1.0f})}}},
    };

    nodes.push_back(makeRootNode(name));

    auto add_haste_beam = [&](std::string node_name,
                              Json path,
                              Json start_width,
                              Json end_width,
                              Json start_color,
                              Json end_color,
                              float uv_repeat,
                              float uv_scroll_speed,
                              std::string blend_mode = "additive",
                              float edge_softness = 0.30f,
                              std::string texture_suffix = "haste_streak_atlas") {
      nodes.push_back(makeBeamNode(next_node_id++,
                                   0u,
                                   std::move(node_name),
                                   makeBeam(texture_key(texture_suffix),
                                            std::move(path),
                                            std::move(start_width),
                                            std::move(end_width),
                                            std::move(start_color),
                                            std::move(end_color),
                                            uv_repeat,
                                            uv_scroll_speed,
                                            std::move(blend_mode),
                                            edge_softness,
                                            0u)));
    };

    add_haste_beam("base_speed_ring",
                   makeHasteRingPath(1.16f, 0.05f, 72u, 0.20f, 0.015f),
                   scaledVar("radius", 0.145f),
                   scaledVar("radius", 0.060f),
                   var("color"),
                   color(1.6f, 1.6f, 1.6f, 0.20f),
                   2.4f,
                   -2.10f,
                   "additive",
                   0.42f);
    add_haste_beam("ankle_speed_ring",
                   makeHasteRingPath(0.92f, 0.12f, 64u, 0.72f, 0.020f),
                   scaledVar("radius", 0.085f),
                   scaledVar("radius", 0.035f),
                   color(2.4f, 2.4f, 2.4f, 0.78f),
                   color(1.6f, 1.6f, 1.6f, 0.10f),
                   1.8f,
                   2.30f,
                   "additive",
                   0.34f);
    add_haste_beam("waist_speed_ring",
                   makeHasteRingPath(0.98f, 0.47f, 64u, 1.36f, 0.018f),
                   scaledVar("radius", 0.115f),
                   scaledVar("radius", 0.050f),
                   var("color"),
                   color(1.6f, 1.6f, 1.6f, 0.16f),
                   2.2f,
                   -2.55f,
                   "additive",
                   0.36f);
    add_haste_beam("shoulder_speed_ring",
                   makeHasteRingPath(0.70f, 0.74f, 56u, 2.18f, 0.012f),
                   scaledVar("radius", 0.075f),
                   scaledVar("radius", 0.030f),
                   color(2.3f, 2.3f, 2.3f, 0.68f),
                   color(1.6f, 1.6f, 1.6f, 0.08f),
                   1.5f,
                   2.80f,
                   "additive",
                   0.30f);

    for (uint32_t i = 0u; i < 8u; ++i) {
      const float u = static_cast<float>(i) / 8.0f;
      const float phase = u * kPi * 2.0f + 0.18f;
      const float lean = (0.18f + 0.06f * std::sin(phase * 2.0f)) *
                         (std::cos(phase) >= 0.0f ? 1.0f : -1.0f);
      add_haste_beam("speed_streak_" + std::to_string(i),
                     makeHasteStreakPath(phase,
                                         0.68f + 0.14f * std::sin(phase * 3.0f),
                                         0.04f,
                                         0.50f,
                                         1.03f,
                                         lean),
                     scaledVar("radius", 0.070f + 0.018f * static_cast<float>(i % 3u)),
                     scaledVar("radius", 0.028f),
                     color(2.8f, 2.8f, 2.8f, 0.80f),
                     color(1.8f, 1.8f, 1.8f, 0.0f),
                     1.1f,
                     -3.25f - static_cast<float>(i) * 0.11f,
                     "additive",
                     0.28f);
    }

    add_haste_beam("afterimage_left",
                   makeHasteAfterimagePath(-1.0f, 0.10f, 0.46f, 0.92f, 0.78f),
                   scaledVar("radius", 0.56f),
                   scaledVar("radius", 0.18f),
                   color(1.4f, 1.4f, 1.4f, 0.34f),
                   color(1.2f, 1.2f, 1.2f, 0.0f),
                   0.85f,
                   -0.42f,
                   "alpha",
                   0.76f,
                   "haste_haze_atlas");
    add_haste_beam("afterimage_right",
                   makeHasteAfterimagePath(1.0f, 0.08f, 0.42f, 0.88f, 0.70f),
                   scaledVar("radius", 0.48f),
                   scaledVar("radius", 0.16f),
                   color(1.4f, 1.4f, 1.4f, 0.30f),
                   color(1.2f, 1.2f, 1.2f, 0.0f),
                   0.80f,
                   -0.36f,
                   "alpha",
                   0.72f,
                   "haste_haze_atlas");

    auto body_override = [&](float outer_radius_scale,
                             float jitter_scale,
                             float size_scale) {
      return Json{
          {"emission_scale", var("intensity")},
          {"lifetime_scale", var("duration")},
          {"size_scale",
           expr("(" + radius_scale_expression + ") * (" + numberExpr(size_scale) + ")")},
          {"radius_scale", expr(radius_scale_expression)},
          {"velocity_scale", expr(radius_scale_expression)},
          {"source_height", var("height")},
          {"source_outer_radius", scaledVar("radius", outer_radius_scale)},
          {"source_jitter_radius", scaledVar("radius", jitter_scale)},
      };
    };
    Json ring_override{
        {"emission_scale", var("intensity")},
        {"lifetime_scale", var("duration")},
        {"size_scale", expr(radius_scale_expression)},
        {"radius_scale", expr(radius_scale_expression)},
        {"velocity_scale", expr(radius_scale_expression)},
        {"source_inner_radius", scaledVar("radius", 0.58f)},
        {"source_outer_radius", scaledVar("radius", 1.22f)},
        {"source_jitter_radius", scaledVar("radius", 0.030f)},
    };

    Json speed_streaks = makeEmitter("haste_speed_streaks",
                                     texture_key("haste_streak_atlas"),
                                     "additive",
                                     "cylinder",
                                     360,
                                     132,
                                     118.0f,
                                     0.16f,
                                     0.48f,
                                     0.090f * radius,
                                     0.260f * radius,
                                     0.016f * radius,
                                     0.060f * radius,
                                     color(2.8f, 2.8f, 2.8f, 0.90f),
                                     color(1.8f, 1.8f, 1.8f, 0.0f),
                                     Json::array(),
                                     8201u);
    configureHasteAtlas(speed_streaks);
    speed_streaks["render"]["layer"] = 1u;
    speed_streaks["source"]["height"] = body_height;
    speed_streaks["source"]["outer_radius"] = 1.06f * radius;
    speed_streaks["source"]["jitter_radius"] = 0.025f * radius;
    speed_streaks["source"]["radial_speed_min"] = 0.02f * radius;
    speed_streaks["source"]["radial_speed_max"] = 0.34f * radius;
    speed_streaks["motion"]["velocity_min"] = vec3(-0.22f * radius,
                                                   0.46f * radius,
                                                   -0.62f * radius);
    speed_streaks["motion"]["velocity_max"] = vec3(0.22f * radius,
                                                   1.18f * radius,
                                                   0.20f * radius);
    speed_streaks["motion"]["acceleration"] = vec3(0.0f, 0.12f * radius, 0.0f);
    speed_streaks["motion"]["drag"] = 0.34f;
    speed_streaks["motion"]["orbit_speed"] = 2.85f;
    speed_streaks["rotation"]["angular_velocity_min"] = -5.4f;
    speed_streaks["rotation"]["angular_velocity_max"] = 5.4f;
    speed_streaks["size"]["curve_exponent"] = 0.42f;
    speed_streaks["color"]["alpha_curve_exponent"] = 0.78f;

    Json tick_sparks = makeEmitter("haste_tick_sparks",
                                   texture_key("haste_spark_atlas"),
                                   "additive",
                                   "ring",
                                   250,
                                   66,
                                   54.0f,
                                   0.20f,
                                   0.72f,
                                   0.060f * radius,
                                   0.180f * radius,
                                   0.010f * radius,
                                   0.040f * radius,
                                   color(2.8f, 2.8f, 2.8f, 1.0f),
                                   color(1.8f, 1.8f, 1.8f, 0.0f),
                                   Json::array(),
                                   8211u);
    configureHasteAtlas(tick_sparks);
    configureDazeRingSource(tick_sparks, radius, 0.58f, 1.22f, 0.08f, 0.42f);
    tick_sparks["motion"]["velocity_min"] = vec3(-0.16f * radius,
                                                 0.06f * radius,
                                                 -0.16f * radius);
    tick_sparks["motion"]["velocity_max"] = vec3(0.16f * radius,
                                                 0.48f * radius,
                                                 0.16f * radius);
    tick_sparks["motion"]["acceleration"] = vec3(0.0f, 0.05f * radius, 0.0f);
    tick_sparks["motion"]["drag"] = 0.40f;
    tick_sparks["motion"]["orbit_speed"] = 4.10f;
    tick_sparks["rotation"]["angular_velocity_min"] = -6.2f;
    tick_sparks["rotation"]["angular_velocity_max"] = 6.2f;
    tick_sparks["size"]["curve_exponent"] = 0.50f;
    tick_sparks["color"]["alpha_curve_exponent"] = 0.86f;

    Json afterimage_haze = makeEmitter("haste_afterimage_haze",
                                       texture_key("haste_haze_atlas"),
                                       "alpha",
                                       "cylinder",
                                       190,
                                       44,
                                       28.0f,
                                       0.58f,
                                       1.34f,
                                       0.36f * radius,
                                       0.82f * radius,
                                       0.90f * radius,
                                       1.58f * radius,
                                       color(1.35f, 1.35f, 1.35f, 0.42f),
                                       color(1.2f, 1.2f, 1.2f, 0.0f),
                                       Json::array(),
                                       8221u);
    configureHasteAtlas(afterimage_haze);
    afterimage_haze["render"]["soft_particle_distance"] = 0.85f;
    afterimage_haze["source"]["height"] = body_height * 0.96f;
    afterimage_haze["source"]["outer_radius"] = 0.74f * radius;
    afterimage_haze["source"]["jitter_radius"] = 0.055f * radius;
    afterimage_haze["source"]["radial_speed_min"] = 0.0f;
    afterimage_haze["source"]["radial_speed_max"] = 0.045f * radius;
    afterimage_haze["motion"]["velocity_min"] = vec3(-0.08f * radius,
                                                     0.08f * radius,
                                                     -0.82f * radius);
    afterimage_haze["motion"]["velocity_max"] = vec3(0.08f * radius,
                                                     0.38f * radius,
                                                     -0.22f * radius);
    afterimage_haze["motion"]["acceleration"] = vec3(0.0f, 0.025f * radius, 0.0f);
    afterimage_haze["motion"]["drag"] = 0.58f;
    afterimage_haze["motion"]["orbit_speed"] = 0.48f;
    afterimage_haze["rotation"]["angular_velocity_min"] = -0.60f;
    afterimage_haze["rotation"]["angular_velocity_max"] = 0.60f;
    afterimage_haze["size"]["curve_exponent"] = 0.72f;
    afterimage_haze["color"]["alpha_curve_exponent"] = 0.68f;

    Json distortion = makeEmitter("haste_air_shear",
                                  texture_key("heat_atlas"),
                                  "distortion",
                                  "cylinder",
                                  62,
                                  12,
                                  8.0f,
                                  0.26f,
                                  0.74f,
                                  0.22f * radius,
                                  0.52f * radius,
                                  0.58f * radius,
                                  1.05f * radius,
                                  color(1.0f, 1.0f, 1.0f, 0.12f),
                                  color(1.0f, 1.0f, 1.0f, 0.0f),
                                  Json::array(),
                                  8231u);
    distortion["playback"]["local_space"] = true;
    distortion["render"]["soft_particle_distance"] = 0.92f;
    distortion["render"]["distortion_strength"] = 3.0f;
    distortion["source"]["height"] = body_height * 0.92f;
    distortion["source"]["outer_radius"] = 1.02f * radius;
    distortion["source"]["jitter_radius"] = 0.060f * radius;
    distortion["source"]["radial_speed_min"] = 0.0f;
    distortion["source"]["radial_speed_max"] = 0.075f * radius;
    distortion["motion"]["velocity_min"] = vec3(-0.06f * radius,
                                                0.04f * radius,
                                                -0.18f * radius);
    distortion["motion"]["velocity_max"] = vec3(0.06f * radius,
                                                0.24f * radius,
                                                0.18f * radius);
    distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    distortion["motion"]["drag"] = 0.62f;
    distortion["motion"]["orbit_speed"] = 1.20f;
    distortion["rotation"]["angular_velocity_min"] = -1.5f;
    distortion["rotation"]["angular_velocity_max"] = 1.5f;
    distortion["size"]["curve_exponent"] = 0.64f;
    distortion["color"]["alpha_curve_exponent"] = 0.82f;

    if (!write_effect("haste_speed_streaks.kpeffect",
                      effect_key("speed_streaks"),
                      effect(std::move(speed_streaks))) ||
        !write_effect("haste_tick_sparks.kpeffect",
                      effect_key("tick_sparks"),
                      effect(std::move(tick_sparks))) ||
        !write_effect("haste_afterimage_haze.kpeffect",
                      effect_key("afterimage_haze"),
                      effect(std::move(afterimage_haze))) ||
        !write_effect("haste_distortion.kpeffect",
                      effect_key("distortion"),
                      effect(std::move(distortion)))) {
      return false;
    }

    nodes.push_back(makeParticleNodeWithOverride(next_node_id++,
                                                 0u,
                                                 "speed_streaks",
                                                 effect_key("speed_streaks"),
                                                 body_override(1.06f, 0.025f, 1.0f),
                                                 body_center));
    nodes.push_back(makeParticleNodeWithOverride(next_node_id++,
                                                 0u,
                                                 "tick_sparks",
                                                 effect_key("tick_sparks"),
                                                 std::move(ring_override),
                                                 variableVec3(0.0f, 0.48f, 0.0f)));
    nodes.push_back(makeParticleNodeWithOverride(next_node_id++,
                                                 0u,
                                                 "afterimage_haze",
                                                 effect_key("afterimage_haze"),
                                                 body_override(0.74f, 0.055f, 1.08f),
                                                 body_center));
    nodes.push_back(makeParticleNodeWithOverride(next_node_id++,
                                                 0u,
                                                 "distortion",
                                                 effect_key("distortion"),
                                                 body_override(1.02f, 0.060f, 1.0f),
                                                 body_center));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "haste_glow",
                                       var("color"),
                                       expr("intensity * 3.4"),
                                       scaledVar("radius", 3.6f),
                                       variableVec3(0.0f, 0.54f, 0.0f)));
  } else if (preset == "detect_magic") {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/glow_atlas.png",
                     "textures/glow_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                     "textures/spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/smoke_atlas.png",
                     "textures/smoke_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/heat_atlas.png",
                     "textures/heat_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/detect_magic/textures/pixie_dust_atlas.png",
                     "textures/pixie_dust_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("glow_atlas"), "textures/glow_atlas.png");
    addTexture(package_assets, texture_key("spark_atlas"), "textures/spark_atlas.png");
    addTexture(package_assets, texture_key("smoke_atlas"), "textures/smoke_atlas.png");
    addTexture(package_assets, texture_key("heat_atlas"), "textures/heat_atlas.png");
    addTexture(package_assets, texture_key("pixie_dust_atlas"), "textures/pixie_dust_atlas.png");
    const std::string interior_material_key =
        asset_namespace + "/detect_magic_interior_volume";
    const std::string surface_material_key =
        asset_namespace + "/detect_magic_surface_volume";
    if (!writeTextFile(output_dir / "shaders" / "detect_magic_volume_vs.hlsl",
                       kDetectMagicVolumeVertexShader,
                       diagnostic) ||
        !writeTextFile(output_dir / "shaders" / "detect_magic_interior_ps.hlsl",
                       std::string(kDetectMagicVolumePixelHeader) +
                           std::string(kDetectMagicInteriorPixelShader),
                       diagnostic) ||
        !writeTextFile(output_dir / "shaders" / "detect_magic_surface_ps.hlsl",
                       std::string(kDetectMagicVolumePixelHeader) +
                           std::string(kDetectMagicSurfacePixelShader),
                       diagnostic) ||
        !writeJson(output_dir / "materials" / "detect_magic_interior.mat",
                   makeDetectMagicVolumeMaterial("../shaders/detect_magic_interior_ps.hlsl",
                                                 1.0f),
                   diagnostic) ||
        !writeJson(output_dir / "materials" / "detect_magic_surface.mat",
                   makeDetectMagicVolumeMaterial("../shaders/detect_magic_surface_ps.hlsl",
                                                 1.0f),
                   diagnostic)) {
      return false;
    }
    addMaterial(package_assets,
                interior_material_key,
                "materials/detect_magic_interior.mat");
    addMaterial(package_assets,
                surface_material_key,
                "materials/detect_magic_surface.mat");

    const std::string radius_scale_expression =
        "radius / (" + numberExpr(radius) + ")";
    prefab_variables = Json{
        {"radius", Json{{"type", "float"}, {"default", radius}}},
        {"duration", Json{{"type", "float"}, {"default", 1.0f}}},
        {"intensity", Json{{"type", "float"}, {"default", 1.0f}}},
        {"color",
         Json{{"type", "color"},
              {"default", Json::array({0.92f, 0.98f, 1.0f, 0.12f})}}},
    };

    nodes.push_back(makeRootNode(name));
    nodes.push_back(makeVolumetricNode(next_node_id++,
                                       0u,
                                       "shimmer_volume",
                                       vec3(0.0f, 0.92f, 0.0f),
                                       var("radius"),
                                       interior_material_key,
                                       surface_material_key,
                                       true));

    auto area_override = [&](float outer_radius_scale,
                             float height_scale,
                             float jitter_scale,
                             float size_scale,
                             float velocity_scale) {
      return Json{
          {"emission_scale", var("intensity")},
          {"lifetime_scale", var("duration")},
          {"size_scale",
           expr("(" + radius_scale_expression + ") * (" + numberExpr(size_scale) + ")")},
          {"radius_scale", expr(radius_scale_expression)},
          {"velocity_scale",
           expr("(" + radius_scale_expression + ") * (" + numberExpr(velocity_scale) + ")")},
          {"source_outer_radius", scaledVar("radius", outer_radius_scale)},
          {"source_height", scaledVar("radius", height_scale)},
          {"source_jitter_radius", scaledVar("radius", jitter_scale)},
      };
    };

    Json swirl = makeEmitter("detect_magic_swirl",
                             texture_key("glow_atlas"),
                             "additive",
                             "ring",
                             720,
                             180,
                             168.0f,
                             1.05f,
                             2.35f,
                             0.16f,
                             0.42f,
                             0.035f,
                             0.095f,
                             color(1.45f, 1.65f, 2.0f, 0.42f),
                             color(0.88f, 0.96f, 1.0f, 0.0f),
                             Json::array(),
                             9301u);
    configureDazeAtlas(swirl);
    configureDazeRingSource(swirl, radius, 0.18f, 1.0f, 0.01f, 0.10f);
    swirl["render"]["layer"] = 1u;
    swirl["source"]["jitter_radius"] = 0.018f * radius;
    swirl["motion"]["velocity_min"] = vec3(-0.018f * radius,
                                           0.010f * radius,
                                           -0.018f * radius);
    swirl["motion"]["velocity_max"] = vec3(0.018f * radius,
                                           0.050f * radius,
                                           0.018f * radius);
    swirl["motion"]["acceleration"] = vec3(0.0f, 0.004f * radius, 0.0f);
    swirl["motion"]["drag"] = 0.36f;
    swirl["motion"]["orbit_speed"] = 0.46f;
    swirl["rotation"]["angular_velocity_min"] = -1.4f;
    swirl["rotation"]["angular_velocity_max"] = 1.4f;
    swirl["size"]["curve_exponent"] = 0.72f;
    swirl["color"]["alpha_curve_exponent"] = 1.18f;

    Json pixie_dust = makeEmitter("detect_magic_pixie_dust",
                                  texture_key("pixie_dust_atlas"),
                                  "additive",
                                  "sphere",
                                  2200,
                                  360,
                                  180.0f,
                                  3.20f,
                                  6.00f,
                                  0.075f,
                                  0.240f,
                                  0.014f,
                                  0.055f,
                                  color(1.60f, 1.38f, 0.82f, 0.55f),
                                  color(0.95f, 0.78f, 0.42f, 0.0f),
                                  Json::array(),
                                  9311u);
    configurePixieDustAtlas(pixie_dust);
    pixie_dust["render"]["layer"] = 0u;
    pixie_dust["source"]["radius_min"] = 0.02f * radius;
    pixie_dust["source"]["radius_max"] = 1.0f * radius;
    pixie_dust["source"]["jitter_radius"] = 0.008f * radius;
    pixie_dust["source"]["radial_speed_min"] = 0.000f * radius;
    pixie_dust["source"]["radial_speed_max"] = 0.018f * radius;
    pixie_dust["motion"]["velocity_min"] = vec3(-0.012f * radius,
                                                0.003f * radius,
                                                -0.012f * radius);
    pixie_dust["motion"]["velocity_max"] = vec3(0.012f * radius,
                                                0.035f * radius,
                                                0.012f * radius);
    pixie_dust["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    pixie_dust["motion"]["drag"] = 0.42f;
    pixie_dust["motion"]["orbit_speed"] = 0.24f;
    pixie_dust["rotation"]["angular_velocity_min"] = -1.2f;
    pixie_dust["rotation"]["angular_velocity_max"] = 1.2f;
    pixie_dust["size"]["curve_exponent"] = 0.62f;
    pixie_dust["color"]["alpha_curve_exponent"] = 1.35f;

    Json mist = makeEmitter("detect_magic_mist",
                            texture_key("smoke_atlas"),
                            "alpha",
                            "cylinder",
                            420,
                            70,
                            48.0f,
                            2.20f,
                            4.80f,
                            0.28f,
                            0.70f,
                            0.72f,
                            1.55f,
                            color(0.82f, 0.90f, 1.0f, 0.12f),
                            color(0.70f, 0.80f, 0.92f, 0.0f),
                            Json::array(),
                            9321u);
    configureDazeAtlas(mist);
    mist["render"]["soft_particle_distance"] = 1.0f;
    mist["source"]["height"] = 0.26f * radius;
    mist["source"]["outer_radius"] = 0.88f * radius;
    mist["source"]["jitter_radius"] = 0.065f * radius;
    mist["source"]["radial_speed_min"] = 0.0f;
    mist["source"]["radial_speed_max"] = 0.018f * radius;
    mist["motion"]["velocity_min"] = vec3(-0.012f * radius,
                                          0.006f * radius,
                                          -0.012f * radius);
    mist["motion"]["velocity_max"] = vec3(0.012f * radius,
                                          0.040f * radius,
                                          0.012f * radius);
    mist["motion"]["acceleration"] = vec3(0.0f, 0.001f * radius, 0.0f);
    mist["motion"]["drag"] = 0.50f;
    mist["motion"]["orbit_speed"] = 0.16f;
    mist["rotation"]["angular_velocity_min"] = -0.22f;
    mist["rotation"]["angular_velocity_max"] = 0.22f;
    mist["size"]["curve_exponent"] = 0.82f;
    mist["color"]["alpha_curve_exponent"] = 1.55f;

    Json shimmer_distortion = makeEmitter("detect_magic_shimmer_distortion",
                                          texture_key("heat_atlas"),
                                          "distortion",
                                          "cylinder",
                                          230,
                                          44,
                                          38.0f,
                                          0.85f,
                                          1.90f,
                                          0.42f,
                                          1.05f,
                                          0.74f,
                                          1.95f,
                                          color(1.0f, 1.0f, 1.0f, 0.10f),
                                          color(1.0f, 1.0f, 1.0f, 0.0f),
                                          Json::array(),
                                          9331u);
    shimmer_distortion["playback"]["local_space"] = true;
    shimmer_distortion["render"]["soft_particle_distance"] = 1.2f;
    shimmer_distortion["render"]["distortion_strength"] = 2.8f;
    shimmer_distortion["source"]["height"] = 0.30f * radius;
    shimmer_distortion["source"]["outer_radius"] = 0.94f * radius;
    shimmer_distortion["source"]["jitter_radius"] = 0.045f * radius;
    shimmer_distortion["source"]["radial_speed_min"] = 0.0f;
    shimmer_distortion["source"]["radial_speed_max"] = 0.030f * radius;
    shimmer_distortion["motion"]["velocity_min"] = vec3(-0.010f * radius,
                                                        0.004f * radius,
                                                        -0.010f * radius);
    shimmer_distortion["motion"]["velocity_max"] = vec3(0.010f * radius,
                                                        0.032f * radius,
                                                        0.010f * radius);
    shimmer_distortion["motion"]["acceleration"] = vec3(0.0f, 0.0f, 0.0f);
    shimmer_distortion["motion"]["drag"] = 0.60f;
    shimmer_distortion["motion"]["orbit_speed"] = 0.34f;
    shimmer_distortion["rotation"]["angular_velocity_min"] = -0.75f;
    shimmer_distortion["rotation"]["angular_velocity_max"] = 0.75f;
    shimmer_distortion["size"]["curve_exponent"] = 0.70f;
    shimmer_distortion["color"]["alpha_curve_exponent"] = 1.25f;

    if (!write_effect("detect_magic_swirl.kpeffect",
                      effect_key("swirl"),
                      effect(std::move(swirl))) ||
        !write_effect("detect_magic_pixie_dust.kpeffect",
                      effect_key("pixie_dust"),
                      effect(std::move(pixie_dust))) ||
        !write_effect("detect_magic_mist.kpeffect",
                      effect_key("mist"),
                      effect(std::move(mist))) ||
        !write_effect("detect_magic_shimmer_distortion.kpeffect",
                      effect_key("shimmer_distortion"),
                      effect(std::move(shimmer_distortion)))) {
      return false;
    }

    auto disabled_particle_node = [](Json node) {
      node["components"]["ParticleEmitterComponent"]["enabled"] = false;
      node["components"]["ParticleEmitterComponent"]["playing"] = false;
      return node;
    };

    nodes.push_back(disabled_particle_node(
        makeParticleNodeWithOverride(next_node_id++,
                                     0u,
                                     "swirl",
                                     effect_key("swirl"),
                                     area_override(1.0f, 0.0f, 0.018f, 1.0f, 1.0f),
                                     vec3(0.0f, 0.08f, 0.0f))));
    nodes.push_back(makeParticleNodeWithOverride(next_node_id++,
                                                 0u,
                                                 "pixie_dust",
                                                 effect_key("pixie_dust"),
                                                 area_override(1.0f, 0.0f, 0.008f, 1.08f, 0.55f),
                                                 vec3(0.0f, 0.92f, 0.0f)));
    nodes.push_back(disabled_particle_node(
        makeParticleNodeWithOverride(next_node_id++,
                                     0u,
                                     "mist",
                                     effect_key("mist"),
                                     area_override(0.88f, 0.26f, 0.065f, 1.0f, 1.0f),
                                     vec3(0.0f, 0.72f, 0.0f))));
    nodes.push_back(disabled_particle_node(
        makeParticleNodeWithOverride(next_node_id++,
                                     0u,
                                     "shimmer_distortion",
                                     effect_key("shimmer_distortion"),
                                     area_override(0.94f, 0.30f, 0.045f, 1.0f, 1.0f),
                                     vec3(0.0f, 0.92f, 0.0f))));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "detect_magic_glow",
                                       color(0.90f, 0.96f, 1.0f, 1.0f),
                                       0.0f,
                                       scaledVar("radius", 0.70f),
                                       vec3(0.0f, 1.20f, 0.0f)));
  } else if (preset == "energy_orb") {
    const std::string mesh_key = asset_namespace + "/orb_shell";
    if (!copyAssetFile(repo_root,
                       output_dir,
                       "examples/assets/orb_shell.glb",
                       "meshes/orb_shell.glb",
                       "mesh",
                       diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_core_atlas.png",
                     "textures/orb_core_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_arc_atlas.png",
                     "textures/orb_arc_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_halo_atlas.png",
                     "textures/orb_halo_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/energy_orb/textures/orb_distortion_atlas.png",
                     "textures/orb_distortion_atlas.png",
                     diagnostic)) {
      return false;
    }
    addMesh(package_assets, mesh_key, "meshes/orb_shell.glb");
    addTexture(package_assets, texture_key("orb_core_atlas"), "textures/orb_core_atlas.png");
    addTexture(package_assets, texture_key("orb_arc_atlas"), "textures/orb_arc_atlas.png");
    addTexture(package_assets, texture_key("orb_halo_atlas"), "textures/orb_halo_atlas.png");
    addTexture(package_assets,
               texture_key("orb_distortion_atlas"),
               "textures/orb_distortion_atlas.png");

    nodes.push_back(makeRootNode(name));
    nodes.push_back(makeMeshNode(next_node_id++,
                                 0u,
                                 "shell",
                                 mesh_key,
                                 0.45f * radius));
    if (!write_effect("orb_core.kpeffect",
                      effect_key("core"),
                      effect(makeOrbCoreEmitter(texture_key("orb_core_atlas"), radius))) ||
        !write_effect("orb_arcs.kpeffect",
                      effect_key("arcs"),
                      effect(makeOrbArcEmitter(texture_key("orb_arc_atlas"), radius))) ||
        !write_effect("orb_halo.kpeffect",
                      effect_key("halo"),
                      effect(makeOrbHaloEmitter(texture_key("orb_halo_atlas"), radius))) ||
        !write_effect("orb_distortion.kpeffect",
                      effect_key("distortion"),
                      effect(makeOrbDistortionEmitter(texture_key("orb_distortion_atlas"),
                                                      radius)))) {
      return false;
    }
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "core", effect_key("core")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "arcs", effect_key("arcs")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "halo", effect_key("halo")));
    nodes.push_back(makeParticleNode(next_node_id++,
                                     0u,
                                     "distortion",
                                     effect_key("distortion")));
    nodes.push_back(makePointLightNode(next_node_id++,
                                       0u,
                                       "glow",
                                       color(0.42f, 0.86f, 1.0f, 1.0f),
                                       7.0f * radius,
                                       4.0f * radius));
  } else {
    if (!copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/glow_atlas.png",
                     "textures/glow_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                     "textures/spark_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/smoke_atlas.png",
                     "textures/smoke_atlas.png",
                     diagnostic) ||
        !copyTexture(repo_root,
                     output_dir,
                     "examples/assets/prefabs/explosion/textures/shock_ring_atlas.png",
                     "textures/shock_ring_atlas.png",
                     diagnostic)) {
      return false;
    }
    addTexture(package_assets, texture_key("glow_atlas"), "textures/glow_atlas.png");
    addTexture(package_assets, texture_key("spark_atlas"), "textures/spark_atlas.png");
    addTexture(package_assets, texture_key("smoke_atlas"), "textures/smoke_atlas.png");
    addTexture(package_assets, texture_key("shock_ring_atlas"), "textures/shock_ring_atlas.png");

    nodes.push_back(makeRootNode(name));
    if (!write_effect("impact_flash.kpeffect",
                      effect_key("flash"),
                      effect(makeEmitter("impact_flash",
                                         texture_key("glow_atlas"),
                                         "additive",
                                         "sphere",
                                         36,
                                         36,
                                         0.0f,
                                         0.12f,
                                         0.28f,
                                         0.28f,
                                         0.52f,
                                         0.04f,
                                         0.08f,
                                         color(1.0f, 0.9f, 0.42f, 1.0f),
                                         color(1.0f, 0.28f, 0.05f, 0.0f),
                                         Json::array(),
                                         31u))) ||
        !write_effect("impact_sparks.kpeffect",
                      effect_key("sparks"),
                      effect(makeEmitter("impact_sparks",
                                         texture_key("spark_atlas"),
                                         "additive",
                                         "sphere_surface",
                                         180,
                                         80,
                                         0.0f,
                                         0.26f,
                                         0.82f,
                                         0.04f,
                                         0.10f,
                                         0.004f,
                                         0.02f,
                                         color(1.0f, 0.78f, 0.28f, 1.0f),
                                         color(1.0f, 0.08f, 0.0f, 0.0f),
                                         Json::array(),
                                         32u))) ||
        !write_effect("impact_smoke.kpeffect",
                      effect_key("smoke"),
                      effect(makeEmitter("impact_smoke",
                                         texture_key("smoke_atlas"),
                                         "alpha",
                                         "sphere",
                                         96,
                                         24,
                                         4.0f,
                                         0.9f,
                                         1.8f,
                                         0.28f,
                                         0.52f,
                                         0.8f,
                                         1.25f,
                                         color(0.32f, 0.30f, 0.28f, 0.42f),
                                         color(0.12f, 0.12f, 0.12f, 0.0f),
                                         Json::array(),
                                         33u)))) {
      return false;
    }
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "flash", effect_key("flash")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "sparks", effect_key("sparks")));
    nodes.push_back(makeParticleNode(next_node_id++, 0u, "smoke", effect_key("smoke")));
  }

  if (!writeJson(output_dir / "assets.package.json", packageManifest(package_assets), diagnostic)) {
    return false;
  }
  Json prefab{{"version", 2}, {"root", 0}, {"nodes", std::move(nodes)}};
  if (!prefab_variables.empty()) {
    prefab["variables"] = std::move(prefab_variables);
  }
  if (!writeJson(output_dir / "prefab.json", prefab, diagnostic)) {
    return false;
  }
  if (!validateGeneratedEffects(output_dir, effect_paths, diagnostic)) {
    return false;
  }
  return true;
}

}  // namespace karma::tools::particles
