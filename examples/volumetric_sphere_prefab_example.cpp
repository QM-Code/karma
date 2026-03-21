#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <string>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

constexpr math::Vec3 kSpherePosition{0.0f, 2.2f, -5.3f};

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

}  // namespace

class VolumetricSpherePrefabExample final : public app::GameInterface {
 public:
  void onStart() override {
    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    spawnWorld();
    spawnLighting();
    spawnCamera();
    spawnSphere();
  }

  void onFixedUpdate(float dt) override { (void)dt; }
  void onUpdate(float dt) override { (void)dt; }
  void onShutdown() override {}

 private:
  void spawnWorld() {
    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
                                 .mesh_key = world_mesh_,
                                 .visible = true,
                             });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                 .environment_map = environment_map_,
                                 .intensity = 0.16f,
                                 .draw_skybox = true,
                             });
  }

  void spawnLighting() {
    const ecs::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 48.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.54f, -0.92f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 0.97f, 0.92f, 1.0f},
                        .intensity = 0.42f,
                    });
  }

  void spawnCamera() {
    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Camera");
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 2.55f, 6.7f});
    camera_transform.setRotation(math::fromYawPitch(0.0f, -0.03f));
    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.03f;
    camera_component.far_clip = 220.0f;
    camera_component.render_shadows = false;
    camera_component.is_primary = true;
    world->add(camera, camera_transform);
    world->add(camera, camera_component);
  }

  void spawnSphere() {
    const auto instance = prefabs::instantiatePrefab(
        *world,
        graphics,
        resolveExampleAssetPath("prefabs/volumetric_sphere"),
        prefabs::PrefabInstantiateDesc{
            .name = "Prefab Volumetric Sphere",
            .transform = makeTransform(kSpherePosition),
        });
    if (!instance.has_value()) {
      spdlog::error("Volumetric sphere prefab example failed to instantiate the prefab");
    }
  }

  std::string world_mesh_;
  std::string environment_map_;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::VolumetricSpherePrefabExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Volumetric Sphere Prefab Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.0f;
  config.lighting_exposure = 0.95f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
