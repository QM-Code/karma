#include "prefab_parse_support.h"

namespace karma::prefabs::detail {

bool applyBeamField(Prefab&,
                    PrefabEntry& entry,
                    const std::string& key,
                    const std::string& raw_value,
                    std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (applyColorBindingField(entry.beam.core_color_binding, key, raw_value, "core_color")) {
    return true;
  }
  if (applyColorBindingField(entry.beam.glow_color_binding, key, raw_value, "glow_color")) {
    return true;
  }

  auto& beam = entry.beam.beam;
  if (key == "points") {
    return parseVec3List(raw_value, beam.points);
  }
  if (key == "core_radius") {
    return parseNumber(raw_value, beam.core_radius);
  }
  if (key == "glow_radius") {
    return parseNumber(raw_value, beam.glow_radius);
  }
  if (key == "core_intensity") {
    return parseNumber(raw_value, beam.core_intensity);
  }
  if (key == "glow_intensity") {
    return parseNumber(raw_value, beam.glow_intensity);
  }
  if (key == "endpoint_core_size") {
    return parseNumber(raw_value, beam.endpoint_core_size);
  }
  if (key == "endpoint_glow_size") {
    return parseNumber(raw_value, beam.endpoint_glow_size);
  }
  if (key == "light_count") {
    int value = 0;
    if (!parseNumber(raw_value, value) || value < 0) {
      return false;
    }
    beam.light_count = static_cast<uint32_t>(value);
    return true;
  }
  if (key == "light_intensity") {
    return parseNumber(raw_value, beam.light_intensity);
  }
  if (key == "light_range") {
    return parseNumber(raw_value, beam.light_range);
  }
  if (key == "light_spacing") {
    return parseNumber(raw_value, beam.light_spacing);
  }
  if (key == "electric_intensity") {
    return parseNumber(raw_value, beam.electric_intensity);
  }
  if (key == "electric_size") {
    return parseNumber(raw_value, beam.electric_size);
  }
  if (key == "electric_spacing") {
    return parseNumber(raw_value, beam.electric_spacing);
  }
  if (key == "electric_jitter_radius") {
    return parseNumber(raw_value, beam.electric_jitter_radius);
  }
  if (key == "electric_speed") {
    return parseNumber(raw_value, beam.electric_speed);
  }
  if (key == "distortion_intensity") {
    return parseNumber(raw_value, beam.distortion_intensity);
  }
  if (key == "distortion_size") {
    return parseNumber(raw_value, beam.distortion_size);
  }
  if (key == "distortion_spacing") {
    return parseNumber(raw_value, beam.distortion_spacing);
  }
  if (key == "distortion_jitter_radius") {
    return parseNumber(raw_value, beam.distortion_jitter_radius);
  }
  if (key == "distortion_strength") {
    return parseNumber(raw_value, beam.distortion_strength);
  }
  if (key == "distortion_soft_particle_distance") {
    return parseNumber(raw_value, beam.distortion_soft_particle_distance);
  }
  if (key == "distortion_speed") {
    return parseNumber(raw_value, beam.distortion_speed);
  }
  if (key == "layer") {
    int value = 0;
    if (!parseNumber(raw_value, value) || value < 0) {
      return false;
    }
    beam.layer = static_cast<renderer::LayerId>(value);
    return true;
  }
  if (key == "visible") {
    return parseBool(raw_value, beam.visible);
  }
  if (key == "depth_test") {
    return parseBool(raw_value, beam.depth_test);
  }
  if (key == "closed_loop") {
    return parseBool(raw_value, beam.closed_loop);
  }
  if (key == "world_space") {
    return parseBool(raw_value, beam.world_space);
  }
  if (key == "endpoint_flares") {
    return parseBool(raw_value, beam.endpoint_flares);
  }

  out_error = "unknown beam field '" + key + "'";
  return false;
}

}  // namespace karma::prefabs::detail
