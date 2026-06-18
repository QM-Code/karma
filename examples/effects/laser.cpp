#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "karma/core/math/glm.h"

namespace karma::demo {

namespace {

struct SceneBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

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

void expandBounds(SceneBounds& bounds, const glm::vec3& point) {
  if (!bounds.valid) {
    bounds.min = point;
    bounds.max = point;
    bounds.valid = true;
    return;
  }
  bounds.min = glm::min(bounds.min, point);
  bounds.max = glm::max(bounds.max, point);
}

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  return {
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

SceneBounds computePointBounds(const std::vector<math::Vec3>& points) {
  SceneBounds bounds{};
  for (const math::Vec3& point : points) {
    expandBounds(bounds, math::toGlm(point));
  }
  return bounds;
}

const std::vector<math::Vec3>& beamPrefabPoints() {
  static const std::vector<math::Vec3> points{
      {-7.9555f, 4.0975f, -3.4132f},
      {-4.8736f, 2.277f, -2.8742f},
      {-1.2182f, 2.8607f, -4.6603f},
      {0.6275f, 1.9098f, -0.4058f},
      {4.0246f, 3.073f, -1.7853f},
      {8.5249f, 1.7081f, 0.9211f},
  };
  return points;
}

}  // namespace

class LaserExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    spawnWorld();
    spawnLighting();
    spawnBeam();
    spawnCamera();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    const float look_sensitivity = 0.0008f;
    const float move_speed = 18.0f;
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
    math::Vec3 right = math::normalize(math::cross(forward, up));

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
    SceneBounds bounds = computePointBounds(beamPrefabPoints());
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(2.0f, 2.0f, 2.0f);
    const float radius = std::max(4.0f, glm::length(extents));
    const glm::vec3 eye = center + glm::normalize(glm::vec3(0.28f, 0.32f, 1.0f)) * radius * 2.25f;
    const LookAngles look = lookAnglesToTarget(eye, center);

    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Camera");
    camera_entity_ = camera;
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;
    components::TransformComponent camera_transform{};
    camera_transform.setPosition(math::fromGlm(eye));
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.05f;
    camera_component.far_clip = std::max(200.0f, radius * 12.0f);
    camera_component.render_shadows = false;
    camera_component.is_primary = true;
    world->add(camera, camera_transform);
    world->add(camera, camera_component);
  }

  void spawnBeam() {
    const std::string_view effect_mode = laserEffectMode();
    const std::string prefab_path =
        effect_mode == "impostor" ? "prefabs/beam_impostor" : "prefabs/beam";
    const auto beam = prefabs::instantiatePrefab(
        *world,
        *scene,
        resolveExampleAssetPath(prefab_path),
        prefabs::PrefabInstantiateDesc{
            .name_override =
                effect_mode == "impostor" ? "Laser Path Impostor" : "Laser Path",
        });
    if (!beam.has_value()) {
      spdlog::error("Laser example failed to instantiate beam prefab");
      return;
    }

    beam_entity_ = beam->find(effect_mode == "impostor" ? "path_hot_core" : "beam");
    if (!beam_entity_.isValid()) {
      beam_entity_ = beam->root;
    }
  }

  std::string world_mesh_;
  std::string environment_map_;
  ecs::Entity camera_entity_{};
  ecs::Entity beam_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::LaserExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Laser Example";
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
