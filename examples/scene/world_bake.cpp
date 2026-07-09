#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "karma/math.h"

namespace karma::demo {

namespace {

constexpr const char* kWorldBakeSceneKey = "examples/scene/world_bake";
constexpr const char* kWorldBakeGltfSceneKey = "examples/scene/world_bake/world";
constexpr const char* kWorldBakeSkyboxKey = "examples/scene/world_bake/skybox";

struct SceneBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct LightImportStats {
  std::size_t directional = 0u;
  std::size_t point = 0u;
  std::size_t spot = 0u;
};

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) {
    return {};
  }

  return LookAngles{
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

LightImportStats countImportedLights(const assets::GltfSceneAsset& scene_asset) {
  LightImportStats stats{};
  for (const assets::GltfSceneAssetNode& node : scene_asset.nodes) {
    if (!node.has_light) {
      continue;
    }
    switch (node.light.type) {
      case components::LightComponent::Type::Directional:
        ++stats.directional;
        break;
      case components::LightComponent::Type::Point:
        ++stats.point;
        break;
      case components::LightComponent::Type::Spot:
        ++stats.spot;
        break;
    }
  }
  return stats;
}

void countLight(const components::LightComponent& light, LightImportStats& stats) {
  switch (light.type) {
    case components::LightComponent::Type::Directional:
      ++stats.directional;
      break;
    case components::LightComponent::Type::Point:
      ++stats.point;
      break;
    case components::LightComponent::Type::Spot:
      ++stats.spot;
      break;
  }
}

}  // namespace

class WorldBakeSkyboxExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_up", platform::Key::E);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    SceneBounds bounds{};
    if (const assets::GltfSceneAsset* scene_asset =
            assets->findGltfSceneAsset(kWorldBakeGltfSceneKey)) {
      const helpers::GltfSceneAssetBounds asset_bounds =
          helpers::computeGltfSceneAssetBounds(*assets, *scene_asset);
      bounds = SceneBounds{.min = asset_bounds.min,
                           .max = asset_bounds.max,
                           .valid = asset_bounds.valid};
      logSceneSummary(*scene_asset, bounds);
    } else {
      spdlog::error("Missing packaged world bake glTF scene '{}'", kWorldBakeGltfSceneKey);
    }

    if (assets->findEnvironmentMap(kWorldBakeSkyboxKey) == nullptr) {
      spdlog::error("Missing packaged world bake skybox '{}'", kWorldBakeSkyboxKey);
    }

    spawnCamera(bounds);
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kSmoothing = 20.0f;
    const float move_speed = std::max(8.0f, camera_move_speed_);

    if (input->actionDown("cam_look")) {
      target_camera_yaw_ -= input->mouseDeltaX() * kLookSensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * kLookSensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.55f, 1.55f);

    const float alpha = 1.0f - std::exp(-kSmoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

    auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat camera_rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 world_up{0.0f, 1.0f, 0.0f};
    math::Vec3 right = math::normalize(math::cross(forward, world_up));
    if (math::lengthSquared(right) <= 0.0001f) {
      right = {1.0f, 0.0f, 0.0f};
    }

    float forward_input = 0.0f;
    float right_input = 0.0f;
    float up_input = 0.0f;
    if (input->actionDown("cam_forward")) {
      forward_input += 1.0f;
    }
    if (input->actionDown("cam_backward")) {
      forward_input -= 1.0f;
    }
    if (input->actionDown("cam_right")) {
      right_input += 1.0f;
    }
    if (input->actionDown("cam_left")) {
      right_input -= 1.0f;
    }
    if (input->actionDown("cam_up")) {
      up_input += 1.0f;
    }
    if (input->actionDown("cam_down")) {
      up_input -= 1.0f;
    }

    math::Vec3 movement = math::add(math::scale(forward, forward_input),
                                    math::scale(right, right_input));
    movement = math::add(movement, math::scale(world_up, up_input));
    if (math::lengthSquared(movement) > 0.0001f) {
      movement = math::scale(math::normalize(movement), move_speed * dt);
      camera_xform.setPosition(math::add(camera_xform.getPosition(), movement));
    }
    camera_xform.setRotation(camera_rotation);
  }

  void onShutdown() override {}

 private:
  void logSceneSummary(const assets::GltfSceneAsset& scene_asset,
                       const SceneBounds& bounds) const {
    const helpers::GltfSceneAssetStats stats =
        helpers::summarizeGltfSceneAsset(*assets, scene_asset);
    const LightImportStats light_stats = countImportedLights(scene_asset);
    LightImportStats runtime_light_stats{};
    std::size_t runtime_shadow_lights = 0u;
    world->forEach<components::LightComponent>(
        [&](world::Entity entity) {
          (void)entity;
          const components::LightComponent& light =
              world->get<components::LightComponent>(entity);
          countLight(light, runtime_light_stats);
          if (light.casts_shadows) {
            ++runtime_shadow_lights;
          }
        });

    if (bounds.valid) {
      const glm::vec3 size = bounds.max - bounds.min;
      spdlog::info(
          "World bake scene loaded '{}': nodes={}, primitives={}, vertices={}, triangles={}, bounds=({:.2f}, {:.2f}, {:.2f}), imported_lights={{directional:{}, point:{}, spot:{}}}, runtime_lights={{directional:{}, point:{}, spot:{}, shadow_casting:{}}}",
          scene_asset.source_path.string(),
          stats.node_count,
          stats.primitive_count,
          stats.vertex_count,
          stats.triangle_count,
          size.x,
          size.y,
          size.z,
          light_stats.directional,
          light_stats.point,
          light_stats.spot,
          runtime_light_stats.directional,
          runtime_light_stats.point,
          runtime_light_stats.spot,
          runtime_shadow_lights);
    } else {
      spdlog::info(
          "World bake scene loaded '{}': nodes={}, primitives={}, vertices={}, triangles={}, imported_lights={{directional:{}, point:{}, spot:{}}}, runtime_lights={{directional:{}, point:{}, spot:{}, shadow_casting:{}}}",
          scene_asset.source_path.string(),
          stats.node_count,
          stats.primitive_count,
          stats.vertex_count,
          stats.triangle_count,
          light_stats.directional,
          light_stats.point,
          light_stats.spot,
          runtime_light_stats.directional,
          runtime_light_stats.point,
          runtime_light_stats.spot,
          runtime_shadow_lights);
    }
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 2.0f, 0.0f);
    const glm::vec3 extents = bounds.valid ? (bounds.max - bounds.min) * 0.5f
                                           : glm::vec3(10.0f, 5.0f, 10.0f);
    const float radius = std::max(10.0f, glm::length(extents));
    constexpr float kFovYDegrees = 60.0f;
    const float fit_distance = radius / std::sin(glm::radians(kFovYDegrees) * 0.5f);
    const glm::vec3 target = center + glm::vec3(0.0f, extents.y * 0.08f, 0.0f);
    const glm::vec3 view_direction = glm::normalize(glm::vec3(0.48f, 0.25f, 1.0f));
    const glm::vec3 eye = target + view_direction * std::max(18.0f, fit_distance * 0.72f);
    const LookAngles look = lookAnglesToTarget(eye, target);

    camera_move_speed_ = std::max(12.0f, radius * 0.22f);
    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {eye.x, eye.y, eye.z},
                                          math::fromYawPitch(look.yaw, look.pitch),
                                          components::CameraComponent{
                                              .render_shadows = false,
                                              .fov_y_degrees = kFovYDegrees,
                                              .near_clip = std::max(0.05f, radius * 0.0005f),
                                              .far_clip = std::max(1000.0f, radius * 8.0f),
                                              .is_primary = true,
                                          });
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;
  }

  world::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  float camera_move_speed_ = 20.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::WorldBakeSkyboxExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma World Bake Skybox Example";
  config.window.width = 1600;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 4096;
  config.shadow_pcf_radius = 1;
  config.shadow_bias = 0.00045f;
  config.shadow_receiver_bias_scale = 0.8f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_max_lights = 4;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.forward_plus_max_local_lights = 2048;
  config.local_light_distance_damping = 0.04f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.35f;
  config.lighting_exposure = 1.0f;
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("scene/world_bake/scene.package.json"));
  config.startup_scene_assets.push_back("examples/scene/world_bake");

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
