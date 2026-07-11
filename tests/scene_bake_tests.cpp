#include "karma/scenes.h"
#include "karma/assets.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {

#define KARMA_REQUIRE(expression)                                      \
  do {                                                                \
    if (!(expression)) {                                               \
      std::cerr << "Requirement failed: " << #expression << " at "   \
                << __FILE__ << ":" << __LINE__ << '\n';              \
      std::abort();                                                    \
    }                                                                 \
  } while (false)

std::filesystem::path makeTempDir() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_scene_bake_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
                              std::istreambuf_iterator<char>());
}

std::string gridObj(uint32_t cells) {
  std::ostringstream out;
  for (uint32_t z = 0u; z <= cells; ++z) {
    for (uint32_t x = 0u; x <= cells; ++x) {
      const float fx = -1.0f + 2.0f * static_cast<float>(x) / cells;
      const float fz = -1.0f + 2.0f * static_cast<float>(z) / cells;
      out << "v " << fx << " 0 " << fz << '\n';
    }
  }
  const uint32_t stride = cells + 1u;
  for (uint32_t z = 0u; z < cells; ++z) {
    for (uint32_t x = 0u; x < cells; ++x) {
      const uint32_t a = z * stride + x + 1u;
      const uint32_t b = a + stride;
      const uint32_t c = a + 1u;
      const uint32_t d = b + 1u;
      out << "f " << a << ' ' << b << ' ' << c << '\n';
      out << "f " << c << ' ' << b << ' ' << d << '\n';
    }
  }
  return out.str();
}

struct LightingFixture {
  std::filesystem::path directory;
  karma::scenes::SceneDocument document;
};

LightingFixture lightingFixture(bool blocker_shadows = false,
                                bool dense_blocker = false) {
  LightingFixture fixture{};
  fixture.directory = makeTempDir();
  writeText(fixture.directory / "plane.obj", R"(v -1 0 -1
v -1 0 1
v 1 0 -1
v 1 0 1
f 1 2 3
f 3 2 4
)");
  writeText(fixture.directory / "blocker.obj",
            dense_blocker ? gridObj(20u) : gridObj(1u));
  writeText(fixture.directory / "assets.package.json", R"({
  "version": 1,
  "assets": [
    {"type": "mesh", "key": "tests/bake/plane", "path": "plane.obj"},
    {"type": "mesh", "key": "tests/bake/blocker", "path": "blocker.obj"}
  ]
})");

  auto& document = fixture.document;
  document.name = "CPU Lighting Fixture";
  document.source_path = fixture.directory / "fixture.kscene.json";
  document.asset_packages.push_back(karma::scenes::SceneAssetRef{
      .id = "lighting_assets",
      .path = "assets.package.json",
      .type = "asset_package",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "target",
      .name = "Target",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "zz_blocker",
      .name = "Blocker",
      .transform = karma::scenes::SceneTransform{
          .position = {0.0f, 0.5f, 0.0f},
      },
  });
  constexpr float kHalfSqrtTwo = 0.70710678118f;
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "directional_entity",
      .name = "Directional",
      .transform = karma::scenes::SceneTransform{
          .rotation = {-kHalfSqrtTwo, 0.0f, 0.0f, kHalfSqrtTwo},
      },
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "point_entity",
      .name = "Point",
      .transform = karma::scenes::SceneTransform{
          .position = {0.0f, 3.0f, 0.0f},
      },
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "spot_entity",
      .name = "Spot",
      .transform = karma::scenes::SceneTransform{
          .position = {0.0f, 3.0f, 0.0f},
          .rotation = {-kHalfSqrtTwo, 0.0f, 0.0f, kHalfSqrtTwo},
      },
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "target_static",
      .entity_id = "target",
      .mesh_asset_key = "tests/bake/plane",
      .render = true,
      .lighting = true,
      .casts_shadows = true,
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "blocker_static",
      .entity_id = "zz_blocker",
      .mesh_asset_key = "tests/bake/blocker",
      .render = true,
      .lighting = false,
      .casts_shadows = blocker_shadows,
  });
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "a_directional",
      .entity_id = "directional_entity",
      .component = karma::components::LightComponent{
          .type = karma::components::LightComponent::Type::Directional,
          .bake_mode = karma::components::LightComponent::BakeMode::Mixed,
          .intensity = 0.2f,
          .casts_shadows = blocker_shadows,
      },
  });
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "z_point",
      .entity_id = "point_entity",
      .component = karma::components::LightComponent{
          .type = karma::components::LightComponent::Type::Point,
          .bake_mode = karma::components::LightComponent::BakeMode::Mixed,
          .intensity = 0.25f,
          .range = 10.0f,
          .casts_shadows = blocker_shadows,
      },
  });
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "m_spot",
      .entity_id = "spot_entity",
      .component = karma::components::LightComponent{
          .type = karma::components::LightComponent::Type::Spot,
          .bake_mode = karma::components::LightComponent::BakeMode::Baked,
          .intensity = 0.15f,
          .range = 10.0f,
          .inner_cone_degrees = 25.0f,
          .outer_cone_degrees = 45.0f,
          .casts_shadows = blocker_shadows,
      },
  });
  document.bakes.push_back(karma::scenes::SceneBakeDesc{
      .id = "lighting",
      .path = "bakes/lighting.kbake.json",
      .lighting = karma::scenes::SceneLightmapBakeSettings{
          .enabled = true,
          .generate_uv1 = true,
          .texels_per_unit = 8.0f,
          .max_atlas_size = 32u,
          .padding = 1u,
          .dilation = 1u,
          .sky_samples = 4u,
          .ao_max_distance = 2.0f,
          .directional = false,
      },
      .navigation = karma::scenes::SceneNavigationBakeSettings{.enabled = false},
  });
  return fixture;
}

const karma::scenes::SceneAssetRef& producedAsset(
    const karma::scenes::SceneBakeResult& result,
    std::string_view type) {
  const auto found = std::find_if(
      result.produced_assets.begin(),
      result.produced_assets.end(),
      [&](const auto& asset) { return asset.type == type; });
  KARMA_REQUIRE(found != result.produced_assets.end());
  return *found;
}

uint64_t rgbSum(const karma::assets::Rgba8Image& image) {
  uint64_t sum = 0u;
  for (size_t index = 0u; index + 3u < image.pixels.size(); index += 4u) {
    sum += image.pixels[index];
    sum += image.pixels[index + 1u];
    sum += image.pixels[index + 2u];
  }
  return sum;
}

void addInstancedBlocker(LightingFixture& fixture, bool planar) {
  using karma::components::StaticComponentLighting;
  using karma::components::StaticComponentShadows;
  nlohmann::json instances = nlohmann::json::array();
  nlohmann::json planar_instances = nlohmann::json::array();
  if (planar) {
    planar_instances.push_back({
        {"position", {0.0f, 0.25f, 0.0f}},
        {"yaw_radians", 0.0f},
        {"scale", {1.0f, 1.0f, 1.0f}},
        {"params", {0.0f, 0.0f, 0.0f, 0.0f}},
    });
  } else {
    instances.push_back({
        {"position", {0.0f, 0.25f, 0.0f}},
        {"rotation", {0.0f, 0.0f, 0.0f, 1.0f}},
        {"scale", {1.0f, 1.0f, 1.0f}},
        {"params", {0.0f, 0.0f, 0.0f, 0.0f}},
    });
  }
  fixture.document.entities.push_back(karma::scenes::SceneEntity{
      .id = planar ? "instanced_planar_blocker" : "instanced_matrix_blocker",
      .name = "Instanced Blocker",
      .transform = karma::scenes::SceneTransform{
          .position = {0.0f, 0.25f, 0.0f},
      },
      .components = {
          {"StaticComponent",
           {{"enabled", true},
            {"include_descendants", false},
            {"flags",
             static_cast<uint32_t>(StaticComponentLighting |
                                   StaticComponentShadows)}}},
          {"InstancedMeshComponent",
           {{"mesh_asset_key", "tests/bake/blocker"},
            {"materials", nlohmann::json::array()},
            {"gpu_layout",
             planar ? "position_yaw_scale_params" : "matrix4x4_params"},
            {"instances", std::move(instances)},
            {"planar_instances", std::move(planar_instances)},
            {"instance_revision", 1u},
            {"dynamic", false},
            {"visible", true},
            {"shadow_visible", true}}},
      },
  });
}

std::string sceneJson(std::string_view root_name) {
  return std::string(R"({
  "version": 1,
  "name": "Bake Fixture",
  "entities": [
    {
      "id": "root",
      "name": ")") + std::string(root_name) + R"(",
      "transform": {
        "position": [1.0, 2.0, 3.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      }
    }
  ],
  "static": [
    {
      "id": "root_static",
      "entity": "root",
      "transform": true,
      "render": false,
      "lighting": true
    }
  ],
  "bakes": [
    {
      "id": "main",
      "path": "bakes/main.kbake.json",
      "static": ["root_static"],
      "nav_cache": ["bakes/main.knav"]
    }
  ]
})";
}

karma::scenes::SceneDocument loadDocument(const std::filesystem::path& path) {
  karma::scenes::SceneLoadResult load = karma::scenes::loadSceneDocument(path);
  if (!load.success()) {
    for (const std::string& diagnostic : load.diagnostics) {
      std::cerr << diagnostic << '\n';
    }
  }
  KARMA_REQUIRE(load.success());
  return *load.document;
}

void testSceneBakeOutputIsDeterministic() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_path = dir / "fixture.kscene.json";
  writeText(scene_path, sceneJson("Root"));
  writeText(dir / "bakes/main.knav", "nav-cache-v1");

  const karma::scenes::SceneDocument document = loadDocument(scene_path);
  KARMA_REQUIRE(document.bakes.size() == 1u);

  const karma::scenes::SceneBakeResult first =
      karma::scenes::bakeScene(document, document.bakes.front());
  const karma::scenes::SceneBakeResult second =
      karma::scenes::bakeScene(document, document.bakes.front());

  KARMA_REQUIRE(first.success);
  KARMA_REQUIRE(second.success);
  KARMA_REQUIRE(!first.scene_fingerprint.empty());
  KARMA_REQUIRE(first.scene_fingerprint == second.scene_fingerprint);
  KARMA_REQUIRE(first.metadata.dump(2) == second.metadata.dump(2));
  KARMA_REQUIRE(first.metadata["schema"] == "karma.scene_bake");
  KARMA_REQUIRE(first.metadata["version"] == 2u);
  KARMA_REQUIRE(first.metadata["static_ids"].size() == 1u);
  KARMA_REQUIRE(first.metadata["static_metadata"]["transforms"].size() == 1u);
  KARMA_REQUIRE(first.metadata["nav_cache_files"].size() == 1u);
  KARMA_REQUIRE(first.metadata["lightmap_bindings"].empty());
  KARMA_REQUIRE(first.metadata["baked_lighting"].empty());
  KARMA_REQUIRE(!first.metadata["lighting_output"]["generated"].get<bool>());
}

void testSceneBakeFingerprintChangesWhenSourceChanges() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_path = dir / "fixture.kscene.json";
  writeText(scene_path, sceneJson("Root"));
  writeText(dir / "bakes/main.knav", "nav-cache-v1");

  const karma::scenes::SceneDocument original = loadDocument(scene_path);
  const karma::scenes::SceneBakeResult original_bake =
      karma::scenes::bakeScene(original, original.bakes.front());
  KARMA_REQUIRE(original_bake.success);

  writeText(scene_path, sceneJson("Changed Root"));
  const karma::scenes::SceneDocument changed = loadDocument(scene_path);
  const karma::scenes::SceneBakeResult changed_bake =
      karma::scenes::bakeScene(changed, changed.bakes.front());
  KARMA_REQUIRE(changed_bake.success);
  KARMA_REQUIRE(original_bake.scene_fingerprint != changed_bake.scene_fingerprint);
}

void testSceneBakeSettingsRoundTripAndFingerprint() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_path = dir / "fixture.kscene.json";
  writeText(scene_path, sceneJson("Root"));
  writeText(dir / "bakes/main.knav", "nav-cache-v1");

  karma::scenes::SceneDocument document = loadDocument(scene_path);
  KARMA_REQUIRE(document.bakes.front().enabled);
  KARMA_REQUIRE(document.bakes.front().load_at_runtime);
  KARMA_REQUIRE(document.bakes.front().lighting.enabled);
  KARMA_REQUIRE(document.bakes.front().lighting.generate_uv1);
  KARMA_REQUIRE(document.bakes.front().lighting.texels_per_unit == 16.0f);
  KARMA_REQUIRE(document.bakes.front().lighting.max_atlas_size == 2048u);
  KARMA_REQUIRE(document.bakes.front().navigation.enabled);

  const karma::scenes::SceneBakeResult baseline =
      karma::scenes::bakeScene(document, document.bakes.front());
  KARMA_REQUIRE(baseline.success);

  document.bakes.front().lighting.texels_per_unit = 24.0f;
  document.bakes.front().lighting.max_atlas_size = 4096u;
  document.bakes.front().lighting.directional = false;
  document.bakes.front().navigation.enabled = false;
  document.bakes.front().baked_lighting.entity_id = "root";
  document.bakes.front().baked_lighting.lightmap_path =
      "bakes/not-actually-generated.png";
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "baked_light",
      .entity_id = "root",
      .component = karma::components::LightComponent{
          .type = karma::components::LightComponent::Type::Point,
          .bake_mode = karma::components::LightComponent::BakeMode::Mixed,
      },
  });

  const karma::scenes::SceneBakeResult changed =
      karma::scenes::bakeScene(document, document.bakes.front());
  KARMA_REQUIRE(changed.success);
  KARMA_REQUIRE(changed.scene_fingerprint != baseline.scene_fingerprint);
  KARMA_REQUIRE(changed.lightmap_bindings.empty());
  KARMA_REQUIRE(changed.baked_lighting.empty());
  KARMA_REQUIRE(changed.metadata["lightmap_bindings"].empty());
  KARMA_REQUIRE(changed.metadata["baked_lighting"].empty());

  const std::filesystem::path saved_path = dir / "roundtrip.kscene.json";
  KARMA_REQUIRE(karma::scenes::saveSceneDocument(document, saved_path).success());
  const karma::scenes::SceneDocument restored = loadDocument(saved_path);
  KARMA_REQUIRE(restored.bakes.front().lighting.texels_per_unit == 24.0f);
  KARMA_REQUIRE(restored.bakes.front().lighting.max_atlas_size == 4096u);
  KARMA_REQUIRE(!restored.bakes.front().lighting.directional);
  KARMA_REQUIRE(!restored.bakes.front().navigation.enabled);
  KARMA_REQUIRE(restored.lights.front().component.bake_mode ==
                karma::components::LightComponent::BakeMode::Mixed);
}

void testSceneBakeProgressAndCancellation() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_path = dir / "fixture.kscene.json";
  writeText(scene_path, sceneJson("Root"));
  const karma::scenes::SceneDocument document = loadDocument(scene_path);

  std::vector<karma::scenes::SceneBakeProgress> progress;
  const karma::scenes::SceneBakeResult completed = karma::scenes::bakeScene(
      document,
      document.bakes.front(),
      karma::scenes::SceneBakeExecutionOptions{
          .on_progress = [&](const karma::scenes::SceneBakeProgress& value) {
            progress.push_back(value);
          },
      });
  KARMA_REQUIRE(completed.success);
  KARMA_REQUIRE(!completed.cancelled);
  KARMA_REQUIRE(!progress.empty());
  KARMA_REQUIRE(progress.front().stage ==
                karma::scenes::SceneBakeStage::Preparing);
  KARMA_REQUIRE(progress.back().stage ==
                karma::scenes::SceneBakeStage::Complete);
  KARMA_REQUIRE(progress.back().current == progress.back().total);

  bool cancel = false;
  size_t callback_count = 0u;
  const karma::scenes::SceneBakeResult cancelled = karma::scenes::bakeScene(
      document,
      document.bakes.front(),
      karma::scenes::SceneBakeExecutionOptions{
          .is_cancelled = [&] { return cancel; },
          .on_progress = [&](const karma::scenes::SceneBakeProgress&) {
            ++callback_count;
            cancel = true;
          },
      });
  KARMA_REQUIRE(!cancelled.success);
  KARMA_REQUIRE(cancelled.cancelled);
  KARMA_REQUIRE(callback_count == 1u);
  KARMA_REQUIRE(cancelled.diagnostic.find("cancelled") != std::string::npos);
}

void testCpuLightingArtifactsAndDeterminism() {
  LightingFixture fixture = lightingFixture();
  fixture.document.bakes.front().lighting.directional = true;

  const karma::scenes::SceneBakeResult first = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(first.success);
  KARMA_REQUIRE(first.lightmap_bindings.size() == 1u);
  KARMA_REQUIRE(first.produced_assets.size() == 3u);
  KARMA_REQUIRE(first.mixed_light_ids ==
                std::vector<std::string>({"scene_light:a_directional",
                                          "scene_light:z_point"}));
  KARMA_REQUIRE(first.lightmap_bindings.front().mixed_light_mask == 3u);
  KARMA_REQUIRE(!first.lightmap_bindings.front().direction_asset_key.empty());
  KARMA_REQUIRE(first.metadata["produced_assets"].size() == 3u);
  KARMA_REQUIRE(first.metadata["lighting_output"]["generated"].get<bool>());

  const auto& mesh_asset = producedAsset(first, "baked_mesh");
  const auto& irradiance_asset =
      producedAsset(first, "baked_irradiance_rgba8");
  const auto& direction_asset = producedAsset(first, "baked_direction_rgba8");
  KARMA_REQUIRE(!mesh_asset.path.is_absolute());
  KARMA_REQUIRE(mesh_asset.path.extension() == ".kbmesh");
  std::string diagnostic;
  const auto mesh = karma::assets::loadBakedMeshArtifact(
      fixture.directory / mesh_asset.path, &diagnostic);
  KARMA_REQUIRE(mesh.has_value());
  KARMA_REQUIRE(diagnostic.empty());
  KARMA_REQUIRE(mesh->uvs1.size() == mesh->vertices.size());
  KARMA_REQUIRE(mesh->vertices.size() == 6u);

  const auto irradiance = karma::assets::loadBakedRgba8Artifact(
      fixture.directory / irradiance_asset.path, &diagnostic);
  KARMA_REQUIRE(irradiance.has_value());
  KARMA_REQUIRE(irradiance->valid());
  KARMA_REQUIRE(rgbSum(*irradiance) > 0u);
  const auto direction = karma::assets::loadBakedRgba8Artifact(
      fixture.directory / direction_asset.path, &diagnostic);
  KARMA_REQUIRE(direction.has_value());
  KARMA_REQUIRE(direction->width == irradiance->width);
  KARMA_REQUIRE(direction->height == irradiance->height);
  bool has_directional_strength = false;
  for (size_t index = 3u; index < direction->pixels.size(); index += 4u) {
    has_directional_strength |= direction->pixels[index] != 0u;
  }
  KARMA_REQUIRE(has_directional_strength);

  const std::vector<uint8_t> mesh_bytes =
      readBytes(fixture.directory / mesh_asset.path);
  const std::vector<uint8_t> irradiance_bytes =
      readBytes(fixture.directory / irradiance_asset.path);
  const std::vector<uint8_t> direction_bytes =
      readBytes(fixture.directory / direction_asset.path);
  const karma::scenes::SceneBakeResult second = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(second.success);
  KARMA_REQUIRE(first.scene_fingerprint == second.scene_fingerprint);
  KARMA_REQUIRE(first.metadata.dump() == second.metadata.dump());
  KARMA_REQUIRE(mesh_bytes == readBytes(fixture.directory / mesh_asset.path));
  KARMA_REQUIRE(irradiance_bytes ==
                readBytes(fixture.directory / irradiance_asset.path));
  KARMA_REQUIRE(direction_bytes ==
                readBytes(fixture.directory / direction_asset.path));
}

void testCpuLightingModesFilteringAndStageSelection() {
  LightingFixture fixture = lightingFixture();
  fixture.document.bakes.front().lighting.directional = false;
  for (auto& light : fixture.document.lights) {
    light.component.bake_mode =
        karma::components::LightComponent::BakeMode::Realtime;
  }
  const karma::scenes::SceneBakeResult realtime = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(realtime.success);
  KARMA_REQUIRE(realtime.lightmap_bindings.size() == 1u);
  KARMA_REQUIRE(realtime.produced_assets.size() == 2u);
  KARMA_REQUIRE(realtime.mixed_light_ids.empty());
  KARMA_REQUIRE(realtime.lightmap_bindings.front().mixed_light_mask == 0u);
  std::string diagnostic;
  const auto realtime_image = karma::assets::loadBakedRgba8Artifact(
      fixture.directory /
          producedAsset(realtime, "baked_irradiance_rgba8").path,
      &diagnostic);
  KARMA_REQUIRE(realtime_image.has_value());
  const uint64_t realtime_sum = rgbSum(*realtime_image);

  for (auto& light : fixture.document.lights) {
    light.component.bake_mode =
        karma::components::LightComponent::BakeMode::Baked;
  }
  const karma::scenes::SceneBakeResult baked = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(baked.success);
  KARMA_REQUIRE(baked.lightmap_bindings.size() == 1u);
  const auto baked_image = karma::assets::loadBakedRgba8Artifact(
      fixture.directory / producedAsset(baked, "baked_irradiance_rgba8").path,
      &diagnostic);
  KARMA_REQUIRE(baked_image.has_value());
  KARMA_REQUIRE(rgbSum(*baked_image) > realtime_sum);

  const karma::scenes::SceneBakeResult skipped = karma::scenes::bakeScene(
      fixture.document,
      fixture.document.bakes.front(),
      karma::scenes::SceneBakeExecutionOptions{
          .bake_lighting = false,
          .bake_navigation = false,
      });
  KARMA_REQUIRE(skipped.success);
  KARMA_REQUIRE(skipped.produced_assets.empty());
  KARMA_REQUIRE(skipped.lightmap_bindings.empty());
  KARMA_REQUIRE(skipped.metadata["lighting_output"]["status"] ==
                "not_requested");
  KARMA_REQUIRE(skipped.metadata["navigation_output"]["status"] ==
                "not_requested");
}

void testShadowFilteringAndBvhWorkBound() {
  LightingFixture unblocked = lightingFixture(false, true);
  unblocked.document.bakes.front().lighting.directional = false;
  const karma::scenes::SceneBakeResult open = karma::scenes::bakeScene(
      unblocked.document, unblocked.document.bakes.front());
  KARMA_REQUIRE(open.success);
  std::string diagnostic;
  const auto open_image = karma::assets::loadBakedRgba8Artifact(
      unblocked.directory /
          producedAsset(open, "baked_irradiance_rgba8").path,
      &diagnostic);
  KARMA_REQUIRE(open_image.has_value());

  LightingFixture blocked = lightingFixture(true, true);
  blocked.document.bakes.front().lighting.directional = false;
  const karma::scenes::SceneBakeResult shadowed = karma::scenes::bakeScene(
      blocked.document, blocked.document.bakes.front());
  KARMA_REQUIRE(shadowed.success);
  const auto shadowed_image = karma::assets::loadBakedRgba8Artifact(
      blocked.directory /
          producedAsset(shadowed, "baked_irradiance_rgba8").path,
      &diagnostic);
  KARMA_REQUIRE(shadowed_image.has_value());
  KARMA_REQUIRE(rgbSum(*shadowed_image) < rgbSum(*open_image));
  KARMA_REQUIRE(shadowed.lighting_statistics.ray_queries > 0u);
  KARMA_REQUIRE(shadowed.lighting_statistics.bvh_node_visits > 0u);
  KARMA_REQUIRE(shadowed.lighting_statistics.triangle_tests <
                shadowed.lighting_statistics.ray_queries * 64u);
}

void testInstancedStaticShadowOccludersAndReceiverWarning() {
  LightingFixture unblocked = lightingFixture(true, false);
  unblocked.document.static_components[1].casts_shadows = false;
  unblocked.document.bakes.front().lighting.directional = false;
  const karma::scenes::SceneBakeResult open = karma::scenes::bakeScene(
      unblocked.document, unblocked.document.bakes.front());
  KARMA_REQUIRE(open.success);
  std::string diagnostic;
  const auto open_image = karma::assets::loadBakedRgba8Artifact(
      unblocked.directory /
          producedAsset(open, "baked_irradiance_rgba8").path,
      &diagnostic);
  KARMA_REQUIRE(open_image.has_value());

  for (const bool planar : {false, true}) {
    LightingFixture blocked = lightingFixture(true, false);
    blocked.document.static_components[1].casts_shadows = false;
    blocked.document.bakes.front().lighting.directional = false;
    addInstancedBlocker(blocked, planar);
    const karma::scenes::SceneBakeResult shadowed = karma::scenes::bakeScene(
        blocked.document, blocked.document.bakes.front());
    KARMA_REQUIRE(shadowed.success);
    KARMA_REQUIRE(shadowed.lightmap_bindings.size() == 1u);
    KARMA_REQUIRE(shadowed.lighting_warnings.size() == 1u);
    KARMA_REQUIRE(shadowed.lighting_warnings.front().find(
                      "no per-instance lightmap binding") !=
                  std::string::npos);
    KARMA_REQUIRE(shadowed.metadata["lighting_output"]["warnings"].size() ==
                  1u);
    const auto shadowed_image = karma::assets::loadBakedRgba8Artifact(
        blocked.directory /
            producedAsset(shadowed, "baked_irradiance_rgba8").path,
        &diagnostic);
    KARMA_REQUIRE(shadowed_image.has_value());
    KARMA_REQUIRE(rgbSum(*shadowed_image) < rgbSum(*open_image));
  }
}

void testArtifactTransactionPreservesPreviousBakeOnFailure() {
  LightingFixture fixture = lightingFixture(false, true);
  fixture.document.bakes.front().lighting.directional = false;
  const karma::scenes::SceneBakeResult baseline = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(baseline.success);
  const auto mesh_path =
      fixture.directory / producedAsset(baseline, "baked_mesh").path;
  const auto irradiance_path =
      fixture.directory /
      producedAsset(baseline, "baked_irradiance_rgba8").path;
  const std::vector<uint8_t> mesh_bytes = readBytes(mesh_path);
  const std::vector<uint8_t> irradiance_bytes = readBytes(irradiance_path);

  fixture.document.static_components[1].lighting = true;
  const karma::scenes::SceneBakeResult failed = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(!failed.success);
  KARMA_REQUIRE(failed.diagnostic.find("atlas limit") != std::string::npos);
  KARMA_REQUIRE(readBytes(mesh_path) == mesh_bytes);
  KARMA_REQUIRE(readBytes(irradiance_path) == irradiance_bytes);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(fixture.directory)) {
    KARMA_REQUIRE(entry.path().filename().string().find(".stage.") ==
                  std::string::npos);
  }
}

void testArtifactTransactionPreservesPreviousBakeOnCancellation() {
  LightingFixture fixture = lightingFixture(false, false);
  fixture.document.bakes.front().lighting.directional = false;
  const karma::scenes::SceneBakeResult baseline = karma::scenes::bakeScene(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(baseline.success);
  const auto mesh_path =
      fixture.directory / producedAsset(baseline, "baked_mesh").path;
  const auto irradiance_path =
      fixture.directory /
      producedAsset(baseline, "baked_irradiance_rgba8").path;
  const std::vector<uint8_t> mesh_bytes = readBytes(mesh_path);
  const std::vector<uint8_t> irradiance_bytes = readBytes(irradiance_path);

  fixture.document.static_components[1].lighting = true;
  bool cancel = false;
  const karma::scenes::SceneBakeResult cancelled = karma::scenes::bakeScene(
      fixture.document,
      fixture.document.bakes.front(),
      karma::scenes::SceneBakeExecutionOptions{
          .is_cancelled = [&] { return cancel; },
          .on_progress = [&](const karma::scenes::SceneBakeProgress& progress) {
            if (progress.stage == karma::scenes::SceneBakeStage::Lighting &&
                progress.total == 2u && progress.current == 1u) {
              cancel = true;
            }
          },
      });
  KARMA_REQUIRE(!cancelled.success);
  KARMA_REQUIRE(cancelled.cancelled);
  KARMA_REQUIRE(readBytes(mesh_path) == mesh_bytes);
  KARMA_REQUIRE(readBytes(irradiance_path) == irradiance_bytes);
}

void testSceneBakeFingerprintTracksPackageAndPrefabContent() {
  LightingFixture fixture = lightingFixture();
  const std::string baseline = karma::scenes::sceneBakeFingerprint(
      fixture.document, fixture.document.bakes.front());
  writeText(fixture.directory / "plane.obj", R"(v -1 0 -1
v -1 0 1
v 1 0 -1
v 1 0 1
f 1 2 3
f 3 2 4
# source changed
)");
  const std::string package_changed = karma::scenes::sceneBakeFingerprint(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(package_changed != baseline);

  writeText(fixture.directory / "prefabs/tree/prefab.json", R"({
  "version": 2,
  "asset_package": "assets.package.json",
  "root": 0,
  "nodes": [{"id": 0, "name": "Tree", "parent": null, "components": {}}]
})");
  writeText(fixture.directory / "prefabs/tree/assets.package.json", R"({
  "version": 1,
  "assets": [
    {"type": "material", "key": "tests/tree/material", "path": "tree.mat"}
  ]
})");
  writeText(fixture.directory / "prefabs/tree/tree.mat",
            R"({"version": 1, "name": "Tree A"})");
  fixture.document.prefab_instances.push_back(
      karma::scenes::ScenePrefabInstance{
          .id = "tree",
          .prefab_path = "prefabs/tree/prefab.json",
      });
  const std::string prefab_baseline = karma::scenes::sceneBakeFingerprint(
      fixture.document, fixture.document.bakes.front());
  writeText(fixture.directory / "prefabs/tree/tree.mat",
            R"({"version": 1, "name": "Tree B"})");
  const std::string prefab_changed = karma::scenes::sceneBakeFingerprint(
      fixture.document, fixture.document.bakes.front());
  KARMA_REQUIRE(prefab_changed != prefab_baseline);
}

void testSceneBakeFingerprintIsPortableAcrossContentRoots() {
  LightingFixture first = lightingFixture();
  LightingFixture second = lightingFixture();
  first.document.reference_root = first.directory;
  second.document.reference_root = second.directory;
  writeText(first.document.source_path, "equivalent portable scene source\n");
  writeText(second.document.source_path, "equivalent portable scene source\n");

  const auto add_prefab = [](LightingFixture& fixture) {
    writeText(fixture.directory / "prefabs/tree/prefab.json", R"({
  "version": 2,
  "asset_package": "assets.package.json",
  "root": 0,
  "nodes": [{"id": 0, "name": "Tree", "parent": null, "components": {}}]
})");
    writeText(fixture.directory / "prefabs/tree/assets.package.json", R"({
  "version": 1,
  "assets": [
    {"type": "material", "key": "tests/tree/material", "path": "tree.mat",
     "dependencies": ["tree.png"]}
  ]
})");
    writeText(fixture.directory / "prefabs/tree/tree.mat",
              R"({"version": 1, "name": "Portable Tree"})");
    writeText(fixture.directory / "prefabs/tree/tree.png",
              "portable texture bytes");
    fixture.document.prefab_instances.push_back(
        karma::scenes::ScenePrefabInstance{
            .id = "tree",
            .prefab_path = "prefabs/tree/prefab.json",
        });
  };
  add_prefab(first);
  add_prefab(second);

  const std::string first_fingerprint = karma::scenes::sceneBakeFingerprint(
      first.document, first.document.bakes.front());
  const std::string second_fingerprint = karma::scenes::sceneBakeFingerprint(
      second.document, second.document.bakes.front());
  KARMA_REQUIRE(first_fingerprint == second_fingerprint);
}

}  // namespace

int main() {
  testSceneBakeOutputIsDeterministic();
  testSceneBakeFingerprintChangesWhenSourceChanges();
  testSceneBakeSettingsRoundTripAndFingerprint();
  testSceneBakeProgressAndCancellation();
  testCpuLightingArtifactsAndDeterminism();
  testCpuLightingModesFilteringAndStageSelection();
  testShadowFilteringAndBvhWorkBound();
  testInstancedStaticShadowOccludersAndReceiverWarning();
  testArtifactTransactionPreservesPreviousBakeOnFailure();
  testArtifactTransactionPreservesPreviousBakeOnCancellation();
  testSceneBakeFingerprintTracksPackageAndPrefabContent();
  testSceneBakeFingerprintIsPortableAcrossContentRoots();
  return 0;
}
