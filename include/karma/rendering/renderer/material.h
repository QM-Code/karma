#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <filesystem>
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
/// Standard material surface and render-state data.
struct MaterialDesc {
  /// Transparent blending mode for material draws.
  enum class BlendMode : uint32_t {
    Alpha = 0,
    Additive = 1,
  };

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
  bool analytic_sphere_normals = false;
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
  std::string name = "standard";
  std::filesystem::path vertex_shader_path;
  std::filesystem::path fragment_shader_path;
  std::string vertex_entry_point = "main";
  std::string fragment_entry_point = "main";
  std::vector<std::string> defines;
};

/// Named material parameter value used by material assets and variants.
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
  std::unordered_map<std::string, std::string> textures;
  std::filesystem::path material_asset_path;
  uint32_t material_asset_index = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const ImportedMaterialData> imported_material;
};

/// Material variant definition registered by key.
///
/// A variant inherits a base material's pipeline, surface, textures, and import
/// payload, then applies local params and texture assignments. Variants are just
/// materials: any mesh slot can be assigned either an asset key or a variant key.
struct MaterialVariantDesc {
  std::string material_key;
  std::string base_material_key;
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::string> textures;
};

/// Flattened renderer-facing material after asset/variant inheritance.
struct ResolvedMaterialDesc {
  MaterialPipelineDesc pipeline{};
  MaterialDesc surface{};
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::string> textures;
  std::unordered_map<std::string, TextureId> texture_handles;
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
