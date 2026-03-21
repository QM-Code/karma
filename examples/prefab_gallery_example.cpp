#include "demo_asset_paths.h"
#include "energy_orb_prefab_package.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct ColorVariant {
  const char* name = "";
  math::Color color{};
};

constexpr std::array<ColorVariant, 4> kColorVariants{{
    {"Red", {1.00f, 0.20f, 0.20f, 1.0f}},
    {"Blue", {0.24f, 0.56f, 1.00f, 1.0f}},
    {"Green", {0.20f, 1.00f, 0.36f, 1.0f}},
    {"Purple", {0.78f, 0.34f, 1.00f, 1.0f}},
}};

constexpr std::array<float, 4> kColumnX{{-18.0f, -6.0f, 6.0f, 18.0f}};
constexpr float kBeamRowZ = -12.0f;
constexpr float kOrbRowZ = 0.0f;
constexpr float kWaveRowZ = 12.0f;
constexpr float kBeamScale = 0.48f;
constexpr float kWaveRadius = 2.4f;

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

components::TransformComponent makeScaledTransform(const math::Vec3& position, float uniform_scale) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  transform.setScale({uniform_scale, uniform_scale, uniform_scale});
  return transform;
}

math::Vec3 toMath(const glm::vec3& value) {
  return {value.x, value.y, value.z};
}

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  return {
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

}  // namespace

class PrefabGalleryExample final : public app::GameInterface {
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
    spawnCamera();
    spawnPrefabs();
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    const float look_sensitivity = 0.0008f;
    const float move_speed = 22.0f;
    const float smoothing = 20.0f;

    if (input->actionDown("cam_look")) {
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
    camera_position.x += (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_position.y += (forward.y * forward_input) * move_speed * dt;
    camera_position.z += (forward.z * forward_input + right.z * right_input) * move_speed * dt;
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
                                 .mesh_key = world_mesh_,
                                 .visible = true,
                             });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                 .environment_map = environment_map_,
                                 .intensity = 0.18f,
                                 .draw_skybox = false,
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
                        .color = {0.80f, 0.84f, 1.0f, 1.0f},
                        .intensity = 0.36f,
                    });
  }

  void spawnCamera() {
    const glm::vec3 target(0.0f, 4.0f, 2.0f);
    const glm::vec3 eye(0.0f, 13.0f, 34.0f);
    const LookAngles look = lookAnglesToTarget(eye, target);

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;

    components::TransformComponent camera_transform{};
    camera_transform.setPosition(toMath(eye));
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));

    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.05f;
    camera_component.far_clip = 240.0f;
    camera_component.render_shadows = false;
    camera_component.is_primary = true;

    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, camera_component);
  }

  void spawnPrefabs() {
    if (prefab_registry == nullptr ||
        !registerEnergyOrbPrefabPackage(*prefab_registry)) {
      spdlog::error("Prefab gallery failed to register the orb package");
    }

    for (size_t i = 0; i < kColorVariants.size(); ++i) {
      const auto& variant = kColorVariants[i];
      const float x = kColumnX[i];

      const auto beam = prefabs::instantiatePrefab(
          *world,
          graphics,
          resolveExampleAssetPath("prefabs/beam"),
          prefabs::PrefabInstantiateDesc{
              .name = std::string(variant.name) + " Beam",
              .transform = makeScaledTransform({x, 0.0f, kBeamRowZ}, kBeamScale),
              .param_overrides = {{"glow", variant.color}},
          });
      if (!beam.has_value()) {
        spdlog::error("Prefab gallery failed to instantiate {} beam", variant.name);
      } else {
        const ecs::Entity beam_entity = beam->find("beam");
        if (beam_entity.isValid() && world->has<components::BeamPathComponent>(beam_entity)) {
          auto& beam_component = world->get<components::BeamPathComponent>(beam_entity);
          // Keep the gallery on the lighter local-light path. Four authored beams
          // with along-path helper lights can otherwise push this showcase scene
          // into the heavier lighting path on some drivers.
          beam_component.light_count = 0u;
          beam_component.light_spacing = 0.0f;
          beam_component.light_intensity = 0.0f;
        }
      }

      if (prefab_registry != nullptr) {
        const auto orb = prefab_registry->instantiate(
            *world,
            kEnergyOrbPrefabKey,
            prefabs::PrefabInstantiateDesc{
                .name = std::string(variant.name) + " Orb",
                .transform = makeTransform({x, 1.85f, kOrbRowZ}),
                .param_overrides = {{"accent", variant.color}},
            });
        if (!orb.has_value()) {
          spdlog::error("Prefab gallery failed to instantiate {} orb", variant.name);
        }
      }

      const auto wave = prefabs::instantiatePrefab(
          *world,
          graphics,
          resolveExampleAssetPath("prefabs/wave"),
          prefabs::PrefabInstantiateDesc{
              .name = std::string(variant.name) + " Wave",
              .transform = makeTransform({x, 2.2f, kWaveRowZ}),
              .param_overrides = {
                  {"color", variant.color},
                  {"radius", kWaveRadius},
                  {"opacity", 0.3f},
                  {"light_intensity", 82.0f},
                  {"light_range", 13.0f},
              },
          });
      if (!wave.has_value()) {
        spdlog::error("Prefab gallery failed to instantiate {} wave", variant.name);
      }
    }
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
  karma::demo::PrefabGalleryExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Prefab Gallery Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0009f;
  config.shadow_pcf_radius = 1;
  config.local_light_distance_damping = 0.05f;
  config.local_light_range_falloff_exponent = 1.35f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.0f;
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
