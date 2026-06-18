#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// Terrain tile data source.
enum class TerrainSourceType : uint8_t {
  Procedural = 0,
  ImageTileDirectory = 1,
  SingleImage = 2,
};

/// Height/data map file encoding.
enum class TerrainHeightFormat : uint8_t {
  Auto = 0,
  ImageFile = 1,
  Raw16Unsigned = 2,
  R32Float = 3,
};

/// Standard terrain auxiliary data map role.
enum class TerrainDataMapKind : uint8_t {
  Custom = 0,
  Flow = 1,
  Wear = 2,
  Deposit = 3,
  Slope = 4,
  Curvature = 5,
};

/// Repeated terrain material layer controlled by a packed weight/splat map.
struct TerrainMaterialLayer {
  std::string name;
  /// Preferred shared material key resolved through content::AssetRegistry.
  /// Explicit image paths below are used as a direct texture fallback.
  std::string material_key;
  std::filesystem::path albedo_image;
  std::filesystem::path normal_image;
  std::filesystem::path roughness_image;
  float uv_scale = 16.0f;
  bool enabled = true;

  friend bool operator==(const TerrainMaterialLayer& lhs,
                         const TerrainMaterialLayer& rhs) = default;
};

/// Optional auxiliary terrain data map exported by terrain authoring tools.
struct TerrainDataMapBinding {
  std::string name;
  TerrainDataMapKind kind = TerrainDataMapKind::Custom;
  std::filesystem::path image;
  std::string pattern;
  TerrainHeightFormat format = TerrainHeightFormat::Auto;
  uint32_t raw_width = 0u;
  uint32_t raw_height = 0u;
  uint32_t channel = 0u;
  bool enabled = true;

  friend bool operator==(const TerrainDataMapBinding& lhs,
                         const TerrainDataMapBinding& rhs) = default;
};

/// \ingroup karma_components
/// Streamed height-field terrain authoring data.
///
/// Terrain uses Karma's Y-up convention: tiles cover the XZ plane and height
/// displaces along Y. File-backed terrain expects decoded image tiles under
/// `tile_directory` using `{x}` and `{z}` placeholders in the filename patterns.
/// `{y}` is accepted as an alias for `{z}` for tiled terrain-tool exports.
/// Single-image terrain renders one fixed-size tile using `terrain_size`.
struct TerrainComponent : ecs::ComponentTag {
  TerrainSourceType source = TerrainSourceType::Procedural;
  std::filesystem::path tile_directory;
  std::string height_pattern = "height_{x}_{z}.png";
  std::string color_pattern = "color_{x}_{z}.png";
  std::string control_pattern = "control_{x}_{z}.png";
  std::filesystem::path height_image;
  std::filesystem::path heatmap_image;
  std::filesystem::path color_image;
  std::filesystem::path control_image;
  TerrainHeightFormat height_format = TerrainHeightFormat::Auto;
  uint32_t raw_width = 0u;
  uint32_t raw_height = 0u;
  bool raw_little_endian = true;
  bool flip_y = false;
  float height_value_min = 0.0f;
  float height_value_max = 1.0f;
  int32_t tile_index_base = 0;
  std::vector<TerrainMaterialLayer> material_layers;
  std::vector<TerrainDataMapBinding> data_maps;
  float terrain_size = 1000.0f;
  float tile_size = 1000.0f;
  uint32_t tile_resolution = 257u;
  int32_t origin_tile_x = 0;
  int32_t origin_tile_z = 0;
  float height_scale = 120.0f;
  float height_offset = 0.0f;
  float view_distance = 4000.0f;
  uint32_t base_patch_size = 16u;
  float tessellation_factor = 16.0f;
  float target_tessellated_edge_size = 12.0f;
  uint32_t layer = 0u;
  bool visible = true;
  bool cpu_fallback_enabled = true;
};

}  // namespace karma::components
