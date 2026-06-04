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
  KARMA_REQUIRE(system.liveParticleCount(entity) == 0u);
  system.update(world, 0.049f, 1.0f);
  KARMA_REQUIRE(system.liveParticleCount(entity) == 0u);
  system.update(world, 0.002f, 1.0f);
  KARMA_REQUIRE(system.liveParticleCount(entity) == 4u);
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
