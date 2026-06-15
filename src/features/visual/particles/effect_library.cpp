#include "karma/features/visual/particles/effect_library.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace karma::particles {

namespace {

using Json = nlohmann::json;

bool keyAllowed(std::string_view key, std::initializer_list<std::string_view> allowed) {
  return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

bool rejectUnknownFields(const Json& object,
                         std::initializer_list<std::string_view> allowed,
                         std::string_view context,
                         std::string& out_error) {
  if (!object.is_object()) {
    out_error = std::string(context) + " must be an object";
    return false;
  }
  for (const auto& [key, value] : object.items()) {
    (void)value;
    if (!keyAllowed(key, allowed)) {
      out_error = std::string(context) + " has unknown field '" + key + "'";
      return false;
    }
  }
  return true;
}

bool readBool(const Json& object, std::string_view key, bool& out_value, std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_boolean()) {
    out_error = "field '" + std::string(key) + "' must be a boolean";
    return false;
  }
  out_value = it->get<bool>();
  return true;
}

bool readFloatValue(const Json& value, float& out_value) {
  if (!value.is_number()) {
    return false;
  }
  out_value = value.get<float>();
  return true;
}

bool readFloat(const Json& object, std::string_view key, float& out_value, std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!readFloatValue(*it, out_value)) {
    out_error = "field '" + std::string(key) + "' must be a number";
    return false;
  }
  return true;
}

bool readUint32(const Json& object,
                std::string_view key,
                uint32_t& out_value,
                std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number_unsigned() && !it->is_number_integer()) {
    out_error = "field '" + std::string(key) + "' must be an unsigned integer";
    return false;
  }
  const int64_t value = it->get<int64_t>();
  if (value < 0 || value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
    out_error = "field '" + std::string(key) + "' is outside uint32 range";
    return false;
  }
  out_value = static_cast<uint32_t>(value);
  return true;
}

bool readString(const Json& object,
                std::string_view key,
                std::string& out_value,
                std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  out_value = it->get<std::string>();
  return true;
}

bool readVec3Value(const Json& value, math::Vec3& out_value) {
  if (!value.is_array() || value.size() != 3u) {
    return false;
  }
  return readFloatValue(value[0], out_value.x) &&
         readFloatValue(value[1], out_value.y) &&
         readFloatValue(value[2], out_value.z);
}

bool readVec3(const Json& object,
              std::string_view key,
              math::Vec3& out_value,
              std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!readVec3Value(*it, out_value)) {
    out_error = "field '" + std::string(key) + "' must be a 3-number array";
    return false;
  }
  return true;
}

bool readColorValue(const Json& value, math::Color& out_value) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatValue(value[0], out_value.r) &&
         readFloatValue(value[1], out_value.g) &&
         readFloatValue(value[2], out_value.b) &&
         readFloatValue(value[3], out_value.a);
}

bool readColor(const Json& object,
               std::string_view key,
               math::Color& out_value,
               std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!readColorValue(*it, out_value)) {
    out_error = "field '" + std::string(key) + "' must be a 4-number array";
    return false;
  }
  return true;
}

bool readBlendMode(const Json& object,
                   std::string_view key,
                   components::ParticleBlendMode& out_value,
                   std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "additive") {
    out_value = components::ParticleBlendMode::Additive;
    return true;
  }
  if (value == "alpha") {
    out_value = components::ParticleBlendMode::Alpha;
    return true;
  }
  if (value == "distortion") {
    out_value = components::ParticleBlendMode::Distortion;
    return true;
  }
  out_error = "field '" + std::string(key) + "' has invalid blend mode '" + value + "'";
  return false;
}

bool readAlignment(const Json& object,
                   std::string_view key,
                   components::ParticleAlignment& out_value,
                   std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "billboard") {
    out_value = components::ParticleAlignment::Billboard;
    return true;
  }
  if (value == "ground") {
    out_value = components::ParticleAlignment::Ground;
    return true;
  }
  out_error = "field '" + std::string(key) + "' has invalid alignment '" + value + "'";
  return false;
}

bool readShadingMode(const Json& object,
                     std::string_view key,
                     components::ParticleShadingMode& out_value,
                     std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "standard") {
    out_value = components::ParticleShadingMode::Standard;
    return true;
  }
  if (value == "shell") {
    out_value = components::ParticleShadingMode::Shell;
    return true;
  }
  out_error = "field '" + std::string(key) + "' has invalid shading mode '" + value + "'";
  return false;
}

bool readSourceShape(const Json& object,
                     std::string_view key,
                     components::ParticleSourceShape& out_value,
                     std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "box") {
    out_value = components::ParticleSourceShape::Box;
    return true;
  }
  if (value == "sphere") {
    out_value = components::ParticleSourceShape::Sphere;
    return true;
  }
  if (value == "sphere_surface") {
    out_value = components::ParticleSourceShape::SphereSurface;
    return true;
  }
  if (value == "disc") {
    out_value = components::ParticleSourceShape::Disc;
    return true;
  }
  if (value == "ring") {
    out_value = components::ParticleSourceShape::Ring;
    return true;
  }
  if (value == "cylinder") {
    out_value = components::ParticleSourceShape::Cylinder;
    return true;
  }
  if (value == "capsule") {
    out_value = components::ParticleSourceShape::Capsule;
    return true;
  }
  if (value == "cone") {
    out_value = components::ParticleSourceShape::Cone;
    return true;
  }
  if (value == "line") {
    out_value = components::ParticleSourceShape::Line;
    return true;
  }
  if (value == "path") {
    out_value = components::ParticleSourceShape::Path;
    return true;
  }
  if (value == "trail_path") {
    out_value = components::ParticleSourceShape::TrailPath;
    return true;
  }
  if (value == "mesh_surface") {
    out_value = components::ParticleSourceShape::MeshSurface;
    return true;
  }
  out_error = "field '" + std::string(key) + "' has invalid source shape '" + value + "'";
  return false;
}

bool readSourceSampling(const Json& object,
                        std::string_view key,
                        components::ParticleSourceSamplingMode& out_value,
                        std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "random") {
    out_value = components::ParticleSourceSamplingMode::Random;
    return true;
  }
  if (value == "sequential") {
    out_value = components::ParticleSourceSamplingMode::Sequential;
    return true;
  }
  if (value == "vertices") {
    out_value = components::ParticleSourceSamplingMode::Vertices;
    return true;
  }
  out_error = "field '" + std::string(key) + "' has invalid source sampling mode '" + value + "'";
  return false;
}

bool readSourceDistribution(const Json& object,
                            std::string_view key,
                            components::ParticleSourceDistribution& out_value,
                            std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    out_error = "field '" + std::string(key) + "' must be a string";
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "uniform") {
    out_value = components::ParticleSourceDistribution::Uniform;
    return true;
  }
  if (value == "surface") {
    out_value = components::ParticleSourceDistribution::Surface;
    return true;
  }
  if (value == "edge") {
    out_value = components::ParticleSourceDistribution::Edge;
    return true;
  }
  out_error = "field '" + std::string(key) + "' has invalid source distribution '" + value + "'";
  return false;
}

bool readVec3Array(const Json& object,
                   std::string_view key,
                   std::vector<math::Vec3>& out_value,
                   std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    out_error = "field '" + std::string(key) + "' must be an array of 3-number arrays";
    return false;
  }
  out_value.clear();
  out_value.reserve(it->size());
  for (const Json& point_json : *it) {
    math::Vec3 point{};
    if (!readVec3Value(point_json, point)) {
      out_error = "field '" + std::string(key) + "' must be an array of 3-number arrays";
      return false;
    }
    out_value.push_back(point);
  }
  return true;
}

const Json* requiredBlock(const Json& object, std::string_view key, std::string& out_error) {
  const auto it = object.find(key);
  if (it == object.end()) {
    out_error = "emitter is missing required block '" + std::string(key) + "'";
    return nullptr;
  }
  if (!it->is_object()) {
    out_error = "block '" + std::string(key) + "' must be an object";
    return nullptr;
  }
  return &*it;
}

bool parseEmitter(const Json& json, ParticleEmitterDesc& out_desc, std::string& out_error) {
  if (!rejectUnknownFields(json,
                           {"name",
                            "texture",
                            "playback",
                            "render",
                            "atlas",
                            "emission",
                            "lifetime",
                            "size",
                            "rotation",
                            "source",
                            "motion",
                            "collision",
                            "color"},
                           "emitter",
                           out_error)) {
    return false;
  }

  if (const auto texture_it = json.find("texture"); texture_it != json.end()) {
    if (!texture_it->is_string()) {
      out_error = "field 'texture' must be a string";
      return false;
    }
    out_desc.texture_key = texture_it->get<std::string>();
  }

  auto& emitter = out_desc.emitter;

  const Json* playback = requiredBlock(json, "playback", out_error);
  const Json* render = requiredBlock(json, "render", out_error);
  const Json* atlas = requiredBlock(json, "atlas", out_error);
  const Json* emission = requiredBlock(json, "emission", out_error);
  const Json* lifetime = requiredBlock(json, "lifetime", out_error);
  const Json* size = requiredBlock(json, "size", out_error);
  const Json* rotation = requiredBlock(json, "rotation", out_error);
  const Json* source = requiredBlock(json, "source", out_error);
  const Json* motion = requiredBlock(json, "motion", out_error);
  const Json* collision = requiredBlock(json, "collision", out_error);
  const Json* color = requiredBlock(json, "color", out_error);
  if (!playback || !render || !atlas || !emission || !lifetime || !size || !rotation ||
      !source || !motion || !collision || !color) {
    return false;
  }
  if (source->find("shape") == source->end()) {
    out_error = "source is missing required field 'shape'";
    return false;
  }

  if (!rejectUnknownFields(*playback,
                           {"enabled",
                            "playing",
                            "loop",
                            "emit_burst_on_start",
                            "local_space",
                            "time_scale",
                            "start_delay",
                            "duration"},
                           "playback",
                           out_error) ||
      !readBool(*playback, "enabled", emitter.enabled, out_error) ||
      !readBool(*playback, "playing", emitter.playing, out_error) ||
      !readBool(*playback, "loop", emitter.loop, out_error) ||
      !readBool(*playback, "emit_burst_on_start", emitter.emit_burst_on_start, out_error) ||
      !readBool(*playback, "local_space", emitter.local_space, out_error) ||
      !readFloat(*playback, "time_scale", emitter.time_scale, out_error) ||
      !readFloat(*playback, "start_delay", emitter.start_delay, out_error) ||
      !readFloat(*playback, "duration", emitter.duration, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*render,
                           {"layer",
                            "depth_test",
                            "blend_mode",
                            "alignment",
                            "shading_mode",
                            "use_soft_mask",
                            "soft_particle_distance",
                            "distortion_strength",
                            "fresnel_power",
                            "fresnel_strength",
                            "refraction_strength",
                            "interior_glow"},
                           "render",
                           out_error) ||
      !readUint32(*render, "layer", emitter.layer, out_error) ||
      !readBool(*render, "depth_test", emitter.depth_test, out_error) ||
      !readBlendMode(*render, "blend_mode", emitter.blend_mode, out_error) ||
      !readAlignment(*render, "alignment", emitter.alignment, out_error) ||
      !readShadingMode(*render, "shading_mode", emitter.shading_mode, out_error) ||
      !readBool(*render, "use_soft_mask", emitter.use_soft_mask, out_error) ||
      !readFloat(*render, "soft_particle_distance", emitter.soft_particle_distance, out_error) ||
      !readFloat(*render, "distortion_strength", emitter.distortion_strength, out_error) ||
      !readFloat(*render, "fresnel_power", emitter.fresnel_power, out_error) ||
      !readFloat(*render, "fresnel_strength", emitter.fresnel_strength, out_error) ||
      !readFloat(*render, "refraction_strength", emitter.refraction_strength, out_error) ||
      !readFloat(*render, "interior_glow", emitter.interior_glow, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*atlas,
                           {"columns",
                            "rows",
                            "frame_count",
                            "frame_width",
                            "frame_height",
                            "border_x",
                            "border_y",
                            "spacing_x",
                            "spacing_y",
                            "animation_fps",
                            "animate_over_lifetime",
                            "random_start_frame"},
                           "atlas",
                           out_error) ||
      !readUint32(*atlas, "columns", emitter.atlas_columns, out_error) ||
      !readUint32(*atlas, "rows", emitter.atlas_rows, out_error) ||
      !readUint32(*atlas, "frame_count", emitter.atlas_frame_count, out_error) ||
      !readUint32(*atlas, "frame_width", emitter.atlas_frame_width, out_error) ||
      !readUint32(*atlas, "frame_height", emitter.atlas_frame_height, out_error) ||
      !readUint32(*atlas, "border_x", emitter.atlas_border_x, out_error) ||
      !readUint32(*atlas, "border_y", emitter.atlas_border_y, out_error) ||
      !readUint32(*atlas, "spacing_x", emitter.atlas_spacing_x, out_error) ||
      !readUint32(*atlas, "spacing_y", emitter.atlas_spacing_y, out_error) ||
      !readFloat(*atlas, "animation_fps", emitter.animation_fps, out_error) ||
      !readBool(*atlas, "animate_over_lifetime", emitter.animate_over_lifetime, out_error) ||
      !readBool(*atlas, "random_start_frame", emitter.random_start_frame, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*emission,
                           {"max_particles", "burst_count", "seed", "spawn_rate"},
                           "emission",
                           out_error) ||
      !readUint32(*emission, "max_particles", emitter.max_particles, out_error) ||
      !readUint32(*emission, "burst_count", emitter.burst_count, out_error) ||
      !readUint32(*emission, "seed", emitter.seed, out_error) ||
      !readFloat(*emission, "spawn_rate", emitter.spawn_rate, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*lifetime, {"min", "max"}, "lifetime", out_error) ||
      !readFloat(*lifetime, "min", emitter.particle_lifetime_min, out_error) ||
      !readFloat(*lifetime, "max", emitter.particle_lifetime_max, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*size,
                           {"start_min", "start_max", "end_min", "end_max", "curve_exponent"},
                           "size",
                           out_error) ||
      !readFloat(*size, "start_min", emitter.start_size_min, out_error) ||
      !readFloat(*size, "start_max", emitter.start_size_max, out_error) ||
      !readFloat(*size, "end_min", emitter.end_size_min, out_error) ||
      !readFloat(*size, "end_max", emitter.end_size_max, out_error) ||
      !readFloat(*size, "curve_exponent", emitter.size_curve_exponent, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*rotation,
                           {"initial_min", "initial_max", "angular_velocity_min",
                            "angular_velocity_max"},
                           "rotation",
                           out_error) ||
      !readFloat(*rotation, "initial_min", emitter.initial_rotation_min, out_error) ||
      !readFloat(*rotation, "initial_max", emitter.initial_rotation_max, out_error) ||
      !readFloat(*rotation, "angular_velocity_min", emitter.angular_velocity_min, out_error) ||
      !readFloat(*rotation, "angular_velocity_max", emitter.angular_velocity_max, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*source,
                           {"shape",
                            "box_extents",
                            "dimensions",
                            "radius_min",
                            "radius_max",
                            "inner_radius",
                            "outer_radius",
                            "height",
                            "angle",
                            "points",
                            "closed_loop",
                            "sampling",
                            "jitter_radius",
                            "mesh_key",
                            "mesh_path",
                            "distribution",
                            "radial_speed_min",
                            "radial_speed_max"},
                           "source",
                           out_error) ||
      !readSourceShape(*source, "shape", emitter.source_shape, out_error) ||
      !readVec3(*source, "box_extents", emitter.source_box_extents, out_error) ||
      !readVec3(*source, "dimensions", emitter.source_dimensions, out_error) ||
      !readFloat(*source, "radius_min", emitter.source_radius_min, out_error) ||
      !readFloat(*source, "radius_max", emitter.source_radius_max, out_error) ||
      !readFloat(*source, "inner_radius", emitter.source_inner_radius, out_error) ||
      !readFloat(*source, "outer_radius", emitter.source_outer_radius, out_error) ||
      !readFloat(*source, "height", emitter.source_height, out_error) ||
      !readFloat(*source, "angle", emitter.source_angle, out_error) ||
      !readVec3Array(*source, "points", emitter.source_path_points, out_error) ||
      !readBool(*source, "closed_loop", emitter.source_closed_loop, out_error) ||
      !readSourceSampling(*source, "sampling", emitter.source_sampling, out_error) ||
      !readFloat(*source, "jitter_radius", emitter.source_jitter_radius, out_error) ||
      !readString(*source, "mesh_key", emitter.source_mesh_key, out_error) ||
      !readString(*source, "mesh_path", emitter.source_mesh_path, out_error) ||
      !readSourceDistribution(*source, "distribution", emitter.source_distribution, out_error) ||
      !readFloat(*source, "radial_speed_min", emitter.radial_speed_min, out_error) ||
      !readFloat(*source, "radial_speed_max", emitter.radial_speed_max, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*motion,
                           {"velocity_min", "velocity_max", "acceleration", "drag"},
                           "motion",
                           out_error) ||
      !readVec3(*motion, "velocity_min", emitter.velocity_min, out_error) ||
      !readVec3(*motion, "velocity_max", emitter.velocity_max, out_error) ||
      !readVec3(*motion, "acceleration", emitter.acceleration, out_error) ||
      !readFloat(*motion, "drag", emitter.drag, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*collision,
                           {"ground",
                            "ground_height",
                            "bounce_damping",
                            "friction",
                            "rest_speed_threshold"},
                           "collision",
                           out_error) ||
      !readBool(*collision, "ground", emitter.collide_with_ground, out_error) ||
      !readFloat(*collision, "ground_height", emitter.ground_height, out_error) ||
      !readFloat(*collision, "bounce_damping", emitter.bounce_damping, out_error) ||
      !readFloat(*collision, "friction", emitter.collision_friction, out_error) ||
      !readFloat(*collision, "rest_speed_threshold", emitter.rest_speed_threshold, out_error)) {
    return false;
  }

  if (!rejectUnknownFields(*color, {"start", "end", "alpha_curve_exponent"}, "color", out_error) ||
      !readColor(*color, "start", emitter.start_color, out_error) ||
      !readColor(*color, "end", emitter.end_color, out_error) ||
      !readFloat(*color, "alpha_curve_exponent", emitter.alpha_curve_exponent, out_error)) {
    return false;
  }

  return true;
}

bool parseEffectJson(const Json& json, ParticleEffectAsset& out_asset, std::string& out_error) {
  if (!rejectUnknownFields(json, {"version", "emitters"}, "effect", out_error)) {
    return false;
  }

  const auto version_it = json.find("version");
  if (version_it == json.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 3) {
    out_error = "effect must declare version 3";
    return false;
  }

  const auto emitters_it = json.find("emitters");
  if (emitters_it == json.end() || !emitters_it->is_array() || emitters_it->empty()) {
    out_error = "effect must contain a non-empty 'emitters' array";
    return false;
  }

  ParticleEffectAsset asset{};
  asset.emitters.reserve(emitters_it->size());
  for (const Json& emitter_json : *emitters_it) {
    ParticleEmitterDesc emitter{};
    if (!parseEmitter(emitter_json, emitter, out_error)) {
      return false;
    }
    asset.emitters.push_back(std::move(emitter));
  }

  out_asset = std::move(asset);
  return true;
}

}  // namespace

void ParticleLibrary::registerEffect(const std::string& key, ParticleEffectAsset asset) {
  effects_[key] = std::move(asset);
  version_ += 1;
}

void ParticleLibrary::registerEmitterTemplate(const std::string& key,
                                              components::ParticleEmitterComponent emitter) {
  ParticleEffectAsset asset{};
  asset.emitters.push_back(ParticleEmitterDesc{.emitter = std::move(emitter)});
  registerEffect(key, std::move(asset));
}

bool ParticleLibrary::registerEffectFile(const std::string& key,
                                         const std::filesystem::path& path) {
  EffectFileRecord& record = effect_files_[key];
  record.path = path;
  record.last_write_time = std::filesystem::file_time_type{};
  return reloadEffectFile(key, record);
}

void ParticleLibrary::unregisterEffect(const std::string& key) {
  const size_t erased_effects = effects_.erase(key);
  const size_t erased_files = effect_files_.erase(key);
  if (erased_effects > 0 || erased_files > 0) {
    version_ += 1;
  }
}

void ParticleLibrary::clear() {
  if (!effects_.empty() || !effect_files_.empty()) {
    effects_.clear();
    effect_files_.clear();
    version_ += 1;
  }
}

void ParticleLibrary::registerTextureAlias(const std::string& key,
                                           renderer::TextureId texture) {
  auto it = texture_aliases_.find(key);
  if (it != texture_aliases_.end() && it->second == texture) {
    return;
  }
  texture_aliases_[key] = texture;
  version_ += 1;
}

void ParticleLibrary::registerTextureAliases(
    std::initializer_list<ParticleTextureAliasRegistration> aliases) {
  for (const ParticleTextureAliasRegistration& alias : aliases) {
    registerTextureAlias(std::string(alias.key), alias.texture);
  }
}

void ParticleLibrary::unregisterTextureAlias(const std::string& key) {
  if (texture_aliases_.erase(key) > 0) {
    version_ += 1;
  }
}

void ParticleLibrary::clearTextureAliases() {
  if (!texture_aliases_.empty()) {
    texture_aliases_.clear();
    version_ += 1;
  }
}

renderer::TextureId ParticleLibrary::resolveTextureAlias(const std::string& key) const {
  auto it = texture_aliases_.find(key);
  if (it == texture_aliases_.end()) {
    return renderer::kInvalidTexture;
  }
  return it->second;
}

void ParticleLibrary::registerMeshSourceAlias(const std::string& key,
                                              renderer::MeshId mesh) {
  auto it = mesh_source_aliases_.find(key);
  if (it != mesh_source_aliases_.end() && it->second == mesh) {
    return;
  }
  mesh_source_aliases_[key] = mesh;
  version_ += 1;
}

void ParticleLibrary::registerMeshSourceAliases(
    std::initializer_list<ParticleMeshSourceAliasRegistration> aliases) {
  for (const ParticleMeshSourceAliasRegistration& alias : aliases) {
    registerMeshSourceAlias(std::string(alias.key), alias.mesh);
  }
}

void ParticleLibrary::unregisterMeshSourceAlias(const std::string& key) {
  if (mesh_source_aliases_.erase(key) > 0) {
    version_ += 1;
  }
}

void ParticleLibrary::clearMeshSourceAliases() {
  if (!mesh_source_aliases_.empty()) {
    mesh_source_aliases_.clear();
    version_ += 1;
  }
}

renderer::MeshId ParticleLibrary::resolveMeshSourceAlias(const std::string& key) const {
  auto it = mesh_source_aliases_.find(key);
  if (it == mesh_source_aliases_.end()) {
    return renderer::kInvalidMesh;
  }
  return it->second;
}

bool ParticleLibrary::registerEffectFiles(
    std::initializer_list<ParticleEffectFileRegistration> effects) {
  bool all_ok = true;
  for (const ParticleEffectFileRegistration& effect : effects) {
    all_ok = registerEffectFile(std::string(effect.key), effect.path) && all_ok;
  }
  return all_ok;
}

void ParticleLibrary::update() {
  if (effect_files_.empty()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (next_poll_time_ != std::chrono::steady_clock::time_point{} &&
      now < next_poll_time_) {
    return;
  }
  next_poll_time_ = now + poll_interval_;

  for (auto& [key, record] : effect_files_) {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(record.path, ec);
    if (ec) {
      continue;
    }
    if (record.last_write_time != write_time) {
      reloadEffectFile(key, record);
    }
  }
}

const ParticleEffectAsset* ParticleLibrary::find(const std::string& key) const {
  auto it = effects_.find(key);
  if (it == effects_.end()) {
    return nullptr;
  }
  return &it->second;
}

const components::ParticleEmitterComponent* ParticleLibrary::findEmitterTemplate(
    const std::string& key) const {
  const ParticleEffectAsset* effect = find(key);
  const ParticleEmitterDesc* primary = effect ? effect->primaryEmitter() : nullptr;
  return primary ? &primary->emitter : nullptr;
}

bool ParticleLibrary::instantiateEmitter(const std::string& key,
                                         components::ParticleEmitterComponent& out_emitter) const {
  const ParticleEffectAsset* effect = find(key);
  const ParticleEmitterDesc* primary = effect ? effect->primaryEmitter() : nullptr;
  if (primary == nullptr) {
    return false;
  }
  out_emitter = primary->emitter;
  if (!primary->texture_key.empty()) {
    out_emitter.texture_key = primary->texture_key;
  }
  return true;
}

std::optional<components::ParticleEmitterComponent> ParticleLibrary::instantiateEmitter(
    const std::string& key) const {
  components::ParticleEmitterComponent emitter{};
  if (!instantiateEmitter(key, emitter)) {
    return std::nullopt;
  }
  return emitter;
}

bool ParticleLibrary::reloadEffectFile(const std::string& key, EffectFileRecord& record) {
  ParticleEffectAsset asset{};
  if (!parseEffectFile(record.path, asset)) {
    return false;
  }

  std::error_code ec;
  record.last_write_time = std::filesystem::last_write_time(record.path, ec);
  if (ec) {
    record.last_write_time = std::filesystem::file_time_type{};
  }

  effects_[key] = std::move(asset);
  version_ += 1;
  return true;
}

bool ParticleLibrary::parseEffectFile(const std::filesystem::path& path,
                                      ParticleEffectAsset& out_asset) const {
  std::ifstream stream(path);
  if (!stream) {
    spdlog::error("Failed to open particle effect '{}'", path.string());
    return false;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception& e) {
    spdlog::error("Failed to parse particle effect '{}': {}", path.string(), e.what());
    return false;
  }

  std::string error;
  if (!parseEffectJson(json, out_asset, error)) {
    spdlog::error("Invalid particle effect '{}': {}", path.string(), error);
    return false;
  }

  return true;
}

}  // namespace karma::particles
