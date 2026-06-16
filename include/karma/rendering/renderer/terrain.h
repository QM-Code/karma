#pragma once

#include <cstddef>
#include <cstdint>
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
  float target_tessellated_edge_size = 18.0f;
  bool cpu_fallback_enabled = true;
};

/// Decoded tile payload uploaded to the renderer backend.
///
/// `heights` are normalized scalar samples. Backends apply `height_scale` and
/// `height_offset` from `TerrainDesc`. `color_rgba8` is an orthophoto/albedo
/// tile in top-left row order.
struct TerrainTileData {
  TerrainTileCoord coord{};
  uint32_t resolution = 0u;
  std::vector<float> heights;
  uint32_t color_width = 0u;
  uint32_t color_height = 0u;
  std::vector<uint8_t> color_rgba8;

  bool valid() const {
    const std::size_t sample_count =
        static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution);
    const std::size_t color_count =
        static_cast<std::size_t>(color_width) * static_cast<std::size_t>(color_height) * 4u;
    return resolution >= 2u &&
           heights.size() == sample_count &&
           color_width > 0u &&
           color_height > 0u &&
           color_rgba8.size() == color_count;
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
