#ifdef NDEBUG
#undef NDEBUG
#endif

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/scene_authoring.h"
#include "karma/visual.h"
#include "karma/world.h"

#include <cassert>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>
#include <variant>

namespace {

bool nearly(float a, float b, float epsilon = 0.001f) {
  return std::abs(a - b) <= epsilon;
}

std::filesystem::path makeTempDir() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_scene_authoring_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

karma::scene_authoring::TerrainCanvas makeCanvas(float initial_height = 0.5f) {
  auto canvas = karma::scene_authoring::TerrainCanvas::create(
      karma::scene_authoring::TerrainCanvasDesc{
          .resolution = 5u,
          .control_resolution = 5u,
          .terrain_size = 4.0f,
          .height_scale = 10.0f,
          .height_offset = -2.0f,
      },
      initial_height);
  assert(canvas.has_value());
  return std::move(*canvas);
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void testCreateAndImport() {
  using karma::scene_authoring::TerrainCanvas;
  using karma::scene_authoring::TerrainCanvasDesc;

  assert(!TerrainCanvas::create(TerrainCanvasDesc{.resolution = 6u}).has_value());
  assert(!TerrainCanvas::create(
              TerrainCanvasDesc{.resolution = 5u, .terrain_size = 0.0f})
              .has_value());

  karma::assets::ScalarImage source{};
  source.width = 2;
  source.height = 2;
  source.values = {0.0f, 1.0f, 0.5f, 0.25f};
  auto imported = TerrainCanvas::import(
      TerrainCanvasDesc{
          .resolution = 5u,
          .control_resolution = 3u,
          .terrain_size = 8.0f,
      },
      source);
  assert(imported.has_value());
  assert(imported->valid());
  assert(imported->resolution() == 5u);
  assert(imported->controlResolution() == 3u);
  assert(nearly(imported->sampleNormalizedHeight(0.0f, 0.0f).value(), 0.0f));
  assert(nearly(imported->sampleNormalizedHeight(8.0f, 0.0f).value(), 1.0f));
  assert(nearly(imported->sampleNormalizedHeight(0.0f, 8.0f).value(), 0.5f));
  assert(nearly(imported->sampleNormalizedHeight(8.0f, 8.0f).value(), 0.25f));
  assert(nearly(imported->sampleNormalizedHeight(4.0f, 4.0f).value(), 0.4375f));
  assert(!imported->sampleNormalizedHeight(-0.1f, 0.0f).has_value());

  const auto control = imported->controlRgba8();
  for (std::size_t offset = 0u; offset < control.size(); offset += 4u) {
    assert(control[offset] == 255u);
    assert(control[offset + 1u] == 0u);
    assert(control[offset + 2u] == 0u);
    assert(control[offset + 3u] == 0u);
  }
}

void testSamplingAndRaycast() {
  const auto canvas = makeCanvas();
  const karma::math::Vec3 terrain_origin{10.0f, 5.0f, 20.0f};
  const auto world_height = canvas.sampleWorldHeight(12.0f, 22.0f, terrain_origin);
  assert(world_height.has_value());
  assert(nearly(*world_height, 8.0f));

  const auto hit = canvas.raycast({12.0f, 20.0f, 22.0f},
                                  {0.0f, -2.0f, 0.0f},
                                  terrain_origin,
                                  30.0f);
  assert(hit.has_value());
  assert(nearly(hit->position.x, 12.0f));
  assert(nearly(hit->position.y, 8.0f));
  assert(nearly(hit->position.z, 22.0f));
  assert(nearly(hit->distance, 12.0f));
  assert(nearly(hit->normalized_height, 0.5f));
  assert(nearly(hit->normal.x, 0.0f));
  assert(nearly(hit->normal.y, 1.0f));
  assert(nearly(hit->normal.z, 0.0f));
  assert(!canvas.raycast({20.0f, 20.0f, 30.0f},
                         {0.0f, -1.0f, 0.0f},
                         terrain_origin)
              .has_value());
}

void testSculptModesAndBoundaries() {
  using karma::scene_authoring::TerrainBrush;
  using karma::scene_authoring::TerrainBrushFalloff;
  using karma::scene_authoring::TerrainSculptMode;

  auto canvas = makeCanvas();
  const TerrainBrush one_sample{
      .radius = 0.49f,
      .strength = 0.1f,
      .falloff = TerrainBrushFalloff::Constant,
  };
  assert(canvas.applySculpt(2.0f, 2.0f, TerrainSculptMode::Raise, one_sample));
  assert(nearly(canvas.sampleNormalizedHeight(2.0f, 2.0f).value(), 0.6f));
  assert(canvas.applySculpt(2.0f, 2.0f, TerrainSculptMode::Lower, one_sample));
  assert(nearly(canvas.sampleNormalizedHeight(2.0f, 2.0f).value(), 0.5f));

  TerrainBrush full_strength = one_sample;
  full_strength.strength = 1.0f;
  assert(canvas.applySculpt(
      2.0f, 2.0f, TerrainSculptMode::SetHeight, full_strength, 0.8f));
  assert(nearly(canvas.sampleNormalizedHeight(2.0f, 2.0f).value(), 0.8f));
  assert(canvas.applySculpt(
      2.0f, 2.0f, TerrainSculptMode::Flatten, one_sample, 0.3f));
  assert(nearly(canvas.sampleNormalizedHeight(2.0f, 2.0f).value(), 0.7f));
  assert(canvas.applySculpt(2.0f, 2.0f, TerrainSculptMode::Smooth, full_strength));
  assert(nearly(canvas.sampleNormalizedHeight(2.0f, 2.0f).value(),
                (0.7f + 8.0f * 0.5f) / 9.0f));

  assert(canvas.applySculpt(
      0.0f, 0.0f, TerrainSculptMode::Raise, full_strength));
  assert(nearly(canvas.sampleNormalizedHeight(0.0f, 0.0f).value(), 1.0f));
  assert(!canvas.applySculpt(
      -10.0f, -10.0f, TerrainSculptMode::Raise, full_strength));
}

void testSplatPaintingStaysNormalized() {
  using karma::scene_authoring::TerrainBrush;
  using karma::scene_authoring::TerrainBrushFalloff;

  auto canvas = makeCanvas();
  const TerrainBrush brush{
      .radius = 0.49f,
      .strength = 0.5f,
      .falloff = TerrainBrushFalloff::Constant,
  };
  assert(canvas.paintLayer(2.0f, 2.0f, 1u, brush));
  assert(canvas.paintLayer(2.0f, 2.0f, 2u, brush));
  assert(!canvas.paintLayer(2.0f, 2.0f, 4u, brush));

  const auto control = canvas.controlRgba8();
  const std::size_t center = (2u * 5u + 2u) * 4u;
  const uint32_t sum = static_cast<uint32_t>(control[center]) +
                       static_cast<uint32_t>(control[center + 1u]) +
                       static_cast<uint32_t>(control[center + 2u]) +
                       static_cast<uint32_t>(control[center + 3u]);
  assert(sum == 255u);
  assert(control[center] < 128u);
  assert(control[center + 1u] < 128u);
  assert(control[center + 2u] >= 127u);
  assert(canvas.valid());
}

void testTileAndExports() {
  const std::filesystem::path dir = makeTempDir();
  auto canvas = makeCanvas(0.25f);
  const auto tile = canvas.buildTileData({.x = 4, .z = -7});
  assert(tile.valid());
  assert(tile.coord.x == 4);
  assert(tile.coord.z == -7);
  assert(tile.resolution == 5u);
  assert(tile.heights == std::vector<float>(25u, 0.25f));
  assert(tile.control_width == 5u);
  assert(tile.control_height == 5u);
  assert(tile.control_rgba8 ==
         std::vector<uint8_t>(canvas.controlRgba8().begin(),
                              canvas.controlRgba8().end()));

  const std::filesystem::path height_path = dir / "height.r32";
  const std::filesystem::path control_path = dir / "control.tga";
  std::string diagnostic;
  assert(canvas.saveHeightR32(height_path, &diagnostic));
  assert(canvas.saveHeightR32(height_path, &diagnostic));
  assert(diagnostic.empty());
  assert(canvas.saveControlTga(control_path, &diagnostic));
  assert(canvas.saveControlTga(control_path, &diagnostic));
  assert(diagnostic.empty());

  const auto height = karma::assets::loadScalarImage(
      height_path,
      karma::assets::ScalarImageLoadOptions{
          .format = karma::assets::ScalarImageFormat::R32Float,
          .raw_width = 5u,
          .raw_height = 5u,
      });
  assert(height.has_value());
  assert(height->values == std::vector<float>(25u, 0.25f));
  const auto control = karma::assets::loadRgba8Image(control_path);
  assert(control.has_value());
  assert(control->width == 5);
  assert(control->height == 5);
  assert(control->pixels ==
         std::vector<uint8_t>(canvas.controlRgba8().begin(),
                              canvas.controlRgba8().end()));

  canvas.mutableHeights()[0] = 2.0f;
  assert(!canvas.valid());
  assert(!canvas.saveHeightR32(dir / "invalid.r32", &diagnostic));
  assert(!diagnostic.empty());
  std::filesystem::remove_all(dir);
}

void testExportsAreByteExact() {
  const std::filesystem::path dir = makeTempDir();
  auto created = karma::scene_authoring::TerrainCanvas::create(
      karma::scene_authoring::TerrainCanvasDesc{
          .resolution = 65u,
          .control_resolution = 65u,
          .terrain_size = 64.0f,
      },
      0.0f);
  assert(created.has_value());
  auto canvas = std::move(*created);
  auto heights = canvas.mutableHeights();
  auto control = canvas.mutableControlRgba8();
  for (std::size_t index = 0u; index < heights.size(); ++index) {
    heights[index] = static_cast<float>(index) /
                     static_cast<float>(heights.size() - 1u);
    const std::size_t offset = index * 4u;
    const uint8_t red = static_cast<uint8_t>(index % 64u);
    const uint8_t green = static_cast<uint8_t>((index / 64u) % 64u);
    const uint8_t blue = static_cast<uint8_t>((index / 4096u) % 64u);
    control[offset + 0u] = red;
    control[offset + 1u] = green;
    control[offset + 2u] = blue;
    control[offset + 3u] = static_cast<uint8_t>(255u - red - green - blue);
  }
  assert(canvas.valid());

  const std::filesystem::path height_path = dir / "height.r32";
  const std::filesystem::path control_path = dir / "control.tga";
  std::string diagnostic;
  assert(canvas.saveHeightR32(height_path, &diagnostic));
  assert(canvas.saveControlTga(control_path, &diagnostic));

  std::vector<uint8_t> expected_height;
  expected_height.reserve(heights.size() * sizeof(float));
  for (float height : heights) {
    const uint32_t bits = std::bit_cast<uint32_t>(height);
    expected_height.push_back(static_cast<uint8_t>(bits & 0xFFu));
    expected_height.push_back(static_cast<uint8_t>((bits >> 8u) & 0xFFu));
    expected_height.push_back(static_cast<uint8_t>((bits >> 16u) & 0xFFu));
    expected_height.push_back(static_cast<uint8_t>((bits >> 24u) & 0xFFu));
  }
  assert(readBytes(height_path) == expected_height);

  std::vector<uint8_t> expected_control(18u, 0u);
  expected_control[2] = 2u;
  expected_control[12] = 65u;
  expected_control[14] = 65u;
  expected_control[16] = 32u;
  expected_control[17] = 0x28u;
  expected_control.reserve(18u + control.size());
  for (std::size_t offset = 0u; offset < control.size(); offset += 4u) {
    expected_control.push_back(control[offset + 2u]);
    expected_control.push_back(control[offset + 1u]);
    expected_control.push_back(control[offset + 0u]);
    expected_control.push_back(control[offset + 3u]);
  }
  assert(readBytes(control_path) == expected_control);
  std::filesystem::remove_all(dir);
}

void testRuntimeTileOverrideContract() {
  auto canvas = makeCanvas();
  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, karma::components::TransformComponent{});
  world.add(entity, karma::components::TerrainComponent{
                        .source = karma::components::TerrainSourceType::SingleImage,
                        .height_image = "intentionally_missing.r32",
                        .height_format = karma::components::TerrainHeightFormat::R32Float,
                        .raw_width = 5u,
                        .raw_height = 5u,
                        .terrain_size = 4.0f,
                        .tile_resolution = 5u,
                        .height_scale = 10.0f,
                        .height_offset = -2.0f,
                    });
  world.add(entity, karma::components::ColliderComponent{});
  karma::visual::terrain::TerrainSystem system(nullptr);
  assert(system.setSingleImageTileOverride(entity, canvas.buildTileData()));
  assert(system.hasSingleImageTileOverride(entity));
  system.syncTerrainColliders(world);
  assert(world.has<karma::components::ColliderComponent>(entity));
  const auto& collider = world.get<karma::components::ColliderComponent>(entity);
  const auto* height_field =
      std::get_if<karma::components::HeightFieldColliderShape>(&collider.shape);
  assert(height_field != nullptr);
  assert(height_field->sample_count == 5u);
  assert(height_field->samples == std::vector<float>(25u, 0.5f));

  system.clearSingleImageTileOverride(entity);
  assert(!system.hasSingleImageTileOverride(entity));
  system.syncTerrainColliders(world);
  assert(!world.has<karma::components::ColliderComponent>(entity));
  assert(!system.setSingleImageTileOverride(entity, {}));
  auto out_of_range_tile = canvas.buildTileData();
  out_of_range_tile.heights.front() = 1.01f;
  assert(!system.setSingleImageTileOverride(
      world, entity, std::move(out_of_range_tile)));

  karma::visual::terrain::TerrainRuntimeModule module;
  assert(module.setSingleImageTileOverride(entity, canvas.buildTileData()));
  module.clearSingleImageTileOverride(entity);
  world.add(entity, karma::components::ColliderComponent{});
  assert(module.setSingleImageTileOverride(world, entity,
                                           canvas.buildTileData()));
  module.onAttach({});
  module.onFrameBegin(world, 0.0f);
  assert(world.get<karma::components::ColliderComponent>(entity).type ==
         karma::components::ColliderShapeType::HeightField);
  module.clearSingleImageTileOverride(world, entity);
  module.onFrameBegin(world, 0.0f);
  assert(!world.has<karma::components::ColliderComponent>(entity));
  module.onDetach();

  const karma::world::Entity stale_entity = world.createEntity();
  assert(system.setSingleImageTileOverride(stale_entity, canvas.buildTileData()));
  world.destroyEntity(stale_entity);
  system.update(world, 0.0f, 1.0f);
  assert(!system.hasSingleImageTileOverride(stale_entity));

  karma::world::World first_world;
  const karma::world::Entity first_entity = first_world.createEntity();
  first_world.add(first_entity, karma::components::TransformComponent{});
  first_world.add(first_entity, karma::components::TerrainComponent{
                                    .source = karma::components::TerrainSourceType::SingleImage,
                                    .height_image = "missing_first_world.r32",
                                    .height_format =
                                        karma::components::TerrainHeightFormat::R32Float,
                                    .raw_width = 5u,
                                    .raw_height = 5u,
                                    .terrain_size = 4.0f,
                                    .tile_resolution = 5u,
                                });
  first_world.add(first_entity, karma::components::ColliderComponent{});
  karma::visual::terrain::TerrainSystem switching_system(nullptr);
  assert(switching_system.setSingleImageTileOverride(first_entity,
                                                     canvas.buildTileData()));
  switching_system.update(first_world, 0.0f, 1.0f);
  assert(first_world.get<karma::components::ColliderComponent>(first_entity).type ==
         karma::components::ColliderShapeType::HeightField);

  karma::world::World second_world;
  const karma::world::Entity second_entity = second_world.createEntity();
  assert(second_entity == first_entity);
  second_world.add(second_entity, karma::components::TransformComponent{});
  second_world.add(second_entity,
                   karma::components::TerrainComponent{
                       .source = karma::components::TerrainSourceType::SingleImage,
                       .height_image = "missing_second_world.r32",
                       .height_format =
                           karma::components::TerrainHeightFormat::R32Float,
                       .raw_width = 5u,
                       .raw_height = 5u,
                       .terrain_size = 4.0f,
                       .tile_resolution = 5u,
                   });
  second_world.add(second_entity, karma::components::ColliderComponent{});
  auto second_canvas = makeCanvas(0.75f);
  assert(switching_system.setSingleImageTileOverride(
      second_world, second_entity, second_canvas.buildTileData()));
  assert(switching_system.hasSingleImageTileOverride(second_world,
                                                     second_entity));
  switching_system.update(second_world, 0.0f, 1.0f);
  assert(!switching_system.hasSingleImageTileOverride(first_world,
                                                      first_entity));
  assert(switching_system.hasSingleImageTileOverride(second_world,
                                                     second_entity));
  const auto& second_collider =
      second_world.get<karma::components::ColliderComponent>(second_entity);
  const auto* second_height_field =
      std::get_if<karma::components::HeightFieldColliderShape>(
          &second_collider.shape);
  assert(second_height_field != nullptr);
  assert(second_height_field->samples == std::vector<float>(25u, 0.75f));

  karma::world::World third_world;
  const karma::world::Entity third_entity = third_world.createEntity();
  assert(third_entity == first_entity);
  third_world.add(third_entity, karma::components::TransformComponent{});
  third_world.add(third_entity,
                  karma::components::TerrainComponent{
                      .source = karma::components::TerrainSourceType::SingleImage,
                      .height_image = "missing_third_world.r32",
                      .height_format =
                          karma::components::TerrainHeightFormat::R32Float,
                      .raw_width = 5u,
                      .raw_height = 5u,
                      .terrain_size = 4.0f,
                      .tile_resolution = 5u,
                  });
  third_world.add(third_entity, karma::components::ColliderComponent{});
  switching_system.update(third_world, 0.0f, 1.0f);
  assert(!switching_system.hasSingleImageTileOverride(second_world,
                                                      second_entity));
  assert(!switching_system.hasSingleImageTileOverride(third_world,
                                                      third_entity));
  assert(third_world.get<karma::components::ColliderComponent>(third_entity).type !=
         karma::components::ColliderShapeType::HeightField);
}

}  // namespace

int main() {
  testCreateAndImport();
  testSamplingAndRaycast();
  testSculptModesAndBoundaries();
  testSplatPaintingStaysNormalized();
  testTileAndExports();
  testExportsAreByteExact();
  testRuntimeTileOverrideContract();
  return 0;
}
