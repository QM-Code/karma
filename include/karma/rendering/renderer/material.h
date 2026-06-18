#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

/// Texture transform slot count used by imported glTF-style materials.
inline constexpr size_t kImportedMaterialTextureCoordSlotCount = 12u;

/// Renderer-facing semantic for an imported material texture.
enum class ImportedMaterialTextureSemantic : uint32_t {
  BaseColor = 0,
  Normal,
  MetallicRoughness,
  Occlusion,
  Emissive,
  Clearcoat,
  ClearcoatRoughness,
  ClearcoatNormal,
  SheenColor,
  SheenRoughness,
  Transmission,
  Thickness,
};

/// Source texture reference captured by a content importer for renderer upload.
struct ImportedMaterialTexture {
  ImportedMaterialTextureSemantic semantic = ImportedMaterialTextureSemantic::BaseColor;
  std::string source_key;
  std::string raw_name;
  std::filesystem::path resolved_path;
  std::string label;
  std::vector<uint8_t> source_bytes;
  uint32_t width = 0u;
  uint32_t height = 0u;
  bool embedded = false;
  bool compressed = true;
  bool srgb = false;
};

/// Imported material data captured while the source scene is already loaded.
struct ImportedMaterialData {
  ImportedMaterialData() {
    resetTextureTransforms();
  }

  void resetTextureTransforms() {
    for (size_t i = 0; i < kImportedMaterialTextureCoordSlotCount; ++i) {
      texcoord_row0[i] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
      texcoord_row1[i] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    }
  }

  MaterialDesc material{};
  std::vector<ImportedMaterialTexture> textures;
  std::array<glm::vec4, kImportedMaterialTextureCoordSlotCount> texcoord_row0{};
  std::array<glm::vec4, kImportedMaterialTextureCoordSlotCount> texcoord_row1{};
};

/// Pipeline family for a material asset.
struct MaterialPipelineDesc {
  enum class Type : uint32_t {
    Standard = 0,
    Custom = 1,
  };

  Type type = Type::Standard;
  std::filesystem::path vertex_shader_path;
  std::filesystem::path fragment_shader_path;
  std::string vertex_entry_point = "main";
  std::string fragment_entry_point = "main";
  std::vector<std::string> defines;
};

/// Named material parameter value used by material assets and instances.
using MaterialParameterValue =
    std::variant<bool,
                 int32_t,
                 uint32_t,
                 float,
                 Color,
                 glm::vec2,
                 glm::vec3,
                 glm::vec4,
                 std::string>;

/// Shared material asset definition registered by key.
struct MaterialAssetDesc {
  std::string material_key;
  MaterialPipelineDesc pipeline{};
  MaterialDesc surface{};
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::filesystem::path> textures;
  std::filesystem::path material_asset_path;
  uint32_t material_asset_index = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const ImportedMaterialData> imported_material;
};

/// Per-object material instance definition registered by key.
struct MaterialInstanceDesc {
  std::string material_key;
  std::string parent_material_key;
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::filesystem::path> textures;
};

/// Flattened renderer-facing material after asset/instance inheritance.
struct ResolvedMaterialDesc {
  MaterialPipelineDesc pipeline{};
  MaterialDesc surface{};
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::filesystem::path> textures;
  std::filesystem::path material_asset_path;
  uint32_t material_asset_index = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const ImportedMaterialData> imported_material;

  static ResolvedMaterialDesc fromSurface(MaterialDesc material) {
    ResolvedMaterialDesc desc{};
    desc.surface = std::move(material);
    return desc;
  }
};

}  // namespace karma::renderer
