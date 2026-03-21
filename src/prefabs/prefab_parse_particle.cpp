#include "prefab_parse_support.h"

namespace karma::prefabs::detail {

bool applyParticleField(Prefab&,
                        PrefabEntry& entry,
                        const std::string& key,
                        const std::string& raw_value,
                        std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (key == "effect") {
    entry.particle.effect_key = stripQuotes(trim(raw_value));
    return !entry.particle.effect_key.empty();
  }
  if (key == "enabled") {
    return parseBool(raw_value, entry.particle.enabled);
  }
  if (key == "playing") {
    return parseBool(raw_value, entry.particle.playing);
  }
  if (key == "auto_apply") {
    return parseBool(raw_value, entry.particle.auto_apply);
  }
  if (key == "preserve_enabled") {
    return parseBool(raw_value, entry.particle.preserve_enabled);
  }
  if (key == "preserve_playing") {
    return parseBool(raw_value, entry.particle.preserve_playing);
  }

  auto& effect_override = entry.particle.effect_override;
  if (applyColorBindingField(entry.particle.start_color_binding,
                             key,
                             raw_value,
                             "override.start_color")) {
    return true;
  }
  if (applyColorBindingField(entry.particle.end_color_binding,
                             key,
                             raw_value,
                             "override.end_color")) {
    return true;
  }
  if (key == "override.active") {
    return parseBool(raw_value, effect_override.active);
  }
  if (key == "override.time_scale") {
    return parseNumber(raw_value, effect_override.time_scale);
  }
  if (key == "override.spawn_rate_scale") {
    return parseNumber(raw_value, effect_override.spawn_rate_scale);
  }
  if (key == "override.lifetime_scale") {
    return parseNumber(raw_value, effect_override.lifetime_scale);
  }
  if (key == "override.size_scale") {
    return parseNumber(raw_value, effect_override.size_scale);
  }
  if (key == "override.radius_scale") {
    return parseNumber(raw_value, effect_override.radius_scale);
  }
  if (key == "override.velocity_scale") {
    return parseNumber(raw_value, effect_override.velocity_scale);
  }
  if (key == "override.angular_velocity_scale") {
    return parseNumber(raw_value, effect_override.angular_velocity_scale);
  }
  if (key == "override.alpha_scale") {
    return parseNumber(raw_value, effect_override.alpha_scale);
  }

  out_error = "unknown particle field '" + key + "'";
  return false;
}

}  // namespace karma::prefabs::detail
