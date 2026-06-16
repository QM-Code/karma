#ifdef NDEBUG
#undef NDEBUG
#endif

#include <chrono>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "karma/features/visual/terrain/terrain_system.h"

namespace {

using karma::renderer::TerrainTileCoord;
using karma::terrain::TileCoordHash;

bool nearly(float a, float b) {
  return std::abs(a - b) < 0.0001f;
}

std::filesystem::path makeTempDir() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_terrain_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeRgbaTga(const std::filesystem::path& path,
                  uint16_t width,
                  uint16_t height,
                  const std::vector<uint8_t>& rgba) {
  assert(rgba.size() == static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height) * 4u);
  uint8_t header[18] = {};
  header[2] = 2u;
  header[12] = static_cast<uint8_t>(width & 0xFFu);
  header[13] = static_cast<uint8_t>((width >> 8u) & 0xFFu);
  header[14] = static_cast<uint8_t>(height & 0xFFu);
  header[15] = static_cast<uint8_t>((height >> 8u) & 0xFFu);
  header[16] = 32u;
  header[17] = 0x28u;

  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(header), sizeof(header));
  for (std::size_t i = 0u; i < rgba.size(); i += 4u) {
    const uint8_t bgra[4] = {
        rgba[i + 2u],
        rgba[i + 1u],
        rgba[i + 0u],
        rgba[i + 3u],
    };
    stream.write(reinterpret_cast<const char*>(bgra), sizeof(bgra));
  }
}

void testChunkCountMath() {
  const auto coords = karma::terrain::terrainChunkCoordsAround(
      TerrainTileCoord{.x = 0, .z = 0}, 4000.0f, 1000.0f);
  assert(coords.size() == 81u);
}

void testChunkRecenterDelta() {
  const auto a = karma::terrain::terrainChunkCoordsAround(
      TerrainTileCoord{.x = 0, .z = 0}, 1000.0f, 1000.0f);
  const auto b = karma::terrain::terrainChunkCoordsAround(
      TerrainTileCoord{.x = 1, .z = 0}, 1000.0f, 1000.0f);
  std::unordered_set<TerrainTileCoord, TileCoordHash> set_a(a.begin(), a.end());
  std::unordered_set<TerrainTileCoord, TileCoordHash> set_b(b.begin(), b.end());

  int evicted = 0;
  int requested = 0;
  for (const TerrainTileCoord& coord : set_a) {
    if (!set_b.contains(coord)) {
      ++evicted;
      assert(coord.x == -1);
    }
  }
  for (const TerrainTileCoord& coord : set_b) {
    if (!set_a.contains(coord)) {
      ++requested;
      assert(coord.x == 2);
    }
  }
  assert(evicted == 3);
  assert(requested == 3);
}

void testProceduralDeterminismAndBorders() {
  karma::components::TerrainComponent terrain{};
  terrain.tile_resolution = 17u;
  terrain.tile_size = 1000.0f;
  terrain.origin_tile_x = -2;
  terrain.origin_tile_z = 5;

  const auto a = karma::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 0, .z = 0});
  const auto a_repeat = karma::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 0, .z = 0});
  const auto b = karma::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 1, .z = 0});
  const auto c = karma::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 0, .z = 1});

  assert(a.valid());
  assert(a_repeat.valid());
  assert(b.valid());
  assert(c.valid());
  assert(a.heights == a_repeat.heights);
  assert(a.color_rgba8 == a_repeat.color_rgba8);

  const uint32_t r = a.resolution;
  for (uint32_t z = 0u; z < r; ++z) {
    const float right_edge = a.heights[static_cast<std::size_t>(z) * r + (r - 1u)];
    const float left_edge = b.heights[static_cast<std::size_t>(z) * r];
    assert(nearly(right_edge, left_edge));
  }
  for (uint32_t x = 0u; x < r; ++x) {
    const float bottom_edge = a.heights[static_cast<std::size_t>(r - 1u) * r + x];
    const float top_edge = c.heights[x];
    assert(nearly(bottom_edge, top_edge));
  }
}

void testImageHeightConversion() {
  karma::content::Rgba8Image image{};
  image.width = 2;
  image.height = 2;
  image.pixels = {
      0, 0, 0, 255,
      255, 0, 0, 255,
      128, 0, 0, 255,
      64, 0, 0, 255,
  };
  const auto heights = karma::terrain::convertHeightImageToNormalizedHeights(image, 2u);
  assert(heights.size() == 4u);
  assert(nearly(heights[0], 0.0f));
  assert(nearly(heights[1], 1.0f));
  assert(nearly(heights[2], 128.0f / 255.0f));
  assert(nearly(heights[3], 64.0f / 255.0f));
}

void testTerrainDescClamping() {
  karma::components::TerrainComponent terrain{};
  terrain.tile_size = -10.0f;
  terrain.tile_resolution = 1u;
  terrain.base_patch_size = 0u;
  terrain.tessellation_factor = 100.0f;
  terrain.target_tessellated_edge_size = 0.0f;
  const auto desc = karma::terrain::terrainDescFromComponent(terrain);
  assert(nearly(desc.tile_size, 0.001f));
  assert(desc.tile_resolution == 2u);
  assert(desc.base_patch_size == 1u);
  assert(nearly(desc.max_tessellation_factor, 64.0f));
  assert(nearly(desc.target_tessellated_edge_size, 1.0f));
}

void testSingleImageTerrainDescUsesTerrainSize() {
  karma::components::TerrainComponent terrain{};
  terrain.source = karma::components::TerrainSourceType::SingleImage;
  terrain.tile_size = 1000.0f;
  terrain.terrain_size = 250.0f;
  const auto desc = karma::terrain::terrainDescFromComponent(terrain);
  assert(nearly(desc.tile_size, 250.0f));
}

void testSingleImageTerrainTileLoadsHeatmapAndColor() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path heatmap_path = dir / "heatmap.tga";
  const std::filesystem::path color_path = dir / "color.tga";
  const std::vector<uint8_t> heatmap_rgba{
      0, 12, 24, 255,
      255, 32, 16, 255,
      128, 80, 40, 255,
      64, 16, 8, 255,
  };
  const std::vector<uint8_t> color_rgba{
      11, 22, 33, 255,
      44, 55, 66, 255,
      77, 88, 99, 255,
      101, 112, 123, 255,
  };
  writeRgbaTga(heatmap_path, 2u, 2u, heatmap_rgba);
  writeRgbaTga(color_path, 2u, 2u, color_rgba);

  karma::components::TerrainComponent terrain{};
  terrain.source = karma::components::TerrainSourceType::SingleImage;
  terrain.heatmap_image = heatmap_path;
  terrain.color_image = color_path;
  terrain.tile_resolution = 2u;
  terrain.origin_tile_x = 4;
  terrain.origin_tile_z = -2;

  const auto tile = karma::terrain::loadSingleImageTerrainTile(terrain);
  assert(tile.has_value());
  assert(tile->valid());
  assert(tile->coord.x == 4);
  assert(tile->coord.z == -2);
  assert(tile->resolution == 2u);
  assert(nearly(tile->heights[0], 0.0f));
  assert(nearly(tile->heights[1], 1.0f));
  assert(nearly(tile->heights[2], 128.0f / 255.0f));
  assert(nearly(tile->heights[3], 64.0f / 255.0f));
  assert(tile->color_rgba8 == color_rgba);

  terrain.color_image.clear();
  const auto fallback_tile = karma::terrain::loadSingleImageTerrainTile(terrain);
  assert(fallback_tile.has_value());
  assert(fallback_tile->valid());
  assert(fallback_tile->color_rgba8 == heatmap_rgba);

  std::filesystem::remove_all(dir);
}

void testTerrainTilePatternFormatting() {
  const std::string path = karma::terrain::formatTerrainTilePattern(
      "height_{x}_{z}_{x}.png", TerrainTileCoord{.x = -3, .z = 12});
  assert(path == "height_-3_12_-3.png");
}

}  // namespace

int main() {
  testChunkCountMath();
  testChunkRecenterDelta();
  testProceduralDeterminismAndBorders();
  testImageHeightConversion();
  testTerrainDescClamping();
  testSingleImageTerrainDescUsesTerrainSize();
  testSingleImageTerrainTileLoadsHeatmapAndColor();
  testTerrainTilePatternFormatting();
  return 0;
}
