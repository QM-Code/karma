#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace karma::demo {

namespace {

std::string_view laserEffectMode() {
  const char* raw_mode = std::getenv("KARMA_LASER_EFFECT");
  if (raw_mode == nullptr || raw_mode[0] == '\0') {
    return "volumetric";
  }

  const std::string_view mode{raw_mode};
  if (mode == "impostor" || mode == "legacy" || mode == "sprite_path") {
    return "impostor";
  }
  return "volumetric";
}

}  // namespace

class LaserPrefabExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_pan", platform::MouseButton::Right);

    world_mesh_ = importExampleMeshAsset(assets, "world.glb");
    environment_map_ = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");

    spawnWorld();
    spawnLighting();
    spawnCamera();
    spawnBeam();
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    const float look_sensitivity = 0.0008f;
    const float move_speed = 18.0f;
    const float smoothing = 20.0f;

    if (input->actionDown("cam_pan")) {
      target_camera_yaw_ -= input->mouseDeltaX() * look_sensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * look_sensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.55f, 1.55f);

    const float alpha = 1.0f - std::exp(-smoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat camera_rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    const math::Vec3 right = math::normalize(math::cross(forward, up));

    float forward_input = 0.0f;
    float right_input = 0.0f;
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;

    math::Vec3 camera_position = camera_transform.getPosition();
    camera_position.x +=
        (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_position.y += forward.y * forward_input * move_speed * dt;
    camera_position.z +=
        (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_transform.setPosition(camera_position);
    camera_transform.setRotation(camera_rotation);
  }

  void onShutdown() override {}

 private:
  void spawnWorld() {
    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
                                 .mesh_asset_key = world_mesh_,
                                 .visible = true,
                             });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                 .environment_map_asset_key = environment_map_,
                                 .intensity = 0.28f,
                                 .draw_skybox = true,
                             });
  }

  void spawnLighting() {
    const ecs::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 48.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.52f, -0.92f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 0.95f, 0.90f, 1.0f},
                        .intensity = 0.72f,
                    });
  }

  void spawnCamera() {
    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Camera");
    camera_entity_ = camera;
    camera_yaw_ = 0.02f;
    target_camera_yaw_ = camera_yaw_;
    camera_pitch_ = -0.34f;
    target_camera_pitch_ = camera_pitch_;
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({1.4f, 9.0f, 22.0f});
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.05f;
    camera_component.far_clip = 200.0f;
    camera_component.render_shadows = false;
    camera_component.is_primary = true;
    world->add(camera, camera_transform);
    world->add(camera, camera_component);
  }

  void spawnBeam() {
    const std::string_view effect_mode = laserEffectMode();
    const std::string prefab_path =
        effect_mode == "impostor" ? "prefabs/beam_impostor" : "prefabs/beam";
    prefabs::instantiatePrefab(
        *world,
        *scene,
        resolveExampleAssetPath(prefab_path),
        prefabs::PrefabInstantiateDesc{
            .name_override =
                effect_mode == "impostor" ? "Prefab Laser Impostor" : "Prefab Laser",
        });
  }

  std::string world_mesh_;
  std::string environment_map_;
  ecs::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::LaserPrefabExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Laser Prefab Example";
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

  engine.addRuntimeModule(std::make_unique<karma::volumes::VolumeRuntimeModule>());

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
