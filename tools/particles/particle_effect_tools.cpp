#include "particle_effect_tools.h"

#include "karma/assets.h"
#include "karma/visual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
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
  if (preset == "breathe_fire") {
    return "Generated Breathe Fire";
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
                        float intensity,
                        float range,
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
                {"intensity", intensity},
                {"range", range},
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
              float start_width,
              float end_width,
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

Json makeChromaticRayPath(float length,
                          float y_offset = 0.0f,
                          float z_offset = 0.0f) {
  constexpr float kDefaultChromaticLength = 6.4f;
  const float scale = std::max(length, 0.25f) / kDefaultChromaticLength;
  auto point = [scale, y_offset, z_offset](float x, float y, float z) {
    return vec3(x * scale, y * scale + y_offset, z * scale + z_offset);
  };

  return Json::array({
      point(-3.15f, 0.72f, 0.00f),
      point(-1.35f, 0.98f, 0.04f),
      point(0.85f, 1.38f, -0.06f),
      point(3.18f, 1.88f, 0.00f),
  });
}

Json makeChromaticHelixPath(float length,
                            float strand_radius,
                            float phase,
                            float turns = 2.35f,
                            std::size_t segments = 22u) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kDefaultChromaticLength = 6.4f;
  constexpr std::array<std::array<float, 3>, 4> kBasePoints{{
      {-3.15f, 0.72f, 0.00f},
      {-1.35f, 0.98f, 0.04f},
      {0.85f, 1.38f, -0.06f},
      {3.18f, 1.88f, 0.00f},
  }};

  const float scale = std::max(length, 0.25f) / kDefaultChromaticLength;
  Json path = Json::array();
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(segments);
    const float segment_position = u * static_cast<float>(kBasePoints.size() - 1u);
    const std::size_t segment =
        std::min<std::size_t>(static_cast<std::size_t>(segment_position),
                              kBasePoints.size() - 2u);
    const float t = segment_position - static_cast<float>(segment);
    const auto& a = kBasePoints[segment];
    const auto& b = kBasePoints[segment + 1u];
    const float x = (a[0] + (b[0] - a[0]) * t) * scale;
    const float y = (a[1] + (b[1] - a[1]) * t) * scale;
    const float z = (a[2] + (b[2] - a[2]) * t) * scale;

    const float angle = phase + u * turns * kPi * 2.0f;
    const float tapered_orbit = strand_radius * (0.54f + 0.46f * std::sin(u * kPi));
    path.push_back(vec3(x,
                        y + std::cos(angle) * tapered_orbit,
                        z + std::sin(angle) * tapered_orbit * 0.54f));
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
      preset != "breathe_fire" &&
      preset != "impact_burst" && preset != "energy_orb") {
    return fail(diagnostic,
                "preset must be one of: fire_ray, magic_missile, "
                "arcane_barrage, blade_barrier, chromatic_ray, daze, "
                "heal, breathe_fire, impact_burst, energy_orb");
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
  const float default_radius =
      preset == "blade_barrier" ? 1.65f
                                     : ((preset == "daze" || preset == "heal")
                                            ? 1.55f
                                            : (preset == "breathe_fire" ? 1.35f : 1.0f));
  const float radius = std::clamp(readFloat(*spec, "radius", default_radius), 0.2f, 6.0f);
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
      const float strand_radius = (0.32f + 0.030f * static_cast<float>(i % 3u)) * radius;
      const Json thread_path =
          makeChromaticHelixPath(length, strand_radius, phase, 2.45f, 24u);
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
                                  0.72f * radius,
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
                                  0.54f * radius,
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
                                  0.62f * radius,
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
                                  0.38f * radius,
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
                                    0.48f * radius,
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
                                    0.60f * radius,
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
  Json prefab{{"version", 1}, {"root", 0}, {"nodes", std::move(nodes)}};
  if (!writeJson(output_dir / "prefab.json", prefab, diagnostic)) {
    return false;
  }
  if (!validateGeneratedEffects(output_dir, effect_paths, diagnostic)) {
    return false;
  }
  return true;
}

}  // namespace karma::tools::particles
