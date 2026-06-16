#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// Terrain tile data source.
enum class TerrainSourceType : uint8_t {
  Procedural = 0,
  ImageTileDirectory = 1,
  SingleImage = 2,
};

/// \ingroup karma_components
/// Streamed height-field terrain authoring data.
///
/// Terrain uses Karma's Y-up convention: tiles cover the XZ plane and height
/// displaces along Y. File-backed terrain expects decoded image tiles under
/// `tile_directory` using `{x}` and `{z}` placeholders in the filename patterns.
/// Single-image terrain renders one fixed-size tile using `terrain_size`.
struct TerrainComponent : ecs::ComponentTag {
  TerrainSourceType source = TerrainSourceType::Procedural;
  std::filesystem::path tile_directory;
  std::string height_pattern = "height_{x}_{z}.png";
  std::string color_pattern = "color_{x}_{z}.png";
  std::filesystem::path height_image;
  std::filesystem::path heatmap_image;
  std::filesystem::path color_image;
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
  float target_tessellated_edge_size = 18.0f;
  uint32_t layer = 0u;
  bool visible = true;
  bool cpu_fallback_enabled = true;
};

}  // namespace karma::components
