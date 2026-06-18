#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "karma/rendering/renderer/ids.h"

namespace karma::renderer {

/// Integer terrain tile coordinate in the terrain XZ tile grid.
struct TerrainTileCoord {
  int32_t x = 0;
  int32_t z = 0;

  friend bool operator==(const TerrainTileCoord& lhs,
                         const TerrainTileCoord& rhs) = default;
};

/// Terrain resource creation descriptor.
struct TerrainDesc {
  float tile_size = 1000.0f;
  uint32_t tile_resolution = 257u;
  int32_t origin_tile_x = 0;
  int32_t origin_tile_z = 0;
  float height_scale = 120.0f;
  float height_offset = 0.0f;
  uint32_t base_patch_size = 16u;
  float max_tessellation_factor = 16.0f;
  float target_tessellated_edge_size = 12.0f;
  bool cpu_fallback_enabled = true;
};

/// Decoded RGBA8 terrain texture payload.
struct TerrainTextureData {
  uint32_t width = 0u;
  uint32_t height = 0u;
  std::vector<uint8_t> rgba8;

  bool valid() const {
    const std::size_t byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    return width > 0u && height > 0u && rgba8.size() == byte_count;
  }
};

/// Shared repeated material layer uploaded to a terrain resource.
struct TerrainMaterialLayerData {
  uint32_t layer = 0u;
  std::string name;
  float uv_scale = 16.0f;
  bool enabled = true;
  TerrainTextureData albedo;
  TerrainTextureData normal;
  TerrainTextureData roughness;

  bool valid() const {
    return enabled && layer < 4u && uv_scale > 0.0f && albedo.valid();
  }
};

/// Optional decoded terrain data map payload carried with a tile.
struct TerrainDataMapTileData {
  std::string name;
  uint32_t width = 0u;
  uint32_t height = 0u;
  std::vector<float> values;

  bool valid() const {
    return width > 0u && height > 0u &&
           values.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height);
  }
};

/// Decoded tile payload uploaded to the renderer backend.
///
/// `heights` are normalized scalar samples. Backends apply `height_scale` and
/// `height_offset` from `TerrainDesc`. `color_rgba8` is an orthophoto/albedo
/// tile in top-left row order. `control_rgba8`, when provided, packs up to four
/// material-layer weights in RGBA channels.
struct TerrainTileData {
  TerrainTileCoord coord{};
  uint32_t resolution = 0u;
  std::vector<float> heights;
  uint32_t color_width = 0u;
  uint32_t color_height = 0u;
  std::vector<uint8_t> color_rgba8;
  uint32_t control_width = 0u;
  uint32_t control_height = 0u;
  std::vector<uint8_t> control_rgba8;
  std::vector<TerrainDataMapTileData> data_maps;

  bool valid() const {
    const std::size_t sample_count =
        static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution);
    const std::size_t color_count =
        static_cast<std::size_t>(color_width) * static_cast<std::size_t>(color_height) * 4u;
    const std::size_t control_count =
        static_cast<std::size_t>(control_width) *
        static_cast<std::size_t>(control_height) * 4u;
    const bool control_valid =
        control_rgba8.empty() ||
        (control_width > 0u &&
         control_height > 0u &&
         control_rgba8.size() == control_count);
    return resolution >= 2u &&
           heights.size() == sample_count &&
           color_width > 0u &&
           color_height > 0u &&
           color_rgba8.size() == color_count &&
           control_valid;
  }
};

/// One terrain tile submission for the current frame.
struct TerrainDrawItem {
  InstanceId instance = kInvalidInstance;
  TerrainId terrain = kInvalidTerrain;
  TerrainTileCoord coord{};
  glm::mat4 transform{1.0f};
  LayerId layer = 0u;
  bool visible = true;
};

/// Backend terrain support flags.
struct TerrainCapabilities {
  bool supported = false;
  bool hardware_tessellation = false;
  bool cpu_fallback = true;
  uint32_t max_tessellation_factor = 64u;
};

/// Per-frame terrain diagnostics from the active backend.
struct TerrainStats {
  uint32_t terrain_count = 0u;
  uint32_t resident_tiles = 0u;
  uint32_t submitted_tiles = 0u;
  uint32_t drawn_tiles = 0u;
  uint32_t culled_tiles = 0u;
  uint32_t upload_count = 0u;
  uint32_t eviction_count = 0u;
  uint32_t tessellated_tiles = 0u;
  uint32_t cpu_fallback_tiles = 0u;
};

}  // namespace karma::renderer
