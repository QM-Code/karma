#ifdef NDEBUG
#undef NDEBUG
#endif

#include <chrono>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "karma/assets.h"
#include "karma/visual.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace {

using karma::rendering::TerrainTileCoord;
using karma::visual::terrain::TileCoordHash;

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

void writeRaw16Le(const std::filesystem::path& path,
                  const std::vector<uint16_t>& samples) {
  std::ofstream stream(path, std::ios::binary);
  for (uint16_t sample : samples) {
    const uint8_t bytes[2] = {
        static_cast<uint8_t>(sample & 0xFFu),
        static_cast<uint8_t>((sample >> 8u) & 0xFFu),
    };
    stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
  }
}

void writeR32Le(const std::filesystem::path& path,
                const std::vector<float>& samples) {
  std::ofstream stream(path, std::ios::binary);
  for (float sample : samples) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &sample, sizeof(bits));
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(bits & 0xFFu),
        static_cast<uint8_t>((bits >> 8u) & 0xFFu),
        static_cast<uint8_t>((bits >> 16u) & 0xFFu),
        static_cast<uint8_t>((bits >> 24u) & 0xFFu),
    };
    stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
  }
}

void testChunkCountMath() {
  const auto coords = karma::visual::terrain::terrainChunkCoordsAround(
      TerrainTileCoord{.x = 0, .z = 0}, 4000.0f, 1000.0f);
  assert(coords.size() == 81u);
  assert(coords.front() == TerrainTileCoord{});
  uint64_t previous_distance = 0u;
  for (const TerrainTileCoord coord : coords) {
    const int64_t x = coord.x;
    const int64_t z = coord.z;
    const uint64_t distance = static_cast<uint64_t>(x * x + z * z);
    assert(distance >= previous_distance);
    previous_distance = distance;
  }
  assert(karma::visual::terrain::kMaxTerrainOutstandingTileRequests == 64u);
}

void testChunkRecenterDelta() {
  const auto a = karma::visual::terrain::terrainChunkCoordsAround(
      TerrainTileCoord{.x = 0, .z = 0}, 1000.0f, 1000.0f);
  const auto b = karma::visual::terrain::terrainChunkCoordsAround(
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

void testTerrainStreamingMathIsBounded() {
  assert(karma::visual::terrain::terrainTileRadius(
             std::numeric_limits<float>::quiet_NaN(), 1000.0f) == 0);
  assert(karma::visual::terrain::terrainTileRadius(1000.0f, 0.0f) == 0);
  assert(karma::visual::terrain::terrainTileRadius(1.0e30f, 0.001f) ==
         karma::visual::terrain::kMaxTerrainStreamingTileRadius);

  const auto edge_coords = karma::visual::terrain::terrainChunkCoordsAround(
      TerrainTileCoord{.x = std::numeric_limits<int32_t>::max(), .z = 0},
      1000.0f,
      1000.0f);
  assert(edge_coords.size() == 6u);
  for (const TerrainTileCoord coord : edge_coords) {
    assert(coord.x <= std::numeric_limits<int32_t>::max());
  }

  karma::components::TerrainComponent terrain{};
  terrain.origin_tile_x = 7;
  terrain.origin_tile_z = -4;
  const TerrainTileCoord invalid_position =
      karma::visual::terrain::terrainTileCoordForWorldPosition(
          {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
          {},
          terrain);
  assert(invalid_position.x == 7);
  assert(invalid_position.z == -4);

  terrain.tile_resolution = std::numeric_limits<uint32_t>::max();
  terrain.height_scale = std::numeric_limits<float>::quiet_NaN();
  terrain.tessellation_factor = std::numeric_limits<float>::infinity();
  const auto desc = karma::visual::terrain::terrainDescFromComponent(terrain);
  assert(desc.tile_resolution == karma::visual::terrain::kMaxTerrainTileResolution);
  assert(nearly(desc.height_scale, 0.0f));
  assert(nearly(desc.max_tessellation_factor, 1.0f));
}

void testProceduralDeterminismAndBorders() {
  karma::components::TerrainComponent terrain{};
  terrain.tile_resolution = 17u;
  terrain.tile_size = 1000.0f;
  terrain.origin_tile_x = -2;
  terrain.origin_tile_z = 5;

  const auto a = karma::visual::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 0, .z = 0});
  const auto a_repeat = karma::visual::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 0, .z = 0});
  const auto b = karma::visual::terrain::generateProceduralTerrainTile(
      terrain, TerrainTileCoord{.x = 1, .z = 0});
  const auto c = karma::visual::terrain::generateProceduralTerrainTile(
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
  karma::assets::Rgba8Image image{};
  image.width = 2;
  image.height = 2;
  image.pixels = {
      0, 0, 0, 255,
      255, 0, 0, 255,
      128, 0, 0, 255,
      64, 0, 0, 255,
  };
  const auto heights = karma::visual::terrain::convertHeightImageToNormalizedHeights(image, 2u);
  assert(heights.size() == 4u);
  assert(nearly(heights[0], 0.0f));
  assert(nearly(heights[1], 1.0f));
  assert(nearly(heights[2], 128.0f / 255.0f));
  assert(nearly(heights[3], 64.0f / 255.0f));
}

void testRaw16ScalarHeightLoading() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path path = dir / "height.raw";
  writeRaw16Le(path, {0u, 65535u, 32768u, 16384u});

  karma::assets::ScalarImageLoadOptions options{};
  options.format = karma::assets::ScalarImageFormat::Raw16Unsigned;
  options.raw_width = 2u;
  options.raw_height = 2u;
  const auto image = karma::assets::loadScalarImage(path, options);
  assert(image.has_value());
  assert(image->valid());
  assert(nearly(image->values[0], 0.0f));
  assert(nearly(image->values[1], 1.0f));
  assert(nearly(image->values[2], 32768.0f / 65535.0f));
  assert(nearly(image->values[3], 16384.0f / 65535.0f));

  std::filesystem::remove_all(dir);
}

void testR32ScalarHeightLoading() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path path = dir / "height.r32";
  writeR32Le(path, {0.0f, 1.0f, 0.5f, 0.25f});

  karma::assets::ScalarImageLoadOptions options{};
  options.format = karma::assets::ScalarImageFormat::R32Float;
  options.raw_width = 2u;
  options.raw_height = 2u;
  const auto image = karma::assets::loadScalarImage(path, options);
  assert(image.has_value());
  assert(image->valid());
  assert(nearly(image->values[0], 0.0f));
  assert(nearly(image->values[1], 1.0f));
  assert(nearly(image->values[2], 0.5f));
  assert(nearly(image->values[3], 0.25f));

  std::filesystem::remove_all(dir);
}

void testTerrainDescClamping() {
  karma::components::TerrainComponent terrain{};
  terrain.tile_size = -10.0f;
  terrain.tile_resolution = 1u;
  terrain.base_patch_size = 0u;
  terrain.tessellation_factor = 100.0f;
  terrain.target_tessellated_edge_size = 0.0f;
  const auto desc = karma::visual::terrain::terrainDescFromComponent(terrain);
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
  const auto desc = karma::visual::terrain::terrainDescFromComponent(terrain);
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

  const auto tile = karma::visual::terrain::loadSingleImageTerrainTile(terrain);
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
  const auto fallback_tile = karma::visual::terrain::loadSingleImageTerrainTile(terrain);
  assert(fallback_tile.has_value());
  assert(fallback_tile->valid());
  assert(fallback_tile->color_rgba8 == heatmap_rgba);

  std::filesystem::remove_all(dir);
}

void testImageTileLoadsControlAndDataMaps() {
  const std::filesystem::path dir = makeTempDir();
  const std::vector<uint8_t> height_rgba{
      0, 0, 0, 255,
      255, 0, 0, 255,
      128, 0, 0, 255,
      64, 0, 0, 255,
  };
  const std::vector<uint8_t> color_rgba{
      10, 20, 30, 255,
      40, 50, 60, 255,
      70, 80, 90, 255,
      100, 110, 120, 255,
  };
  const std::vector<uint8_t> control_rgba{
      255, 0, 0, 255,
      0, 255, 0, 255,
      0, 0, 255, 255,
      0, 0, 0, 255,
  };
  const std::vector<uint8_t> flow_rgba{
      0, 1, 0, 255,
      0, 2, 0, 255,
      0, 3, 0, 255,
      0, 4, 0, 255,
  };
  writeRgbaTga(dir / "height_2_5.tga", 2u, 2u, height_rgba);
  writeRgbaTga(dir / "color_2_5.tga", 2u, 2u, color_rgba);
  writeRgbaTga(dir / "control_2_5.tga", 2u, 2u, control_rgba);
  writeRgbaTga(dir / "flow_2_5.tga", 2u, 2u, flow_rgba);

  karma::components::TerrainComponent terrain{};
  terrain.source = karma::components::TerrainSourceType::ImageTileDirectory;
  terrain.tile_directory = dir;
  terrain.height_pattern = "height_{x}_{y}.tga";
  terrain.color_pattern = "color_{x}_{z}.tga";
  terrain.control_pattern = "control_{tile_x}_{tile_y}.tga";
  terrain.tile_resolution = 2u;
  terrain.material_layers.push_back(karma::components::TerrainMaterialLayer{
      .name = "rock",
      .albedo_image = dir / "color_2_5.tga",
  });
  terrain.data_maps.push_back(karma::components::TerrainDataMapBinding{
      .name = "flow",
      .kind = karma::components::TerrainDataMapKind::Flow,
      .pattern = "flow_{X}_{Y}.tga",
      .channel = 1u,
  });

  const auto tile =
      karma::visual::terrain::loadImageTerrainTile(terrain, TerrainTileCoord{.x = 2, .z = 5});
  assert(tile.has_value());
  assert(tile->valid());
  assert(tile->control_width == 2u);
  assert(tile->control_height == 2u);
  assert(tile->control_rgba8 == control_rgba);
  assert(tile->data_maps.size() == 1u);
  assert(tile->data_maps[0].name == "flow");
  assert(tile->data_maps[0].valid());
  assert(nearly(tile->data_maps[0].values[0], 1.0f / 255.0f));

  std::filesystem::remove_all(dir);
}

void testTerrainMaterialLayerLoading() {
  const std::filesystem::path dir = makeTempDir();
  const std::vector<uint8_t> albedo_rgba{
      20, 40, 60, 255,
      80, 100, 120, 255,
      140, 160, 180, 255,
      200, 220, 240, 255,
  };
  writeRgbaTga(dir / "rock_albedo.tga", 2u, 2u, albedo_rgba);

  const auto layer = karma::visual::terrain::loadTerrainMaterialLayer(
      karma::components::TerrainMaterialLayer{
          .name = "rock",
          .albedo_image = dir / "rock_albedo.tga",
          .uv_scale = 12.0f,
      },
      1u);
  assert(layer.has_value());
  assert(layer->valid());
  assert(layer->layer == 1u);
  assert(layer->name == "rock");
  assert(nearly(layer->uv_scale, 12.0f));
  assert(layer->albedo.width == 2u);
  assert(layer->albedo.height == 2u);
  assert(layer->albedo.rgba8 == albedo_rgba);

  std::filesystem::remove_all(dir);
}

void testTerrainMaterialLayerResolvesMaterialKey() {
  karma::assets::AssetRegistry assets;
  karma::rendering::MaterialDesc ground{};
  ground.base_color = karma::math::Color{0.25f, 0.5f, 0.75f, 1.0f};
  ground.roughness = 0.6f;
  ground.metallic = 0.0f;
  assets.registerMaterialAsset("terrain/ground", ground);

  const auto layer = karma::visual::terrain::loadTerrainMaterialLayer(
      karma::components::TerrainMaterialLayer{
          .material_key = "terrain/ground",
          .uv_scale = 18.0f,
      },
      2u,
      &assets);
  assert(layer.has_value());
  assert(layer->valid());
  assert(layer->layer == 2u);
  assert(layer->name == "terrain/ground");
  assert(nearly(layer->uv_scale, 18.0f));
  assert(layer->albedo.width == 1u);
  assert(layer->albedo.height == 1u);
  assert((layer->albedo.rgba8 == std::vector<uint8_t>{64u, 128u, 191u, 255u}));
  assert(layer->roughness.width == 1u);
  assert(layer->roughness.height == 1u);
  assert((layer->roughness.rgba8 == std::vector<uint8_t>{153u, 153u, 153u, 255u}));
}

void testTerrainMaterialLayerResolvesAssetRegistryMaterialKey() {
  karma::assets::AssetRegistry assets;
  karma::rendering::MaterialDesc ground{};
  ground.base_color = karma::math::Color{0.5f, 0.25f, 0.125f, 1.0f};
  ground.roughness = 0.25f;
  ground.metallic = 0.0f;
  assets.registerMaterialAsset("terrain/asset_ground", ground);

  const auto layer = karma::visual::terrain::loadTerrainMaterialLayer(
      karma::components::TerrainMaterialLayer{
          .material_key = "terrain/asset_ground",
          .uv_scale = 9.0f,
      },
      1u,
      &assets);
  assert(layer.has_value());
  assert(layer->valid());
  assert(layer->layer == 1u);
  assert(layer->name == "terrain/asset_ground");
  assert(nearly(layer->uv_scale, 9.0f));
  assert(layer->albedo.width == 1u);
  assert(layer->albedo.height == 1u);
  assert((layer->albedo.rgba8 == std::vector<uint8_t>{128u, 64u, 32u, 255u}));
  assert(layer->roughness.width == 1u);
  assert(layer->roughness.height == 1u);
  assert((layer->roughness.rgba8 == std::vector<uint8_t>{64u, 64u, 64u, 255u}));
}

void testTerrainColliderSyncCreatesHeightField() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path heatmap_path = dir / "collider_height.tga";
  const std::vector<uint8_t> heatmap_rgba{
      0, 0, 0, 255,
      255, 0, 0, 255,
      128, 0, 0, 255,
      64, 0, 0, 255,
  };
  writeRgbaTga(heatmap_path, 2u, 2u, heatmap_rgba);

  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, karma::components::TransformComponent{});
  world.add(entity, karma::components::ColliderComponent{
                        .is_trigger = true,
                        .debug_draw = true,
                    });
  world.add(entity, karma::components::TerrainComponent{
                        .source = karma::components::TerrainSourceType::SingleImage,
                        .heatmap_image = heatmap_path,
                        .terrain_size = 32.0f,
                        .tile_resolution = 2u,
                        .height_scale = 10.0f,
                        .height_offset = -2.0f,
                    });

  karma::visual::terrain::TerrainSystem system(nullptr);
  system.syncTerrainColliders(world);

  assert(world.has<karma::components::ColliderComponent>(entity));
  const auto& collider = world.get<karma::components::ColliderComponent>(entity);
  assert(collider.type == karma::components::ColliderShapeType::HeightField);
  const auto* height_field =
      std::get_if<karma::components::HeightFieldColliderShape>(&collider.shape);
  assert(height_field != nullptr);
  assert(collider.is_trigger);
  assert(collider.debug_draw);
  assert(height_field->sample_count == 2u);
  assert(height_field->samples.size() == 4u);
  assert(nearly(height_field->samples[0], 0.0f));
  assert(nearly(height_field->samples[1], 1.0f));
  assert(nearly(height_field->samples[2], 128.0f / 255.0f));
  assert(nearly(height_field->samples[3], 64.0f / 255.0f));
  assert(nearly(height_field->offset.x, 0.0f));
  assert(nearly(height_field->offset.y, -2.0f));
  assert(nearly(height_field->offset.z, 0.0f));
  assert(nearly(height_field->scale.x, 32.0f));
  assert(nearly(height_field->scale.y, 10.0f));
  assert(nearly(height_field->scale.z, 32.0f));

  world.remove<karma::components::ColliderComponent>(entity);
  system.syncTerrainColliders(world);
  assert(!world.has<karma::components::ColliderComponent>(entity));

  std::filesystem::remove_all(dir);
}

void testTerrainTilePatternFormatting() {
  const std::string path = karma::visual::terrain::formatTerrainTilePattern(
      "height_{x}_{z}_{x}.png", TerrainTileCoord{.x = -3, .z = 12});
  assert(path == "height_-3_12_-3.png");

  const std::string gaea_path = karma::visual::terrain::formatTerrainTilePattern(
      "tile_{tile_x}_{tile_y}_{X}_{Y}.png",
      TerrainTileCoord{.x = 0, .z = 4},
      1);
  assert(gaea_path == "tile_1_5_1_5.png");

  const std::string positive_boundary =
      karma::visual::terrain::formatTerrainTilePattern(
          "tile_{x}_{z}.png",
          TerrainTileCoord{.x = std::numeric_limits<int32_t>::max(),
                           .z = std::numeric_limits<int32_t>::max()},
          std::numeric_limits<int32_t>::max());
  assert(positive_boundary == "tile_4294967294_4294967294.png");

  const std::string negative_boundary =
      karma::visual::terrain::formatTerrainTilePattern(
          "tile_{x}_{z}.png",
          TerrainTileCoord{.x = std::numeric_limits<int32_t>::min(),
                           .z = std::numeric_limits<int32_t>::min()},
          std::numeric_limits<int32_t>::min());
  assert(negative_boundary == "tile_-4294967296_-4294967296.png");
}

void testGaeaSingleImageImportDetectsOutputs() {
  const std::filesystem::path dir = makeTempDir();
  writeR32Le(dir / "Erosion.r32", {0.0f, 1.0f, 0.5f, 0.25f});
  writeRgbaTga(dir / "Color.tga",
               2u,
               2u,
               {
                   10, 20, 30, 255,
                   40, 50, 60, 255,
                   70, 80, 90, 255,
                   100, 110, 120, 255,
               });
  writeRgbaTga(dir / "Splat.tga",
               2u,
               2u,
               {
                   255, 0, 0, 255,
                   0, 255, 0, 255,
                   0, 0, 255, 255,
                   0, 0, 0, 255,
               });
  writeRgbaTga(dir / "Flow.tga",
               2u,
               2u,
               {
                   1, 0, 0, 255,
                   2, 0, 0, 255,
                   3, 0, 0, 255,
                   4, 0, 0, 255,
               });

  std::string diagnostic;
  const auto terrain = karma::visual::terrain::importGaeaTerrainDirectory(
      karma::visual::terrain::GaeaTerrainImportDesc{
          .directory = dir,
          .raw_width = 2u,
          .raw_height = 2u,
          .terrain_size = 512.0f,
          .tile_resolution = 2u,
          .height_scale = 64.0f,
          .height_offset = -8.0f,
      },
      &diagnostic);
  assert(terrain.has_value());
  assert(diagnostic.empty());
  assert(terrain->source == karma::components::TerrainSourceType::SingleImage);
  assert(terrain->height_image.filename() == "Erosion.r32");
  assert(terrain->height_format == karma::components::TerrainHeightFormat::R32Float);
  assert(terrain->raw_width == 2u);
  assert(terrain->raw_height == 2u);
  assert(terrain->color_image.filename() == "Color.tga");
  assert(terrain->control_image.filename() == "Splat.tga");
  assert(nearly(terrain->terrain_size, 512.0f));
  assert(nearly(terrain->height_scale, 64.0f));
  assert(nearly(terrain->height_offset, -8.0f));
  assert(terrain->data_maps.size() == 1u);
  assert(terrain->data_maps[0].name == "flow");
  assert(terrain->data_maps[0].kind == karma::components::TerrainDataMapKind::Flow);
  assert(terrain->data_maps[0].image.filename() == "Flow.tga");

  const auto tile = karma::visual::terrain::loadSingleImageTerrainTile(*terrain);
  assert(tile.has_value());
  assert(tile->valid());
  assert(tile->data_maps.size() == 1u);
  assert(tile->data_maps[0].name == "flow");
  assert(nearly(tile->data_maps[0].values[0], 1.0f / 255.0f));

  std::filesystem::remove_all(dir);
}

void testGaeaTiledImportInfersPatterns() {
  const std::filesystem::path dir = makeTempDir();
  const std::vector<uint8_t> height_rgba{
      0, 0, 0, 255,
      255, 0, 0, 255,
      128, 0, 0, 255,
      64, 0, 0, 255,
  };
  const std::vector<uint8_t> color_rgba{
      10, 20, 30, 255,
      40, 50, 60, 255,
      70, 80, 90, 255,
      100, 110, 120, 255,
  };
  const std::vector<uint8_t> wear_rgba{
      5, 0, 0, 255,
      6, 0, 0, 255,
      7, 0, 0, 255,
      8, 0, 0, 255,
  };
  writeRgbaTga(dir / "Height_2_5.tga", 2u, 2u, height_rgba);
  writeRgbaTga(dir / "Color_2_5.tga", 2u, 2u, color_rgba);
  writeRgbaTga(dir / "Splat_2_5.tga",
               2u,
               2u,
               {
                   255, 0, 0, 255,
                   0, 255, 0, 255,
                   0, 0, 255, 255,
                   0, 0, 0, 255,
               });
  writeRgbaTga(dir / "Wear_2_5.tga", 2u, 2u, wear_rgba);

  std::string diagnostic;
  const auto terrain = karma::visual::terrain::importGaeaTerrainDirectory(
      karma::visual::terrain::GaeaTerrainImportDesc{
          .directory = dir,
          .tiled = true,
          .tile_size = 256.0f,
          .tile_resolution = 2u,
      },
      &diagnostic);
  assert(terrain.has_value());
  assert(diagnostic.empty());
  assert(terrain->source == karma::components::TerrainSourceType::ImageTileDirectory);
  assert(terrain->tile_directory == dir.lexically_normal());
  assert(terrain->height_pattern == "Height_{x}_{z}.tga");
  assert(terrain->color_pattern == "Color_{x}_{z}.tga");
  assert(terrain->control_pattern == "Splat_{x}_{z}.tga");
  assert(terrain->data_maps.size() == 1u);
  assert(terrain->data_maps[0].kind == karma::components::TerrainDataMapKind::Wear);
  assert(terrain->data_maps[0].pattern == "Wear_{x}_{z}.tga");

  const auto tile =
      karma::visual::terrain::loadImageTerrainTile(*terrain, TerrainTileCoord{.x = 2, .z = 5});
  assert(tile.has_value());
  assert(tile->valid());
  assert(tile->color_rgba8 == color_rgba);
  assert(tile->data_maps.size() == 1u);
  assert(nearly(tile->data_maps[0].values[0], 5.0f / 255.0f));

  std::filesystem::remove_all(dir);
}

void testGaeaImportRejectsUnsupportedHeightFormats() {
  const std::filesystem::path dir = makeTempDir();
  std::ofstream(dir / "Height.exr", std::ios::binary).put('\0');

  std::string diagnostic;
  const auto terrain = karma::visual::terrain::importGaeaTerrainDirectory(
      karma::visual::terrain::GaeaTerrainImportDesc{.directory = dir},
      &diagnostic);
  assert(!terrain.has_value());
  assert(diagnostic.find("EXR/TIFF") != std::string::npos);

  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  testChunkCountMath();
  testChunkRecenterDelta();
  testTerrainStreamingMathIsBounded();
  testProceduralDeterminismAndBorders();
  testImageHeightConversion();
  testRaw16ScalarHeightLoading();
  testR32ScalarHeightLoading();
  testTerrainDescClamping();
  testSingleImageTerrainDescUsesTerrainSize();
  testSingleImageTerrainTileLoadsHeatmapAndColor();
  testImageTileLoadsControlAndDataMaps();
  testTerrainMaterialLayerLoading();
  testTerrainMaterialLayerResolvesMaterialKey();
  testTerrainMaterialLayerResolvesAssetRegistryMaterialKey();
  testTerrainColliderSyncCreatesHeightField();
  testTerrainTilePatternFormatting();
  testGaeaSingleImageImportDetectsOutputs();
  testGaeaTiledImportInfersPatterns();
  testGaeaImportRejectsUnsupportedHeightFormats();
  return 0;
}
