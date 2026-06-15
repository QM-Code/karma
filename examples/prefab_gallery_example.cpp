#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "karma/core/math/glm.h"

namespace karma::demo {

namespace {

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct GalleryVariant {
  const char* name = "";
  const char* beam_prefab = "";
  const char* wave_prefab = "";
  math::Color orb_accent{};
};

constexpr std::array<GalleryVariant, 4> kGalleryVariants{{
    {"Red",
     "prefabs/gallery_beam_red",
     "prefabs/gallery_wave_red",
     {1.00f, 0.20f, 0.20f, 1.0f}},
    {"Blue",
     "prefabs/gallery_beam_blue",
     "prefabs/gallery_wave_blue",
     {0.24f, 0.56f, 1.00f, 1.0f}},
    {"Green",
     "prefabs/gallery_beam_green",
     "prefabs/gallery_wave_green",
     {0.20f, 1.00f, 0.36f, 1.0f}},
    {"Purple",
     "prefabs/gallery_beam_purple",
     "prefabs/gallery_wave_purple",
     {0.78f, 0.34f, 1.00f, 1.0f}},
}};

constexpr std::array<float, 4> kColumnX{{-18.0f, -6.0f, 6.0f, 18.0f}};
constexpr std::array<float, 4> kExplosionColumnX{{-28.0f, -9.5f, 9.5f, 28.0f}};
constexpr float kBeamRowZ = -12.0f;
constexpr float kOrbRowZ = 0.0f;
constexpr float kWaveRowZ = 12.0f;
constexpr float kExplosionRowZ = 25.0f;
constexpr float kBeamScale = 0.48f;
constexpr float kExplosionReplayPeriod = 3.6f;
constexpr float kPerfLogPeriod = 0.1f;
constexpr float kExplosionVisualWindow = 2.4f;

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

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

components::TransformComponent makeScaledTransform(const math::Vec3& position, float uniform_scale) {
  components::TransformComponent transform{};
  transform.setPosition(position);
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

  const ecs::Entity shell_entity = instance.find("shell");
  if (!shell_material_key.empty() && materials != nullptr && world.isAlive(shell_entity) &&
      world.has<components::MeshComponent>(shell_entity)) {
    auto& shell_mesh = world.get<components::MeshComponent>(shell_entity);
    const std::string material_key(shell_material_key);
    const math::Color shell_tint =
        withAlpha(mixColor(color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.18f), 1.0f);
    materials->registerFromMeshTint(material_key, shell_mesh.mesh_key, shell_tint);
    shell_mesh.material_key = material_key;
  }

  const ecs::Entity light_entity = instance.find("glow");
  if (world.isAlive(light_entity) && world.has<components::LightComponent>(light_entity)) {
    world.get<components::LightComponent>(light_entity).color =
        mixColor(color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.22f);
  }
}

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
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

}  // namespace

class PrefabGalleryExample final : public app::GameInterface {
 public:
  void onStart() override {
    startup_start_ = core::SteadyClock::now();
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    world_mesh_ = resolveExampleAssetPath("world.glb").string();

    {
      ScopedStartupTimer timer("Prefab gallery world spawn");
      spawnWorld();
    }
    {
      ScopedStartupTimer timer("Prefab gallery lighting spawn");
      spawnLighting();
    }
    {
      ScopedStartupTimer timer("Prefab gallery camera spawn");
      spawnCamera();
    }
    {
      ScopedStartupTimer timer("Prefab gallery prefab spawn");
      spawnPrefabs();
    }
    log_perf_stats_ = envFlagEnabled("KARMA_PREFAB_GALLERY_STATS");

    if (log_perf_stats_) {
      spdlog::info(
          "Prefab gallery perf logging enabled: {}s window, explosion window={}s",
          kPerfLogPeriod,
          kExplosionVisualWindow);
    }
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    if (!first_update_logged_) {
      spdlog::info("Prefab gallery first onUpdate after onStart begin took {:.2f} ms",
                   core::elapsedMillisecondsSince(startup_start_));
      first_update_logged_ = true;
    }
    time_ += dt;
    accumulatePerfSample(dt);

    if (!world->isAlive(camera_entity_)) {
      cleanupExpiredExplosions();
      maybeLogPerfStats();
      return;
    }

    const float look_sensitivity = 0.0008f;
    const float move_speed = 22.0f;
    const float smoothing = 20.0f;

    if (input->actionDown("cam_look")) {
      target_camera_yaw_ -= input->mouseDeltaX() * look_sensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * look_sensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.55f, 1.55f);

    const float alpha = 1.0f - std::exp(-smoothing * dt);
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
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;

    math::Vec3 camera_position = camera_transform.getPosition();
    camera_position.x += (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_position.y += (forward.y * forward_input) * move_speed * dt;
    camera_position.z += (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_transform.setPosition(camera_position);
    camera_transform.setRotation(camera_rotation);

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

    maybeLogPerfStats();
  }

  void onShutdown() override {}

 private:
  void accumulatePerfSample(float dt) {
    if (!log_perf_stats_) {
      return;
    }

    if (update_debug_count_ < 8u) {
      spdlog::info("Gallery update: frame={} dt_ms={:.2f} sim_t={:.2f}",
                   update_debug_count_,
                   dt * 1000.0f,
                   time_);
      spdlog::default_logger()->flush();
      update_debug_count_ += 1u;
    }

    perf_log_elapsed_ += dt;
    perf_frame_time_sum_ += dt;
    perf_frame_time_max_ = std::max(perf_frame_time_max_, dt);
    perf_frame_count_ += 1u;
  }

  void maybeLogPerfStats() {
    if (!log_perf_stats_ || perf_log_elapsed_ < kPerfLogPeriod || perf_frame_count_ == 0u) {
      return;
    }

    std::size_t recently_triggered_explosions = 0u;
    for (const auto& explosion : explosions_) {
      if (time_ - explosion.last_trigger_time <= kExplosionVisualWindow) {
        recently_triggered_explosions += 1u;
      }
    }
    const std::size_t active_explosion_visuals = active_explosions_.size();
    std::size_t active_explosion_lights = 0u;
    if (world != nullptr) {
      for (ecs::Entity entity :
           world->view<components::LightPulseComponent, components::LightComponent>()) {
        const auto& pulse = world->get<components::LightPulseComponent>(entity);
        if (pulse.active) {
          active_explosion_lights += 1u;
        }
      }
    }

    int fb_width = 0;
    int fb_height = 0;
    renderer::ForwardPlusStats stats{};
    if (graphics != nullptr) {
      graphics->getFramebufferSize(fb_width, fb_height);
      stats = graphics->getForwardPlusStats();
    }

    const float average_dt = perf_frame_time_sum_ / static_cast<float>(perf_frame_count_);
    const float average_fps = average_dt > 1.0e-6f ? 1.0f / average_dt : 0.0f;
    spdlog::info(
        "Gallery perf: t={:.2f}s fps={:.1f} avg_ms={:.2f} worst_ms={:.2f} active_visuals={} recent_triggers={} active_lights={} framebuffer={}x{} local_lights={} cpu_fallback={} fp_active={}",
        time_,
        average_fps,
        average_dt * 1000.0f,
        perf_frame_time_max_ * 1000.0f,
        active_explosion_visuals,
        recently_triggered_explosions,
        active_explosion_lights,
        fb_width,
        fb_height,
        static_cast<unsigned int>(stats.local_light_count),
        stats.cpu_fallback,
        stats.active);
    spdlog::default_logger()->flush();

    perf_log_elapsed_ = 0.0f;
    perf_frame_time_sum_ = 0.0f;
    perf_frame_time_max_ = 0.0f;
    perf_frame_count_ = 0u;
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
    sun_transform.setPosition({0.0f, 48.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.54f, -0.92f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {0.80f, 0.84f, 1.0f, 1.0f},
                        .intensity = 0.36f,
                    });
  }

  void spawnCamera() {
    const glm::vec3 target(0.0f, 4.0f, 2.0f);
    const glm::vec3 eye(0.0f, 13.0f, 34.0f);
    const LookAngles look = lookAnglesToTarget(eye, target);

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;

    components::TransformComponent camera_transform{};
    camera_transform.setPosition(math::fromGlm(eye));
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));

    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.05f;
    camera_component.far_clip = 240.0f;
    camera_component.render_shadows = false;
    camera_component.is_primary = true;

    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, camera_component);
  }

  void spawnPrefabs() {
    for (size_t i = 0; i < kGalleryVariants.size(); ++i) {
      const auto& variant = kGalleryVariants[i];
      const float x = kColumnX[i];

      const auto beam = prefabs::instantiatePrefab(
          *world,
          *scene,
          resolveExampleAssetPath(variant.beam_prefab),
          prefabs::PrefabInstantiateDesc{
              .root_transform = makeScaledTransform({x, 0.0f, kBeamRowZ}, kBeamScale),
              .name_override = std::string(variant.name) + " Beam",
          });
      if (!beam.has_value()) {
        spdlog::error("Prefab gallery failed to instantiate {} beam", variant.name);
      }

      const auto orb = prefabs::instantiatePrefab(
          *world,
          *scene,
          resolveExampleAssetPath("prefabs/energy_orb"),
          prefabs::PrefabInstantiateDesc{
              .root_transform = makeTransform({x, 1.85f, kOrbRowZ}),
              .name_override = std::string(variant.name) + " Orb",
          });
      if (!orb.has_value()) {
        spdlog::error("Prefab gallery failed to instantiate {} orb", variant.name);
      } else {
        const std::string orb_material_key =
            "gallery_orb_shell_" + std::string(variant.name);
        applyOrbAccent(*world, materials, *orb, variant.orb_accent, orb_material_key);
      }

      const auto wave = prefabs::instantiatePrefab(
          *world,
          *scene,
          resolveExampleAssetPath(variant.wave_prefab),
          prefabs::PrefabInstantiateDesc{
              .root_transform = makeTransform({x, 2.2f, kWaveRowZ}),
              .name_override = std::string(variant.name) + " Wave",
          });
      if (!wave.has_value()) {
        spdlog::error("Prefab gallery failed to instantiate {} wave", variant.name);
      }
    }

    explosions_.clear();
    explosions_.reserve(kGalleryVariants.size());
    for (size_t i = 0; i < kGalleryVariants.size(); ++i) {
      explosions_.push_back(ExplosionGalleryItem{
          .position = {kExplosionColumnX[i], 0.0f, kExplosionRowZ},
          .name = std::string(kGalleryVariants[i].name) + " Explosion",
          .next_trigger_time = 0.9f + static_cast<float>(i) * 0.65f,
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
      spdlog::error("Prefab gallery failed to instantiate {}", explosion.name);
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
  std::vector<ExplosionGalleryItem> explosions_{};
  std::vector<ActiveExplosionInstance> active_explosions_{};
  bool log_perf_stats_ = false;
  bool first_update_logged_ = false;
  float time_ = 0.0f;
  float perf_log_elapsed_ = 0.0f;
  float perf_frame_time_sum_ = 0.0f;
  float perf_frame_time_max_ = 0.0f;
  std::uint32_t perf_frame_count_ = 0u;
  std::uint32_t update_debug_count_ = 0u;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  core::SteadyClock::time_point startup_start_{};
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  engine.addRuntimeModule(std::make_unique<karma::volumes::VolumeRuntimeModule>());
  karma::demo::PrefabGalleryExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Prefab Gallery Example";
  config.window.samples = 1;
  config.loading_splash.enabled = true;
  config.loading_splash.image_path = karma::demo::resolveExamplePath("docs/logo.png");
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0009f;
  config.shadow_pcf_radius = 1;
  config.local_light_distance_damping = 0.05f;
  config.local_light_range_falloff_exponent = 1.35f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.0f;
  config.lighting_exposure = 1.15f;
  config.environment_map = karma::demo::resolveExampleAssetPath("golden_gate_hills_4k.hdr");
  config.environment_intensity = 0.18f;
  config.environment_draw_skybox = false;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
