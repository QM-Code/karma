#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/content/prefabs/prefab.h"
#include "karma/content/prefabs/prefab_resource_context.h"
#include "karma/features/visual/lights/light_pulse_system.h"
#include "karma/features/visual/particles/effect_library.h"
#include "karma/features/visual/particles/particle_system.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/particle_stats_report.h"
#include "karma/world/components/light.h"
#include "karma/world/components/light_pulse.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/particle_effect.h"
#include "karma/world/components/particle_effect_override.h"
#include "karma/world/components/particle_emitter.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

namespace {

using Json = nlohmann::json;

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
      ("karma_prefab_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream stream(path);
  stream << text;
}

Json readJson(const std::filesystem::path& path) {
  std::ifstream stream(path);
  Json json;
  stream >> json;
  return json;
}

bool nearly(float a, float b) {
  const float diff = a > b ? a - b : b - a;
  return diff < 0.0001f;
}

Json validParticleEffectJson() {
  return Json{
      {"version", 2},
      {"emitters",
       Json::array({Json{
           {"texture", "test/texture"},
           {"playback",
            Json{{"enabled", true},
                 {"playing", true},
                 {"loop", true},
                 {"emit_burst_on_start", true},
                 {"local_space", false},
                 {"time_scale", 1.0f},
                 {"start_delay", 0.0f},
                 {"duration", 0.0f}}},
           {"render",
            Json{{"layer", 0},
                 {"depth_test", true},
                 {"blend_mode", "additive"},
                 {"alignment", "billboard"},
                 {"shading_mode", "standard"},
                 {"use_soft_mask", true},
                 {"soft_particle_distance", 0.25f},
                 {"distortion_strength", 0.0f},
                 {"fresnel_power", 4.0f},
                 {"fresnel_strength", 1.0f},
                 {"refraction_strength", 0.0f},
                 {"interior_glow", 0.0f}}},
           {"atlas",
            Json{{"columns", 1},
                 {"rows", 1},
                 {"frame_count", 1},
                 {"frame_width", 0},
                 {"frame_height", 0},
                 {"border_x", 0},
                 {"border_y", 0},
                 {"spacing_x", 0},
                 {"spacing_y", 0},
                 {"animation_fps", 0.0f},
                 {"animate_over_lifetime", false},
                 {"random_start_frame", false}}},
           {"emission",
            Json{{"max_particles", 8}, {"burst_count", 4}, {"seed", 7}, {"spawn_rate", 0.0f}}},
           {"lifetime", Json{{"min", 1.0f}, {"max", 1.0f}}},
           {"size",
            Json{{"start_min", 0.1f},
                 {"start_max", 0.1f},
                 {"end_min", 0.0f},
                 {"end_max", 0.0f},
                 {"curve_exponent", 1.0f}}},
           {"rotation",
            Json{{"initial_min", 0.0f},
                 {"initial_max", 0.0f},
                 {"angular_velocity_min", 0.0f},
                 {"angular_velocity_max", 0.0f}}},
           {"spawn",
            Json{{"shape", "box"},
                 {"box_extents", Json::array({0.0f, 0.0f, 0.0f})},
                 {"radius_min", 0.0f},
                 {"radius_max", 0.0f},
                 {"radial_speed_min", 0.0f},
                 {"radial_speed_max", 0.0f}}},
           {"motion",
            Json{{"velocity_min", Json::array({0.0f, 0.0f, 0.0f})},
                 {"velocity_max", Json::array({0.0f, 0.0f, 0.0f})},
                 {"acceleration", Json::array({0.0f, 0.0f, 0.0f})},
                 {"drag", 0.0f}}},
           {"collision",
            Json{{"ground", false},
                 {"ground_height", 0.0f},
                 {"bounce_damping", 0.35f},
                 {"friction", 0.25f},
                 {"rest_speed_threshold", 0.35f}}},
           {"color",
            Json{{"start", Json::array({1.0f, 1.0f, 1.0f, 1.0f})},
                 {"end", Json::array({1.0f, 1.0f, 1.0f, 0.0f})},
                 {"alpha_curve_exponent", 1.0f}}},
       }})},
  };
}

std::string simplePrefabJson() {
  return R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Root",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    }
  ]
})";
}

std::filesystem::path findRepoRoot() {
  std::vector<std::filesystem::path> starts{std::filesystem::current_path()};
  std::filesystem::path source_path = std::filesystem::path(__FILE__);
  if (source_path.is_absolute()) {
    starts.push_back(source_path.parent_path());
  }

  for (std::filesystem::path start : starts) {
    for (std::filesystem::path cursor = start; !cursor.empty(); cursor = cursor.parent_path()) {
      if (std::filesystem::exists(cursor / "examples/assets/prefabs/explosion/prefab.json")) {
        return cursor;
      }
      if (cursor == cursor.parent_path()) {
        break;
      }
    }
  }
  return {};
}

void testSaveLoadSingleEntity(const std::filesystem::path& dir) {
  karma::ecs::World world;
  karma::scene::Scene scene;
  const karma::ecs::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "Crate");
  world.add(root, karma::components::TransformComponent{
                      {1.0f, 2.0f, 3.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f},
                      {2.0f, 2.0f, 2.0f},
                  });
  world.add(root, karma::components::MeshComponent{
                      .mesh_key = "assets/crate.glb",
                      .material_key = "crate",
                      .texture_key = "crate_albedo",
                      .mesh_id = 42,
                      .material_id = 99,
                      .owns_mesh_id = true,
                      .owns_material_id = true,
                      .visible = true,
                      .shadow_visible = false,
                  });
  world.add(root, karma::components::LightComponent{
                      .type = karma::components::LightComponent::Type::Point,
                      .color = {0.5f, 0.6f, 0.7f, 1.0f},
                      .intensity = 4.0f,
                      .range = 12.0f,
                  });

  const std::filesystem::path path = dir / "single.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  const Json saved = readJson(path);
  KARMA_REQUIRE(saved["nodes"][0]["components"]["MeshComponent"]["mesh_key"] == "assets/crate.glb");
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("mesh_id"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("material_id"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("owns_mesh_id"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("owns_material_id"));

  karma::ecs::World loaded_world;
  karma::scene::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->valid());
  KARMA_REQUIRE(instance->root_scene_node != karma::scene::Node::kInvalidId);
  KARMA_REQUIRE(loaded_world.has<karma::components::TagComponent>(instance->root));
  KARMA_REQUIRE(loaded_world.get<karma::components::TagComponent>(instance->root).name == "Crate");

  const auto& transform =
      loaded_world.get<karma::components::TransformComponent>(instance->root);
  KARMA_REQUIRE(nearly(transform.getPosition().x, 1.0f));
  KARMA_REQUIRE(nearly(transform.getPosition().y, 2.0f));
  KARMA_REQUIRE(nearly(transform.getPosition().z, 3.0f));

  const auto& mesh = loaded_world.get<karma::components::MeshComponent>(instance->root);
  KARMA_REQUIRE(mesh.mesh_key == "assets/crate.glb");
  KARMA_REQUIRE(mesh.material_key == "crate");
  KARMA_REQUIRE(mesh.texture_key == "crate_albedo");
  KARMA_REQUIRE(mesh.mesh_id == karma::renderer::kInvalidMesh);
  KARMA_REQUIRE(mesh.material_id == karma::renderer::kInvalidMaterial);
  KARMA_REQUIRE(!mesh.owns_mesh_id);
  KARMA_REQUIRE(!mesh.owns_material_id);
  KARMA_REQUIRE(!mesh.shadow_visible);

  const auto& light = loaded_world.get<karma::components::LightComponent>(instance->root);
  KARMA_REQUIRE(light.type == karma::components::LightComponent::Type::Point);
  KARMA_REQUIRE(nearly(light.color.r, 0.5f));
  KARMA_REQUIRE(nearly(light.intensity, 4.0f));
  KARMA_REQUIRE(nearly(light.range, 12.0f));
}

void testHierarchyRoundTrip(const std::filesystem::path& dir) {
  karma::ecs::World world;
  karma::scene::Scene scene;
  const karma::ecs::Entity root = world.createEntity();
  const karma::ecs::Entity child = world.createEntity();
  const auto root_node = scene.createNode(root);
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  world.setName(root, "Root");
  world.add(root, karma::components::TransformComponent{});
  world.setName(child, "Child");
  world.add(child,
            karma::components::LocalTransformComponent{
                {2.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f},
                {1.0f, 1.0f, 1.0f},
            });
  world.add(child,
            karma::components::TransformComponent{
                {2.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f},
                {1.0f, 1.0f, 1.0f},
            });

  const std::filesystem::path path = dir / "hierarchy.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  karma::ecs::World loaded_world;
  karma::scene::Scene loaded_scene;
  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.root_transform.setPosition({10.0f, 0.0f, 0.0f});
  const auto instance =
      karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path, desc);
  KARMA_REQUIRE(instance.has_value());

  const karma::ecs::Entity loaded_child = instance->find("Child");
  KARMA_REQUIRE(loaded_child.isValid());
  const auto loaded_root_node = loaded_scene.findNode(instance->root);
  const auto loaded_child_node = loaded_scene.findNode(loaded_child);
  KARMA_REQUIRE(loaded_scene.isAlive(loaded_root_node));
  KARMA_REQUIRE(loaded_scene.isAlive(loaded_child_node));
  KARMA_REQUIRE(loaded_scene.get(loaded_child_node).parent == loaded_root_node);

  const auto& child_local =
      loaded_world.get<karma::components::LocalTransformComponent>(loaded_child);
  KARMA_REQUIRE(nearly(child_local.position.x, 2.0f));
  const auto& child_transform =
      loaded_world.get<karma::components::TransformComponent>(loaded_child);
  KARMA_REQUIRE(nearly(child_transform.getPosition().x, 12.0f));
}

void testUnknownComponentSkips(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "unknown.json";
  writeText(path,
            R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "UnknownCarrier",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
        "OptionalFeature": { "enabled": true }
      }
    }
  ]
})");

  karma::ecs::World world;
  karma::scene::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(world.isAlive(instance->root));
  KARMA_REQUIRE(world.has<karma::components::TransformComponent>(instance->root));
}

void testMalformedAndInvalidPayloads(const std::filesystem::path& dir) {
  const std::filesystem::path malformed = dir / "malformed.json";
  writeText(malformed, "{ invalid json");
  karma::ecs::World world_a;
  karma::scene::Scene scene_a;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world_a, scene_a, malformed).has_value());
  KARMA_REQUIRE(world_a.entities().empty());

  const std::filesystem::path invalid = dir / "invalid_component.json";
  writeText(invalid,
            R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Broken",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    }
  ]
})");
  karma::ecs::World world_b;
  karma::scene::Scene scene_b;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world_b, scene_b, invalid).has_value());
  KARMA_REQUIRE(world_b.entities().empty());
}

void testDestroyPrefab(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "destroy.json";
  writeText(path,
            R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Root",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    },
    {
      "id": 1,
      "name": "Child",
      "parent": 0,
      "components": {
        "LocalTransformComponent": { "position": [1, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    }
  ]
})");

  karma::ecs::World world;
  karma::scene::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path);
  KARMA_REQUIRE(instance.has_value());
  const karma::ecs::Entity root = instance->root;
  const karma::ecs::Entity child = instance->find("Child");
  KARMA_REQUIRE(world.isAlive(root));
  KARMA_REQUIRE(world.isAlive(child));
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, root));
  KARMA_REQUIRE(!world.isAlive(root));
  KARMA_REQUIRE(!world.isAlive(child));
  KARMA_REQUIRE(scene.findNode(root) == karma::scene::Node::kInvalidId);
  KARMA_REQUIRE(scene.findNode(child) == karma::scene::Node::kInvalidId);
}

void testMissingSidecarKeepsPrefabLoad(const std::filesystem::path& dir) {
  const std::filesystem::path prefab_dir = dir / "missing_sidecar";
  std::filesystem::create_directories(prefab_dir);
  writeText(prefab_dir / "prefab.json", simplePrefabJson());

  karma::ecs::World world;
  karma::scene::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(world.isAlive(instance->root));
}

void testSidecarParsingSuccessAndFailure(const std::filesystem::path& dir) {
  {
    const std::filesystem::path prefab_dir = dir / "sidecar_success";
    std::filesystem::create_directories(prefab_dir / "particles");
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "particles/test.kpeffect", validParticleEffectJson().dump(2));
    writeText(prefab_dir / "prefab.resources.json",
              R"({
  "version": 1,
  "particle_effects": [
    { "key": "test/effect", "path": "particles/test.kpeffect" }
  ]
})");

    karma::particles::ParticleLibrary library;
    karma::prefabs::bindPrefabResourceContext(karma::prefabs::PrefabResourceContext{
        .particle_effects = &library,
    });
    karma::ecs::World world;
    karma::scene::Scene scene;
    const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
    KARMA_REQUIRE(instance.has_value());
    KARMA_REQUIRE(library.find("test/effect") != nullptr);
    karma::prefabs::clearPrefabResourceContext();
  }

  {
    const std::filesystem::path prefab_dir = dir / "sidecar_failure";
    std::filesystem::create_directories(prefab_dir);
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "prefab.resources.json",
              R"({ "version": 1, "textures": { "key": "bad" } })");

    karma::ecs::World world;
    karma::scene::Scene scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    KARMA_REQUIRE(world.entities().empty());
  }
}

void testSidecarMissingContextAndResourceFailure(const std::filesystem::path& dir) {
  {
    const std::filesystem::path prefab_dir = dir / "missing_context";
    std::filesystem::create_directories(prefab_dir);
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "prefab.resources.json",
              R"({
  "version": 1,
  "textures": [
    { "key": "missing/texture", "path": "textures/missing.png" }
  ]
})");

    karma::prefabs::clearPrefabResourceContext();
    karma::ecs::World world;
    karma::scene::Scene scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    KARMA_REQUIRE(world.entities().empty());
  }

  {
    const std::filesystem::path prefab_dir = dir / "bad_effect";
    std::filesystem::create_directories(prefab_dir);
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "prefab.resources.json",
              R"({
  "version": 1,
  "particle_effects": [
    { "key": "bad/effect", "path": "particles/missing.kpeffect" }
  ]
})");

    karma::particles::ParticleLibrary library;
    karma::prefabs::bindPrefabResourceContext(karma::prefabs::PrefabResourceContext{
        .particle_effects = &library,
    });
    karma::ecs::World world;
    karma::scene::Scene scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    KARMA_REQUIRE(world.entities().empty());
    karma::prefabs::clearPrefabResourceContext();
  }
}

void testParticleEffectParserV2() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path valid = dir / "valid.kpeffect";
  writeText(valid, validParticleEffectJson().dump(2));

  karma::particles::ParticleLibrary library;
  library.registerTextureAlias("test/texture", 77u);
  KARMA_REQUIRE(library.registerEffectFile("test/effect", valid));
  const auto* effect = library.find("test/effect");
  KARMA_REQUIRE(effect != nullptr);
  KARMA_REQUIRE(effect->texture_key == "test/texture");
  KARMA_REQUIRE(effect->emitter.max_particles == 8u);
  auto emitter = library.instantiateEmitter("test/effect");
  KARMA_REQUIRE(emitter.has_value());
  KARMA_REQUIRE(emitter->texture == 77u);

  Json unknown = validParticleEffectJson();
  unknown["emitters"][0]["render"]["unknown"] = 1;
  const std::filesystem::path unknown_path = dir / "unknown.kpeffect";
  writeText(unknown_path, unknown.dump(2));
  KARMA_REQUIRE(!library.registerEffectFile("test/unknown", unknown_path));

  Json invalid_enum = validParticleEffectJson();
  invalid_enum["emitters"][0]["render"]["blend_mode"] = "multiply";
  const std::filesystem::path invalid_enum_path = dir / "invalid_enum.kpeffect";
  writeText(invalid_enum_path, invalid_enum.dump(2));
  KARMA_REQUIRE(!library.registerEffectFile("test/invalid_enum", invalid_enum_path));

  Json missing_block = validParticleEffectJson();
  missing_block["emitters"][0].erase("motion");
  const std::filesystem::path missing_block_path = dir / "missing_block.kpeffect";
  writeText(missing_block_path, missing_block.dump(2));
  KARMA_REQUIRE(!library.registerEffectFile("test/missing_block", missing_block_path));

  std::filesystem::remove_all(dir);
}

void testParticleSystemRendererOwnedState() {
  karma::ecs::World world;
  const karma::ecs::Entity entity = world.createEntity();
  world.add(entity, karma::components::TransformComponent{});
  karma::components::ParticleEmitterComponent emitter{};
  emitter.enabled = true;
  emitter.playing = true;
  emitter.loop = false;
  emitter.emit_burst_on_start = true;
  emitter.max_particles = 8;
  emitter.burst_count = 4;
  emitter.start_delay = 0.1f;
  emitter.particle_lifetime_min = 1.0f;
  emitter.particle_lifetime_max = 1.0f;
  world.add(entity, emitter);

  karma::particles::ParticleSystem system(nullptr, nullptr);
  system.update(world, 0.05f, 1.0f);
  KARMA_REQUIRE(system.liveParticleCount(entity) == 0u);
}

void testParticleSystemEffectLifecycleReapply() {
  karma::particles::ParticleLibrary library;
  karma::particles::ParticleEffectDesc effect{};
  effect.emitter.enabled = true;
  effect.emitter.playing = true;
  effect.emitter.layer = 2u;
  effect.emitter.texture = 11u;
  effect.emitter.max_particles = 32u;
  effect.emitter.start_delay = 0.1f;
  effect.emitter.start_size_min = 0.2f;
  effect.emitter.start_size_max = 0.4f;
  effect.emitter.start_color = {1.0f, 0.5f, 0.25f, 0.8f};
  library.registerEffect("spark", effect);

  karma::ecs::World world;
  const karma::ecs::Entity entity = world.createEntity();
  world.add(entity, karma::components::TransformComponent{});
  world.add(entity, karma::components::VisibilityComponent{.visible = false});
  world.add(entity, karma::components::ParticleEffectComponent{
                        .effect_key = "spark",
                        .preserve_enabled = true,
                        .preserve_playing = true,
                        .preserve_start_delay = true,
                    });
  karma::components::ParticleEmitterComponent existing{};
  existing.enabled = false;
  existing.playing = false;
  existing.start_delay = 0.75f;
  existing.max_particles = 1u;
  world.add(entity, existing);
  karma::components::ParticleEffectOverrideComponent effect_override{};
  effect_override.size_scale = 2.0f;
  effect_override.alpha_scale = 0.5f;
  effect_override.texture = 99u;
  world.add(entity, effect_override);

  karma::particles::ParticleSystem system(nullptr, &library);
  system.update(world, 0.016f, 1.0f);
  const auto& applied = world.get<karma::components::ParticleEmitterComponent>(entity);
  KARMA_REQUIRE(!applied.enabled);
  KARMA_REQUIRE(!applied.playing);
  KARMA_REQUIRE(nearly(applied.start_delay, 0.75f));
  KARMA_REQUIRE(applied.layer == 2u);
  KARMA_REQUIRE(applied.texture == 99u);
  KARMA_REQUIRE(applied.max_particles == 32u);
  KARMA_REQUIRE(nearly(applied.start_size_min, 0.4f));
  KARMA_REQUIRE(nearly(applied.start_size_max, 0.8f));
  KARMA_REQUIRE(nearly(applied.start_color.a, 0.4f));

  auto& override_component =
      world.get<karma::components::ParticleEffectOverrideComponent>(entity);
  override_component.texture = 123u;
  system.update(world, 0.016f, 1.0f);
  KARMA_REQUIRE(world.get<karma::components::ParticleEmitterComponent>(entity).texture == 123u);

  auto& effect_component =
      world.get<karma::components::ParticleEffectComponent>(entity);
  effect_component.restart_count += 1u;
  system.update(world, 0.016f, 1.0f);
  KARMA_REQUIRE(effect_component.applied_restart_count == effect_component.restart_count);

  effect.emitter.layer = 7u;
  effect.emitter.max_particles = 64u;
  library.registerEffect("spark", effect);
  system.update(world, 0.016f, 1.0f);
  const auto& reapplied = world.get<karma::components::ParticleEmitterComponent>(entity);
  KARMA_REQUIRE(reapplied.layer == 7u);
  KARMA_REQUIRE(reapplied.max_particles == 64u);
  KARMA_REQUIRE(!reapplied.enabled);
  KARMA_REQUIRE(!reapplied.playing);
  KARMA_REQUIRE(nearly(reapplied.start_delay, 0.75f));
}

void testLightPulseSystem() {
  karma::ecs::World world;
  const karma::ecs::Entity entity = world.createEntity();
  world.add(entity, karma::components::LightComponent{
                        .type = karma::components::LightComponent::Type::Point,
                        .intensity = 0.0f,
                        .range = 0.1f,
                    });
  world.add(entity, karma::components::VisibilityComponent{.visible = false});
  world.add(entity, karma::components::LightPulseComponent{
                        .duration = 1.0f,
                        .peak_intensity = 10.0f,
                        .peak_range = 5.0f,
                        .off_intensity = 0.0f,
                        .off_range = 0.1f,
                    });

  karma::visual::LightPulseSystem system;
  system.update(world, 0.0f);
  auto& light = world.get<karma::components::LightComponent>(entity);
  auto& visibility = world.get<karma::components::VisibilityComponent>(entity);
  KARMA_REQUIRE(nearly(light.intensity, 10.0f));
  KARMA_REQUIRE(nearly(light.range, 5.0f));
  KARMA_REQUIRE(visibility.visible);

  system.update(world, 0.5f);
  KARMA_REQUIRE(light.intensity > 0.0f && light.intensity < 10.0f);
  KARMA_REQUIRE(light.range > 0.1f && light.range < 5.0f);
  KARMA_REQUIRE(visibility.visible);

  system.update(world, 0.6f);
  const auto& pulse = world.get<karma::components::LightPulseComponent>(entity);
  KARMA_REQUIRE(!pulse.active);
  KARMA_REQUIRE(nearly(light.intensity, 0.0f));
  KARMA_REQUIRE(nearly(light.range, 0.1f));
  KARMA_REQUIRE(!visibility.visible);
}

void testExplosionPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path prefab_dir = repo_root / "examples/assets/prefabs/explosion";

  struct UploadedTexture {
    int width = 0;
    int height = 0;
  };

  std::uint32_t next_texture = 100u;
  std::vector<UploadedTexture> uploaded_textures;
  std::vector<karma::renderer::TextureId> destroyed_textures;
  karma::particles::ParticleLibrary library;
  karma::prefabs::bindPrefabResourceContext(karma::prefabs::PrefabResourceContext{
      .particle_effects = &library,
      .create_texture_rgba8 =
          [&next_texture, &uploaded_textures](int width, int height, const void* pixels) {
            KARMA_REQUIRE(width > 0);
            KARMA_REQUIRE(height > 0);
            KARMA_REQUIRE(pixels != nullptr);
            const karma::renderer::TextureId id = next_texture++;
            uploaded_textures.push_back(UploadedTexture{
                .width = width,
                .height = height,
            });
            return id;
          },
      .destroy_texture =
          [&destroyed_textures](karma::renderer::TextureId texture) {
            destroyed_textures.push_back(texture);
          },
  });

  karma::ecs::World world;
  karma::scene::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->find("flash").isValid());
  KARMA_REQUIRE(instance->find("smoke").isValid());
  const karma::ecs::Entity glow = instance->find("glow");
  KARMA_REQUIRE(glow.isValid());
  KARMA_REQUIRE(world.has<karma::components::LightPulseComponent>(glow));
  const karma::ecs::Entity smoke = instance->find("smoke");
  KARMA_REQUIRE(world.has<karma::components::ParticleEmitterComponent>(smoke));
  const auto& smoke_emitter = world.get<karma::components::ParticleEmitterComponent>(smoke);
  KARMA_REQUIRE(nearly(smoke_emitter.start_delay, 0.24f));
  KARMA_REQUIRE(library.find("prefabs/explosion/flash") != nullptr);
  KARMA_REQUIRE(library.find("prefabs/explosion/smoke_flipbook") != nullptr);

  auto countTextureSize = [&uploaded_textures](int width, int height) {
    std::uint32_t count = 0u;
    for (const UploadedTexture& texture : uploaded_textures) {
      if (texture.width == width && texture.height == height) {
        count += 1u;
      }
    }
    return count;
  };
  KARMA_REQUIRE(uploaded_textures.size() == 10u);
  KARMA_REQUIRE(countTextureSize(256, 64) == 8u);
  KARMA_REQUIRE(countTextureSize(2024, 2024) == 2u);

  const auto* core_flipbook = library.find("prefabs/explosion/core_flipbook");
  KARMA_REQUIRE(core_flipbook != nullptr);
  KARMA_REQUIRE(core_flipbook->texture_key == "prefabs/explosion/explosion00_flipbook");
  KARMA_REQUIRE(core_flipbook->emitter.atlas_frame_count == 25u);
  KARMA_REQUIRE(core_flipbook->emitter.atlas_frame_width == 400u);
  KARMA_REQUIRE(core_flipbook->emitter.atlas_frame_height == 400u);
  KARMA_REQUIRE(core_flipbook->emitter.atlas_border_x == 4u);
  KARMA_REQUIRE(core_flipbook->emitter.atlas_border_y == 4u);
  KARMA_REQUIRE(core_flipbook->emitter.atlas_spacing_x == 4u);
  KARMA_REQUIRE(core_flipbook->emitter.atlas_spacing_y == 4u);
  KARMA_REQUIRE(core_flipbook->emitter.blend_mode == karma::renderer::ParticleBlendMode::Additive);

  const auto* smoke_flipbook = library.find("prefabs/explosion/smoke_flipbook");
  KARMA_REQUIRE(smoke_flipbook != nullptr);
  KARMA_REQUIRE(smoke_flipbook->texture_key == "prefabs/explosion/explosion01_smoke_flipbook");
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_frame_count == 25u);
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_frame_width == 400u);
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_frame_height == 400u);
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_border_x == 4u);
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_border_y == 4u);
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_spacing_x == 4u);
  KARMA_REQUIRE(smoke_flipbook->emitter.atlas_spacing_y == 4u);
  KARMA_REQUIRE(smoke_flipbook->emitter.blend_mode == karma::renderer::ParticleBlendMode::Alpha);

  karma::prefabs::clearPrefabResourceContext();
  KARMA_REQUIRE(destroyed_textures.size() == uploaded_textures.size());
}

void testEnergyOrbPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path prefab_dir = repo_root / "examples/assets/prefabs/energy_orb";

  std::uint32_t next_texture = 200u;
  std::vector<karma::renderer::TextureId> destroyed_textures;
  karma::particles::ParticleLibrary library;
  karma::prefabs::bindPrefabResourceContext(karma::prefabs::PrefabResourceContext{
      .particle_effects = &library,
      .create_texture_rgba8 =
          [&next_texture](int width, int height, const void* pixels) {
            KARMA_REQUIRE(width == 768);
            KARMA_REQUIRE(height == 128);
            KARMA_REQUIRE(pixels != nullptr);
            return next_texture++;
          },
      .destroy_texture =
          [&destroyed_textures](karma::renderer::TextureId texture) {
            destroyed_textures.push_back(texture);
          },
  });

  karma::ecs::World world;
  karma::scene::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->find("shell").isValid());
  KARMA_REQUIRE(instance->find("core").isValid());
  KARMA_REQUIRE(instance->find("arcs").isValid());
  KARMA_REQUIRE(instance->find("halo").isValid());
  KARMA_REQUIRE(instance->find("distortion").isValid());
  const karma::ecs::Entity glow = instance->find("glow");
  KARMA_REQUIRE(glow.isValid());
  KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));

  const karma::ecs::Entity shell = instance->find("shell");
  KARMA_REQUIRE(world.has<karma::components::MeshComponent>(shell));
  KARMA_REQUIRE(world.get<karma::components::MeshComponent>(shell).mesh_key ==
         "examples/assets/orb_shell.glb");
  KARMA_REQUIRE(world.has<karma::components::LocalTransformComponent>(shell));
  const auto& shell_local = world.get<karma::components::LocalTransformComponent>(shell);
  KARMA_REQUIRE(nearly(shell_local.scale.x, 0.28875f));
  KARMA_REQUIRE(nearly(shell_local.scale.y, 0.28875f));
  KARMA_REQUIRE(nearly(shell_local.scale.z, 0.28875f));

  const karma::ecs::Entity core = instance->find("core");
  KARMA_REQUIRE(world.has<karma::components::ParticleEffectComponent>(core));
  KARMA_REQUIRE(world.has<karma::components::ParticleEmitterComponent>(core));
  KARMA_REQUIRE(world.get<karma::components::ParticleEffectComponent>(core).effect_key ==
         "energy_orb_core");
  KARMA_REQUIRE(world.get<karma::components::ParticleEmitterComponent>(core).playing);
  KARMA_REQUIRE(library.find("energy_orb_core") != nullptr);
  KARMA_REQUIRE(library.find("energy_orb_arcs") != nullptr);
  KARMA_REQUIRE(library.find("energy_orb_halo") != nullptr);
  KARMA_REQUIRE(library.find("energy_orb_distortion") != nullptr);

  karma::prefabs::clearPrefabResourceContext();
  KARMA_REQUIRE(destroyed_textures.size() == 4u);
}

void testParticleStatsReportFormatting() {
  karma::renderer::ParticlePassStats totals{};
  karma::renderer::ParticlePassStats frame{};
  frame.submitted_emitters = 3u;
  frame.gpu_particle_capacity = 128u;
  frame.gpu_alive_particles = 42u;
  frame.gpu_dead_particles = 86u;
  frame.gpu_compute_dispatches = 2u;
  frame.gpu_indirect_draws = 4u;
  frame.gpu_indirect_dispatches = 1u;
  frame.gpu_sort_key_count = 12u;
  frame.gpu_sort_passes = 1u;
  frame.gpu_stats_readback_age = 1u;
  frame.gpu_allocator_live_emitters = 5u;
  frame.gpu_allocator_free_ranges = 2u;
  frame.gpu_allocator_active_capacity = 96u;
  frame.gpu_allocator_high_water_capacity = 160u;
  frame.gpu_allocator_retired_emitters = 1u;
  frame.gpu_allocator_reused_slots = 3u;
  frame.gpu_allocator_allocation_failures = 1u;
  frame.gpu_culled_emitters = 2u;
  frame.gpu_culled_particles = 9u;
  frame.gpu_culling_dispatches = 2u;
  frame.cpu_fallback_particles = 7u;
  frame.simulation_ms = 0.5f;
  frame.scene_color_copy = true;
  frame.gpu_sort_overflow = true;
  frame.gpu_grouped_sort_fallback = true;
  karma::renderer::accumulateParticleStats(totals, frame);
  karma::renderer::accumulateParticleStats(totals, frame);

  const std::string line = karma::renderer::formatParticleStatsReport(
      karma::renderer::ParticleStatsReport{
          .totals = totals,
          .frame_count = 2u,
          .elapsed_seconds = 1.0,
      });
  KARMA_REQUIRE(line.find("submitted_emitters=3.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_particle_capacity=128.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_alive_particles=42.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_dead_particles=86.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_compute_dispatches=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_indirect_draws=4.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_indirect_dispatches=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_key_count=12.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_passes=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_stats_readback_age=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_live_emitters=5.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_free_ranges=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_active_capacity=96.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_high_water_capacity=160.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_retired_emitters=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_reused_slots=3.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_allocation_failures=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_culled_emitters=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_culled_particles=9.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_culling_dispatches=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("cpu_fallback_particles=7.0") != std::string::npos);
  KARMA_REQUIRE(line.find("simulation_ms=0.500") != std::string::npos);
  KARMA_REQUIRE(line.find("scene_color_copy=true") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_overflow=true") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_global_sort_active=false") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_grouped_sort_fallback=true") != std::string::npos);
}

}  // namespace

int main() {
  const std::filesystem::path dir = makeTempDir();
  testSaveLoadSingleEntity(dir);
  testHierarchyRoundTrip(dir);
  testUnknownComponentSkips(dir);
  testMalformedAndInvalidPayloads(dir);
  testDestroyPrefab(dir);
  testMissingSidecarKeepsPrefabLoad(dir);
  testSidecarParsingSuccessAndFailure(dir);
  testSidecarMissingContextAndResourceFailure(dir);
  testParticleEffectParserV2();
  testParticleSystemRendererOwnedState();
  testParticleSystemEffectLifecycleReapply();
  testParticleStatsReportFormatting();
  testLightPulseSystem();
  testExplosionPrefabDirectLoad();
  testEnergyOrbPrefabDirectLoad();
  std::filesystem::remove_all(dir);
  return 0;
}
