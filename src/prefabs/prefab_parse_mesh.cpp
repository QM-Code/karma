#include "prefab_parse_support.h"

namespace karma::prefabs::detail {

bool applyMeshField(Prefab& prefab,
                    PrefabEntry& entry,
                    const std::string& key,
                    const std::string& raw_value,
                    std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (key == "mesh") {
    std::filesystem::path mesh_path = stripQuotes(trim(raw_value));
    if (!mesh_path.is_absolute()) {
      mesh_path = prefab.source_path.parent_path() / mesh_path;
    }
    entry.mesh.mesh_path = mesh_path.lexically_normal();
    return true;
  }
  if (key == "visible") {
    return parseBool(raw_value, entry.mesh.visible);
  }
  if (key == "shadow_visible") {
    return parseBool(raw_value, entry.mesh.shadow_visible);
  }

  auto& material = entry.mesh.material.material;
  auto& base_binding = entry.mesh.material.base_color_binding;
  auto& emissive_binding = entry.mesh.material.emissive_color_binding;
  if (applyColorBindingField(base_binding, key, raw_value, "material.base_color")) {
    return true;
  }
  if (applyColorBindingField(emissive_binding, key, raw_value, "material.emissive_color")) {
    return true;
  }
  if (key == "material.metallic") {
    return parseNumber(raw_value, material.metallic);
  }
  if (key == "material.roughness") {
    return parseNumber(raw_value, material.roughness);
  }
  if (key == "material.normal_scale") {
    return parseNumber(raw_value, material.normal_scale);
  }
  if (key == "material.occlusion_strength") {
    return parseNumber(raw_value, material.occlusion_strength);
  }
  if (key == "material.shading_model") {
    return parseShadingModel(raw_value, material.shading_model);
  }
  if (key == "material.blend_mode") {
    return parseBlendMode(raw_value, material.blend_mode);
  }
  if (key == "material.shell_fresnel_power") {
    return parseNumber(raw_value, material.shell_fresnel_power);
  }
  if (key == "material.shell_fresnel_strength") {
    return parseNumber(raw_value, material.shell_fresnel_strength);
  }
  if (key == "material.shell_refraction_strength") {
    return parseNumber(raw_value, material.shell_refraction_strength);
  }
  if (key == "material.shell_interior_strength") {
    return parseNumber(raw_value, material.shell_interior_strength);
  }
  if (key == "material.shell_highlight_strength") {
    return parseNumber(raw_value, material.shell_highlight_strength);
  }
  if (key == "material.shell_alpha_boost") {
    return parseNumber(raw_value, material.shell_alpha_boost);
  }
  if (key == "material.shell_swirl_strength") {
    return parseNumber(raw_value, material.shell_swirl_strength);
  }
  if (key == "material.shell_body_strength") {
    return parseNumber(raw_value, material.shell_body_strength);
  }
  if (key == "material.wave_tint_strength") {
    return parseNumber(raw_value, material.wave_tint_strength);
  }
  if (key == "material.wave_distortion_strength") {
    return parseNumber(raw_value, material.wave_distortion_strength);
  }
  if (key == "material.wave_edge_strength") {
    return parseNumber(raw_value, material.wave_edge_strength);
  }
  if (key == "material.wave_noise_strength") {
    return parseNumber(raw_value, material.wave_noise_strength);
  }
  if (key == "material.analytic_sphere_normals") {
    return parseBool(raw_value, material.analytic_sphere_normals);
  }
  if (key == "material.unlit") {
    return parseBool(raw_value, material.unlit);
  }
  if (key == "material.transparent") {
    return parseBool(raw_value, material.transparent);
  }
  if (key == "material.depth_test") {
    return parseBool(raw_value, material.depth_test);
  }
  if (key == "material.depth_write") {
    return parseBool(raw_value, material.depth_write);
  }
  if (key == "material.wireframe") {
    return parseBool(raw_value, material.wireframe);
  }
  if (key == "material.double_sided") {
    return parseBool(raw_value, material.double_sided);
  }

  out_error = "unknown mesh field '" + key + "'";
  return false;
}

}  // namespace karma::prefabs::detail
