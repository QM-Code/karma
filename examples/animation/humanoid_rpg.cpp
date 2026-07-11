#include "demo_asset_paths.h"
#include "scene_helpers.h"

#include "karma/karma.h"
#include "karma/ui_imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr const char* kModelPackage =
    "humanoid_rpg/tripo_human_character/assets.package.json";
constexpr const char* kAnimationPackage =
    "humanoid_rpg/character_animations/assets.package.json";
constexpr const char* kModelKey = "pf2e/models/tripo_human_character";

struct ClipSpec {
  const char* clip_key = "";
  const char* rig_key = "";
  const char* name = "";
};

constexpr std::array<ClipSpec, 9> kClips{{
    {"pf2e/animations/character/idle", "pf2e/animations/character/idle/rig", "Idle"},
    {"pf2e/animations/character/stride", "pf2e/animations/character/stride/rig", "Stride"},
    {"pf2e/animations/character/crawl", "pf2e/animations/character/crawl/rig", "Crawl"},
    {"pf2e/animations/character/idle_prone", "pf2e/animations/character/idle_prone/rig", "ProneIdle"},
    {"pf2e/animations/character/jump_start", "pf2e/animations/character/jump_start/rig", "JumpStart"},
    {"pf2e/animations/character/jump_end", "pf2e/animations/character/jump_end/rig", "JumpEnd"},
    {"pf2e/animations/character/fall", "pf2e/animations/character/fall/rig", "Fall"},
    {"pf2e/animations/character/stand", "pf2e/animations/character/stand/rig", "Stand"},
    {"pf2e/animations/character/melee_attack", "pf2e/animations/character/melee_attack/rig", "MeleeAttack"},
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

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

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

std::string formatBounds(const SceneBounds& bounds) {
  if (!bounds.valid) {
    return "none";
  }
  const glm::vec3 size = bounds.max - bounds.min;
  char buffer[192];
  std::snprintf(buffer,
                sizeof(buffer),
                "min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f) size(%.3f, %.3f, %.3f)",
                bounds.min.x,
                bounds.min.y,
                bounds.min.z,
                bounds.max.x,
                bounds.max.y,
                bounds.max.z,
                size.x,
                size.y,
                size.z);
  return buffer;
}

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  if (!std::isfinite(direction.x) ||
      !std::isfinite(direction.y) ||
      !std::isfinite(direction.z)) {
    return {};
  }
  return LookAngles{
      .yaw = std::atan2(-direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

std::vector<world::Skeleton> collectSkeletons(const assets::AssetRegistry& assets,
                                              const assets::GltfSceneAsset& model) {
  std::vector<world::Skeleton> skeletons;
  skeletons.reserve(model.skeleton_keys.size());
  for (const std::string& key : model.skeleton_keys) {
    if (const world::Skeleton* skeleton = assets.findSkeleton(key)) {
      skeletons.push_back(*skeleton);
    }
  }
  return skeletons;
}

std::vector<world::Skin> collectSkins(const assets::AssetRegistry& assets,
                                      const assets::GltfSceneAsset& model) {
  std::vector<world::Skin> skins;
  skins.reserve(model.skin_keys.size());
  for (const std::string& key : model.skin_keys) {
    if (const world::Skin* skin = assets.findSkin(key)) {
      skins.push_back(*skin);
    }
  }
  return skins;
}

std::vector<world::HumanoidRig> collectHumanoidRigs(
    const assets::AssetRegistry& assets,
    const assets::GltfSceneAsset& model) {
  std::vector<world::HumanoidRig> rigs;
  rigs.reserve(model.humanoid_rig_keys.size());
  for (const std::string& key : model.humanoid_rig_keys) {
    if (const world::HumanoidRig* rig = assets.findHumanoidRig(key)) {
      rigs.push_back(*rig);
    }
  }
  return rigs;
}

}  // namespace

class RpgHumanoidAnimationExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("toggle_play", platform::Key::Space, app::Trigger::Pressed);
    input->bindKey("previous_clip", platform::Key::P, app::Trigger::Pressed);
    input->bindKey("next_clip", platform::Key::N, app::Trigger::Pressed);
    input->bindKey("toggle_deformation", platform::Key::G, app::Trigger::Pressed);

    importPackages();
    const assets::GltfSceneAsset* model = assets->findGltfSceneAsset(kModelKey);
    if (model == nullptr) {
      spdlog::error("Humanoid RPG example missing model asset '{}'", kModelKey);
      spawnLights();
      spawnCamera(SceneBounds{});
      spawnEnvironment();
      return;
    }

    const helpers::GltfSceneAssetBounds model_bounds =
        helpers::computeGltfSceneAssetBounds(*assets, *model);
    bounds_ = SceneBounds{
        .min = model_bounds.min,
        .max = model_bounds.max,
        .valid = model_bounds.valid,
    };
    spdlog::info("RPG humanoid asset bounds: {}", formatBounds(bounds_));

    const helpers::GltfSceneAssetStats stats =
        helpers::summarizeGltfSceneAsset(*assets, *model);
    spdlog::info("RPG humanoid model: {} nodes, {} meshes, {} triangles, {} skeletons, {} skins, {} humanoid rigs",
                 model->nodes.size(),
                 model->mesh_asset_keys.size(),
                 stats.triangle_count,
                 model->skeleton_keys.size(),
                 model->skin_keys.size(),
                 model->humanoid_rig_keys.size());
    imported_ = world::instantiateGltfSceneAsset(
        *world,
        *scene,
        *assets,
        *model,
        world::GltfSceneInstantiateOptions{
            .create_synthetic_root = true,
            .autoplay_animations = false,
        });
    if (!imported_.valid()) {
      spdlog::error("Failed to instantiate RPG humanoid model");
      spawnLights();
      spawnCamera(bounds_);
      spawnEnvironment();
      return;
    }
    imported_root_ = imported_.root_entity;
    imported_entities_ = imported_.entities;

    attachRetargetedAnimator(*model);
    setImportedDeformationPath(components::DeformationPath::Gpu);
    spawnGround(bounds_);
    spawnLights();
    spawnCamera(bounds_);
    spawnEnvironment();
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    (void)dt;
    components::AnimatorComponent* animator = liveAnimator();
    if (animator == nullptr) {
      return;
    }

    if (input->actionPressed("toggle_play")) {
      animator->playing ? components::pauseAnimator(*animator)
                        : components::playAnimator(*animator);
    }
    if (input->actionPressed("previous_clip") && !animator->clips.empty()) {
      const size_t current =
          animator->current_clip_index < animator->clips.size()
              ? animator->current_clip_index
              : 0u;
      playClip(current == 0u ? animator->clips.size() - 1u : current - 1u, true);
    }
    if (input->actionPressed("next_clip") && !animator->clips.empty()) {
      const size_t current =
          animator->current_clip_index < animator->clips.size()
              ? animator->current_clip_index
              : 0u;
      playClip((current + 1u) % animator->clips.size(), true);
    }
    if (input->actionPressed("toggle_deformation")) {
      use_gpu_deformation_ = !use_gpu_deformation_;
      setImportedDeformationPath(use_gpu_deformation_
                                     ? components::DeformationPath::Gpu
                                     : components::DeformationPath::CpuReference);
    }
    animator->speed = playback_speed_;
    animator->loop = loop_;
    if (animator->current_clip_index < animator->clips.size()) {
      selected_clip_index_ = animator->current_clip_index;
    }
    live_skinned_bounds_ = computeCurrentSkinnedBounds();
    if (!logged_live_bounds_ && live_skinned_bounds_.valid) {
      logged_live_bounds_ = true;
      spdlog::info("RPG humanoid live skinned bounds: {}",
                   formatBounds(live_skinned_bounds_));
    }
  }

  void onShutdown() override {}

  void drawUi(app::UIContext& ctx) {
    (void)ctx;
    components::AnimatorComponent* animator = liveAnimator();

    ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("RPG Humanoid FBX")) {
      ImGui::End();
      return;
    }

    ImGui::TextUnformatted("Mixamo character FBX + retargeted standalone clips");
    ImGui::Text("Model package: %s", kModelPackage);
    ImGui::Text("Animation package: %s", kAnimationPackage);
    ImGui::Separator();

    if (animator == nullptr) {
      ImGui::TextUnformatted("Animator unavailable");
      ImGui::TextWrapped("%s", startup_diagnostic_.c_str());
      ImGui::End();
      return;
    }

    drawPlaybackUi(*animator);
    drawClipUi(*animator);
    drawDiagnosticsUi(*animator);

    ImGui::End();
  }

 private:
  void importPackages() {
    const std::filesystem::path model_package = resolveExampleAssetPath(kModelPackage);
    const std::filesystem::path animation_package = resolveExampleAssetPath(kAnimationPackage);
    std::string diagnostic;
    if (auto handle = assets::importAssetPackage(*assets, model_package, &diagnostic)) {
      package_handles_.push_back(*handle);
      spdlog::info("Imported RPG humanoid model package from {}", model_package.string());
    } else {
      startup_diagnostic_ += "Model package failed: " + diagnostic + "\n";
      spdlog::error("Failed to import RPG humanoid model package '{}': {}",
                    model_package.string(),
                    diagnostic);
    }
    diagnostic.clear();
    if (auto handle = assets::importAssetPackage(*assets, animation_package, &diagnostic)) {
      package_handles_.push_back(*handle);
      spdlog::info("Imported RPG humanoid animation package from {}",
                   animation_package.string());
    } else {
      startup_diagnostic_ += "Animation package failed: " + diagnostic + "\n";
      spdlog::error("Failed to import RPG humanoid animation package '{}': {}",
                    animation_package.string(),
                    diagnostic);
    }
  }

  void attachRetargetedAnimator(const assets::GltfSceneAsset& model) {
    const std::vector<world::HumanoidRig> target_rigs = collectHumanoidRigs(*assets, model);
    if (target_rigs.empty()) {
      startup_diagnostic_ += "Target humanoid rig missing\n";
      spdlog::error("RPG humanoid model has no registered humanoid rig");
      return;
    }
    target_rig_binding_count_ = target_rigs.front().bindings.size();
    const float target_height = world::humanoidRigHeight(target_rigs.front());
    spdlog::info("Target humanoid rig: height {:.4f}, bindings {}, joints {}",
                 target_height,
                 target_rigs.front().bindings.size(),
                 target_rigs.front().skeleton.joints.size());

    std::vector<world::AnimationClip> clips;
    clips.reserve(kClips.size());
    clip_names_.clear();
    clip_diagnostics_.clear();
    size_t initial_clip = 0u;

    for (const ClipSpec& spec : kClips) {
      const world::AnimationClip* source_clip = assets->findAnimationClip(spec.clip_key);
      const world::HumanoidRig* source_rig = assets->findHumanoidRig(spec.rig_key);
      if (source_clip == nullptr || source_rig == nullptr) {
        clip_diagnostics_.push_back(std::string(spec.name) + ": missing source clip or rig");
        spdlog::warn("Humanoid clip '{}' missing source clip or rig", spec.name);
        continue;
      }

      world::HumanoidRetargetDiagnostic diagnostic;
      world::AnimationClip clip = world::retargetHumanoidClip(*source_clip,
                                                             *source_rig,
                                                             target_rigs.front(),
                                                             {},
                                                             &diagnostic);
      clip.name = spec.name;
      const size_t channel_count = clip.channels.size();
      const size_t skipped = diagnostic.channels_skipped.size();
      if (!diagnostic.valid() || clip.channels.empty()) {
        const std::string failure =
            std::string(spec.name) + ": invalid humanoid mapping or empty clip";
        clip_diagnostics_.push_back(failure);
        spdlog::warn("{}", failure);
        continue;
      }

      if (std::string_view(spec.name) == "Stride") {
        initial_clip = clips.size();
      }
      std::string summary = clip.name + ": " +
                            std::to_string(channel_count) + " channels, " +
                            std::to_string(skipped) + " skipped";
      clip_diagnostics_.push_back(summary);
      clip_names_.push_back(clip.name);
      clips.push_back(std::move(clip));

      spdlog::info("Retargeted {} from '{}': {} channels, {} skipped",
                   spec.name,
                   spec.clip_key,
                   channel_count,
                   skipped);
      const float source_height = world::humanoidRigHeight(*source_rig);
      const float root_scale =
          source_height > 0.0001f ? target_height / source_height : 1.0f;
      spdlog::info("  source height {:.4f}, target height {:.4f}, root scale {:.4f}",
                   source_height,
                   target_height,
                   root_scale);
    }

    if (clips.empty()) {
      startup_diagnostic_ += "No retargeted clips were produced\n";
      return;
    }

    initial_clip = std::min(initial_clip, clips.size() - 1u);
    components::AnimatorComponent animator{
        .clips = std::move(clips),
        .node_entities_by_index = imported_.node_entities_by_index,
        .morph_entities_by_node_index = imported_.morph_entities_by_node_index,
        .skeletons = collectSkeletons(*assets, model),
        .skins = collectSkins(*assets, model),
        .humanoid_rigs = target_rigs,
        .current_clip_index = initial_clip,
        .time_seconds = 0.0f,
        .speed = playback_speed_,
        .loop = true,
        .playing = true,
    };
    if (world->has<components::AnimatorComponent>(imported_root_)) {
      world->get<components::AnimatorComponent>(imported_root_) = std::move(animator);
    } else {
      world->add(imported_root_, std::move(animator));
    }
    world->add(imported_root_, components::AnimationEventBufferComponent{});
    selected_clip_index_ = initial_clip;
    spdlog::info("Attached {} RPG humanoid clips to example animator", clip_names_.size());
  }

  components::AnimatorComponent* liveAnimator() {
    if (world == nullptr ||
        !world->isAlive(imported_root_) ||
        !world->has<components::AnimatorComponent>(imported_root_)) {
      return nullptr;
    }
    return &world->get<components::AnimatorComponent>(imported_root_);
  }

  const char* clipLabel(size_t index) const {
    if (index < clip_names_.size() && !clip_names_[index].empty()) {
      return clip_names_[index].c_str();
    }
    return "Clip";
  }

  void playClip(size_t index, bool reset_time) {
    components::AnimatorComponent* animator = liveAnimator();
    if (animator == nullptr || index >= animator->clips.size()) {
      return;
    }
    components::setAnimatorClip(*animator, index, reset_time, blend_seconds_);
    components::playAnimator(*animator);
    selected_clip_index_ = index;
  }

  void drawPlaybackUi(components::AnimatorComponent& animator) {
    if (ImGui::Button(animator.playing ? "Pause" : "Play")) {
      animator.playing ? components::pauseAnimator(animator)
                       : components::playAnimator(animator);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
      animator.time_seconds = 0.0f;
      animator.blend_active = false;
      components::playAnimator(animator);
    }
    ImGui::SameLine();
    if (ImGui::Button("Previous") && !animator.clips.empty()) {
      const size_t current =
          animator.current_clip_index < animator.clips.size()
              ? animator.current_clip_index
              : 0u;
      playClip(current == 0u ? animator.clips.size() - 1u : current - 1u, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next") && !animator.clips.empty()) {
      const size_t current =
          animator.current_clip_index < animator.clips.size()
              ? animator.current_clip_index
              : 0u;
      playClip((current + 1u) % animator.clips.size(), true);
    }

    ImGui::Checkbox("Loop", &loop_);
    ImGui::SliderFloat("Speed", &playback_speed_, 0.05f, 3.0f, "%.2f");
    ImGui::SliderFloat("Crossfade", &blend_seconds_, 0.0f, 1.5f, "%.2f s");
    if (ImGui::Checkbox("GPU Deformation", &use_gpu_deformation_)) {
      setImportedDeformationPath(use_gpu_deformation_
                                     ? components::DeformationPath::Gpu
                                     : components::DeformationPath::CpuReference);
    }

    if (animator.current_clip_index < animator.clips.size()) {
      const world::AnimationClip& clip = animator.clips[animator.current_clip_index];
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

  void drawClipUi(components::AnimatorComponent& animator) {
    ImGui::Separator();
    ImGui::Text("Retargeted Clips: %d", static_cast<int>(animator.clips.size()));
    if (ImGui::BeginListBox("##clips", ImVec2(-1.0f, 210.0f))) {
      for (size_t index = 0; index < animator.clips.size(); ++index) {
        std::string label = std::to_string(index) + "  " + clipLabel(index);
        label += "  ";
        label += std::to_string(animator.clips[index].channels.size());
        label += " ch";
        if (ImGui::Selectable(label.c_str(), selected_clip_index_ == index)) {
          playClip(index, true);
        }
      }
      ImGui::EndListBox();
    }
  }

  void drawDiagnosticsUi(components::AnimatorComponent& animator) {
    ImGui::Separator();
    ImGui::Text("Current: %s", clipLabel(animator.current_clip_index));
    ImGui::Text("Playing: %s", animator.playing ? "yes" : "no");
    ImGui::Text("Target rig bindings: %d", static_cast<int>(target_rig_binding_count_));
    ImGui::Text("Animator skeletons: %d  skins: %d",
                static_cast<int>(animator.skeletons.size()),
                static_cast<int>(animator.skins.size()));
    ImGui::TextWrapped("Asset bounds: %s", formatBounds(bounds_).c_str());
    live_skinned_bounds_ = computeCurrentSkinnedBounds();
    ImGui::TextWrapped("Live skinned bounds: %s",
                       formatBounds(live_skinned_bounds_).c_str());

    int deformable_count = 0;
    int palette_valid_count = 0;
    std::string first_deformation_diagnostic;
    for (const world::Entity entity : imported_entities_) {
      if (!world->isAlive(entity) ||
          !world->has<components::DeformableMeshComponent>(entity)) {
        continue;
      }
      const auto& deformation = world->get<components::DeformableMeshComponent>(entity);
      ++deformable_count;
      if (deformation.palette_valid) {
        ++palette_valid_count;
      }
      if (first_deformation_diagnostic.empty() && !deformation.diagnostic.empty()) {
        first_deformation_diagnostic = deformation.diagnostic;
      }
    }
    ImGui::Text("Deformable meshes: %d  valid palettes: %d",
                deformable_count,
                palette_valid_count);
    if (!first_deformation_diagnostic.empty()) {
      ImGui::TextWrapped("Deformation: %s", first_deformation_diagnostic.c_str());
    }
    if (graphics != nullptr) {
      const rendering::DeformationStats stats = graphics->getDeformationStats();
      ImGui::Text("Renderer deformations: %u  joint matrices: %u",
                  stats.resource_count,
                  stats.joint_matrix_count);
    }
    ImGui::Separator();
    for (const std::string& line : clip_diagnostics_) {
      ImGui::BulletText("%s", line.c_str());
    }
  }

  SceneBounds computeCurrentSkinnedBounds() const {
    SceneBounds bounds{};
    if (world == nullptr) {
      return bounds;
    }
    for (const world::Entity entity : imported_entities_) {
      if (!world->isAlive(entity) ||
          !world->has<components::DeformableMeshComponent>(entity)) {
        continue;
      }
      const auto& deformation = world->get<components::DeformableMeshComponent>(entity);
      if (!deformation.palette_valid || deformation.joint_palette.empty()) {
        continue;
      }
      const world::MeshData skinned =
          world::skinMesh(deformation.bind_mesh,
                          deformation.vertex_influences,
                          deformation.joint_palette);
      for (const glm::vec3& vertex : skinned.vertices) {
        expandBounds(bounds, vertex);
      }
    }
    return bounds;
  }

  void spawnGround(const SceneBounds& bounds) {
    const glm::vec3 center =
        bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(0.75f);
    const float radius = std::max(1.0f, glm::length(extents));
    const float floor_y = bounds.valid ? bounds.min.y - 0.02f : -0.02f;
    const std::string mesh_key = "runtime/rpg_humanoid/ground/mesh";
    const std::string material_key = "runtime/rpg_humanoid/ground/material";
    assets->registerMeshAsset(mesh_key,
                              helpers::makeBoxMesh({radius * 2.4f, 0.02f, radius * 2.4f}));
    rendering::MaterialDesc material;
    material.base_color = {0.14f, 0.16f, 0.16f, 1.0f};
    material.roughness = 0.86f;
    assets->registerMaterialAsset(material_key, material);
    helpers::spawnMesh(*world,
                       "Ground",
                       mesh_key,
                       material_key,
                       {center.x, floor_y, center.z},
                       true);
  }

  void spawnLights() {
    helpers::spawnDirectionalLight(
        *world,
        "Key Light",
        {0.0f, 4.0f, 0.0f},
        math::fromYawPitch(-0.55f, -0.78f),
        components::LightComponent{
            .type = components::LightComponent::Type::Directional,
            .color = {1.0f, 0.95f, 0.87f, 1.0f},
            .intensity = 1.55f,
            .casts_shadows = true,
            .shadow_extent = 5.0f,
        });
    helpers::spawnPointLight(
        *world,
        "Fill Light",
        {-1.8f, 1.5f, 2.0f},
        components::LightComponent{
            .type = components::LightComponent::Type::Point,
            .color = {0.55f, 0.68f, 1.0f, 1.0f},
            .intensity = 1.3f,
            .range = 5.0f,
        });
  }

  void spawnCamera(const SceneBounds& bounds) {
    const glm::vec3 center =
        bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3(0.0f, 0.7f, 0.0f);
    const glm::vec3 extents =
        bounds.valid ? (bounds.max - bounds.min) * 0.5f : glm::vec3(0.7f);
    const float radius = std::max(1.0f, glm::length(extents));
    const glm::vec3 target = center + glm::vec3(0.0f, radius * 0.12f, 0.0f);
    const glm::vec3 eye = target + glm::normalize(glm::vec3{0.8f, 0.35f, 1.0f}) *
                                      std::max(2.5f, radius * 3.0f);
    const LookAngles look = lookAnglesToTarget(eye, target);
    components::CameraComponent camera;
    camera.is_primary = true;
    camera.render_shadows = true;
    camera.fov_y_degrees = 45.0f;
    camera.near_clip = 0.03f;
    camera.far_clip = 80.0f;
    helpers::spawnCamera(*world,
                         "Camera",
                         {eye.x, eye.y, eye.z},
                         math::fromYawPitch(look.yaw, look.pitch),
                         camera);
  }

  void spawnEnvironment() {
    helpers::spawnEnvironment(*world,
                              assets,
                              "Environment",
                              registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
                              0.28f,
                              true);
  }

  void setImportedDeformationPath(components::DeformationPath path) {
    for (const world::Entity entity : imported_entities_) {
      if (!world->isAlive(entity) ||
          !world->has<components::DeformableMeshComponent>(entity)) {
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
  }

  std::vector<assets::AssetPackageHandle> package_handles_;
  world::GltfSceneImportResult imported_;
  world::Entity imported_root_{};
  std::vector<world::Entity> imported_entities_;
  std::vector<std::string> clip_names_;
  std::vector<std::string> clip_diagnostics_;
  SceneBounds bounds_{};
  SceneBounds live_skinned_bounds_{};
  std::string startup_diagnostic_;
  size_t selected_clip_index_ = 0u;
  size_t target_rig_binding_count_ = 0u;
  float playback_speed_ = 1.0f;
  float blend_seconds_ = 0.2f;
  bool loop_ = true;
  bool use_gpu_deformation_ = true;
  bool logged_live_bounds_ = false;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::RpgHumanoidAnimationExample game;
  engine.setUi(karma::ui::imgui::createUiLayer(
      [&game](karma::app::UIContext& ctx) { game.drawUi(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Karma RPG Humanoid FBX Animation";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.loading_splash.enabled = false;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
