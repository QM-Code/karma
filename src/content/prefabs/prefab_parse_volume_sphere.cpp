#include "prefab_parse_support.h"

namespace karma::prefabs::detail {

bool applyVolumeSphereField(Prefab&,
                            PrefabEntry& entry,
                            const std::string& key,
                            const std::string& raw_value,
                            std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (applyColorBindingField(entry.volume_sphere.color_binding, key, raw_value, "color")) {
    return true;
  }
  if (applyColorBindingField(entry.volume_sphere.emissive_color_binding,
                             key,
                             raw_value,
                             "emissive_color")) {
    return true;
  }
  if (applyFloatBindingField(entry.volume_sphere.radius_binding, key, raw_value, "radius")) {
    return true;
  }
  if (applyFloatBindingField(entry.volume_sphere.center_opacity_binding,
                             key,
                             raw_value,
                             "center_opacity")) {
    return true;
  }
  if (applyFloatBindingField(entry.volume_sphere.distortion_strength_binding,
                             key,
                             raw_value,
                             "distortion_strength")) {
    return true;
  }
  if (applyFloatBindingField(entry.volume_sphere.noise_strength_binding,
                             key,
                             raw_value,
                             "noise_strength")) {
    return true;
  }
  if (applyFloatBindingField(entry.volume_sphere.overlay_depth_binding,
                             key,
                             raw_value,
                             "overlay_depth")) {
    return true;
  }
  if (key == "visible") {
    return parseBool(raw_value, entry.volume_sphere.volume.visible);
  }
  if (key == "scale_with_transform") {
    return parseBool(raw_value, entry.volume_sphere.volume.scale_with_transform);
  }

  out_error = "unknown volume sphere field '" + key + "'";
  return false;
}

}  // namespace karma::prefabs::detail
