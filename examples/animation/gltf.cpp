#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/ui_imgui.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "karma/math.h"

namespace karma::demo {

namespace {

constexpr const char* kDefaultAnimationSceneKey = "examples/animation/dustbound_wayfarer";

struct SceneBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) {
    return {};
  }
  return LookAngles{
      .yaw = std::atan2(-direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

bool envFlagEnabled(const char* name) {
  if (const char* value = std::getenv(name)) {
    return value[0] != '\0' && std::string(value) != "0";
  }
  return false;
}

const char* rootMotionModeName(components::RootMotionMode mode) {
  switch (mode) {
    case components::RootMotionMode::Disabled:
      return "Disabled";
    case components::RootMotionMode::ExposeDelta:
      return "Expose Delta";
    case components::RootMotionMode::ApplyToLocalTransform:
      return "Apply To Local Transform";
  }
  return "Unknown";
}

components::RootMotionMode rootMotionModeFromIndex(int index) {
  switch (index) {
    case 1:
      return components::RootMotionMode::ExposeDelta;
    case 2:
      return components::RootMotionMode::ApplyToLocalTransform;
    default:
      return components::RootMotionMode::Disabled;
  }
}

int rootMotionModeIndex(components::RootMotionMode mode) {
  switch (mode) {
    case components::RootMotionMode::Disabled:
      return 0;
    case components::RootMotionMode::ExposeDelta:
      return 1;
    case components::RootMotionMode::ApplyToLocalTransform:
      return 2;
  }
  return 0;
}

}  // namespace

class GltfAnimationExample final : public app::GameInterface {
 public:
  GltfAnimationExample(std::string scene_asset_key, std::string display_name)
      : scene_asset_key_(std::move(scene_asset_key)),
        display_name_(std::move(display_name)) {}

  void onStart() override {
    input->bindKey("toggle_deformation_path", platform::Key::G, app::Trigger::Pressed);

    const assets::GltfSceneAsset* scene_asset =
        assets->findGltfSceneAsset(scene_asset_key_);
    if (scene_asset == nullptr) {
      spdlog::error("Missing packaged animation scene '{}'", scene_asset_key_);
      spawnCamera(SceneBounds{});
      return;
    }

    const helpers::GltfSceneAssetStats stats =
        helpers::summarizeGltfSceneAsset(*assets, *scene_asset);
    spdlog::info("Loaded animation scene '{}': {} nodes, {} meshes, {} triangles, {} clips, {} skeletons, {} skins",
                 scene_asset_key_,
                 stats.node_count,
                 scene_asset->mesh_asset_keys.size(),
                 stats.triangle_count,
                 scene_asset->animation_clip_keys.size(),
                 scene_asset->skeleton_keys.size(),
                 scene_asset->skin_keys.size());
    for (const std::string& clip_key : scene_asset->animation_clip_keys) {
      if (const world::AnimationClip* clip = assets->findAnimationClip(clip_key)) {
        spdlog::info("Animation clip '{}': {:.3f}s, {} transform channels, {} morph tracks",
                     clip->name,
                     clip->duration_seconds,
                     clip->channels.size(),
                     clip->morph_target_tracks.size());
      }
    }
    if (!scene_asset->skin_keys.empty()) {
      const world::Skin* skin = assets->findSkin(scene_asset->skin_keys.front());
      spdlog::info("Skin '{}': {} joints",
                   skin != nullptr ? skin->name : scene_asset->skin_keys.front(),
                   skin != nullptr ? skin->joint_node_indices.size() : 0u);
    }
    root_motion_node_index_ = defaultRootMotionNodeIndex(*scene_asset);
    root_motion_node_index_ui_ = root_motion_node_index_ == world::kInvalidAnimationIndex
                                     ? -1
                                     : static_cast<int>(root_motion_node_index_);

    const helpers::GltfSceneAssetBounds asset_bounds =
        helpers::computeGltfSceneAssetBounds(*assets, *scene_asset);
    bounds_ = SceneBounds{.min = asset_bounds.min,
                          .max = asset_bounds.max,
                          .valid = asset_bounds.valid};
    const world::GltfSceneImportResult imported = world::instantiateGltfSceneAsset(
        *world,
        *scene,
        *assets,
        *scene_asset,
        world::GltfSceneInstantiateOptions{
            .create_synthetic_root = false,
            .autoplay_animations = true,
        });
    if (!imported.valid()) {
      spdlog::error("Failed to instantiate animation scene '{}'", scene_asset_key_);
    } else {
      imported_root_ = imported.root_entity;
      imported_entities_ = imported.entities;
      use_gpu_deformation_ = !envFlagEnabled("KARMA_ANIMATION_CPU_DEFORMATION");
      setImportedDeformationPath(use_gpu_deformation_
                                     ? components::DeformationPath::Gpu
                                     : components::DeformationPath::CpuReference);
      if (world->has<components::AnimatorComponent>(imported_root_)) {
        auto& animator = world->get<components::AnimatorComponent>(imported_root_);
        animator.loop = true;
        animator.playing = true;
        animator.speed = playback_speed_;
        animator.root_motion_mode = root_motion_mode_;
        animator.root_motion_node_index = root_motion_node_index_;
        world->add(imported_root_, components::AnimationEventBufferComponent{});
        world->add(imported_root_, components::RootMotionComponent{
                                      .mode = root_motion_mode_,
                                      .root_motion_node_index = root_motion_node_index_,
                                  });
        clip_names_.clear();
        clip_names_.reserve(animator.clips.size());
        for (const world::AnimationClip& clip : animator.clips) {
          clip_names_.push_back(clip.name.empty() ? std::string("Animation") : clip.name);
        }
        if (!animator.clips.empty()) {
          components::setAnimatorClip(animator, 0, true);
          selected_clip_index_ = 0;
        }
      }
    }

    spawnGround(bounds_);
    spawnLights();
    spawnCamera(bounds_);
    spawnEnvironment();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    if (input != nullptr && input->actionPressed("toggle_deformation_path")) {
      use_gpu_deformation_ = !use_gpu_deformation_;
      setImportedDeformationPath(use_gpu_deformation_
                                     ? components::DeformationPath::Gpu
                                     : components::DeformationPath::CpuReference);
    }
    syncAnimatorSettings();
    updateAutoCycle(dt);
  }

  void onShutdown() override {}

  void drawUi(app::UIContext& ctx) {
    (void)ctx;
    components::AnimatorComponent* animator = liveAnimator();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 660.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("glTF Animation")) {
      ImGui::End();
      return;
    }

    ImGui::TextWrapped("%s", display_name_.c_str());
    if (animator == nullptr) {
      ImGui::TextUnformatted("Animator unavailable");
      ImGui::End();
      return;
    }

    drawPlaybackUi(*animator);
    drawClipListUi(*animator);
    drawTransitionUi(*animator);
    drawRuntimeSettingsUi(*animator);
    drawRuntimeStatsUi(*animator);

    ImGui::End();
  }

 private:
  uint32_t defaultRootMotionNodeIndex(const assets::GltfSceneAsset& scene_asset) const {
    for (const std::string& skeleton_key : scene_asset.skeleton_keys) {
      const world::Skeleton* skeleton = assets->findSkeleton(skeleton_key);
      if (skeleton != nullptr && !skeleton->root_joint_indices.empty()) {
        const uint32_t joint_index = skeleton->root_joint_indices.front();
        if (joint_index < skeleton->joints.size()) {
          return skeleton->joints[joint_index].node_index;
        }
      }
    }
    for (const std::string& skin_key : scene_asset.skin_keys) {
      const world::Skin* skin = assets->findSkin(skin_key);
      if (skin != nullptr && !skin->joint_node_indices.empty()) {
        return skin->joint_node_indices.front();
      }
    }
    return scene_asset.root_node;
  }

  components::AnimatorComponent* liveAnimator() {
    if (world == nullptr ||
        !world->isAlive(imported_root_) ||
        !world->has<components::AnimatorComponent>(imported_root_)) {
      return nullptr;
    }
    return &world->get<components::AnimatorComponent>(imported_root_);
  }

  components::RootMotionComponent* liveRootMotion() {
    if (world == nullptr ||
        !world->isAlive(imported_root_) ||
        !world->has<components::RootMotionComponent>(imported_root_)) {
      return nullptr;
    }
    return &world->get<components::RootMotionComponent>(imported_root_);
  }

  components::AnimationEventBufferComponent* liveEventBuffer() {
    if (world == nullptr ||
        !world->isAlive(imported_root_) ||
        !world->has<components::AnimationEventBufferComponent>(imported_root_)) {
      return nullptr;
    }
    return &world->get<components::AnimationEventBufferComponent>(imported_root_);
  }

  const char* clipLabel(size_t clip_index) const {
    if (clip_index < clip_names_.size() && !clip_names_[clip_index].empty()) {
      return clip_names_[clip_index].c_str();
    }
    return "Animation";
  }

  void playClip(size_t clip_index, bool reset_time) {
    components::AnimatorComponent* animator = liveAnimator();
    if (animator == nullptr || clip_index >= animator->clips.size()) {
      return;
    }
    components::setAnimatorClip(*animator, clip_index, reset_time, blend_duration_seconds_);
    components::playAnimator(*animator);
    selected_clip_index_ = clip_index;
    auto_cycle_elapsed_seconds_ = 0.0f;
  }

  void syncAnimatorSettings() {
    components::AnimatorComponent* animator = liveAnimator();
    if (animator == nullptr) {
      return;
    }
    animator->speed = playback_speed_;
    animator->loop = loop_;
    animator->root_motion_mode = root_motion_mode_;
    animator->root_motion_node_index = root_motion_node_index_;
    if (animator->current_clip_index < animator->clips.size()) {
      selected_clip_index_ = animator->current_clip_index;
    }
    if (components::RootMotionComponent* root_motion = liveRootMotion()) {
      root_motion->mode = root_motion_mode_;
      root_motion->root_motion_node_index = root_motion_node_index_;
    }
  }

  void updateAutoCycle(float dt) {
    if (!auto_cycle_) {
      auto_cycle_elapsed_seconds_ = 0.0f;
      return;
    }
    components::AnimatorComponent* animator = liveAnimator();
    if (animator == nullptr || animator->clips.size() < 2 || animator->blend_active) {
      return;
    }
    auto_cycle_elapsed_seconds_ += dt;
    const float interval = std::max(0.1f, auto_cycle_interval_seconds_);
    if (auto_cycle_elapsed_seconds_ < interval) {
      return;
    }
    const size_t next = (animator->current_clip_index + 1u) % animator->clips.size();
    playClip(next, true);
  }

  void drawPlaybackUi(components::AnimatorComponent& animator) {
    if (ImGui::Button(animator.playing ? "Pause" : "Play")) {
      animator.playing ? components::pauseAnimator(animator) : components::playAnimator(animator);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
      animator.time_seconds = 0.0f;
      animator.blend_active = false;
      components::playAnimator(animator);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
      components::stopAnimator(animator);
    }

    ImGui::Checkbox("Loop", &loop_);
    ImGui::SliderFloat("Speed", &playback_speed_, 0.05f, 3.0f, "%.2f");

    if (animator.current_clip_index < animator.clips.size()) {
      const auto& clip = animator.clips[animator.current_clip_index];
      const float duration = std::max(clip.duration_seconds, 0.001f);
      float time = std::clamp(animator.time_seconds, 0.0f, duration);
      if (ImGui::SliderFloat("Time", &time, 0.0f, duration, "%.3f s")) {
        animator.time_seconds = time;
        animator.blend_active = false;
      }
      ImGui::ProgressBar(std::clamp(time / duration, 0.0f, 1.0f),
                         ImVec2(-1.0f, 0.0f),
                         clipLabel(animator.current_clip_index));
    }
  }

  void drawClipListUi(components::AnimatorComponent& animator) {
    ImGui::Separator();
    ImGui::Text("Clips: %d", static_cast<int>(animator.clips.size()));
    if (ImGui::BeginListBox("##clips", ImVec2(-1.0f, 220.0f))) {
      for (size_t i = 0; i < animator.clips.size(); ++i) {
        const bool selected = i == selected_clip_index_;
        std::string label = std::to_string(i) + "  " + clipLabel(i);
        if (i < animator.clips.size()) {
          label += "  ";
          label += std::to_string(animator.clips[i].duration_seconds);
          label += "s";
        }
        if (ImGui::Selectable(label.c_str(), selected)) {
          playClip(i, true);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndListBox();
    }
    if (ImGui::Button("Previous") && !animator.clips.empty()) {
      const size_t current = animator.current_clip_index < animator.clips.size()
                                 ? animator.current_clip_index
                                 : 0u;
      const size_t previous = current == 0u ? animator.clips.size() - 1u : current - 1u;
      playClip(previous, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next") && !animator.clips.empty()) {
      const size_t current = animator.current_clip_index < animator.clips.size()
                                 ? animator.current_clip_index
                                 : 0u;
      playClip((current + 1u) % animator.clips.size(), true);
    }
  }

  void drawTransitionUi(components::AnimatorComponent& animator) {
    ImGui::Separator();
    ImGui::SliderFloat("Crossfade", &blend_duration_seconds_, 0.0f, 2.0f, "%.2f s");
    ImGui::Checkbox("Auto Cycle", &auto_cycle_);
    ImGui::SliderFloat("Cycle Interval", &auto_cycle_interval_seconds_, 0.25f, 10.0f, "%.2f s");
    if (animator.blend_active && animator.blend_duration_seconds > 0.0f) {
      const float t = std::clamp(animator.blend_elapsed_seconds /
                                     animator.blend_duration_seconds,
                                 0.0f,
                                 1.0f);
      ImGui::ProgressBar(t, ImVec2(-1.0f, 0.0f), "Transition");
      ImGui::Text("From: %s", clipLabel(animator.blend_from_clip_index));
      ImGui::Text("To: %s", clipLabel(animator.current_clip_index));
    } else {
      ImGui::TextUnformatted("Transition: idle");
    }
  }

  void drawRuntimeSettingsUi(components::AnimatorComponent& animator) {
    ImGui::Separator();
    bool gpu = use_gpu_deformation_;
    if (ImGui::Checkbox("GPU Deformation", &gpu)) {
      use_gpu_deformation_ = gpu;
      setImportedDeformationPath(use_gpu_deformation_
                                     ? components::DeformationPath::Gpu
                                     : components::DeformationPath::CpuReference);
    }

    int mode_index = rootMotionModeIndex(root_motion_mode_);
    const char* modes[] = {"Disabled", "Expose Delta", "Apply To Local Transform"};
    if (ImGui::Combo("Root Motion", &mode_index, modes, IM_ARRAYSIZE(modes))) {
      root_motion_mode_ = rootMotionModeFromIndex(mode_index);
      animator.root_motion_mode = root_motion_mode_;
      if (components::RootMotionComponent* root_motion = liveRootMotion()) {
        root_motion->mode = root_motion_mode_;
      }
    }
    if (ImGui::InputInt("Root Node", &root_motion_node_index_ui_)) {
      root_motion_node_index_ = root_motion_node_index_ui_ < 0
                                    ? world::kInvalidAnimationIndex
                                    : static_cast<uint32_t>(root_motion_node_index_ui_);
      animator.root_motion_node_index = root_motion_node_index_;
      if (components::RootMotionComponent* root_motion = liveRootMotion()) {
        root_motion->root_motion_node_index = root_motion_node_index_;
      }
    }
  }

  void drawRuntimeStatsUi(components::AnimatorComponent& animator) {
    ImGui::Separator();
    ImGui::Text("Current: %s", clipLabel(animator.current_clip_index));
    ImGui::Text("Playing: %s", animator.playing ? "yes" : "no");
    ImGui::Text("Root Motion: %s", rootMotionModeName(root_motion_mode_));
    if (const components::RootMotionComponent* root_motion = liveRootMotion()) {
      const auto& delta = root_motion->delta;
      if (delta.position) {
        ImGui::Text("Root Delta: %.3f %.3f %.3f",
                    delta.position->x,
                    delta.position->y,
                    delta.position->z);
      } else {
        ImGui::TextUnformatted("Root Delta: none");
      }
    }
    if (const components::AnimationEventBufferComponent* events = liveEventBuffer()) {
      ImGui::Text("Events: %d", static_cast<int>(events->events.size()));
      for (const components::AnimatorEventRecord& event : events->events) {
        ImGui::BulletText("%s", event.name.c_str());
      }
    }
    if (graphics != nullptr) {
      const rendering::DeformationStats stats = graphics->getDeformationStats();
      ImGui::Text("Deformations: %u", stats.resource_count);
      ImGui::Text("Joints: %u  Morph Weights: %u",
                  stats.joint_matrix_count,
                  stats.morph_weight_count);
    }
  }

  void spawnGround(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.0f, 0.0f);
    const float radius = bounds.valid ? std::max(glm::length((bounds.max - bounds.min) * 0.5f), 1.0f)
                                      : 1.5f;
    const float floor_y = bounds.valid ? bounds.min.y - 0.015f : -0.015f;
    const glm::vec3 half_extents{std::max(2.4f, radius * 2.2f), 0.015f,
                                 std::max(2.4f, radius * 2.2f)};

    const std::string mesh_key = "runtime/gltf_animation/ground/mesh";
    const std::string material_key = "runtime/gltf_animation/ground/material";
    if (graphics != nullptr) {
      assets->registerMeshAsset(mesh_key, helpers::makeBoxMesh(half_extents));
    }
    if (assets != nullptr) {
      rendering::MaterialDesc material;
      material.base_color = math::Color{0.16f, 0.18f, 0.18f, 1.0f};
      material.roughness = 0.82f;
      material.metallic = 0.0f;
      assets->registerMaterialAsset(material_key, material);
    }
    helpers::spawnMesh(*world,
                       "Animation Model Ground",
                       mesh_key,
                       material_key,
                       {center.x, floor_y, center.z},
                       true);
  }

  void spawnLights() {
    const auto sun = world->createEntity();
    world->setName(sun, "Key Light");
    components::TransformComponent sun_transform{};
    sun_transform.setRotation(math::fromYawPitch(-0.55f, -0.78f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 0.96f, 0.9f, 1.0f},
                        .intensity = 1.35f,
                        .casts_shadows = true,
                        .shadow_extent = 6.0f});

    const auto fill = world->createEntity();
    world->setName(fill, "Fill Light");
    components::TransformComponent fill_transform{{-1.8f, 1.7f, 2.1f}};
    world->add(fill, fill_transform);
    world->add(fill, components::LightComponent{
                         .type = components::LightComponent::Type::Point,
                         .color = {0.55f, 0.7f, 1.0f, 1.0f},
                         .intensity = 1.8f,
                         .range = 4.0f,
                         .casts_shadows = false});
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.7f, 0.0f);
    const glm::vec3 extents = bounds.valid ? (bounds.max - bounds.min) * 0.5f
                                           : glm::vec3(0.8f, 0.8f, 0.8f);
    const float radius = std::max(1.0f, glm::length(extents));
    const float camera_distance = std::max(2.2f, radius * 2.9f);
    const glm::vec3 target = center;
    const glm::vec3 view_direction = glm::normalize(glm::vec3{0.75f, 0.34f, 1.0f});

    const glm::vec3 eye = target + view_direction * camera_distance;
    const LookAngles look = lookAnglesToTarget(eye, target);

    const auto camera_entity = world->createEntity();
    world->setName(camera_entity, "Camera");
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({eye.x, eye.y, eye.z});
    camera_transform.setRotation(math::fromYawPitch(look.yaw, look.pitch));
    world->add(camera_entity, camera_transform);
    components::CameraComponent camera{};
    camera.render_shadows = true;
    camera.fov_y_degrees = 50.0f;
    camera.near_clip = 0.04f;
    camera.far_clip = std::max(60.0f, camera_distance + radius * 8.0f);
    camera.is_primary = true;
    world->add(camera_entity, camera);
  }

  void spawnEnvironment() {
    helpers::spawnEnvironment(*world,
                              assets,
                              "Environment",
                              registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
                              0.28f,
                              false);
  }

  void setImportedDeformationPath(components::DeformationPath path) {
    for (const world::Entity entity : imported_entities_) {
      if (!world->isAlive(entity) || !world->has<components::DeformableMeshComponent>(entity)) {
        continue;
      }
      auto& deformation = world->get<components::DeformableMeshComponent>(entity);
      deformation.path = path;
      deformation.palette_valid = false;
      deformation.morph_weights_dirty = true;
      deformation.diagnostic = path == components::DeformationPath::Gpu
                                   ? "GPU deformation path"
                                   : "CPU reference deformation path";
    }
    spdlog::info("Animation model deformation path: {}",
                 path == components::DeformationPath::Gpu ? "GPU" : "CPU reference");
  }

  std::string scene_asset_key_;
  std::string display_name_;
  world::Entity imported_root_{};
  std::vector<world::Entity> imported_entities_;
  std::vector<std::string> clip_names_;
  SceneBounds bounds_{};
  size_t selected_clip_index_ = 0;
  float playback_speed_ = 1.0f;
  float blend_duration_seconds_ = 0.35f;
  float auto_cycle_interval_seconds_ = 3.0f;
  float auto_cycle_elapsed_seconds_ = 0.0f;
  components::RootMotionMode root_motion_mode_ = components::RootMotionMode::Disabled;
  uint32_t root_motion_node_index_ = world::kInvalidAnimationIndex;
  int root_motion_node_index_ui_ = -1;
  bool use_gpu_deformation_ = true;
  bool loop_ = true;
  bool auto_cycle_ = false;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  std::filesystem::path package_path =
      karma::demo::resolveExampleAssetPath("animation/dustbound_wayfarer");
  std::string scene_asset_key = karma::demo::kDefaultAnimationSceneKey;
  std::string display_name = "dustbound_wayfarer_merged_animations.glb";
  if (argc > 1 && argv[1] != nullptr && std::string_view(argv[1]).size() > 0u) {
    package_path = karma::demo::resolveExamplePath(argv[1]);
    display_name = package_path.filename().string();
    if (argc > 2 && argv[2] != nullptr && std::string_view(argv[2]).size() > 0u) {
      scene_asset_key = argv[2];
    } else {
      spdlog::warn("Custom animation package '{}' provided without a scene asset key; using '{}'",
                   package_path.string(),
                   scene_asset_key);
    }
  }

  karma::app::EngineApp engine;
  karma::demo::GltfAnimationExample game(scene_asset_key, display_name);
  engine.setUi(karma::ui::imgui::createUiLayer(
      [&game](karma::app::UIContext& ctx) { game.drawUi(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Karma glTF Animation Showcase";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.startup_asset_packages.push_back(package_path);

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
