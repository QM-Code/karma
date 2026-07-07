#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr const char* kUnitCubeMeshKey = "examples/rendering/selection_shaders/unit_cube";
constexpr const char* kSphereMeshKey = "examples/rendering/selection_shaders/sphere";
constexpr const char* kPlaneMeshKey = "examples/rendering/selection_shaders/plane";
constexpr const char* kPlaneMaterialKey = "examples/rendering/selection_shaders/material/plane";
constexpr const char* kOccluderMaterialKey = "examples/rendering/selection_shaders/material/occluder";
constexpr const char* kBlueMaterialKey = "examples/rendering/selection_shaders/material/blue";
constexpr const char* kGreenMaterialKey = "examples/rendering/selection_shaders/material/green";
constexpr const char* kRedMaterialKey = "examples/rendering/selection_shaders/material/red";
constexpr const char* kGoldMaterialKey = "examples/rendering/selection_shaders/material/gold";
constexpr const char* kSelectionFrameGraphKey =
    "examples/rendering/selection_shaders/graphs/selection_outline";
constexpr const char* kSelectionTag = "selected";

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct SelectableObject {
  world::Entity entity{};
  std::string name;
  math::Vec3 center{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

LookAngles lookAnglesToTarget(const math::Vec3& eye, const math::Vec3& target) {
  const math::Vec3 direction = math::normalize(math::subtract(target, eye));
  return {
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

void registerSurfaceMaterial(assets::AssetRegistry& assets,
                             const std::string& key,
                             const math::Color& color,
                             float roughness = 0.62f) {
  rendering::MaterialDesc material{};
  material.base_color = color;
  material.metallic = 0.0f;
  material.roughness = roughness;
  assets.registerMaterialAsset(key, material);
}

components::TransformComponent makeTransform(const math::Vec3& position,
                                             const math::Vec3& scale = {1.0f, 1.0f, 1.0f},
                                             const math::Quat& rotation = {}) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  transform.setRotation(rotation);
  transform.setScale(scale);
  return transform;
}

bool intersectRayAabb(const rendering::ScreenRay& ray,
                      const math::Vec3& center,
                      const math::Vec3& half_extents,
                      float& out_t) {
  float t_min = 0.0f;
  float t_max = std::numeric_limits<float>::max();

  const std::array<float, 3> origin{ray.origin.x, ray.origin.y, ray.origin.z};
  const std::array<float, 3> direction{ray.direction.x, ray.direction.y, ray.direction.z};
  const std::array<float, 3> min_bounds{center.x - half_extents.x,
                                        center.y - half_extents.y,
                                        center.z - half_extents.z};
  const std::array<float, 3> max_bounds{center.x + half_extents.x,
                                        center.y + half_extents.y,
                                        center.z + half_extents.z};

  for (int axis = 0; axis < 3; ++axis) {
    if (std::abs(direction[axis]) < 0.00001f) {
      if (origin[axis] < min_bounds[axis] || origin[axis] > max_bounds[axis]) {
        return false;
      }
      continue;
    }

    float t0 = (min_bounds[axis] - origin[axis]) / direction[axis];
    float t1 = (max_bounds[axis] - origin[axis]) / direction[axis];
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    t_min = std::max(t_min, t0);
    t_max = std::min(t_max, t1);
    if (t_min > t_max) {
      return false;
    }
  }

  out_t = t_min;
  return t_max >= 0.0f;
}

}  // namespace

class SelectionShadersExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_fast", platform::Key::LeftShift);
    input->bindMouse("cam_look", platform::MouseButton::Right);
    input->bindMouse("select_object", platform::MouseButton::Left, app::Trigger::Pressed);

    registerAssets();
    spawnScene();
    spawnCamera();
    setSelectedIndex(0);

    spdlog::info(
        "Selection shader controls: left click an object to select it, hold RMB to look, WASD to move, Q/E vertical, Left Shift to boost");
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    updateCamera(dt);
    if (input->actionPressed("select_object")) {
      pickObjectAtCursor();
    }
  }

  void onShutdown() override {}

 private:
  void registerAssets() {
    if (assets == nullptr) {
      return;
    }

    assets->registerMeshAsset(kUnitCubeMeshKey,
                              helpers::makeBoxMesh(glm::vec3{0.5f, 0.5f, 0.5f}));
    assets->registerMeshAsset(kSphereMeshKey,
                              world::createSphereMesh(world::SphereMeshDesc{
                                  .radius = 0.5f,
                                  .segments = 48u,
                                  .rings = 24u,
                              }));
    assets->registerMeshAsset(kPlaneMeshKey, world::createPlaneMesh(18.0f, 14.0f));

    registerSurfaceMaterial(*assets, kPlaneMaterialKey, {0.48f, 0.50f, 0.48f, 1.0f}, 0.8f);
    registerSurfaceMaterial(*assets, kOccluderMaterialKey, {0.22f, 0.24f, 0.27f, 1.0f}, 0.7f);
    registerSurfaceMaterial(*assets, kBlueMaterialKey, {0.22f, 0.42f, 0.98f, 1.0f}, 0.52f);
    registerSurfaceMaterial(*assets, kGreenMaterialKey, {0.16f, 0.65f, 0.38f, 1.0f}, 0.56f);
    registerSurfaceMaterial(*assets, kRedMaterialKey, {0.82f, 0.28f, 0.22f, 1.0f}, 0.54f);
    registerSurfaceMaterial(*assets, kGoldMaterialKey, {0.95f, 0.66f, 0.22f, 1.0f}, 0.48f);

  }

  world::Entity spawnMesh(std::string name,
                          const std::string& mesh_key,
                          const std::string& material_key,
                          const math::Vec3& position,
                          const math::Vec3& scale,
                          bool shadow_visible = true,
                          bool visible = true) {
    const world::Entity entity = world->createEntity();
    world->setName(entity, std::move(name));
    world->add(entity, makeTransform(position, scale));
    world->add(entity, components::MeshComponent{
                           .mesh_asset_key = mesh_key,
                           .materials = {components::MeshMaterialAssignment{
                               .slot = 0,
                               .material_key = material_key,
                           }},
                           .visible = visible,
                           .shadow_visible = shadow_visible,
                       });
    return entity;
  }

  void spawnSelectable(std::string name,
                       const std::string& mesh_key,
                       const std::string& material_key,
                       const math::Vec3& center,
                       const math::Vec3& half_extents) {
    const world::Entity entity =
        spawnMesh(name, mesh_key, material_key, center,
                  {half_extents.x * 2.0f, half_extents.y * 2.0f, half_extents.z * 2.0f});
    world->add(entity, components::RenderTagsComponent{});
    selectables_.push_back(SelectableObject{
        .entity = entity,
        .name = std::move(name),
        .center = center,
        .half_extents = half_extents,
    });
  }

  void spawnScene() {
    spawnMesh("Ground Plane",
              kPlaneMeshKey,
              kPlaneMaterialKey,
              {0.0f, 0.0f, 0.0f},
              {1.0f, 1.0f, 1.0f},
              false);

    spawnSelectable("Rear Cube - Silhouette Showcase",
                    kUnitCubeMeshKey,
                    kBlueMaterialKey,
                    {0.0f, 1.0f, -1.55f},
                    {1.0f, 1.0f, 1.0f});
    spawnMesh("Center Occluder",
              kUnitCubeMeshKey,
              kOccluderMaterialKey,
              {0.0f, 1.05f, 0.25f},
              {3.1f, 2.1f, 1.24f});
    spawnSelectable("Left Sphere",
                    kSphereMeshKey,
                    kGreenMaterialKey,
                    {-3.0f, 0.85f, -0.35f},
                    {0.85f, 0.85f, 0.85f});
    spawnSelectable("Right Cube",
                    kUnitCubeMeshKey,
                    kRedMaterialKey,
                    {3.0f, 0.75f, -0.2f},
                    {0.75f, 0.75f, 0.75f});
    spawnSelectable("Gold Tall Cube",
                    kUnitCubeMeshKey,
                    kGoldMaterialKey,
                    {2.0f, 1.25f, -2.65f},
                    {0.62f, 1.25f, 0.62f});

    helpers::spawnDirectionalLight(*world,
                                   "Sun",
                                   {0.0f, 10.0f, 0.0f},
                                   math::fromYawPitch(0.62f, -0.92f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 0.96f, 0.88f, 1.0f},
                                       .intensity = 1.05f,
                                       .casts_shadows = true,
                                       .shadow_extent = 22.0f,
                                   });
    helpers::spawnPointLight(*world,
                             "Cool Fill",
                             {-4.0f, 4.0f, 4.5f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.45f, 0.62f, 1.0f, 1.0f},
                                 .intensity = 18.0f,
                                 .range = 18.0f,
                             });
    helpers::spawnEnvironment(*world,
                              assets,
                              "Environment",
                              registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
                              0.35f,
                              true);
  }

  void spawnCamera() {
    const math::Vec3 eye{5.3f, 3.5f, 7.2f};
    const math::Vec3 target{0.0f, 1.0f, -0.65f};
    const LookAngles look = lookAnglesToTarget(eye, target);
    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          eye,
                                          math::fromYawPitch(look.yaw, look.pitch),
                                          components::CameraComponent{
                                              .render_shadows = true,
                                              .fov_y_degrees = 58.0f,
                                              .near_clip = 0.04f,
                                              .far_clip = 120.0f,
                                              .is_primary = true,
                                              .frame_graph_key = kSelectionFrameGraphKey,
                                          });
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;
  }

  void updateCamera(float dt) {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kMoveSpeed = 7.5f;
    constexpr float kBoostMultiplier = 3.0f;
    constexpr float kSmoothing = 20.0f;

    if (input->actionDown("cam_look")) {
      target_camera_yaw_ -= input->mouseDeltaX() * kLookSensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * kLookSensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.55f, 1.55f);

    const float alpha = 1.0f - std::exp(-kSmoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
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
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;
    if (input->actionDown("cam_up")) up_input += 1.0f;
    if (input->actionDown("cam_down")) up_input -= 1.0f;

    const float speed = kMoveSpeed * (input->actionDown("cam_fast") ? kBoostMultiplier : 1.0f);
    math::Vec3 movement = math::add(math::scale(forward, forward_input),
                                    math::scale(right, right_input));
    movement = math::add(movement, math::scale(world_up, up_input));
    if (math::lengthSquared(movement) > 0.0001f) {
      camera_transform.setPosition(
          math::add(camera_transform.getPosition(),
                    math::scale(math::normalize(movement), speed * dt)));
    }
    camera_transform.setRotation(camera_rotation);
  }

  void pickObjectAtCursor() {
    if (graphics == nullptr || !world->isAlive(camera_entity_)) {
      return;
    }

    double mouse_x = 0.0;
    double mouse_y = 0.0;
    if (!input->mousePosition(mouse_x, mouse_y)) {
      return;
    }

    int width = 0;
    int height = 0;
    graphics->getFramebufferSize(width, height);

    const auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const auto& camera = world->get<components::CameraComponent>(camera_entity_);
    rendering::ScreenRay ray{};
    if (!rendering::screenPointToWorldRay(mouse_x,
                                         mouse_y,
                                         width,
                                         height,
                                         camera_transform.getPosition(),
                                         camera_transform.getRotation(),
                                         camera.fov_y_degrees,
                                         ray)) {
      return;
    }

    int best_index = -1;
    float best_t = std::numeric_limits<float>::max();
    for (size_t i = 0; i < selectables_.size(); ++i) {
      float t = 0.0f;
      const SelectableObject& selectable = selectables_[i];
      if (intersectRayAabb(ray, selectable.center, selectable.half_extents, t) &&
          t < best_t) {
        best_t = t;
        best_index = static_cast<int>(i);
      }
    }

    setSelectedIndex(best_index);
  }

  void setSelectedIndex(int selected_index) {
    selected_index_ = selected_index;
    updateSelectionTags();
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(selectables_.size())) {
      spdlog::info("Selected '{}'", selectables_[static_cast<size_t>(selected_index_)].name);
    } else {
      spdlog::info("Selection cleared");
    }
  }

  void setSelectionTag(world::Entity entity, bool selected) {
    if (!world->isAlive(entity)) {
      return;
    }
    if (!world->has<components::RenderTagsComponent>(entity)) {
      world->add(entity, components::RenderTagsComponent{});
    }

    auto& tags = world->get<components::RenderTagsComponent>(entity).tags;
    const auto it = std::find(tags.begin(), tags.end(), kSelectionTag);
    if (selected) {
      if (it == tags.end()) {
        tags.emplace_back(kSelectionTag);
      }
    } else if (it != tags.end()) {
      tags.erase(it);
    }
  }

  void updateSelectionTags() {
    for (size_t i = 0; i < selectables_.size(); ++i) {
      setSelectionTag(selectables_[i].entity, static_cast<int>(i) == selected_index_);
    }
  }

  std::vector<SelectableObject> selectables_;
  world::Entity camera_entity_{};
  int selected_index_ = -1;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::SelectionShadersExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Selection Shader Example";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.forward_plus_max_local_lights = 256;
  config.local_light_distance_damping = 0.06f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.6f;
  config.lighting_exposure = 1.04f;
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("rendering/selection_shaders"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
