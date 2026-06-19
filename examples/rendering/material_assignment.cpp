#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {
constexpr const char* kMaterialAssignmentWorldSceneKey =
    "examples/rendering/material_assignment/world";
constexpr const char* kMaterialAssignmentTankSceneKey =
    "examples/rendering/material_assignment/tank";

math::Color materialColor(std::string_view key) {
  if (key == "tank_red") {
    return {1.0f, 0.35f, 0.30f, 1.0f};
  }
  if (key == "tank_blue") {
    return {0.35f, 0.55f, 1.0f, 1.0f};
  }
  if (key == "tank_gold") {
    return {1.0f, 0.82f, 0.35f, 1.0f};
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

void registerTintMaterial(content::AssetRegistry& assets,
                          const std::string& key,
                          const math::Color& color) {
  renderer::MaterialDesc material{};
  material.base_color = color;
  material.roughness = 0.58f;
  material.metallic = 0.0f;
  assets.registerMaterialAsset(key, material);
}
}  // namespace

class MaterialAssignmentExample final : public app::GameInterface {
 public:
  void onStart() override {
    environment_map_ = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");

    registerTintMaterial(*assets, "tank_red", materialColor("tank_red"));
    registerTintMaterial(*assets, "tank_blue", materialColor("tank_blue"));
    registerTintMaterial(*assets, "tank_gold", materialColor("tank_gold"));

    spawnWorld();

    auto environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
        .environment_map_asset_key = environment_map_,
        .intensity = 0.4f,
        .draw_skybox = true});

    auto camera = world->createEntity();
    world->setName(camera, "Camera");
    components::TransformComponent camera_xform{};
    camera_xform.setPosition({0.0f, 8.0f, 18.0f});
    camera_xform.setRotation(math::fromYawPitch(0.0f, -0.35f));
    world->add(camera, camera_xform);
    world->add(camera, components::CameraComponent{.is_primary = true});

    auto sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_xform{};
    sun_xform.setPosition({0.0f, 50.0f, 0.0f});
    sun_xform.setRotation(math::fromYawPitch(0.5f, -0.9f));
    world->add(sun, sun_xform);
    world->add(sun, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .intensity = 0.85f,
        .shadow_extent = 60.0f});

    auto warm_fill = world->createEntity();
    world->setName(warm_fill, "Warm Fill");
    components::TransformComponent warm_fill_xform{};
    warm_fill_xform.setPosition({-7.5f, 4.0f, 6.0f});
    world->add(warm_fill, warm_fill_xform);
    world->add(warm_fill, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {1.0f, 0.70f, 0.40f, 1.0f},
        .intensity = 18.0f,
        .range = 24.0f});

    auto cool_fill = world->createEntity();
    world->setName(cool_fill, "Cool Fill");
    components::TransformComponent cool_fill_xform{};
    cool_fill_xform.setPosition({7.5f, 4.0f, 6.0f});
    world->add(cool_fill, cool_fill_xform);
    world->add(cool_fill, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {0.35f, 0.60f, 1.0f, 1.0f},
        .intensity = 18.0f,
        .range = 24.0f});

    const content::GltfSceneAsset* tank_asset =
        assets->findGltfSceneAsset(kMaterialAssignmentTankSceneKey);
    if (tank_asset == nullptr) {
      spdlog::error("Material assignment tank scene was not available from the asset package");
      tank_mesh_ = importExampleMeshAsset(assets, "tank_final.glb");
    }

    const std::array<SpawnDesc, 4> spawns{{
        {"Tank Original", {-9.0f, 0.0f, 0.0f}, "", 0.20f},
        {"Tank Red", {-3.0f, 0.0f, 0.0f}, "tank_red", 0.55f},
        {"Tank Blue", {3.0f, 0.0f, 0.0f}, "tank_blue", 0.90f},
        {"Tank Gold", {9.0f, 0.0f, 0.0f}, "tank_gold", 1.25f},
    }};

    for (size_t i = 0; i < spawns.size(); ++i) {
      const auto& spawn = spawns[i];
      const ecs::Entity entity = spawnTank(spawn, i, tank_asset);
      tanks_[i] = TankEntry{
          .entity = entity,
          .base_position = spawn.position,
          .base_yaw = spawn.initial_yaw,
      };
    }
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    time_ += dt;

    for (const auto& tank : tanks_) {
      if (!world->isAlive(tank.entity)) {
        continue;
      }
      auto& transform = world->get<components::TransformComponent>(tank.entity);
      transform.setPosition({
          tank.base_position.x,
          0.25f * std::sin(time_ * 1.25f + tank.base_yaw),
          tank.base_position.z});
      transform.setRotation(math::fromYawPitch(tank.base_yaw + time_ * 0.35f, 0.0f));
    }
  }

  void onShutdown() override {}

 private:
  struct SpawnDesc {
    const char* name = "";
    math::Vec3 position{};
    const char* material_key = "";
    float initial_yaw = 0.0f;
  };

  struct TankEntry {
    ecs::Entity entity{};
    math::Vec3 base_position{};
    float base_yaw = 0.0f;
  };

  void spawnWorld() {
    bool spawned_world = false;

    if (const content::GltfSceneAsset* cached_world =
            assets->findGltfSceneAsset(kMaterialAssignmentWorldSceneKey)) {
      const scene::GltfSceneImportResult imported = scene::instantiateGltfSceneAsset(
          *world,
          *scene,
          *assets,
          *cached_world,
          scene::GltfSceneInstantiateOptions{
              .create_synthetic_root = true,
              .autoplay_animations = false,
          });
      if (imported.valid()) {
        world->setName(imported.root_entity, "World");
        spawned_world = true;
      } else {
        spdlog::error("Failed to instantiate cached textured world scene '{}'",
                      kMaterialAssignmentWorldSceneKey);
      }
    }

    if (!spawned_world) {
      spdlog::error("Material assignment world scene was not available from the asset package");
      const ecs::Entity world_entity = world->createEntity();
      world->setName(world_entity, "World");
      world->add(world_entity, components::TransformComponent{});
      world->add(world_entity, components::MeshComponent{
                                .mesh_asset_key = importExampleMeshAsset(assets, "world.glb"),
                                .visible = true,
                                .shadow_visible = true,
                            });
    }
  }

  ecs::Entity spawnTank(const SpawnDesc& spawn,
                        size_t spawn_index,
                        const content::GltfSceneAsset* tank_asset) {
    const std::string asset_key_prefix =
        "examples/material_assignment/tanks/" + std::to_string(spawn_index);
    if (tank_asset != nullptr) {
      const scene::GltfSceneImportResult imported = scene::instantiateGltfSceneAsset(
          *world,
          *scene,
          *assets,
          *tank_asset,
          scene::GltfSceneInstantiateOptions{
              .create_synthetic_root = true,
              .autoplay_animations = false,
          });
      if (imported.valid()) {
        world->setName(imported.root_entity, spawn.name);
        auto& transform = world->get<components::TransformComponent>(imported.root_entity);
        transform.setPosition(spawn.position);
        transform.setRotation(math::fromYawPitch(spawn.initial_yaw, 0.0f));
        if (spawn.material_key[0] != '\0') {
          assignTintedMaterialVariants(imported,
                                       asset_key_prefix,
                                       spawn.material_key,
                                       materialColor(spawn.material_key));
        }
        return imported.root_entity;
      }
      spdlog::error("Failed to instantiate cached textured tank scene for {}", spawn.name);
    }

    return spawnFallbackTank(spawn);
  }

  ecs::Entity spawnFallbackTank(const SpawnDesc& spawn) {
    if (tank_mesh_.empty()) {
      tank_mesh_ = importExampleMeshAsset(assets, "tank_final.glb");
    }
    auto entity = world->createEntity();
    world->setName(entity, spawn.name);
    components::TransformComponent transform{};
    transform.setPosition(spawn.position);
    transform.setRotation(math::fromYawPitch(spawn.initial_yaw, 0.0f));
    world->add(entity, transform);
    world->add(entity, components::MeshComponent{
        .mesh_asset_key = tank_mesh_,
        .materials = spawn.material_key[0] == '\0'
            ? std::vector<components::MeshMaterialAssignment>{}
            : std::vector<components::MeshMaterialAssignment>{
                  components::MeshMaterialAssignment{
                      .slot = 0,
                      .material_key = std::string(spawn.material_key),
                  }},
        .visible = true});
    return entity;
  }

  void assignTintedMaterialVariants(const scene::GltfSceneImportResult& imported,
                                    const std::string& asset_key_prefix,
                                    std::string_view material_key_prefix,
                                    const math::Color& color) {
    uint32_t variant_index = 0;
    for (const ecs::Entity entity : imported.entities) {
      if (!world->isAlive(entity) || !world->has<components::MeshComponent>(entity)) {
        continue;
      }

      auto& mesh = world->get<components::MeshComponent>(entity);
      const geometry::MeshData* mesh_asset = assets->findMeshAsset(mesh.mesh_asset_key);
      uint32_t slot_count = 1;
      if (mesh_asset != nullptr && !mesh_asset->material_slots.empty()) {
        slot_count = static_cast<uint32_t>(mesh_asset->material_slots.size());
      }

      mesh.materials.clear();
      mesh.materials.reserve(slot_count);
      for (uint32_t slot = 0; slot < slot_count; ++slot) {
        std::string material_key{material_key_prefix};
        if (mesh_asset != nullptr && slot < mesh_asset->material_slots.size() &&
            !mesh_asset->material_slots[slot].default_material_key.empty()) {
          material_key = asset_key_prefix + "/variants/" + std::string(material_key_prefix) + "/" +
                         std::to_string(variant_index++);
          assets->registerMaterialVariant(material_key,
                                          mesh_asset->material_slots[slot].default_material_key,
                                          {{"base_color", color}});
        }
        mesh.materials.push_back(components::MeshMaterialAssignment{
            .slot = slot,
            .material_key = std::move(material_key),
        });
      }
    }
  }

  std::array<TankEntry, 4> tanks_{};
  std::string tank_mesh_;
  std::string environment_map_;
  float time_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::MaterialAssignmentExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Material Assignment Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.shadow_bias = 0.0006f;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("rendering/material_assignment"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
