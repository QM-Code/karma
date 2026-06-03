#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/content/prefabs/prefab.h"
#include "karma/content/prefabs/prefab_resource_context.h"
#include "karma/features/visual/lights/light_pulse_system.h"
#include "karma/features/visual/particles/effect_library.h"
#include "karma/features/visual/particles/particle_system.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/world/components/light.h"
#include "karma/world/components/light_pulse.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/particle_effect.h"
#include "karma/world/components/particle_emitter.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

namespace {

using Json = nlohmann::json;

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
  assert(karma::prefabs::savePrefab(world, scene, root, path));

  const Json saved = readJson(path);
  assert(saved["nodes"][0]["components"]["MeshComponent"]["mesh_key"] == "assets/crate.glb");
  assert(!saved["nodes"][0]["components"]["MeshComponent"].contains("mesh_id"));
  assert(!saved["nodes"][0]["components"]["MeshComponent"].contains("material_id"));
  assert(!saved["nodes"][0]["components"]["MeshComponent"].contains("owns_mesh_id"));
  assert(!saved["nodes"][0]["components"]["MeshComponent"].contains("owns_material_id"));

  karma::ecs::World loaded_world;
  karma::scene::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  assert(instance.has_value());
  assert(instance->valid());
  assert(instance->root_scene_node != karma::scene::Node::kInvalidId);
  assert(loaded_world.has<karma::components::TagComponent>(instance->root));
  assert(loaded_world.get<karma::components::TagComponent>(instance->root).name == "Crate");

  const auto& transform =
      loaded_world.get<karma::components::TransformComponent>(instance->root);
  assert(nearly(transform.getPosition().x, 1.0f));
  assert(nearly(transform.getPosition().y, 2.0f));
  assert(nearly(transform.getPosition().z, 3.0f));

  const auto& mesh = loaded_world.get<karma::components::MeshComponent>(instance->root);
  assert(mesh.mesh_key == "assets/crate.glb");
  assert(mesh.material_key == "crate");
  assert(mesh.texture_key == "crate_albedo");
  assert(mesh.mesh_id == karma::renderer::kInvalidMesh);
  assert(mesh.material_id == karma::renderer::kInvalidMaterial);
  assert(!mesh.owns_mesh_id);
  assert(!mesh.owns_material_id);
  assert(!mesh.shadow_visible);

  const auto& light = loaded_world.get<karma::components::LightComponent>(instance->root);
  assert(light.type == karma::components::LightComponent::Type::Point);
  assert(nearly(light.color.r, 0.5f));
  assert(nearly(light.intensity, 4.0f));
  assert(nearly(light.range, 12.0f));
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
  assert(karma::prefabs::savePrefab(world, scene, root, path));

  karma::ecs::World loaded_world;
  karma::scene::Scene loaded_scene;
  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.root_transform.setPosition({10.0f, 0.0f, 0.0f});
  const auto instance =
      karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path, desc);
  assert(instance.has_value());

  const karma::ecs::Entity loaded_child = instance->find("Child");
  assert(loaded_child.isValid());
  const auto loaded_root_node = loaded_scene.findNode(instance->root);
  const auto loaded_child_node = loaded_scene.findNode(loaded_child);
  assert(loaded_scene.isAlive(loaded_root_node));
  assert(loaded_scene.isAlive(loaded_child_node));
  assert(loaded_scene.get(loaded_child_node).parent == loaded_root_node);

  const auto& child_local =
      loaded_world.get<karma::components::LocalTransformComponent>(loaded_child);
  assert(nearly(child_local.position.x, 2.0f));
  const auto& child_transform =
      loaded_world.get<karma::components::TransformComponent>(loaded_child);
  assert(nearly(child_transform.getPosition().x, 12.0f));
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
  assert(instance.has_value());
  assert(world.isAlive(instance->root));
  assert(world.has<karma::components::TransformComponent>(instance->root));
}

void testMalformedAndInvalidPayloads(const std::filesystem::path& dir) {
  const std::filesystem::path malformed = dir / "malformed.json";
  writeText(malformed, "{ invalid json");
  karma::ecs::World world_a;
  karma::scene::Scene scene_a;
  assert(!karma::prefabs::instantiatePrefab(world_a, scene_a, malformed).has_value());
  assert(world_a.entities().empty());

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
  assert(!karma::prefabs::instantiatePrefab(world_b, scene_b, invalid).has_value());
  assert(world_b.entities().empty());
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
  assert(instance.has_value());
  const karma::ecs::Entity root = instance->root;
  const karma::ecs::Entity child = instance->find("Child");
  assert(world.isAlive(root));
  assert(world.isAlive(child));
  assert(karma::prefabs::destroyPrefab(world, scene, root));
  assert(!world.isAlive(root));
  assert(!world.isAlive(child));
  assert(scene.findNode(root) == karma::scene::Node::kInvalidId);
  assert(scene.findNode(child) == karma::scene::Node::kInvalidId);
}

void testMissingSidecarKeepsPrefabLoad(const std::filesystem::path& dir) {
  const std::filesystem::path prefab_dir = dir / "missing_sidecar";
  std::filesystem::create_directories(prefab_dir);
  writeText(prefab_dir / "prefab.json", simplePrefabJson());

  karma::ecs::World world;
  karma::scene::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  assert(instance.has_value());
  assert(world.isAlive(instance->root));
}

void testSidecarParsingSuccessAndFailure(const std::filesystem::path& dir) {
  {
    const std::filesystem::path prefab_dir = dir / "sidecar_success";
    std::filesystem::create_directories(prefab_dir / "particles");
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "particles/test.kpeffect", "enabled = true\n");
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
    assert(instance.has_value());
    assert(library.find("test/effect") != nullptr);
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
    assert(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    assert(world.entities().empty());
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
    assert(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    assert(world.entities().empty());
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
    assert(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    assert(world.entities().empty());
    karma::prefabs::clearPrefabResourceContext();
  }
}

void testParticleEmitterStartDelay() {
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
  assert(system.liveParticleCount(entity) == 0u);
  system.update(world, 0.049f, 1.0f);
  assert(system.liveParticleCount(entity) == 0u);
  system.update(world, 0.002f, 1.0f);
  assert(system.liveParticleCount(entity) == 4u);
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
  assert(nearly(light.intensity, 10.0f));
  assert(nearly(light.range, 5.0f));
  assert(visibility.visible);

  system.update(world, 0.5f);
  assert(light.intensity > 0.0f && light.intensity < 10.0f);
  assert(light.range > 0.1f && light.range < 5.0f);
  assert(visibility.visible);

  system.update(world, 0.6f);
  const auto& pulse = world.get<karma::components::LightPulseComponent>(entity);
  assert(!pulse.active);
  assert(nearly(light.intensity, 0.0f));
  assert(nearly(light.range, 0.1f));
  assert(!visibility.visible);
}

void testExplosionPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  assert(!repo_root.empty());
  const std::filesystem::path prefab_dir = repo_root / "examples/assets/prefabs/explosion";

  std::uint32_t next_texture = 100u;
  std::vector<karma::renderer::TextureId> destroyed_textures;
  karma::particles::ParticleLibrary library;
  karma::prefabs::bindPrefabResourceContext(karma::prefabs::PrefabResourceContext{
      .particle_effects = &library,
      .create_texture_rgba8 =
          [&next_texture](int width, int height, const void* pixels) {
            assert(width > 0);
            assert(height > 0);
            assert(pixels != nullptr);
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
  assert(instance.has_value());
  assert(instance->find("flash").isValid());
  assert(instance->find("smoke").isValid());
  const karma::ecs::Entity glow = instance->find("glow");
  assert(glow.isValid());
  assert(world.has<karma::components::LightPulseComponent>(glow));
  const karma::ecs::Entity smoke = instance->find("smoke");
  assert(world.has<karma::components::ParticleEmitterComponent>(smoke));
  assert(nearly(world.get<karma::components::ParticleEmitterComponent>(smoke).start_delay,
                0.24f));
  assert(library.find("prefabs/explosion/flash") != nullptr);
  assert(library.find("prefabs/explosion/smoke_flipbook") != nullptr);

  karma::prefabs::clearPrefabResourceContext();
  assert(!destroyed_textures.empty());
}

void testEnergyOrbPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  assert(!repo_root.empty());
  const std::filesystem::path prefab_dir = repo_root / "examples/assets/prefabs/energy_orb";

  std::uint32_t next_texture = 200u;
  std::vector<karma::renderer::TextureId> destroyed_textures;
  karma::particles::ParticleLibrary library;
  karma::prefabs::bindPrefabResourceContext(karma::prefabs::PrefabResourceContext{
      .particle_effects = &library,
      .create_texture_rgba8 =
          [&next_texture](int width, int height, const void* pixels) {
            assert(width == 768);
            assert(height == 128);
            assert(pixels != nullptr);
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
  assert(instance.has_value());
  assert(instance->find("shell").isValid());
  assert(instance->find("core").isValid());
  assert(instance->find("arcs").isValid());
  assert(instance->find("halo").isValid());
  assert(instance->find("distortion").isValid());
  const karma::ecs::Entity glow = instance->find("glow");
  assert(glow.isValid());
  assert(world.has<karma::components::LightComponent>(glow));

  const karma::ecs::Entity shell = instance->find("shell");
  assert(world.has<karma::components::MeshComponent>(shell));
  assert(world.get<karma::components::MeshComponent>(shell).mesh_key ==
         "examples/assets/shot.glb");

  const karma::ecs::Entity core = instance->find("core");
  assert(world.has<karma::components::ParticleEffectComponent>(core));
  assert(world.has<karma::components::ParticleEmitterComponent>(core));
  assert(world.get<karma::components::ParticleEffectComponent>(core).effect_key ==
         "energy_orb_core");
  assert(world.get<karma::components::ParticleEmitterComponent>(core).playing);
  assert(library.find("energy_orb_core") != nullptr);
  assert(library.find("energy_orb_arcs") != nullptr);
  assert(library.find("energy_orb_halo") != nullptr);
  assert(library.find("energy_orb_distortion") != nullptr);

  karma::prefabs::clearPrefabResourceContext();
  assert(destroyed_textures.size() == 4u);
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
  testParticleEmitterStartDelay();
  testLightPulseSystem();
  testExplosionPrefabDirectLoad();
  testEnergyOrbPrefabDirectLoad();
  std::filesystem::remove_all(dir);
  return 0;
}
