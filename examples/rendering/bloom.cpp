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

constexpr const char* kPanelMeshKey = "diligentfx_bloom/panel";
constexpr const char* kBoxMeshKey = "diligentfx_bloom/box";
constexpr const char* kWarmWindowMaterial = "diligentfx_bloom/material/window_warm";
constexpr const char* kCoolWindowMaterial = "diligentfx_bloom/material/window_cool";
constexpr const char* kMagentaSignMaterial = "diligentfx_bloom/material/sign_magenta";
constexpr const char* kAmberSignMaterial = "diligentfx_bloom/material/sign_amber";
constexpr const char* kRoadGlowMaterial = "diligentfx_bloom/material/road_glow";
constexpr const char* kBloomPostProcessProfileKey = "diligentfx/bloom";
constexpr const char* kBloomSceneKey = "examples/rendering/bloom_city";

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

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
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
    return 60.0f;
  }
  return std::max(10.0f, glm::length((bounds.max - bounds.min) * 0.5f));
}

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

renderer::MaterialDesc emissiveMaterial(const math::Color& color, float strength) {
  renderer::MaterialDesc material{};
  material.base_color = {color.r * 0.35f, color.g * 0.35f, color.b * 0.35f, 1.0f};
  material.emissive_color = color;
  material.emissive_strength = strength;
  material.metallic = 0.0f;
  material.roughness = 0.35f;
  material.unlit = true;
  return material;
}

}  // namespace

class DiligentFxBloomExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_reset", platform::Key::R, input::Trigger::Pressed);
    input->bindMouse("cam_look", platform::MouseButton::Right);
    input->bindKey("bloom_toggle", platform::Key::B, input::Trigger::Pressed);
    input->bindKey("bloom_decrease", platform::Key::Minus);
    input->bindKey("bloom_increase", platform::Key::Equal);
    input->bindKey("bloom_radius_decrease", platform::Key::LeftBracket);
    input->bindKey("bloom_radius_increase", platform::Key::RightBracket);

    SceneBounds bounds{};
    bool spawned_city = false;
    if (const content::GltfSceneAsset* cached_scene =
            assets->findGltfSceneAsset(kBloomSceneKey)) {
      const helpers::GltfSceneAssetBounds asset_bounds =
          helpers::computeGltfSceneAssetBounds(*assets, *cached_scene);
      bounds = SceneBounds{.min = asset_bounds.min,
                           .max = asset_bounds.max,
                           .valid = asset_bounds.valid};
      const scene::GltfSceneImportResult imported = scene::instantiateGltfSceneAsset(
          *world,
          *scene,
          *assets,
          *cached_scene,
          scene::GltfSceneInstantiateOptions{
              .create_synthetic_root = false,
              .autoplay_animations = false,
          });
      spawned_city = imported.valid();
      if (!spawned_city) {
        spdlog::error("Failed to instantiate cached bloom scene '{}'", kBloomSceneKey);
      }
    }

    if (!spawned_city) {
      spdlog::error("Bloom scene was not available from the asset package");
    }

    registerBloomResources();
    spawnLighting(bounds);
    spawnBloomEmitters(bounds);
    spawnEnvironment();
    spawnCamera(bounds);
    configurePostProcess();
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    handlePostProcessInput(dt);

    if (input->actionPressed("cam_reset")) {
      resetCamera();
    }

    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kSmoothing = 20.0f;
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
      movement = math::scale(math::normalize(movement), camera_move_speed_ * dt);
      camera_xform.setPosition(math::add(camera_xform.getPosition(), movement));
    }
    camera_xform.setRotation(camera_rotation);
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onShutdown() override {}

 private:
  void registerBloomResources() {
    assets->registerMeshAsset(kPanelMeshKey, helpers::makeBoxMesh({0.5f, 0.5f, 0.025f}));
    assets->registerMeshAsset(kBoxMeshKey, helpers::makeBoxMesh({0.5f, 0.5f, 0.5f}));

    assets->registerMaterialAsset(kWarmWindowMaterial,
                                 emissiveMaterial({1.0f, 0.72f, 0.34f, 1.0f}, 7.5f));
    assets->registerMaterialAsset(kCoolWindowMaterial,
                                 emissiveMaterial({0.38f, 0.70f, 1.0f, 1.0f}, 6.5f));
    assets->registerMaterialAsset(kMagentaSignMaterial,
                                 emissiveMaterial({1.0f, 0.23f, 0.78f, 1.0f}, 9.0f));
    assets->registerMaterialAsset(kAmberSignMaterial,
                                 emissiveMaterial({1.0f, 0.82f, 0.24f, 1.0f}, 10.0f));
    assets->registerMaterialAsset(kRoadGlowMaterial,
                                 emissiveMaterial({0.17f, 0.95f, 1.0f, 1.0f}, 7.0f));
  }

  ecs::Entity spawnScaledMesh(const std::string& name,
                              const std::string& mesh_key,
                              const std::string& material_key,
                              const glm::vec3& position,
                              const glm::vec3& scale,
                              const math::Quat& rotation = {},
                              bool shadow_visible = false) {
    const ecs::Entity entity = world->createEntity();
    world->setName(entity, name);
    components::TransformComponent transform{};
    transform.setPosition(toMath(position));
    transform.setRotation(rotation);
    transform.setScale(toMath(scale));
    world->add(entity, transform);
    world->add(entity, components::MeshComponent{
                          .mesh_asset_key = mesh_key,
                          .materials = {components::MeshMaterialAssignment{
                              .slot = 0,
                              .material_key = material_key,
                          }},
                          .visible = true,
                          .shadow_visible = shadow_visible,
                      });
    return entity;
  }

  void spawnLighting(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.0f, 0.0f);
    const float radius = boundsRadius(bounds);
    const float local_range = std::max(24.0f, std::min(radius * 0.34f, 90.0f));

    helpers::spawnDirectionalLight(*world,
                                   "Night Key",
                                   {center.x, center.y + radius, center.z},
                                   math::fromYawPitch(-0.75f, -0.88f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {0.50f, 0.58f, 0.76f, 1.0f},
                                       .intensity = 0.45f,
                                       .casts_shadows = true,
                                       .shadow_extent = std::max(80.0f, radius * 1.6f),
                                   });
    helpers::spawnPointLight(*world,
                             "Bloom Warm Fill",
                             {center.x - local_range * 0.45f, center.y + local_range * 0.16f,
                              center.z + local_range * 0.30f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.58f, 0.30f, 1.0f},
                                 .intensity = 30.0f,
                                 .range = local_range,
                             });
    helpers::spawnPointLight(*world,
                             "Bloom Cool Fill",
                             {center.x + local_range * 0.45f, center.y + local_range * 0.20f,
                              center.z - local_range * 0.20f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.34f, 0.67f, 1.0f, 1.0f},
                                 .intensity = 22.0f,
                                 .range = local_range,
                             });
  }

  void spawnBloomEmitters(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 6.0f, 0.0f);
    const glm::vec3 extents = bounds.valid ? (bounds.max - bounds.min) * 0.5f
                                           : glm::vec3(24.0f, 12.0f, 24.0f);
    const float radius = boundsRadius(bounds);
    const float unit = std::clamp(radius * 0.018f, 0.55f, 2.8f);
    const float ground_y = bounds.valid ? bounds.min.y + unit * 0.08f : 0.0f;
    const float facade_z =
        center.z + std::clamp(extents.z * 0.10f, unit * 5.0f, unit * 16.0f);
    const float x_span = std::clamp(extents.x * 0.28f, unit * 8.0f, unit * 22.0f);
    const int columns = 11;
    const int rows = 8;
    const float x_spacing = x_span / static_cast<float>(columns - 1);
    const float y_spacing = unit * 1.05f;
    const float base_y = ground_y + unit * 2.2f;

    for (int column = 0; column < columns; ++column) {
      for (int row = 0; row < rows; ++row) {
        if ((column + row) % 5 == 0) {
          continue;
        }
        const float x = center.x - x_span * 0.5f + static_cast<float>(column) * x_spacing;
        const float y = base_y + static_cast<float>(row) * y_spacing;
        const float z = facade_z + static_cast<float>((column % 3) - 1) * unit * 0.32f;
        const glm::vec3 scale{
            unit * (0.45f + 0.08f * static_cast<float>((column + row) % 3)),
            unit * (0.14f + 0.03f * static_cast<float>(row % 2)),
            unit * 0.16f,
        };
        const std::string material_key =
            ((column + row) % 4 == 0) ? kCoolWindowMaterial : kWarmWindowMaterial;
        spawnScaledMesh("Bloom Window",
                        kPanelMeshKey,
                        material_key,
                        {x, y, z},
                        scale,
                        math::fromYawPitch(-0.06f + static_cast<float>(column) * 0.012f, 0.0f));
      }
    }

    const glm::vec3 sign_a{center.x - unit * 5.6f, base_y + unit * 8.8f,
                           facade_z - unit * 0.9f};
    const glm::vec3 sign_b{center.x + unit * 5.4f, base_y + unit * 6.6f,
                           facade_z + unit * 0.4f};
    spawnScaledMesh("Bloom Amber Sign",
                    kPanelMeshKey,
                    kAmberSignMaterial,
                    sign_a,
                    {unit * 3.9f, unit * 1.15f, unit * 0.20f},
                    math::fromYawPitch(0.12f, 0.0f));
    spawnScaledMesh("Bloom Magenta Sign",
                    kPanelMeshKey,
                    kMagentaSignMaterial,
                    sign_b,
                    {unit * 3.0f, unit * 0.90f, unit * 0.20f},
                    math::fromYawPitch(-0.10f, 0.0f));

    const float road_z = center.z + unit * 3.6f;
    for (int lane = 0; lane < 5; ++lane) {
      const float x = center.x + static_cast<float>(lane - 2) * unit * 2.2f;
      spawnScaledMesh("Bloom Road Strip",
                      kBoxMeshKey,
                      (lane % 2 == 0) ? kRoadGlowMaterial : kWarmWindowMaterial,
                      {x, ground_y + unit * 0.04f, road_z},
                      {unit * 0.10f, unit * 0.035f, unit * 8.0f});
    }

    const float light_range = unit * 18.0f;
    helpers::spawnPointLight(*world,
                             "Bloom Amber Sign Light",
                             toMath(sign_a),
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.70f, 0.28f, 1.0f},
                                 .intensity = 28.0f,
                                 .range = light_range,
                             });
    helpers::spawnPointLight(*world,
                             "Bloom Magenta Sign Light",
                             toMath(sign_b),
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.24f, 0.82f, 1.0f},
                                 .intensity = 24.0f,
                                 .range = light_range,
                             });
    helpers::spawnPointLight(*world,
                             "Bloom Road Light",
                             {center.x, ground_y + unit * 2.0f, road_z},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.22f, 0.88f, 1.0f, 1.0f},
                                 .intensity = 20.0f,
                                 .range = light_range,
                             });
  }

  void spawnEnvironment() {
    helpers::spawnEnvironment(*world, assets,
                              "Dim IBL",
                              registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
                              0.18f,
                              true);
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 6.0f, 0.0f);
    const glm::vec3 extents = bounds.valid ? (bounds.max - bounds.min) * 0.5f
                                           : glm::vec3(24.0f, 12.0f, 24.0f);
    const float radius = boundsRadius(bounds);
    const float unit = std::clamp(radius * 0.018f, 0.55f, 2.8f);
    const float ground_y = bounds.valid ? bounds.min.y + unit * 0.08f : 0.0f;
    const float facade_z =
        center.z + std::clamp(extents.z * 0.10f, unit * 5.0f, unit * 16.0f);
    const glm::vec3 target{center.x, ground_y + unit * 6.2f, facade_z};
    const glm::vec3 view_direction = glm::normalize(glm::vec3(0.55f, 0.30f, 1.0f));
    const float fit_distance = radius / std::sin(glm::radians(60.0f) * 0.5f);
    const float distance = std::clamp(fit_distance * 0.35f, unit * 26.0f, unit * 46.0f);
    const glm::vec3 eye = target + view_direction * distance;
    const LookAngles look = lookAnglesToTarget(eye, target);

    default_camera_position_ = toMath(eye);
    default_camera_yaw_ = look.yaw;
    default_camera_pitch_ = look.pitch;
    camera_move_speed_ = std::max(10.0f, unit * 8.5f);

    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          default_camera_position_,
                                          math::fromYawPitch(look.yaw, look.pitch),
                                          components::CameraComponent{
                                              .render_shadows = true,
                                              .fov_y_degrees = 60.0f,
                                              .near_clip = 0.05f,
                                              .far_clip = std::max(500.0f, radius * 6.0f),
                                              .is_primary = true,
                                              .post_process_profile_key =
                                                  kBloomPostProcessProfileKey,
                                          });
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;
  }

  void resetCamera() {
    auto& transform = world->get<components::TransformComponent>(camera_entity_);
    transform.setPosition(default_camera_position_);
    transform.setRotation(math::fromYawPitch(default_camera_yaw_, default_camera_pitch_));
    camera_yaw_ = default_camera_yaw_;
    target_camera_yaw_ = default_camera_yaw_;
    camera_pitch_ = default_camera_pitch_;
    target_camera_pitch_ = default_camera_pitch_;
  }

  void configurePostProcess() {
    post_process_ = renderer::PostProcessSettings{};
    post_process_.bloom_enabled = true;
    post_process_.bloom_threshold = 0.48f;
    post_process_.bloom_intensity = 0.95f;
    post_process_.bloom_radius = 4.8f;
    post_process_.tone_mapping_enabled = false;
    bloom_enabled_ = true;
    applyPostProcess();
  }

  void applyPostProcess() {
    post_process_.bloom_enabled = bloom_enabled_;
    if (assets) {
      assets->registerPostProcessProfile(kBloomPostProcessProfileKey, post_process_);
    }
  }

  void handlePostProcessInput(float dt) {
    bool changed = false;
    if (input->actionPressed("bloom_toggle")) {
      bloom_enabled_ = !bloom_enabled_;
      changed = true;
    }
    if (input->actionDown("bloom_increase")) {
      post_process_.bloom_intensity =
          std::clamp(post_process_.bloom_intensity + dt * 0.55f, 0.0f, 2.0f);
      changed = true;
    }
    if (input->actionDown("bloom_decrease")) {
      post_process_.bloom_intensity =
          std::clamp(post_process_.bloom_intensity - dt * 0.55f, 0.0f, 2.0f);
      changed = true;
    }
    if (input->actionDown("bloom_radius_increase")) {
      post_process_.bloom_radius =
          std::clamp(post_process_.bloom_radius + dt * 2.0f, 0.35f, 9.0f);
      changed = true;
    }
    if (input->actionDown("bloom_radius_decrease")) {
      post_process_.bloom_radius =
          std::clamp(post_process_.bloom_radius - dt * 2.0f, 0.35f, 9.0f);
      changed = true;
    }
    if (changed) {
      applyPostProcess();
    }
  }

  ecs::Entity camera_entity_{};
  renderer::PostProcessSettings post_process_{};
  math::Vec3 default_camera_position_{};
  float default_camera_yaw_ = 0.0f;
  float default_camera_pitch_ = 0.0f;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  float camera_move_speed_ = 20.0f;
  bool bloom_enabled_ = true;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::DiligentFxBloomExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma DiligentFX Bloom";
  config.window.width = 1600;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 4096;
  config.shadow_pcf_radius = 2;
  config.shadow_bias = 0.00045f;
  config.shadow_receiver_bias_scale = 0.8f;
  config.shadow_normal_bias_scale = 1.0f;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.forward_plus_max_local_lights = 512;
  config.local_light_distance_damping = 0.04f;
  config.local_light_range_falloff_exponent = 1.05f;
  config.ao_affects_local_lights = false;
  config.lighting_exposure = 1.25f;
  config.environment_map_source_path =
      karma::demo::resolveExampleAssetPath("golden_gate_hills_4k.hdr");
  config.environment_intensity = 0.18f;
  config.environment_draw_skybox = true;
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("rendering/bloom_city"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
