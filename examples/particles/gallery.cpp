#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct BeamSpec {
  const char* name = "";
  math::Vec3 position{};
  float yaw = 0.0f;
  float scale = 1.0f;
  float phase = 0.0f;
};

struct OrbSpec {
  const char* name = "";
  math::Vec3 position{};
  math::Color accent{};
  float phase = 0.0f;
};

struct RootAnimation {
  ecs::Entity root{};
  math::Vec3 base_position{};
  float base_yaw = 0.0f;
  float phase = 0.0f;
};

struct ExplosionGalleryItem {
  math::Vec3 position{};
  std::string name;
  float next_trigger_time = 0.0f;
  float last_trigger_time = -1000.0f;
};

struct ActiveExplosionInstance {
  ecs::Entity root{};
  float destroy_time = 0.0f;
};

constexpr std::array<BeamSpec, 3> kBeamSpecs{{
    {"Left Beam", {-18.0f, 0.0f, -10.0f}, 0.30f, 0.52f, 0.0f},
    {"Center Beam", {0.0f, 0.0f, -12.0f}, -0.08f, 0.58f, 1.7f},
    {"Right Beam", {18.0f, 0.0f, -10.0f}, -0.42f, 0.50f, 3.1f},
}};

constexpr std::array<OrbSpec, 4> kOrbSpecs{{
    {"Cyan Orb", {-15.0f, 1.85f, 2.5f}, {0.16f, 0.82f, 1.00f, 1.0f}, 0.0f},
    {"Green Orb", {-5.0f, 1.85f, 2.0f}, {0.18f, 1.00f, 0.34f, 1.0f}, 1.3f},
    {"Amber Orb", {5.0f, 1.85f, 2.0f}, {1.00f, 0.58f, 0.16f, 1.0f}, 2.6f},
    {"Magenta Orb", {15.0f, 1.85f, 2.5f}, {0.95f, 0.30f, 1.00f, 1.0f}, 3.9f},
}};

constexpr std::array<float, 4> kExplosionColumnX{{-18.0f, -6.0f, 6.0f, 18.0f}};
constexpr float kExplosionRowZ = 16.0f;
constexpr float kExplosionReplayPeriod = 4.2f;
constexpr float kExplosionVisualWindow = 2.6f;
constexpr float kPerfLogPeriod = 0.25f;

components::TransformComponent makeTransform(const math::Vec3& position,
                                             float uniform_scale = 1.0f,
                                             float yaw = 0.0f,
                                             float pitch = 0.0f) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  transform.setRotation(math::fromYawPitch(yaw, pitch));
  transform.setScale({uniform_scale, uniform_scale, uniform_scale});
  return transform;
}

math::Color scaleColor(const math::Color& color, float r, float g, float b, float a) {
  return {color.r * r, color.g * g, color.b * b, color.a * a};
}

math::Color mixColor(const math::Color& a, const math::Color& b, float t) {
  const float clamped = std::clamp(t, 0.0f, 1.0f);
  return {
      a.r + (b.r - a.r) * clamped,
      a.g + (b.g - a.g) * clamped,
      a.b + (b.b - a.b) * clamped,
      a.a + (b.a - a.a) * clamped,
  };
}

math::Color withAlpha(math::Color color, float alpha) {
  color.a = alpha;
  return color;
}

LookAngles lookAnglesToTarget(const math::Vec3& eye, const math::Vec3& target) {
  const math::Vec3 direction = math::normalize(math::subtract(target, eye));
  return {
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

bool envFlagEnabled(const char* name) {
  if (const char* value = std::getenv(name)) {
    return value[0] != '\0' && std::string(value) != "0";
  }
  return false;
}

class ScopedStartupTimer {
 public:
  explicit ScopedStartupTimer(std::string label)
      : label_(std::move(label)), start_(core::SteadyClock::now()) {}

  ~ScopedStartupTimer() {
    spdlog::info("{} took {:.2f} ms", label_, core::elapsedMillisecondsSince(start_));
  }

 private:
  std::string label_;
  core::SteadyClock::time_point start_;
};

void setPrefabParticlePlayback(ecs::World& world,
                               const prefabs::PrefabInstance& instance,
                               bool enabled) {
  for (const ecs::Entity entity : instance.entities) {
    if (world.isAlive(entity) && world.has<components::ParticleEmitterComponent>(entity)) {
      particles::setEffectPlayback(world, entity, enabled, enabled);
    }
  }
}

void restartPrefabParticleEffects(ecs::World& world, const prefabs::PrefabInstance& instance) {
  for (const ecs::Entity entity : instance.entities) {
    if (world.isAlive(entity)) {
      particles::restartEffect(world, entity);
    }
  }
}

void applyOrbAccent(ecs::World& world,
                    renderer::MaterialLibrary* materials,
                    const prefabs::PrefabInstance& instance,
                    const math::Color& color,
                    std::string_view shell_material_key) {
  auto setStartAndEndColor =
      [&](std::string_view name, math::Color start_color, math::Color end_color) {
        const ecs::Entity entity = instance.find(name);
        if (world.isAlive(entity) &&
            world.has<components::ParticleEffectOverrideComponent>(entity)) {
          auto& effect_override =
              world.get<components::ParticleEffectOverrideComponent>(entity);
          effect_override.start_color = start_color;
          effect_override.end_color = end_color;
        }
      };

  setStartAndEndColor("core",
                      withAlpha(mixColor(color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.18f), 0.98f),
                      scaleColor(color, 0.95f, 1.0f, 0.95f, 0.0f));
  setStartAndEndColor("arcs",
                      withAlpha(mixColor(color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.08f), 1.0f),
                      scaleColor(color, 1.0f, 1.0f, 1.0f, 0.0f));
  setStartAndEndColor("halo",
                      scaleColor(color, 0.45f, 0.72f, 0.45f, 0.14f),
                      scaleColor(color, 0.25f, 0.40f, 0.25f, 0.0f));
  setStartAndEndColor("distortion",
                      {1.0f, 1.0f, 1.0f, 0.90f},
                      {1.0f, 1.0f, 1.0f, 0.0f});

  const ecs::Entity shell_entity = instance.find("shell");
  if (!shell_material_key.empty() && materials != nullptr && world.isAlive(shell_entity) &&
      world.has<components::MeshComponent>(shell_entity)) {
    auto& shell_mesh = world.get<components::MeshComponent>(shell_entity);
    const std::string material_key(shell_material_key);
    const math::Color shell_tint =
        withAlpha(mixColor(color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.18f), 1.0f);

    renderer::MaterialDesc shell_material{};
    shell_material.base_color = shell_tint;
    shell_material.emissive_color = {
        color.r * 0.28f,
        color.g * 0.28f,
        color.b * 0.28f,
        1.0f,
    };
    shell_material.emissive_strength = 0.75f;
    shell_material.metallic = 0.0f;
    shell_material.roughness = 0.68f;
    shell_material.unlit = true;
    shell_material.transparent = false;
    materials->registerMaterialDesc(material_key, shell_material);
    shell_mesh.materials = {components::MeshMaterialBinding{
        .slot = 0,
        .material_key = material_key,
    }};
  }

  const ecs::Entity light_entity = instance.find("glow");
  if (world.isAlive(light_entity) && world.has<components::LightComponent>(light_entity)) {
    world.get<components::LightComponent>(light_entity).color =
        mixColor(color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.22f);
  }
}

}  // namespace

class ParticleGalleryExample final : public app::GameInterface {
 public:
  void onStart() override {
    startup_start_ = core::SteadyClock::now();

    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_fast", platform::Key::LeftShift);
    input->bindMouse("cam_look", platform::MouseButton::Right);
    input->bindKey("toggle_particles", platform::Key::Space, input::Trigger::Pressed);
    input->bindKey("restart_explosions", platform::Key::R, input::Trigger::Pressed);

    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    log_stats_ = envFlagEnabled("KARMA_PARTICLE_GALLERY_STATS");

    {
      ScopedStartupTimer timer("Particle gallery world spawn");
      spawnWorld();
    }
    {
      ScopedStartupTimer timer("Particle gallery lighting spawn");
      spawnLighting();
    }
    {
      ScopedStartupTimer timer("Particle gallery camera spawn");
      spawnCamera();
    }
    {
      ScopedStartupTimer timer("Particle gallery prefab spawn");
      spawnPrefabs();
    }

    spdlog::info(
        "Particle gallery controls: hold RMB to look, WASD to move, Q/E vertical, Left Shift to boost, Space toggles beam/orb emitters, R restarts explosions");
    if (log_stats_) {
      spdlog::info(
          "Particle gallery stats enabled: period {:.2f}s, explosion visual window {:.2f}s",
          kPerfLogPeriod,
          kExplosionVisualWindow);
    }
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    if (!first_update_logged_) {
      spdlog::info("Particle gallery first onUpdate after onStart begin took {:.2f} ms",
                   core::elapsedMillisecondsSince(startup_start_));
      first_update_logged_ = true;
    }

    time_ += dt;
    accumulatePerfSample(dt);

    if (world->isAlive(camera_entity_)) {
      updateCamera(dt);
    }

    if (input->actionPressed("toggle_particles")) {
      persistent_particles_enabled_ = !persistent_particles_enabled_;
      for (const auto& instance : persistent_instances_) {
        setPrefabParticlePlayback(*world, instance, persistent_particles_enabled_);
      }
    }

    if (input->actionPressed("restart_explosions")) {
      restartExplosionCycle(0.05f);
      for (const auto& instance : persistent_instances_) {
        restartPrefabParticleEffects(*world, instance);
      }
      persistent_particles_enabled_ = true;
    }

    updatePersistentTransforms();
    updateExplosions();
    maybeLogPerfStats();
  }

  void onShutdown() override {
    if (world == nullptr || scene == nullptr) {
      return;
    }
    for (const auto& active : active_explosions_) {
      if (world->isAlive(active.root)) {
        prefabs::destroyPrefab(*world, *scene, active.root);
      }
    }
    active_explosions_.clear();
  }

 private:
  void accumulatePerfSample(float dt) {
    if (!log_stats_) {
      return;
    }

    perf_log_elapsed_ += dt;
    perf_frame_time_sum_ += dt;
    perf_frame_time_max_ = std::max(perf_frame_time_max_, dt);
    perf_frame_count_ += 1u;
  }

  void maybeLogPerfStats() {
    if (!log_stats_ || perf_log_elapsed_ < kPerfLogPeriod || perf_frame_count_ == 0u) {
      return;
    }

    int fb_width = 0;
    int fb_height = 0;
    renderer::ForwardPlusStats forward_stats{};
    renderer::ParticlePassStats particle_stats{};
    if (graphics != nullptr) {
      graphics->getFramebufferSize(fb_width, fb_height);
      forward_stats = graphics->getForwardPlusStats();
      particle_stats = graphics->getParticlePassStats();
    }

    std::size_t recent_explosions = 0u;
    for (const auto& explosion : explosions_) {
      if (time_ - explosion.last_trigger_time <= kExplosionVisualWindow) {
        recent_explosions += 1u;
      }
    }

    const std::size_t world_emitters =
        world != nullptr ? world->view<components::ParticleEmitterComponent>().size() : 0u;
    const std::size_t world_lights =
        world != nullptr ? world->view<components::LightComponent>().size() : 0u;
    const float average_dt = perf_frame_time_sum_ / static_cast<float>(perf_frame_count_);
    const float average_fps = average_dt > 1.0e-6f ? 1.0f / average_dt : 0.0f;

    spdlog::info(
        "Particle gallery: t={:.2f}s fps={:.1f} avg_ms={:.2f} worst_ms={:.2f} persistent={} active_explosions={} recent_explosions={} world_emitters={} world_lights={} framebuffer={}x{} local_lights={} fp_active={} particle_emitters(sim/vis/cull/sub)={}/{}/{}/{} particle_counts(sim/pack/cull)={}/{}/{} gpu_particles(cap/alive/spawn/kill)={}/{}/{}/{} gpu_dispatch(compute/indirect/cull)={}/{}/{} batches(add/alpha/dist)={}/{}/{} particles(add/alpha/dist)={}/{}/{} render_ms(add/alpha/dist/draw)={:.2f}/{:.2f}/{:.2f}/{:.2f} sort(alpha/dist)={}/{} gpu_fallback={} alpha_half_res={}",
        time_,
        average_fps,
        average_dt * 1000.0f,
        perf_frame_time_max_ * 1000.0f,
        persistent_particles_enabled_,
        active_explosions_.size(),
        recent_explosions,
        world_emitters,
        world_lights,
        fb_width,
        fb_height,
        static_cast<unsigned int>(forward_stats.local_light_count),
        forward_stats.active,
        particle_stats.simulated_emitters,
        particle_stats.visible_emitters,
        particle_stats.culled_emitters,
        particle_stats.submitted_emitters,
        particle_stats.simulated_particles,
        particle_stats.packed_particles,
        particle_stats.culled_particles,
        particle_stats.gpu_particle_capacity,
        particle_stats.gpu_alive_particles,
        particle_stats.gpu_spawned_particles,
        particle_stats.gpu_killed_particles,
        particle_stats.gpu_compute_dispatches,
        particle_stats.gpu_indirect_dispatches,
        particle_stats.gpu_culling_dispatches,
        particle_stats.additive_batches,
        particle_stats.alpha_batches,
        particle_stats.distortion_batches,
        particle_stats.additive_particles,
        particle_stats.alpha_particles,
        particle_stats.distortion_particles,
        particle_stats.additive_grouping_ms,
        particle_stats.alpha_sort_ms,
        particle_stats.distortion_sort_ms,
        particle_stats.draw_submission_ms,
        particle_stats.alpha_sorted_particles,
        particle_stats.distortion_sorted_particles,
        particle_stats.gpu_fallback_active,
        particle_stats.alpha_half_res);
    spdlog::default_logger()->flush();

    perf_log_elapsed_ = 0.0f;
    perf_frame_time_sum_ = 0.0f;
    perf_frame_time_max_ = 0.0f;
    perf_frame_count_ = 0u;
  }

  void updateCamera(float dt) {
    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kMoveSpeed = 20.0f;
    constexpr float kBoostMultiplier = 2.7f;
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
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    const math::Vec3 right = math::normalize(math::cross(forward, up));

    float forward_input = 0.0f;
    float right_input = 0.0f;
    float vertical_input = 0.0f;
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;
    if (input->actionDown("cam_up")) vertical_input += 1.0f;
    if (input->actionDown("cam_down")) vertical_input -= 1.0f;

    const float move_speed =
        kMoveSpeed * (input->actionDown("cam_fast") ? kBoostMultiplier : 1.0f);
    math::Vec3 camera_position = camera_transform.getPosition();
    camera_position.x +=
        (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_position.y +=
        (forward.y * forward_input + vertical_input) * move_speed * dt;
    camera_position.z +=
        (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_transform.setPosition(camera_position);
    camera_transform.setRotation(camera_rotation);
  }

  void updatePersistentTransforms() {
    for (const auto& beam : beams_) {
      if (!world->isAlive(beam.root) ||
          !world->has<components::TransformComponent>(beam.root)) {
        continue;
      }
      auto& transform = world->get<components::TransformComponent>(beam.root);
      transform.setRotation(
          math::fromYawPitch(beam.base_yaw + std::sin(time_ * 0.34f + beam.phase) * 0.16f,
                             0.0f));
    }

    for (const auto& orb : orbs_) {
      if (!world->isAlive(orb.root) ||
          !world->has<components::TransformComponent>(orb.root)) {
        continue;
      }
      auto& transform = world->get<components::TransformComponent>(orb.root);
      const math::Vec3 offset{
          std::sin(time_ * 0.72f + orb.phase) * 0.48f,
          std::sin(time_ * 1.35f + orb.phase) * 0.18f,
          std::cos(time_ * 0.58f + orb.phase) * 0.34f,
      };
      transform.setPosition({
          orb.base_position.x + offset.x,
          orb.base_position.y + offset.y,
          orb.base_position.z + offset.z,
      });
      transform.setRotation(math::fromYawPitch(time_ * 0.28f + orb.phase, 0.0f));
    }
  }

  void updateExplosions() {
    for (auto& explosion : explosions_) {
      if (time_ >= explosion.next_trigger_time) {
        if (spawnExplosion(explosion)) {
          explosion.last_trigger_time = time_;
        }
        do {
          explosion.next_trigger_time += kExplosionReplayPeriod;
        } while (time_ >= explosion.next_trigger_time);
      }
    }
    cleanupExpiredExplosions();
  }

  void restartExplosionCycle(float lead_seconds) {
    for (const auto& active : active_explosions_) {
      if (world->isAlive(active.root)) {
        prefabs::destroyPrefab(*world, *scene, active.root);
      }
    }
    active_explosions_.clear();

    for (std::size_t i = 0; i < explosions_.size(); ++i) {
      explosions_[i].next_trigger_time = time_ + lead_seconds + static_cast<float>(i) * 0.45f;
      explosions_[i].last_trigger_time = -1000.0f;
    }
  }

  void spawnWorld() {
    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
                                 .mesh_key = world_mesh_,
                                 .visible = true,
                             });
  }

  void spawnLighting() {
    const ecs::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 46.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.54f, -0.92f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {0.82f, 0.86f, 1.0f, 1.0f},
                        .intensity = 0.34f,
                        .casts_shadows = false,
                    });
  }

  void spawnCamera() {
    const math::Vec3 target{0.0f, 4.0f, 2.0f};
    const math::Vec3 eye{0.0f, 12.0f, 34.0f};
    const LookAngles look = lookAnglesToTarget(eye, target);

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;

    components::TransformComponent camera_transform{};
    camera_transform.setPosition(eye);
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));

    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.05f;
    camera_component.far_clip = 220.0f;
    camera_component.render_shadows = false;
    camera_component.is_primary = true;

    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, camera_component);
  }

  void spawnPrefabs() {
    persistent_instances_.clear();
    beams_.clear();
    orbs_.clear();

    for (const auto& spec : kBeamSpecs) {
      const auto beam = prefabs::instantiatePrefab(
          *world,
          *scene,
          resolveExampleAssetPath("prefabs/beam_impostor"),
          prefabs::PrefabInstantiateDesc{
              .root_transform = makeTransform(spec.position, spec.scale, spec.yaw),
              .name_override = spec.name,
          });
      if (!beam.has_value()) {
        spdlog::error("Particle gallery failed to instantiate {}", spec.name);
        continue;
      }
      persistent_instances_.push_back(*beam);
      beams_.push_back(RootAnimation{
          .root = beam->root,
          .base_position = spec.position,
          .base_yaw = spec.yaw,
          .phase = spec.phase,
      });
    }

    for (std::size_t i = 0; i < kOrbSpecs.size(); ++i) {
      const auto& spec = kOrbSpecs[i];
      const auto orb = prefabs::instantiatePrefab(
          *world,
          *scene,
          resolveExampleAssetPath("prefabs/energy_orb"),
          prefabs::PrefabInstantiateDesc{
              .root_transform = makeTransform(spec.position),
              .name_override = spec.name,
          });
      if (!orb.has_value()) {
        spdlog::error("Particle gallery failed to instantiate {}", spec.name);
        continue;
      }

      applyOrbAccent(*world,
                     materials,
                     *orb,
                     spec.accent,
                     "particle_gallery_orb_shell_" + std::to_string(i));
      persistent_instances_.push_back(*orb);
      orbs_.push_back(RootAnimation{
          .root = orb->root,
          .base_position = spec.position,
          .base_yaw = 0.0f,
          .phase = spec.phase,
      });
    }

    explosions_.clear();
    explosions_.reserve(kExplosionColumnX.size());
    for (std::size_t i = 0; i < kExplosionColumnX.size(); ++i) {
      explosions_.push_back(ExplosionGalleryItem{
          .position = {kExplosionColumnX[i], 0.0f, kExplosionRowZ},
          .name = "Gallery Explosion " + std::to_string(i + 1),
          .next_trigger_time = 0.9f + static_cast<float>(i) * 0.55f,
          .last_trigger_time = -1000.0f,
      });
    }
  }

  bool spawnExplosion(const ExplosionGalleryItem& explosion) {
    const auto instance = prefabs::instantiatePrefab(
        *world,
        *scene,
        resolveExampleAssetPath("prefabs/explosion"),
        prefabs::PrefabInstantiateDesc{
            .root_transform = makeTransform(explosion.position),
            .name_override = explosion.name,
        });
    if (!instance.has_value()) {
      spdlog::error("Particle gallery failed to instantiate {}", explosion.name);
      return false;
    }

    active_explosions_.push_back(ActiveExplosionInstance{
        .root = instance->root,
        .destroy_time = time_ + kExplosionVisualWindow,
    });
    return true;
  }

  void cleanupExpiredExplosions() {
    for (auto it = active_explosions_.begin(); it != active_explosions_.end();) {
      if (time_ < it->destroy_time) {
        ++it;
        continue;
      }
      prefabs::destroyPrefab(*world, *scene, it->root);
      it = active_explosions_.erase(it);
    }
  }

  std::string world_mesh_;
  ecs::Entity camera_entity_{};
  std::vector<prefabs::PrefabInstance> persistent_instances_{};
  std::vector<RootAnimation> beams_{};
  std::vector<RootAnimation> orbs_{};
  std::vector<ExplosionGalleryItem> explosions_{};
  std::vector<ActiveExplosionInstance> active_explosions_{};
  bool persistent_particles_enabled_ = true;
  bool log_stats_ = false;
  bool first_update_logged_ = false;
  float time_ = 0.0f;
  float perf_log_elapsed_ = 0.0f;
  float perf_frame_time_sum_ = 0.0f;
  float perf_frame_time_max_ = 0.0f;
  std::uint32_t perf_frame_count_ = 0u;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  core::SteadyClock::time_point startup_start_{};
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::ParticleGalleryExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Particle Gallery";
  config.window.samples = 1;
  config.loading_splash.enabled = true;
  config.loading_splash.image_path = karma::demo::resolveExamplePath("docs/logo.png");
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.forward_plus_max_local_lights = 512;
  config.shadow_map_size = 512;
  config.shadow_pcf_radius = 1;
  config.point_shadow_max_lights = 1;
  config.local_light_distance_damping = 0.06f;
  config.local_light_range_falloff_exponent = 1.25f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.75f;
  config.lighting_exposure = 1.08f;
  config.environment_map =
      karma::demo::resolveExampleAssetPath("diligent_gltf_viewer/textures/papermill.ktx");
  config.environment_intensity = 0.22f;
  config.environment_draw_skybox = false;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
