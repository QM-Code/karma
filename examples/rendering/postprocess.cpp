#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr const char* kPostProcessProfileKey = "diligentfx/postprocess";
constexpr const char* kPostProcessSceneKey = "examples/rendering/damaged_helmet";

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

math::Vec3 toMath(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}

struct SceneBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

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

float boundsRadius(const SceneBounds& bounds) {
  if (!bounds.valid) {
    return 1.5f;
  }
  return std::max(0.5f, glm::length((bounds.max - bounds.min) * 0.5f));
}

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) {
    return {};
  }
  return {
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

}  // namespace

class DiligentFxPostProcessExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindMouse("orbit", platform::MouseButton::Right);
    input->bindKey("reset", platform::Key::R, app::Trigger::Pressed);

    SceneBounds bounds{};
    bool spawned_model = false;

    if (const assets::GltfSceneAsset* cached_scene =
            assets->findGltfSceneAsset(kPostProcessSceneKey)) {
      const helpers::GltfSceneAssetBounds asset_bounds =
          helpers::computeGltfSceneAssetBounds(*assets, *cached_scene);
      bounds = SceneBounds{.min = asset_bounds.min,
                           .max = asset_bounds.max,
                           .valid = asset_bounds.valid};
      const world::GltfSceneImportResult imported = world::instantiateGltfSceneAsset(
          *world,
          *scene,
          *assets,
          *cached_scene,
          world::GltfSceneInstantiateOptions{
              .create_synthetic_root = false,
              .autoplay_animations = true,
          });
      spawned_model = imported.valid();
      if (!spawned_model) {
        spdlog::error("Failed to instantiate cached postprocess demo model '{}'",
                      kPostProcessSceneKey);
      }
    }

    if (!spawned_model) {
      spdlog::error("Postprocess demo model was not available from the asset package");
    }

    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.75f, 0.0f);
    const float radius = boundsRadius(bounds);
    spawnLighting(center, radius);
    spawnEnvironment();
    spawnCamera(center, radius);
    configurePostProcess(radius);
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }
    if (input->actionPressed("reset")) {
      yaw_ = default_yaw_;
      pitch_ = default_pitch_;
      distance_ = default_distance_;
      orbit_center_ = default_center_;
    }
    if (input->actionDown("orbit")) {
      yaw_ -= input->mouseDeltaX() * 0.0009f;
      pitch_ -= input->mouseDeltaY() * 0.0009f;
      pitch_ = std::clamp(pitch_, -1.42f, 1.42f);
    }

    const math::Quat rotation = math::fromYawPitch(yaw_, pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 eye = math::subtract(orbit_center_, math::scale(forward, distance_));

    auto& transform = world->get<components::TransformComponent>(camera_entity_);
    transform.setPosition(eye);
    transform.setRotation(rotation);

    (void)dt;
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onShutdown() override {}

 private:
  void spawnLighting(const glm::vec3& center, float radius) {
    helpers::spawnDirectionalLight(*world,
                                   "Sun",
                                   {center.x + radius * 1.8f, center.y + radius * 2.4f,
                                    center.z + radius * 1.6f},
                                   math::fromYawPitch(-0.62f, -0.74f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 0.95f, 0.85f, 1.0f},
                                       .intensity = 1.35f,
                                       .casts_shadows = true,
                                       .shadow_extent = std::max(6.0f, radius * 4.5f),
                                   });
    helpers::spawnPointLight(*world,
                             "Warm Fill",
                             {center.x - radius * 2.4f, center.y + radius * 1.5f,
                              center.z + radius * 2.1f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.76f, 0.55f, 1.0f},
                                 .intensity = 2.6f,
                                 .range = std::max(4.0f, radius * 5.0f),
                             });
    helpers::spawnPointLight(*world,
                             "Cool Rim",
                             {center.x + radius * 2.3f, center.y + radius * 1.2f,
                              center.z - radius * 2.2f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.48f, 0.68f, 1.0f, 1.0f},
                                 .intensity = 1.8f,
                                 .range = std::max(4.0f, radius * 5.0f),
                             });
  }

  void spawnEnvironment() {
    helpers::spawnEnvironment(*world, assets,
                              "Papermill",
                              registerExampleEnvironmentMap(
                                  assets,
                                  "diligent_gltf_viewer/textures/papermill.ktx"),
                              1.0f,
                              true);
  }

  void spawnCamera(const glm::vec3& center, float radius) {
    constexpr float kFovYDegrees = 60.0f;
    const float fit_distance = radius / std::sin(glm::radians(kFovYDegrees) * 0.5f);
    default_distance_ = std::max(2.0f, fit_distance * 1.35f);
    default_center_ = toMath(center);
    const glm::vec3 eye = center + glm::normalize(glm::vec3(0.52f, 0.24f, 1.0f)) * default_distance_;
    const LookAngles look = lookAnglesToTarget(eye, center);
    default_yaw_ = look.yaw;
    default_pitch_ = look.pitch;
    yaw_ = default_yaw_;
    pitch_ = default_pitch_;
    distance_ = default_distance_;
    orbit_center_ = default_center_;

    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {eye.x, eye.y, eye.z},
                                          math::fromYawPitch(yaw_, pitch_),
                                          components::CameraComponent{
                                              .render_shadows = true,
                                              .fov_y_degrees = kFovYDegrees,
                                              .near_clip = std::max(0.01f, radius * 0.01f),
                                              .far_clip = std::max(100.0f, radius * 20.0f),
                                              .is_primary = true,
                                              .post_process_profile_key = kPostProcessProfileKey,
                                          });
  }

  void configurePostProcess(float radius) {
    rendering::PostProcessSettings settings{};
    settings.bloom_enabled = true;
    settings.bloom_threshold = 0.72f;
    settings.bloom_intensity = 0.18f;
    settings.bloom_radius = 2.2f;
    settings.tone_mapping_enabled = true;
    settings.tone_exposure = 1.04f;
    settings.tone_contrast = 1.08f;
    settings.tone_saturation = 1.04f;
    settings.ssao_enabled = true;
    settings.ssao_radius = 2.4f;
    settings.ssao_intensity = 0.28f;
    settings.ssao_power = 1.35f;
    settings.screen_space_reflections_enabled = true;
    settings.ssr_intensity = 0.16f;
    settings.ssr_max_roughness = 0.62f;
    settings.ssr_thickness = 0.08f;
    settings.temporal_antialiasing_enabled = true;
    settings.taa_feedback = 0.90f;
    settings.taa_sharpening = 0.05f;
    settings.depth_of_field_enabled = true;
    settings.dof_focus_depth = std::max(1.0f, default_distance_ - radius * 0.35f);
    settings.dof_focus_range = std::max(1.0f, radius * 2.2f);
    settings.dof_intensity = 0.35f;
    if (assets) {
      assets->registerPostProcessProfile(kPostProcessProfileKey, settings);
    }
  }

  world::Entity camera_entity_{};
  math::Vec3 orbit_center_{};
  math::Vec3 default_center_{};
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  float distance_ = 4.0f;
  float default_yaw_ = 0.0f;
  float default_pitch_ = 0.0f;
  float default_distance_ = 4.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::DiligentFxPostProcessExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma DiligentFX Postprocess";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 2;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 64;
  config.forward_plus_max_local_lights = 128;
  config.lighting_exposure = 1.02f;
  config.environment_map_source_path =
      karma::demo::resolveExampleAssetPath("diligent_gltf_viewer/textures/papermill.ktx");
  config.environment_intensity = 1.0f;
  config.environment_draw_skybox = true;
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("rendering/damaged_helmet"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
