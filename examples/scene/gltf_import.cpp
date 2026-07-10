#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "karma/math.h"

namespace karma::demo {

namespace {

constexpr float kDisplayHeight = 3.6f;
constexpr float kModelGap = 1.25f;
constexpr float kHalfPi = 1.57079632679f;

struct ModelDesc {
  const char* asset_key;
  const char* display_name;
  float yaw;
  float pitch;
};

constexpr std::array<ModelDesc, 2> kModels = {{
    {"examples/scene/gltf_import/human_fbx", "FBX Character", 0.0f, -kHalfPi},
    {"examples/scene/gltf_import/human_glb", "GLB Character", -kHalfPi, 0.0f},
}};

struct SceneBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct PreparedModel {
  ModelDesc desc{};
  const assets::GltfSceneAsset* asset = nullptr;
  helpers::GltfSceneAssetBounds source_bounds{};
  SceneBounds oriented_bounds{};
  math::Quat display_rotation{};
  float display_scale = 1.0f;
  float display_width = 1.0f;
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

bool usableBounds(const helpers::GltfSceneAssetBounds& bounds) {
  const glm::vec3 size = bounds.max - bounds.min;
  return bounds.valid && std::isfinite(size.x) && std::isfinite(size.y) &&
         std::isfinite(size.z) && size.x >= 0.0f && size.y > 0.0001f &&
         size.z >= 0.0f;
}

SceneBounds rotateBounds(const helpers::GltfSceneAssetBounds& bounds,
                         const math::Quat& rotation) {
  SceneBounds rotated{};
  for (int x = 0; x < 2; ++x) {
    for (int y = 0; y < 2; ++y) {
      for (int z = 0; z < 2; ++z) {
        const math::Vec3 point{
            x == 0 ? bounds.min.x : bounds.max.x,
            y == 0 ? bounds.min.y : bounds.max.y,
            z == 0 ? bounds.min.z : bounds.max.z,
        };
        const math::Vec3 value = math::rotateVec(rotation, point);
        expandBounds(rotated, {value.x, value.y, value.z});
      }
    }
  }
  return rotated;
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

class GltfSceneImportExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    const SceneBounds bounds = spawnModels();
    spawnGround(bounds);
    spawnLighting(bounds);
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
    const float move_speed = camera_move_speed_;
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
  SceneBounds spawnModels() {
    std::vector<PreparedModel> models;
    models.reserve(kModels.size());
    for (const ModelDesc& desc : kModels) {
      const assets::GltfSceneAsset* asset = assets->findGltfSceneAsset(desc.asset_key);
      if (asset == nullptr) {
        spdlog::error("Missing packaged static model '{}'", desc.asset_key);
        continue;
      }

      helpers::GltfSceneAssetBounds source_bounds =
          helpers::computeGltfSceneAssetBounds(*assets, *asset);
      if (!usableBounds(source_bounds)) {
        spdlog::warn("Static model '{}' has no usable geometry bounds; using fallback bounds",
                     desc.asset_key);
        source_bounds = helpers::GltfSceneAssetBounds{
            .min = {-0.5f, 0.0f, -0.5f},
            .max = {0.5f, 2.0f, 0.5f},
            .valid = true,
        };
      }

      const math::Quat display_rotation =
          math::fromYawPitch(desc.yaw, desc.pitch);
      const SceneBounds oriented_bounds =
          rotateBounds(source_bounds, display_rotation);
      const glm::vec3 size = oriented_bounds.max - oriented_bounds.min;
      const float display_scale = kDisplayHeight / size.y;
      models.push_back(PreparedModel{
          .desc = desc,
          .asset = asset,
          .source_bounds = source_bounds,
          .oriented_bounds = oriented_bounds,
          .display_rotation = display_rotation,
          .display_scale = display_scale,
          .display_width = std::max(size.x * display_scale, 0.25f),
      });
    }

    float total_width = 0.0f;
    for (const PreparedModel& model : models) {
      total_width += model.display_width;
    }
    if (models.size() > 1u) {
      total_width += kModelGap * static_cast<float>(models.size() - 1u);
    }

    SceneBounds scene_bounds{};
    float cursor = total_width * -0.5f;
    for (const PreparedModel& model : models) {
      const float center_x = cursor + model.display_width * 0.5f;
      cursor += model.display_width + kModelGap;

      const world::GltfSceneImportResult imported = world::instantiateGltfSceneAsset(
          *world,
          *scene,
          *assets,
          *model.asset,
          world::GltfSceneInstantiateOptions{
              .create_synthetic_root = true,
              .autoplay_animations = false,
          });
      if (!imported.valid()) {
        spdlog::error("Failed to instantiate packaged static model '{}'",
                      model.desc.asset_key);
        continue;
      }

      world->remove<components::AnimatorComponent>(imported.root_entity);
      for (const world::Entity entity : imported.entities) {
        world->remove<components::DeformableMeshComponent>(entity);
      }

      const glm::vec3 source_center =
          (model.oriented_bounds.min + model.oriented_bounds.max) * 0.5f;
      const math::Vec3 position{
          center_x - source_center.x * model.display_scale,
          -model.oriented_bounds.min.y * model.display_scale,
          -source_center.z * model.display_scale,
      };
      world->setName(imported.root_entity, model.desc.display_name);
      auto& transform =
          world->get<components::TransformComponent>(imported.root_entity);
      transform.setPosition(position);
      transform.setRotation(model.display_rotation);
      transform.setScale({model.display_scale,
                          model.display_scale,
                          model.display_scale});

      world::updateWorldTransforms(*world, *scene);
      SceneBounds actual_bounds = computeWorldBounds(imported);
      if (actual_bounds.valid) {
        const glm::vec3 actual_center =
            (actual_bounds.min + actual_bounds.max) * 0.5f;
        transform.setPosition({
            position.x + center_x - actual_center.x,
            position.y - actual_bounds.min.y,
            position.z - actual_center.z,
        });
        world::updateWorldTransforms(*world, *scene);
        actual_bounds = computeWorldBounds(imported);
      }
      if (actual_bounds.valid) {
        expandBounds(scene_bounds, actual_bounds.min);
        expandBounds(scene_bounds, actual_bounds.max);
      }

      const helpers::GltfSceneAssetStats stats =
          helpers::summarizeGltfSceneAsset(*assets, *model.asset);
      spdlog::info(
          "Static model '{}': source='{}', nodes={}, primitives={}, triangles={}, source_size=({:.3f}, {:.3f}, {:.3f}), display_scale={:.5f}",
          model.desc.display_name,
          model.asset->source_path.string(),
          stats.node_count,
          stats.primitive_count,
          stats.triangle_count,
          model.oriented_bounds.max.x - model.oriented_bounds.min.x,
          model.oriented_bounds.max.y - model.oriented_bounds.min.y,
          model.oriented_bounds.max.z - model.oriented_bounds.min.z,
          model.display_scale);
    }
    return scene_bounds;
  }

  SceneBounds computeWorldBounds(
      const world::GltfSceneImportResult& imported) const {
    SceneBounds bounds{};
    for (const world::Entity entity : imported.entities) {
      if (!world->isAlive(entity) ||
          !world->has<components::MeshComponent>(entity) ||
          !world->has<components::TransformComponent>(entity)) {
        continue;
      }
      const auto& mesh_component =
          world->get<components::MeshComponent>(entity);
      const world::MeshData* mesh =
          assets->findMeshAsset(mesh_component.mesh_asset_key);
      if (mesh == nullptr) {
        continue;
      }
      const auto& transform =
          world->get<components::TransformComponent>(entity);
      for (const glm::vec3& vertex : mesh->vertices) {
        const math::Vec3 scaled = math::multiply(
            {vertex.x, vertex.y, vertex.z}, transform.worldScale());
        const math::Vec3 rotated =
            math::rotateVec(transform.worldRotation(), scaled);
        const math::Vec3 point =
            math::add(transform.worldPosition(), rotated);
        expandBounds(bounds, {point.x, point.y, point.z});
      }
    }
    return bounds;
  }

  void spawnGround(const SceneBounds& bounds) {
    const glm::vec3 center =
        bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(2.0f);
    const glm::vec3 ground_half_extents{
        std::max(3.5f, extents.x + 1.5f),
        0.04f,
        std::max(2.5f, extents.z + 1.5f),
    };
    constexpr const char* kGroundMesh = "examples/scene/gltf_import/ground_mesh";
    constexpr const char* kGroundMaterial =
        "examples/scene/gltf_import/ground_material";
    assets->registerMeshAsset(kGroundMesh, helpers::makeBoxMesh(ground_half_extents));
    rendering::MaterialDesc material{};
    material.base_color = {0.16f, 0.18f, 0.17f, 1.0f};
    material.roughness = 0.88f;
    assets->registerMaterialAsset(kGroundMaterial, material);
    helpers::spawnMesh(*world,
                       "Ground",
                       kGroundMesh,
                       kGroundMaterial,
                       {center.x,
                        bounds.valid ? bounds.min.y - ground_half_extents.y :
                                       -ground_half_extents.y,
                        center.z});
  }

  void spawnLighting(const SceneBounds& bounds) {
    const glm::vec3 center =
        bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(2.0f);
    const float radius = std::max(2.0f, glm::length(extents));
    helpers::spawnDirectionalLight(
        *world,
        "Sun",
        {center.x + radius, center.y + radius * 2.0f, center.z + radius},
        math::fromYawPitch(-0.55f, -0.78f),
        components::LightComponent{
            .type = components::LightComponent::Type::Directional,
            .color = {1.0f, 0.95f, 0.86f, 1.0f},
            .intensity = 1.45f,
            .casts_shadows = true,
            .shadow_extent = radius * 3.0f,
        });
    helpers::spawnPointLight(
        *world,
        "Fill Light",
        {center.x - radius * 1.8f, center.y + radius * 0.8f, center.z + radius},
        components::LightComponent{
            .type = components::LightComponent::Type::Point,
            .color = {0.58f, 0.70f, 1.0f, 1.0f},
            .intensity = 1.6f,
            .range = radius * 5.0f,
        });
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(2.0f, 2.0f, 2.0f);
    const float radius = std::max(1.0f, glm::length(extents));
    camera_move_speed_ = std::max(2.0f, radius * 1.5f);
    constexpr float kFovYRadians = glm::radians(60.0f);
    const float fit_distance = radius / std::sin(kFovYRadians * 0.5f);
    const float distance = std::max(8.0f, fit_distance * 1.15f);
    const glm::vec3 view_dir = glm::normalize(glm::vec3(0.0f, 0.18f, 1.0f));
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

  world::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  float camera_move_speed_ = 4.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::GltfSceneImportExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Static FBX and GLB Scene Import";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.environment_map_source_path =
      karma::demo::resolveExampleAssetPath("diligent_gltf_viewer/textures/papermill.ktx");
  config.environment_intensity = 0.8f;
  config.environment_draw_skybox = false;
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("scene/gltf_import"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
