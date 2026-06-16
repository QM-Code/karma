#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

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

SceneBounds computePrefabBounds(const scene::GlbScenePrefab& prefab) {
  SceneBounds geometry_bounds{};
  SceneBounds fallback_bounds{};

  for (const auto& node : prefab.nodes) {
    const glm::vec3 world_pos = toGlm(node.world_position);
    expandBounds(fallback_bounds, world_pos);

    const glm::vec3 world_scale = toGlm(node.world_scale);
    const glm::mat3 rotation = glm::mat3_cast(toGlm(node.world_rotation));
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

float boundsRadius(const SceneBounds& bounds, float fallback_radius) {
  if (!bounds.valid) {
    return fallback_radius;
  }
  return std::max(0.5f, glm::length((bounds.max - bounds.min) * 0.5f));
}

}  // namespace

class DiligentGltfViewerExample final : public app::GameInterface {
 public:
  explicit DiligentGltfViewerExample(std::filesystem::path model_path)
      : model_path_(std::move(model_path)) {}

  void onStart() override {
    input->bindMouse("viewer_orbit", platform::MouseButton::Right);
    input->bindKey("viewer_pan_forward", platform::Key::W);
    input->bindKey("viewer_pan_backward", platform::Key::S);
    input->bindKey("viewer_pan_left", platform::Key::A);
    input->bindKey("viewer_pan_right", platform::Key::D);
    input->bindKey("viewer_pan_down", platform::Key::Q);
    input->bindKey("viewer_pan_up", platform::Key::E);
    input->bindKey("viewer_zoom_in", platform::Key::Z);
    input->bindKey("viewer_zoom_out", platform::Key::X);
    input->bindKey("viewer_reset", platform::Key::R, input::Trigger::Pressed);

    const scene::GlbScenePrefab prefab = scene::loadGlbScenePrefab(
        model_path_,
        scene::GlbSceneLoadOptions{
            .import_meshes = true,
            .import_lights = false,
        });

    if (!prefab.valid()) {
      spdlog::error("Failed to load Diligent GLTFViewer model from {}", model_path_.string());
      spawnLighting(SceneBounds{});
      spawnCamera(SceneBounds{});
      spawnEnvironment();
      return;
    }

    for (const std::string& diagnostic : prefab.diagnostics) {
      spdlog::warn("Diligent GLTFViewer import diagnostic: {}", diagnostic);
    }

    const SceneBounds bounds = computePrefabBounds(prefab);
    const scene::GlbSceneImportResult imported = scene::instantiateGlbScenePrefab(
        *world,
        *scene,
        *graphics,
        prefab,
        scene::GlbSceneInstantiateOptions{
            .create_synthetic_root = false,
            .autoplay_animations = true,
        },
        materials);
    if (!imported.valid()) {
      spdlog::error("Failed to instantiate Diligent GLTFViewer model from {}", model_path_.string());
    }

    logSceneSummary(prefab, bounds);
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

    if (input->actionPressed("viewer_reset")) {
      resetCameraTargets();
    }

    constexpr float kLookSensitivity = 0.0009f;
    constexpr float kSmoothing = 18.0f;
    if (input->actionDown("viewer_orbit")) {
      target_camera_yaw_ -= input->mouseDeltaX() * kLookSensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * kLookSensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.45f, 1.45f);

    const math::Quat target_rotation = math::fromYawPitch(target_camera_yaw_, target_camera_pitch_);
    const math::Vec3 target_forward =
        math::normalize(math::rotateVec(target_rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 world_up{0.0f, 1.0f, 0.0f};
    math::Vec3 right = math::normalize(math::cross(target_forward, world_up));
    if (math::lengthSquared(right) <= 0.0001f) {
      right = {1.0f, 0.0f, 0.0f};
    }
    math::Vec3 planar_forward{target_forward.x, 0.0f, target_forward.z};
    if (math::lengthSquared(planar_forward) <= 0.0001f) {
      planar_forward = {0.0f, 0.0f, -1.0f};
    } else {
      planar_forward = math::normalize(planar_forward);
    }

    float forward_input = 0.0f;
    float right_input = 0.0f;
    float up_input = 0.0f;
    float zoom_input = 0.0f;
    if (input->actionDown("viewer_pan_forward")) {
      forward_input += 1.0f;
    }
    if (input->actionDown("viewer_pan_backward")) {
      forward_input -= 1.0f;
    }
    if (input->actionDown("viewer_pan_right")) {
      right_input += 1.0f;
    }
    if (input->actionDown("viewer_pan_left")) {
      right_input -= 1.0f;
    }
    if (input->actionDown("viewer_pan_up")) {
      up_input += 1.0f;
    }
    if (input->actionDown("viewer_pan_down")) {
      up_input -= 1.0f;
    }
    if (input->actionDown("viewer_zoom_in")) {
      zoom_input += 1.0f;
    }
    if (input->actionDown("viewer_zoom_out")) {
      zoom_input -= 1.0f;
    }

    math::Vec3 pan = math::add(math::scale(planar_forward, forward_input),
                               math::scale(right, right_input));
    pan = math::add(pan, math::scale(world_up, up_input));
    if (math::lengthSquared(pan) > 0.0001f) {
      const float pan_speed = std::max(0.35f, target_orbit_distance_ * 0.55f);
      target_orbit_center_ =
          math::add(target_orbit_center_, math::scale(math::normalize(pan), pan_speed * dt));
    }

    if (std::abs(zoom_input) > 0.0001f) {
      target_orbit_distance_ *= std::exp(-zoom_input * 2.2f * dt);
      target_orbit_distance_ =
          std::clamp(target_orbit_distance_, min_orbit_distance_, max_orbit_distance_);
    }

    const float alpha = 1.0f - std::exp(-kSmoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;
    orbit_distance_ += (target_orbit_distance_ - orbit_distance_) * alpha;
    orbit_center_.x += (target_orbit_center_.x - orbit_center_.x) * alpha;
    orbit_center_.y += (target_orbit_center_.y - orbit_center_.y) * alpha;
    orbit_center_.z += (target_orbit_center_.z - orbit_center_.z) * alpha;

    const math::Quat camera_rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 eye = math::subtract(orbit_center_, math::scale(forward, orbit_distance_));

    auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
    camera_xform.setPosition(eye);
    camera_xform.setRotation(camera_rotation);
  }

  void onShutdown() override {}

 private:
  void logSceneSummary(const scene::GlbScenePrefab& prefab, const SceneBounds& bounds) const {
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
          "Diligent GLTFViewer model loaded '{}': nodes={}, primitives={}, vertices={}, triangles={}, animations={}, bounds=({:.2f}, {:.2f}, {:.2f})",
          model_path_.string(),
          prefab.nodes.size(),
          primitive_count,
          vertex_count,
          triangle_count,
          prefab.animations.size(),
          size.x,
          size.y,
          size.z);
    } else {
      spdlog::info(
          "Diligent GLTFViewer model loaded '{}': nodes={}, primitives={}, vertices={}, triangles={}, animations={}",
          model_path_.string(),
          prefab.nodes.size(),
          primitive_count,
          vertex_count,
          triangle_count,
          prefab.animations.size());
    }
  }

  void spawnLighting(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.0f, 0.0f);
    const float radius = boundsRadius(bounds, 1.5f);
    const float light_radius = std::max(4.0f, radius * 5.0f);

    helpers::spawnDirectionalLight(*world,
                                   "Sun",
                                   {center.x + radius * 1.5f, center.y + radius * 2.0f,
                                    center.z + radius * 1.5f},
                                   math::fromYawPitch(-0.65f, -0.72f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 0.96f, 0.86f, 1.0f},
                                       .intensity = 1.25f,
                                       .casts_shadows = true,
                                       .shadow_extent = std::max(6.0f, radius * 4.0f),
                                   });

    helpers::spawnPointLight(*world,
                             "Viewer Key Light",
                             {center.x - radius * 2.6f, center.y + radius * 1.4f,
                              center.z + radius * 2.2f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.89f, 0.74f, 1.0f},
                                 .intensity = 2.8f,
                                 .range = light_radius,
                             });
    helpers::spawnPointLight(*world,
                             "Viewer Rim Light",
                             {center.x + radius * 2.4f, center.y + radius * 1.2f,
                              center.z - radius * 2.0f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.55f, 0.70f, 1.0f, 1.0f},
                                 .intensity = 1.65f,
                                 .range = light_radius,
                             });
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.75f, 0.0f);
    const float radius = boundsRadius(bounds, 1.5f);
    constexpr float kFovYDegrees = 60.0f;
    const float fit_distance = radius / std::sin(glm::radians(kFovYDegrees) * 0.5f);
    const float distance = std::max(2.0f, fit_distance * 1.35f);
    const glm::vec3 target = center;
    const glm::vec3 view_direction = glm::normalize(glm::vec3(0.52f, 0.24f, 1.0f));
    const glm::vec3 eye = target + view_direction * distance;
    const LookAngles look = lookAnglesToTarget(eye, target);

    default_orbit_center_ = toMath(target);
    default_orbit_distance_ = distance;
    default_camera_yaw_ = look.yaw;
    default_camera_pitch_ = look.pitch;
    min_orbit_distance_ = std::max(0.25f, radius * 0.22f);
    max_orbit_distance_ = std::max(12.0f, radius * 14.0f);

    orbit_center_ = default_orbit_center_;
    target_orbit_center_ = default_orbit_center_;
    orbit_distance_ = default_orbit_distance_;
    target_orbit_distance_ = default_orbit_distance_;
    camera_yaw_ = default_camera_yaw_;
    target_camera_yaw_ = default_camera_yaw_;
    camera_pitch_ = default_camera_pitch_;
    target_camera_pitch_ = default_camera_pitch_;

    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {eye.x, eye.y, eye.z},
                                          math::fromYawPitch(camera_yaw_, camera_pitch_),
                                          components::CameraComponent{
                                              .render_shadows = true,
                                              .fov_y_degrees = kFovYDegrees,
                                              .near_clip = std::max(0.01f, radius * 0.01f),
                                              .far_clip = std::max(100.0f, radius * 20.0f),
                                              .is_primary = true,
                                          });
  }

  void spawnEnvironment() {
    helpers::spawnEnvironment(*world,
                              "Skybox",
                              resolveExampleAssetPath("diligent_gltf_viewer/textures/papermill.ktx").string(),
                              1.0f,
                              true);
  }

  void resetCameraTargets() {
    target_orbit_center_ = default_orbit_center_;
    target_orbit_distance_ = default_orbit_distance_;
    target_camera_yaw_ = default_camera_yaw_;
    target_camera_pitch_ = default_camera_pitch_;
  }

  std::filesystem::path model_path_;
  ecs::Entity camera_entity_{};
  math::Vec3 orbit_center_{};
  math::Vec3 target_orbit_center_{};
  math::Vec3 default_orbit_center_{};
  float orbit_distance_ = 4.0f;
  float target_orbit_distance_ = 4.0f;
  float default_orbit_distance_ = 4.0f;
  float min_orbit_distance_ = 0.25f;
  float max_orbit_distance_ = 20.0f;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  float default_camera_yaw_ = 0.0f;
  float default_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  std::filesystem::path model_path =
      karma::demo::resolveExampleAssetPath("diligent_gltf_viewer/models/DamagedHelmet/DamagedHelmet.gltf");
  if (argc > 1 && argv[1] != nullptr && std::string(argv[1]).size() > 0u) {
    model_path = karma::demo::resolveExamplePath(argv[1]);
  }

  karma::app::EngineApp engine;
  karma::demo::DiligentGltfViewerExample game(model_path);

  karma::app::EngineConfig config;
  config.window.title = "Karma Diligent GLTF Viewer";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 64;
  config.forward_plus_max_local_lights = 128;
  config.local_light_distance_damping = 0.03f;
  config.ao_affects_local_lights = false;
  config.lighting_exposure = 1.05f;
  config.environment_map =
      karma::demo::resolveExampleAssetPath("diligent_gltf_viewer/textures/papermill.ktx");
  config.environment_intensity = 1.0f;
  config.environment_draw_skybox = true;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
