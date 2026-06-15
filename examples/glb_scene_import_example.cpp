#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

SceneBounds computePrefabBounds(const scene::GlbScenePrefab& prefab) {
  SceneBounds geometry_bounds{};
  SceneBounds fallback_bounds{};

  for (const auto& node : prefab.nodes) {
    const glm::vec3 world_pos = math::toGlm(node.world_position);
    expandBounds(fallback_bounds, world_pos);

    const glm::vec3 world_scale = math::toGlm(node.world_scale);
    const glm::mat3 rotation = glm::mat3_cast(math::toGlm(node.world_rotation));
    for (const auto& primitive : node.primitives) {
      for (const glm::vec3& vertex : primitive.mesh.vertices) {
        const glm::vec3 scaled = vertex * world_scale;
        expandBounds(geometry_bounds, world_pos + rotation * scaled);
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

class GlbSceneImportExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    const std::filesystem::path scene_path = resolveExampleAssetPath("world-with-lights.glb");
    const scene::GlbScenePrefab prefab = scene::loadGlbScenePrefab(
        scene_path,
        scene::GlbSceneLoadOptions{
            .import_meshes = true,
            .import_lights = true,
        });

    if (!prefab.valid()) {
      spdlog::error("Failed to load GLB scene prefab from {}", scene_path.string());
      spawnCamera(SceneBounds{});
      return;
    }

    const SceneBounds bounds = computePrefabBounds(prefab);
    const scene::GlbSceneImportResult imported = scene::instantiateGlbScenePrefab(
        *world,
        *scene,
        *graphics,
        prefab,
        scene::GlbSceneInstantiateOptions{
            .create_synthetic_root = false,
        },
        materials);
    if (!imported.valid()) {
      spdlog::error("Failed to instantiate GLB scene from {}", scene_path.string());
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
  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(2.0f, 2.0f, 2.0f);
    const float radius = std::max(1.0f, glm::length(extents));
    constexpr float kFovYRadians = glm::radians(60.0f);
    const float fit_distance = radius / std::sin(kFovYRadians * 0.5f);
    const float distance = std::max(8.0f, fit_distance * 1.15f);
    const glm::vec3 view_dir = glm::normalize(glm::vec3(0.42f, 0.28f, 1.0f));
    const glm::vec3 eye = center + view_dir * distance;

    auto camera = world->createEntity();
    world->setName(camera, "Camera");
    camera_entity_ = camera;

    const LookAngles look = lookAnglesToTarget(eye, center);
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;

    components::TransformComponent camera_xform{};
    camera_xform.setPosition({eye.x, eye.y, eye.z});
    camera_xform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera, camera_xform);
    world->add(camera, components::CameraComponent{
        .near_clip = std::max(0.05f, radius * 0.01f),
        .far_clip = std::max(250.0f, distance + radius * 8.0f),
        .is_primary = true});
  }

  ecs::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::GlbSceneImportExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma GLB Scene Import Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
