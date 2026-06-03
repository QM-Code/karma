#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <string>

namespace karma::demo {

namespace {

constexpr math::Vec3 kOrbBasePosition{0.0f, 1.85f, 0.0f};
constexpr math::Color kOrbAccentColor{0.18f, 1.0f, 0.28f, 1.0f};

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

void setPrefabInstancePlayback(ecs::World& world,
                               const prefabs::PrefabInstance& instance,
                               bool enabled) {
  for (const ecs::Entity entity : instance.entities) {
    if (!world.isAlive(entity)) {
      continue;
    }
    if (world.has<components::ParticleEmitterComponent>(entity)) {
      particles::setEffectPlayback(world, entity, enabled, enabled);
    }
    if (world.has<components::MeshComponent>(entity)) {
      world.get<components::MeshComponent>(entity).visible = enabled;
    }
    if (world.has<components::VisibilityComponent>(entity)) {
      world.get<components::VisibilityComponent>(entity).visible = enabled;
    }
  }
}

void restartInstanceParticleEffects(ecs::World& world, const prefabs::PrefabInstance& instance) {
  for (const ecs::Entity entity : instance.entities) {
    if (world.isAlive(entity)) {
      particles::restartEffect(world, entity);
    }
  }
}

}  // namespace

class EnergyOrbExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);
    input->bindKey("toggle_orb", platform::Key::Space, input::Trigger::Pressed);
    input->bindKey("restart_orb", platform::Key::R, input::Trigger::Pressed);

    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    spawnWorld();
    spawnLighting();

    const auto orb = prefabs::instantiatePrefab(
        *world,
        *scene,
        resolveExampleAssetPath("prefabs/energy_orb"),
        prefabs::PrefabInstantiateDesc{
            .root_transform = makeTransform(kOrbBasePosition),
            .name_override = "Energy Orb",
        });
    if (orb.has_value()) {
      orb_instance_ = *orb;
      orb_root_entity_ = orb_instance_.root;
    } else {
      spdlog::error("Energy orb example failed to instantiate the orb prefab");
    }

    spawnCamera();
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    time_ += dt;

    if (world->isAlive(camera_entity_)) {
      const float look_sensitivity = 0.0008f;
      const float move_speed = 14.0f;
      const float smoothing = 20.0f;

      if (input->actionDown("cam_look")) {
        target_camera_yaw_ -= input->mouseDeltaX() * look_sensitivity;
        target_camera_pitch_ -= input->mouseDeltaY() * look_sensitivity;
      }
      target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.55f, 1.55f);

      const float alpha = 1.0f - std::exp(-smoothing * dt);
      camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
      camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

      auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
      const math::Quat cam_rot = math::fromYawPitch(camera_yaw_, camera_pitch_);
      math::Vec3 forward = math::normalize(math::rotateVec(cam_rot, {0.0f, 0.0f, -1.0f}));
      const math::Vec3 up{0.0f, 1.0f, 0.0f};
      const math::Vec3 right = math::normalize(math::cross(forward, up));

      float forward_input = 0.0f;
      float right_input = 0.0f;
      if (input->actionDown("cam_forward")) forward_input += 1.0f;
      if (input->actionDown("cam_backward")) forward_input -= 1.0f;
      if (input->actionDown("cam_right")) right_input += 1.0f;
      if (input->actionDown("cam_left")) right_input -= 1.0f;

      math::Vec3 cam_pos = camera_xform.getPosition();
      cam_pos.x += (forward.x * forward_input + right.x * right_input) * move_speed * dt;
      cam_pos.y += (forward.y * forward_input) * move_speed * dt;
      cam_pos.z += (forward.z * forward_input + right.z * right_input) * move_speed * dt;
      camera_xform.setPosition(cam_pos);
      camera_xform.setRotation(cam_rot);
    }

    if (input->actionPressed("toggle_orb")) {
      orb_enabled_ = !orb_enabled_;
      setPrefabInstancePlayback(*world, orb_instance_, orb_enabled_);
    }

    if (input->actionPressed("restart_orb")) {
      restartInstanceParticleEffects(*world, orb_instance_);
      orb_enabled_ = true;
      setPrefabInstancePlayback(*world, orb_instance_, true);
    }

    if (world->isAlive(orb_root_entity_) &&
        world->has<components::TransformComponent>(orb_root_entity_)) {
      auto& orb_transform = world->get<components::TransformComponent>(orb_root_entity_);
      const math::Vec3 orbit_offset{
          std::sin(time_ * 0.72f) * 0.65f,
          std::sin(time_ * 1.35f) * 0.16f,
          std::cos(time_ * 0.58f) * 0.28f,
      };
      orb_transform.setPosition({
          kOrbBasePosition.x + orbit_offset.x,
          kOrbBasePosition.y + orbit_offset.y,
          kOrbBasePosition.z + orbit_offset.z,
      });
    }
  }

  void onShutdown() override {}

 private:
  void spawnWorld() {
    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_key = world_mesh_});

    const ecs::Entity environment_entity = world->createEntity();
    world->setName(environment_entity, "Environment");
    world->add(environment_entity,
               components::EnvironmentComponent{
                   .environment_map = environment_map_,
                   .intensity = 0.18f,
                   .draw_skybox = false,
               });
  }

  void spawnLighting() {
    const ecs::Entity sun_entity = world->createEntity();
    world->setName(sun_entity, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 40.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.48f, -0.82f));
    world->add(sun_entity, sun_transform);
    world->add(sun_entity,
               components::LightComponent{
                   .type = components::LightComponent::Type::Directional,
                   .color = {0.80f, 0.84f, 1.0f, 1.0f},
                   .intensity = 0.36f,
                   .shadow_extent = 50.0f,
               });
  }

  void spawnCamera() {
    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    camera_yaw_ = 0.0f;
    target_camera_yaw_ = 0.0f;
    camera_pitch_ = -0.12f;
    target_camera_pitch_ = -0.12f;
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 2.35f, 6.6f});
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_,
               components::CameraComponent{
                   .near_clip = 0.05f,
                   .far_clip = 120.0f,
                   .is_primary = true,
               });
  }

  std::string world_mesh_;
  std::string environment_map_;
  prefabs::PrefabInstance orb_instance_{};
  ecs::Entity orb_root_entity_{};
  ecs::Entity camera_entity_{};
  bool orb_enabled_ = true;
  float time_ = 0.0f;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::EnergyOrbExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Energy Orb Example";
  config.window.width = 1600;
  config.window.height = 900;
  config.vsync = true;
  config.generate_mipmaps = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0009f;
  config.shadow_pcf_radius = 1;
  config.local_light_distance_damping = 0.05f;
  config.local_light_range_falloff_exponent = 1.35f;
  config.lighting_exposure = 1.15f;
  config.environment_map = karma::demo::resolveExampleAssetPath("golden_gate_hills_4k.hdr");
  config.environment_intensity = 0.18f;
  config.environment_draw_skybox = false;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
