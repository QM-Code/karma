#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "karma/rendering/renderer/material.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// Keyed registry of shared material assets and per-object material instances.
///
/// `RenderSystem` watches `version()` and rebuilds renderer material handles
/// when registrations change.
class MaterialLibrary {
 public:
  /// Registers or replaces a shared material asset.
  void registerMaterialAsset(const std::string& key, MaterialAssetDesc desc) {
    desc.material_key = key;
    assets_[key] = std::move(desc);
    instances_.erase(key);
    version_ += 1;
  }

  /// Registers or replaces a material instance.
  void registerMaterialInstance(const std::string& key, MaterialInstanceDesc desc) {
    desc.material_key = key;
    instances_[key] = std::move(desc);
    assets_.erase(key);
    version_ += 1;
  }

  /// Registers an explicit material asset from renderer material parameters.
  void registerMaterialDesc(const std::string& key, MaterialDesc desc) {
    MaterialAssetDesc asset{};
    asset.surface = std::move(desc);
    registerMaterialAsset(key, std::move(asset));
  }

  /// Registers a material asset sourced from an imported asset material.
  void registerImportedAssetMaterial(const std::string& key,
                                     std::filesystem::path path,
                                     uint32_t material_index,
                                     MaterialDesc fallback = {},
                                     std::shared_ptr<const ImportedMaterialData> imported = {}) {
    MaterialAssetDesc asset{};
    asset.surface = std::move(fallback);
    asset.material_asset_path = std::move(path);
    asset.material_asset_index = material_index;
    asset.imported_material = std::move(imported);
    registerMaterialAsset(key, std::move(asset));
  }

  /// Removes a material asset or instance.
  void unregisterMaterial(const std::string& key) {
    const bool removed = assets_.erase(key) > 0 || instances_.erase(key) > 0;
    if (removed) {
      version_ += 1;
    }
  }

  /// Removes all material assets and instances.
  void clear() {
    if (!assets_.empty() || !instances_.empty()) {
      assets_.clear();
      instances_.clear();
      version_ += 1;
    }
  }

  /// Finds a shared material asset by key.
  const MaterialAssetDesc* findAsset(const std::string& key) const {
    const auto it = assets_.find(key);
    return it != assets_.end() ? &it->second : nullptr;
  }

  /// Finds a material instance by key.
  const MaterialInstanceDesc* findInstance(const std::string& key) const {
    const auto it = instances_.find(key);
    return it != instances_.end() ? &it->second : nullptr;
  }

  /// Resolves a material asset or instance to a flattened backend descriptor.
  std::optional<ResolvedMaterialDesc> resolve(const std::string& key) const {
    return resolveRecursive(key, 0);
  }

  /// Monotonic registry version used for cache invalidation.
  uint64_t version() const {
    return version_;
  }

 private:
  static constexpr uint32_t kMaxResolveDepth = 16;

  static const float* asFloat(const MaterialParameterValue& value) {
    if (const auto* f = std::get_if<float>(&value)) {
      return f;
    }
    return nullptr;
  }

  static const bool* asBool(const MaterialParameterValue& value) {
    if (const auto* b = std::get_if<bool>(&value)) {
      return b;
    }
    return nullptr;
  }

  static const Color* asColor(const MaterialParameterValue& value) {
    if (const auto* c = std::get_if<Color>(&value)) {
      return c;
    }
    return nullptr;
  }

  static const glm::vec3* asVec3(const MaterialParameterValue& value) {
    if (const auto* v = std::get_if<glm::vec3>(&value)) {
      return v;
    }
    return nullptr;
  }

  static const std::string* asString(const MaterialParameterValue& value) {
    if (const auto* s = std::get_if<std::string>(&value)) {
      return s;
    }
    return nullptr;
  }

  static void applyParameter(MaterialDesc& material,
                             const std::string& name,
                             const MaterialParameterValue& value) {
    if (name == "base_color") {
      if (const auto* color = asColor(value)) material.base_color = *color;
    } else if (name == "emissive_color") {
      if (const auto* color = asColor(value)) material.emissive_color = *color;
    } else if (name == "metallic") {
      if (const auto* f = asFloat(value)) material.metallic = *f;
    } else if (name == "roughness") {
      if (const auto* f = asFloat(value)) material.roughness = *f;
    } else if (name == "normal_scale") {
      if (const auto* f = asFloat(value)) material.normal_scale = *f;
    } else if (name == "occlusion_strength") {
      if (const auto* f = asFloat(value)) material.occlusion_strength = *f;
    } else if (name == "emissive_strength") {
      if (const auto* f = asFloat(value)) material.emissive_strength = *f;
    } else if (name == "clearcoat") {
      if (const auto* f = asFloat(value)) material.clearcoat = *f;
    } else if (name == "clearcoat_roughness") {
      if (const auto* f = asFloat(value)) material.clearcoat_roughness = *f;
    } else if (name == "sheen_color") {
      if (const auto* color = asColor(value)) material.sheen_color = *color;
    } else if (name == "sheen_roughness") {
      if (const auto* f = asFloat(value)) material.sheen_roughness = *f;
    } else if (name == "anisotropy") {
      if (const auto* f = asFloat(value)) material.anisotropy = *f;
    } else if (name == "transmission") {
      if (const auto* f = asFloat(value)) material.transmission = *f;
    } else if (name == "ior") {
      if (const auto* f = asFloat(value)) material.ior = *f;
    } else if (name == "thickness") {
      if (const auto* f = asFloat(value)) material.thickness = *f;
    } else if (name == "attenuation_distance") {
      if (const auto* f = asFloat(value)) material.attenuation_distance = *f;
    } else if (name == "attenuation_color") {
      if (const auto* color = asColor(value)) material.attenuation_color = *color;
    } else if (name == "unlit") {
      if (const auto* b = asBool(value)) material.unlit = *b;
    } else if (name == "transparent") {
      if (const auto* b = asBool(value)) material.transparent = *b;
    } else if (name == "depth_test") {
      if (const auto* b = asBool(value)) material.depth_test = *b;
    } else if (name == "depth_write") {
      if (const auto* b = asBool(value)) material.depth_write = *b;
    } else if (name == "wireframe") {
      if (const auto* b = asBool(value)) material.wireframe = *b;
    } else if (name == "double_sided") {
      if (const auto* b = asBool(value)) material.double_sided = *b;
    } else if (name == "blend_mode") {
      if (const auto* s = asString(value)) {
        if (*s == "additive") {
          material.blend_mode = MaterialDesc::BlendMode::Additive;
        } else if (*s == "alpha") {
          material.blend_mode = MaterialDesc::BlendMode::Alpha;
        }
      }
    } else if (name == "shading_model") {
      if (const auto* s = asString(value)) {
        if (*s == "standard") material.shading_model = MaterialDesc::ShadingModel::Standard;
        else if (*s == "energy_shell") material.shading_model = MaterialDesc::ShadingModel::EnergyShell;
        else if (*s == "wave_volume") material.shading_model = MaterialDesc::ShadingModel::WaveVolume;
        else if (*s == "sphere_halo") material.shading_model = MaterialDesc::ShadingModel::SphereHalo;
        else if (*s == "screen_wave") material.shading_model = MaterialDesc::ShadingModel::ScreenWave;
        else if (*s == "sphere_glow_volume") material.shading_model = MaterialDesc::ShadingModel::SphereGlowVolume;
        else if (*s == "volumetric_solid") material.shading_model = MaterialDesc::ShadingModel::VolumetricSolid;
      }
    } else if (name == "shell_fresnel_power") {
      if (const auto* f = asFloat(value)) material.shell_fresnel_power = *f;
    } else if (name == "shell_fresnel_strength") {
      if (const auto* f = asFloat(value)) material.shell_fresnel_strength = *f;
    } else if (name == "shell_refraction_strength") {
      if (const auto* f = asFloat(value)) material.shell_refraction_strength = *f;
    } else if (name == "shell_interior_strength") {
      if (const auto* f = asFloat(value)) material.shell_interior_strength = *f;
    } else if (name == "shell_highlight_strength") {
      if (const auto* f = asFloat(value)) material.shell_highlight_strength = *f;
    } else if (name == "shell_alpha_boost") {
      if (const auto* f = asFloat(value)) material.shell_alpha_boost = *f;
    } else if (name == "shell_swirl_strength") {
      if (const auto* f = asFloat(value)) material.shell_swirl_strength = *f;
    } else if (name == "analytic_sphere_normals") {
      if (const auto* b = asBool(value)) material.analytic_sphere_normals = *b;
    } else if (name == "shell_body_strength") {
      if (const auto* f = asFloat(value)) material.shell_body_strength = *f;
    } else if (name == "screen_center_x") {
      if (const auto* f = asFloat(value)) material.screen_center_x = *f;
    } else if (name == "screen_center_y") {
      if (const auto* f = asFloat(value)) material.screen_center_y = *f;
    } else if (name == "screen_radius_x") {
      if (const auto* f = asFloat(value)) material.screen_radius_x = *f;
    } else if (name == "screen_radius_y") {
      if (const auto* f = asFloat(value)) material.screen_radius_y = *f;
    } else if (name == "wave_tint_strength") {
      if (const auto* f = asFloat(value)) material.wave_tint_strength = *f;
    } else if (name == "wave_distortion_strength") {
      if (const auto* f = asFloat(value)) material.wave_distortion_strength = *f;
    } else if (name == "wave_edge_strength") {
      if (const auto* f = asFloat(value)) material.wave_edge_strength = *f;
    } else if (name == "wave_noise_strength") {
      if (const auto* f = asFloat(value)) material.wave_noise_strength = *f;
    } else if (name == "volume_center") {
      if (const auto* v = asVec3(value)) material.volume_center = *v;
    } else if (name == "volume_axis_x") {
      if (const auto* v = asVec3(value)) material.volume_axis_x = *v;
    } else if (name == "volume_axis_y") {
      if (const auto* v = asVec3(value)) material.volume_axis_y = *v;
    } else if (name == "volume_axis_z") {
      if (const auto* v = asVec3(value)) material.volume_axis_z = *v;
    } else if (name == "volume_radius") {
      if (const auto* f = asFloat(value)) material.volume_radius = *f;
    } else if (name == "volume_capsule_half_length") {
      if (const auto* f = asFloat(value)) material.volume_capsule_half_length = *f;
    } else if (name == "volume_density") {
      if (const auto* f = asFloat(value)) material.volume_density = *f;
    } else if (name == "volume_scattering") {
      if (const auto* f = asFloat(value)) material.volume_scattering = *f;
    } else if (name == "volume_anisotropy") {
      if (const auto* f = asFloat(value)) material.volume_anisotropy = *f;
    } else if (name == "volume_absorption") {
      if (const auto* f = asFloat(value)) material.volume_absorption = *f;
    } else if (name == "volume_distortion_strength") {
      if (const auto* f = asFloat(value)) material.volume_distortion_strength = *f;
    } else if (name == "volume_noise_strength") {
      if (const auto* f = asFloat(value)) material.volume_noise_strength = *f;
    }
  }

  static void applyParameters(MaterialDesc& material,
                              const std::unordered_map<std::string, MaterialParameterValue>& params) {
    for (const auto& [name, value] : params) {
      applyParameter(material, name, value);
    }
  }

  std::optional<ResolvedMaterialDesc> resolveRecursive(const std::string& key,
                                                       uint32_t depth) const {
    if (depth > kMaxResolveDepth) {
      return std::nullopt;
    }

    if (const auto asset_it = assets_.find(key); asset_it != assets_.end()) {
      ResolvedMaterialDesc resolved{};
      resolved.pipeline = asset_it->second.pipeline;
      resolved.surface = asset_it->second.surface;
      resolved.params = asset_it->second.params;
      resolved.textures = asset_it->second.textures;
      resolved.material_asset_path = asset_it->second.material_asset_path;
      resolved.material_asset_index = asset_it->second.material_asset_index;
      resolved.imported_material = asset_it->second.imported_material;
      applyParameters(resolved.surface, resolved.params);
      if (!resolved.pipeline.vertex_shader_path.empty()) {
        resolved.surface.vertex_shader_path = resolved.pipeline.vertex_shader_path;
      }
      if (!resolved.pipeline.fragment_shader_path.empty()) {
        resolved.surface.fragment_shader_path = resolved.pipeline.fragment_shader_path;
      }
      return resolved;
    }

    const auto instance_it = instances_.find(key);
    if (instance_it == instances_.end() ||
        instance_it->second.parent_material_key.empty()) {
      return std::nullopt;
    }

    auto parent = resolveRecursive(instance_it->second.parent_material_key, depth + 1);
    if (!parent.has_value()) {
      return std::nullopt;
    }
    for (const auto& [name, value] : instance_it->second.params) {
      parent->params[name] = value;
    }
    for (const auto& [name, value] : instance_it->second.textures) {
      parent->textures[name] = value;
    }
    applyParameters(parent->surface, instance_it->second.params);
    return parent;
  }

  std::unordered_map<std::string, MaterialAssetDesc> assets_;
  std::unordered_map<std::string, MaterialInstanceDesc> instances_;
  uint64_t version_ = 0;
};

}  // namespace karma::renderer
