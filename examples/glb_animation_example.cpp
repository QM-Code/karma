#include "demo_asset_paths.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

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
        if (primitive.skinned()) {
          expandBounds(geometry_bounds, vertex);
        } else {
          expandBounds(geometry_bounds, world_pos + rotation * (vertex * world_scale));
        }
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

}  // namespace

class GlbAnimationExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("toggle_skinning_path", platform::Key::G, input::Trigger::Pressed);

    const std::filesystem::path model_path =
        resolveExamplePath("examples/assets/animation_model/source/walking.glb");

    const scene::GlbScenePrefab prefab = scene::loadGlbScenePrefab(
        model_path,
        scene::GlbSceneLoadOptions{
            .import_meshes = true,
            .import_lights = false,
        });
    if (!prefab.valid()) {
      spdlog::error("Failed to load animation model from {}", model_path.string());
      spawnCamera(SceneBounds{});
      return;
    }

    spdlog::info("Loaded animation model '{}': {} nodes, {} clips, {} skeletons, {} skins",
                 model_path.string(),
                 prefab.nodes.size(),
                 prefab.animations.size(),
                 prefab.skeletons.size(),
                 prefab.skins.size());
    for (const auto& clip : prefab.animations) {
      spdlog::info("Animation clip '{}': {:.3f}s, {} transform channels, {} morph tracks",
                   clip.name,
                   clip.duration_seconds,
                   clip.channels.size(),
                   clip.morph_target_tracks.size());
    }
    for (const auto& diagnostic : prefab.diagnostics) {
      spdlog::warn("Animation model import diagnostic: {}", diagnostic);
    }
    if (!prefab.skins.empty()) {
      spdlog::info("Skin '{}': {} joints",
                   prefab.skins.front().name,
                   prefab.skins.front().joint_node_indices.size());
    }

    bounds_ = computePrefabBounds(prefab);
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
      spdlog::error("Failed to instantiate animation model from {}", model_path.string());
    } else {
      imported_root_ = imported.root_entity;
      imported_entities_ = imported.entities;
      use_gpu_skinning_ = !envFlagEnabled("KARMA_ANIMATION_CPU_SKINNING");
      setImportedSkinningPath(use_gpu_skinning_ ? components::SkinningPath::Gpu
                                                : components::SkinningPath::Cpu);
      if (world->has<components::AnimatorComponent>(imported_root_)) {
        auto& animator = world->get<components::AnimatorComponent>(imported_root_);
        animator.loop = true;
        animator.playing = true;
        if (!animator.clips.empty()) {
          components::setAnimatorClip(animator, 0, true);
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
    (void)dt;
    if (input != nullptr && input->actionPressed("toggle_skinning_path")) {
      use_gpu_skinning_ = !use_gpu_skinning_;
      setImportedSkinningPath(use_gpu_skinning_ ? components::SkinningPath::Gpu
                                                : components::SkinningPath::Cpu);
    }
  }

  void onShutdown() override {}

 private:
  void spawnGround(const SceneBounds& bounds) {
    const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f
                                          : glm::vec3(0.0f, 0.0f, 0.0f);
    const float radius = bounds.valid ? std::max(glm::length((bounds.max - bounds.min) * 0.5f), 1.0f)
                                      : 1.5f;
    const float floor_y = bounds.valid ? bounds.min.y - 0.015f : -0.015f;
    const glm::vec3 half_extents{std::max(2.4f, radius * 2.2f), 0.015f,
                                 std::max(2.4f, radius * 2.2f)};

    const std::string mesh_key = "runtime/glb_animation/ground/mesh";
    const std::string material_key = "runtime/glb_animation/ground/material";
    if (graphics != nullptr) {
      graphics->registerRuntimeMesh(mesh_key, helpers::makeBoxMesh(half_extents));
    }
    if (materials != nullptr) {
      renderer::MaterialDesc material;
      material.base_color = math::Color{0.16f, 0.18f, 0.18f, 1.0f};
      material.roughness = 0.82f;
      material.metallic = 0.0f;
      materials->registerMaterialDesc(material_key, material);
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
    camera.far_clip = 60.0f;
    camera.is_primary = true;
    world->add(camera_entity, camera);
  }

  void spawnEnvironment() {
    const std::filesystem::path env_path = resolveExampleAssetPath("golden_gate_hills_4k.hdr");
    helpers::spawnEnvironment(*world, "Environment", env_path.string(), 0.28f, false);
  }

  void setImportedSkinningPath(components::SkinningPath path) {
    for (const ecs::Entity entity : imported_entities_) {
      if (!world->isAlive(entity) || !world->has<components::SkinnedMeshComponent>(entity)) {
        continue;
      }
      auto& skin = world->get<components::SkinnedMeshComponent>(entity);
      skin.skinning_path = path;
      skin.palette_valid = false;
      skin.diagnostic = path == components::SkinningPath::Gpu ? "GPU skinning path"
                                                              : "CPU skinning reference path";
    }
    spdlog::info("Animation model skinning path: {}",
                 path == components::SkinningPath::Gpu ? "GPU" : "CPU");
  }

  ecs::Entity imported_root_{};
  std::vector<ecs::Entity> imported_entities_;
  SceneBounds bounds_{};
  bool use_gpu_skinning_ = true;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::GlbAnimationExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma GLB Rigged Animation Example";
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
