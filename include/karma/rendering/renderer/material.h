#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/ids.h"

#include <glm/glm.hpp>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Alias for renderer color values.
using Color = math::Color;

/// \ingroup karma_rendering
/// Material parameters consumed by the renderer backend.
///
/// The standard fields describe PBR-ish material inputs. The feature-specific
/// fields are currently used by built-in shell, wave, halo, and volume shading
/// models until a more general custom-material pipeline exists.
struct MaterialDesc {
  /// Transparent blending mode for material draws.
  enum class BlendMode : uint32_t {
    Alpha = 0,
    Additive = 1,
  };

  /// Built-in material shader family.
  enum class ShadingModel : uint32_t {
    Standard = 0,
    EnergyShell = 1,
    WaveVolume = 2,
    SphereHalo = 3,
    ScreenWave = 4,
    SphereGlowVolume = 5,
    VolumetricSolid = 6,
  };

  std::filesystem::path vertex_shader_path;
  std::filesystem::path fragment_shader_path;
  math::Color base_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
  float metallic = 1.0f;
  float roughness = 1.0f;
  float normal_scale = 1.0f;
  float occlusion_strength = 1.0f;
  float emissive_strength = 1.0f;
  float clearcoat = 0.0f;
  float clearcoat_roughness = 0.0f;
  math::Color sheen_color{0.0f, 0.0f, 0.0f, 1.0f};
  float sheen_roughness = 0.0f;
  float anisotropy = 0.0f;
  float transmission = 0.0f;
  float ior = 1.5f;
  float thickness = 0.0f;
  float attenuation_distance = std::numeric_limits<float>::infinity();
  math::Color attenuation_color{1.0f, 1.0f, 1.0f, 1.0f};
  ShadingModel shading_model = ShadingModel::Standard;
  float shell_fresnel_power = 5.0f;
  float shell_fresnel_strength = 1.0f;
  float shell_refraction_strength = 0.08f;
  float shell_interior_strength = 0.4f;
  float shell_highlight_strength = 1.0f;
  float shell_alpha_boost = 0.0f;
  float shell_swirl_strength = 0.0f;
  bool analytic_sphere_normals = false;
  float shell_body_strength = 1.0f;
  float screen_center_x = 0.5f;
  float screen_center_y = 0.5f;
  float screen_radius_x = 0.25f;
  float screen_radius_y = 0.25f;
  float wave_tint_strength = 0.75f;
  float wave_distortion_strength = 0.6f;
  float wave_edge_strength = 0.35f;
  float wave_noise_strength = 0.65f;
  glm::vec3 volume_center{0.0f, 0.0f, 0.0f};
  glm::vec3 volume_axis_x{1.0f, 0.0f, 0.0f};
  glm::vec3 volume_axis_y{0.0f, 1.0f, 0.0f};
  glm::vec3 volume_axis_z{0.0f, 0.0f, 1.0f};
  uint32_t volume_shape = 0u;
  float volume_radius = 1.0f;
  float volume_capsule_half_length = 0.0f;
  float volume_density = 1.0f;
  float volume_scattering = 1.0f;
  float volume_anisotropy = 0.0f;
  float volume_absorption = 0.0f;
  float volume_distortion_strength = 0.0f;
  float volume_noise_strength = 1.0f;
  TextureId base_color_texture = kInvalidTexture;
  bool unlit = false;
  bool transparent = false;
  BlendMode blend_mode = BlendMode::Alpha;
  bool depth_test = true;
  bool depth_write = true;
  bool wireframe = false;
  bool double_sided = false;
};

/// \ingroup karma_rendering
/// Data-driven material resource description registered by key.
struct MaterialResourceDesc {
  /// Material resource construction mode.
  enum class Kind {
    MeshTint,
    Explicit,
    ImportedAssetMaterial,
  };

  Kind kind = Kind::MeshTint;
  std::string material_key;
  std::string source_mesh_key;
  std::string shader_key;
  std::string albedo_texture_key;
  std::string normal_texture_key;
  std::string metallic_roughness_texture_key;
  Color base_color_tint{1.0f, 1.0f, 1.0f, 1.0f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  bool double_sided = false;
  MaterialDesc material{};
  std::filesystem::path material_asset_path;
  uint32_t material_asset_index = std::numeric_limits<uint32_t>::max();

  /// Creates a material resource that tints materials from a mesh asset.
  static MaterialResourceDesc fromMeshTint(std::string source_mesh, Color tint) {
    MaterialResourceDesc desc{};
    desc.kind = Kind::MeshTint;
    desc.source_mesh_key = std::move(source_mesh);
    desc.base_color_tint = tint;
    return desc;
  }

  /// Creates a material resource from explicit renderer material parameters.
  static MaterialResourceDesc fromMaterial(MaterialDesc material_desc) {
    MaterialResourceDesc desc{};
    desc.kind = Kind::Explicit;
    desc.material = std::move(material_desc);
    return desc;
  }

  /// Creates a material resource from an imported asset material index.
  static MaterialResourceDesc fromImportedAssetMaterial(std::filesystem::path path,
                                                       uint32_t material_index,
                                                       MaterialDesc fallback = {}) {
    MaterialResourceDesc desc{};
    desc.kind = Kind::ImportedAssetMaterial;
    desc.material_asset_path = std::move(path);
    desc.material_asset_index = material_index;
    desc.material = std::move(fallback);
    return desc;
  }
};

}  // namespace karma::renderer
