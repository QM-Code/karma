#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include <glm/gtc/quaternion.hpp>
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

SceneBounds computePrefabBounds(const scene::GltfScenePrefab& prefab) {
  SceneBounds geometry_bounds{};
  SceneBounds fallback_bounds{};

  for (const auto& node : prefab.nodes) {
    const glm::vec3 world_pos = math::toGlm(node.world_position);
    expandBounds(fallback_bounds, world_pos);

    const glm::vec3 world_scale = math::toGlm(node.world_scale);
    const glm::mat3 rotation = glm::mat3_cast(math::toGlm(node.world_rotation));
    for (const auto& primitive : node.primitives) {
      for (const glm::vec3& vertex : primitive.mesh.vertices) {
        expandBounds(geometry_bounds, world_pos + rotation * (vertex * world_scale));
      }
    }
  }

  return geometry_bounds.valid ? geometry_bounds : fallback_bounds;
}

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

}  // namespace

class PostwarCityExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_up", platform::Key::E);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    const std::filesystem::path city_path =
        resolveExampleAssetPath("postwar_city_-_exterior_scene.glb");
    const scene::GltfScenePrefab prefab = scene::loadGltfScenePrefab(
        city_path,
        scene::GltfSceneLoadOptions{
            .import_meshes = true,
            .import_lights = false,
        });
    if (!prefab.valid()) {
      spdlog::error("Failed to load postwar city GLB from {}", city_path.string());
      spawnLighting(SceneBounds{});
      spawnCamera(SceneBounds{});
      spawnEnvironment();
      return;
    }

    for (const std::string& diagnostic : prefab.diagnostics) {
      spdlog::warn("Postwar city import diagnostic: {}", diagnostic);
    }

    const SceneBounds bounds = computePrefabBounds(prefab);
    const scene::GltfSceneImportResult imported = scene::instantiateGltfScenePrefab(
        *world,
        *scene,
        *graphics,
        prefab,
        scene::GltfSceneInstantiateOptions{
            .create_synthetic_root = false,
            .autoplay_animations = false,
        });
    if (!imported.valid()) {
      spdlog::error("Failed to instantiate postwar city GLB from {}", city_path.string());
    }

    logSceneSummary(city_path, prefab, bounds);
    spawnLighting(bounds);
    spawnCamera(bounds);
    spawnEnvironment();
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
  void logSceneSummary(const std::filesystem::path& city_path,
                       const scene::GltfScenePrefab& prefab,
                       const SceneBounds& bounds) const {
    std::size_t primitive_count = 0u;
    std::size_t vertex_count = 0u;
    std::size_t triangle_count = 0u;
    for (const auto& node : prefab.nodes) {
      primitive_count += node.primitives.size();
      for (const auto& primitive : node.primitives) {
        vertex_count += primitive.mesh.vertices.size();
        triangle_count += primitive.mesh.indices.size() / 3u;
      }
    }

    if (bounds.valid) {
      const glm::vec3 size = bounds.max - bounds.min;
      spdlog::info(
          "Postwar city loaded '{}': nodes={}, primitives={}, vertices={}, triangles={}, bounds=({:.2f}, {:.2f}, {:.2f})",
          city_path.string(),
          prefab.nodes.size(),
          primitive_count,
          vertex_count,
          triangle_count,
          size.x,
          size.y,
          size.z);
    } else {
      spdlog::info("Postwar city loaded '{}': nodes={}, primitives={}, vertices={}, triangles={}",
                   city_path.string(),
                   prefab.nodes.size(),
                   primitive_count,
                   vertex_count,
                   triangle_count);
    }
  }

  void spawnLighting(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.0f, 0.0f);
    const float radius = bounds.valid
                             ? std::max(glm::length((bounds.max - bounds.min) * 0.5f), 30.0f)
                             : 80.0f;

    helpers::spawnDirectionalLight(*world,
                                   "Sun",
                                   {center.x, center.y + radius, center.z},
                                   math::fromYawPitch(-0.65f, -0.82f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 0.95f, 0.84f, 1.0f},
                                       .intensity = 1.8f,
                                       .casts_shadows = true,
                                       .shadow_extent = std::max(80.0f, radius * 1.6f),
                                   });
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 4.0f, 0.0f);
    const glm::vec3 extents = bounds.valid ? (bounds.max - bounds.min) * 0.5f
                                           : glm::vec3(12.0f, 6.0f, 12.0f);
    const float radius = std::max(10.0f, glm::length(extents));
    const float fit_distance = radius / std::sin(glm::radians(60.0f) * 0.5f);
    const glm::vec3 target = center + glm::vec3(0.0f, extents.y * 0.10f, 0.0f);
    const glm::vec3 view_direction = glm::normalize(glm::vec3(0.55f, 0.26f, 1.0f));
    const glm::vec3 eye = target + view_direction * std::max(28.0f, fit_distance * 0.62f);
    const LookAngles look = lookAnglesToTarget(eye, target);

    camera_move_speed_ = std::max(14.0f, radius * 0.28f);
    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {eye.x, eye.y, eye.z},
                                          math::fromYawPitch(look.yaw, look.pitch),
                                          components::CameraComponent{
                                              .render_shadows = true,
                                              .fov_y_degrees = 60.0f,
                                              .near_clip = std::max(0.05f, radius * 0.001f),
                                              .far_clip = std::max(500.0f, radius * 6.0f),
                                              .is_primary = true,
                                          });
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;
  }

  void spawnEnvironment() {
    helpers::spawnEnvironment(*world,
                              "Skybox",
                              resolveExampleAssetPath("golden_gate_hills_4k.hdr").string(),
                              0.45f,
                              true);
  }

  ecs::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  float camera_move_speed_ = 20.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::PostwarCityExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Postwar City GLB Example";
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
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.forward_plus_max_local_lights = 1024;
  config.local_light_distance_damping = 0.06f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.65f;
  config.lighting_exposure = 1.05f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
