#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <array>
#include <cmath>
#include <string>

namespace karma::demo {

class MaterialOverrideExample final : public app::GameInterface {
 public:
  void onStart() override {
    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    tank_mesh_ = resolveExampleAssetPath("tank_final.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    materials->registerFromMeshTint(
        "tank_red", tank_mesh_, math::Color{1.0f, 0.35f, 0.30f, 1.0f});
    materials->registerFromMeshTint(
        "tank_blue", tank_mesh_, math::Color{0.35f, 0.55f, 1.0f, 1.0f});
    materials->registerFromMeshTint(
        "tank_gold", tank_mesh_, math::Color{1.0f, 0.82f, 0.35f, 1.0f});

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_key = world_mesh_});

    auto environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
        .environment_map = environment_map_,
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

    const std::array<SpawnDesc, 4> spawns{{
        {"Tank Original", {-9.0f, 0.0f, 0.0f}, "", 0.20f},
        {"Tank Red", {-3.0f, 0.0f, 0.0f}, "tank_red", 0.55f},
        {"Tank Blue", {3.0f, 0.0f, 0.0f}, "tank_blue", 0.90f},
        {"Tank Gold", {9.0f, 0.0f, 0.0f}, "tank_gold", 1.25f},
    }};

    for (size_t i = 0; i < spawns.size(); ++i) {
      const auto& spawn = spawns[i];
      auto entity = world->createEntity();
      world->setName(entity, spawn.name);
      components::TransformComponent transform{};
      transform.setPosition(spawn.position);
      transform.setRotation(math::fromYawPitch(spawn.initial_yaw, 0.0f));
      world->add(entity, transform);
      world->add(entity, components::MeshComponent{
          .mesh_key = tank_mesh_,
          .material_key = spawn.material_key,
          .visible = true});
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

  std::array<TankEntry, 4> tanks_{};
  std::string world_mesh_;
  std::string tank_mesh_;
  std::string environment_map_;
  float time_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::MaterialOverrideExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Material Override Example";
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

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
