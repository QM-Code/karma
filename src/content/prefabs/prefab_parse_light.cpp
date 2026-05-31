#include "prefab_parse_support.h"

namespace karma::prefabs::detail {

bool applyLightField(Prefab&,
                     PrefabEntry& entry,
                     const std::string& key,
                     const std::string& raw_value,
                     std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (applyColorBindingField(entry.light.color_binding, key, raw_value, "color")) {
    return true;
  }
  if (applyFloatBindingField(entry.light.intensity_binding, key, raw_value, "intensity")) {
    return true;
  }
  if (applyFloatBindingField(entry.light.range_binding, key, raw_value, "range")) {
    return true;
  }
  if (key == "type") {
    return parseLightType(raw_value, entry.light.light.type);
  }
  if (key == "casts_shadows") {
    return parseBool(raw_value, entry.light.light.casts_shadows);
  }
  if (key == "inner_cone_degrees") {
    return parseNumber(raw_value, entry.light.light.inner_cone_degrees);
  }
  if (key == "outer_cone_degrees") {
    return parseNumber(raw_value, entry.light.light.outer_cone_degrees);
  }
  if (key == "shadow_extent") {
    return parseNumber(raw_value, entry.light.light.shadow_extent);
  }

  out_error = "unknown light field '" + key + "'";
  return false;
}

}  // namespace karma::prefabs::detail
