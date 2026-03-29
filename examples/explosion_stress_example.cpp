#include "demo_asset_paths.h"
#include "explosion_prefab_package.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

constexpr int kDefaultExplosionCount = 9;
constexpr int kMaxExplosionCount = 64;
constexpr float kExplosionSpacing = 10.0f;
constexpr float kDefaultReplayPeriod = 3.6f;
constexpr float kExplosionVisualWindow = 2.4f;
constexpr float kPerfLogPeriod = 0.2f;
constexpr uint32_t kLayerFlash = 1u << 0u;
constexpr uint32_t kLayerFireball = 1u << 1u;
constexpr uint32_t kLayerHeat = 1u << 2u;
constexpr uint32_t kLayerCoreFlipbook = 1u << 3u;
constexpr uint32_t kLayerSmokeFlipbook = 1u << 4u;
constexpr uint32_t kLayerEmbers = 1u << 5u;
constexpr uint32_t kLayerShockRing = 1u << 6u;
constexpr uint32_t kLayerDebris = 1u << 7u;
constexpr uint32_t kLayerDustRing = 1u << 8u;
constexpr uint32_t kLayerSmoke = 1u << 9u;
constexpr uint32_t kLayerScorch = 1u << 10u;
constexpr uint32_t kLayerLight = 1u << 11u;
constexpr uint32_t kAllExplosionLayers = kLayerFlash | kLayerFireball | kLayerHeat |
                                         kLayerCoreFlipbook | kLayerSmokeFlipbook |
                                         kLayerEmbers | kLayerShockRing | kLayerDebris |
                                         kLayerDustRing | kLayerSmoke | kLayerScorch |
                                         kLayerLight;

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

struct ExplosionStressOptions {
  int explosion_count = kDefaultExplosionCount;
  float replay_period_seconds = kDefaultReplayPeriod;
  bool log_stats = false;
  uint32_t enabled_layers = kAllExplosionLayers;
};

struct ExplosionLayerOption {
  std::string_view token;
  uint32_t bit = 0u;
};

constexpr std::array<ExplosionLayerOption, 12> kExplosionLayerOptions{{
    {"flash", kLayerFlash},
    {"fireball", kLayerFireball},
    {"heat", kLayerHeat},
    {"core_flipbook", kLayerCoreFlipbook},
    {"smoke_flipbook", kLayerSmokeFlipbook},
    {"embers", kLayerEmbers},
    {"shock_ring", kLayerShockRing},
    {"debris", kLayerDebris},
    {"dust_ring", kLayerDustRing},
    {"smoke", kLayerSmoke},
    {"scorch", kLayerScorch},
    {"light", kLayerLight},
}};

struct ExplosionStressItem {
  ExplosionPrefabController controller{};
  float next_trigger_time = 0.0f;
  float last_trigger_time = -1000.0f;
};

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

math::Vec3 toMath(const glm::vec3& value) {
  return {value.x, value.y, value.z};
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

bool isLayerEnabled(uint32_t enabled_layers, uint32_t layer_bit) {
  return (enabled_layers & layer_bit) != 0u;
}

std::string normalizeLayerToken(std::string_view token) {
  std::size_t start = 0u;
  while (start < token.size() &&
         std::isspace(static_cast<unsigned char>(token[start])) != 0) {
    ++start;
  }
  std::size_t end = token.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(token[end - 1u])) != 0) {
    --end;
  }

  std::string normalized;
  normalized.reserve(end - start);
  for (std::size_t i = start; i < end; ++i) {
    normalized.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(token[i]))));
  }
  return normalized;
}

bool disableExplosionLayers(std::string_view layer_list,
                            uint32_t& enabled_layers,
                            std::string& unknown_layer) {
  std::size_t start = 0u;
  while (start <= layer_list.size()) {
    const std::size_t end = layer_list.find(',', start);
    const std::size_t length =
        end == std::string_view::npos ? layer_list.size() - start : end - start;
    const std::string token = normalizeLayerToken(layer_list.substr(start, length));
    if (!token.empty()) {
      if (token == "all") {
        enabled_layers = 0u;
      } else {
        bool matched = false;
        for (const auto& option : kExplosionLayerOptions) {
          if (token == option.token) {
            enabled_layers &= ~option.bit;
            matched = true;
            break;
          }
        }
        if (!matched) {
          unknown_layer = token;
          return false;
        }
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1u;
  }
  return true;
}

std::string describeDisabledLayers(uint32_t enabled_layers) {
  std::string description;
  for (const auto& option : kExplosionLayerOptions) {
    if (isLayerEnabled(enabled_layers, option.bit)) {
      continue;
    }
    if (!description.empty()) {
      description += ", ";
    }
    description += option.token;
  }
  return description.empty() ? std::string("none") : description;
}

}  // namespace

class ExplosionStressExample final : public app::GameInterface {
 public:
  explicit ExplosionStressExample(const ExplosionStressOptions& options)
      : explosion_count_(std::clamp(options.explosion_count, 1, kMaxExplosionCount)),
        replay_period_seconds_(std::max(options.replay_period_seconds, 0.2f)),
        log_stats_(options.log_stats),
        enabled_layers_(options.enabled_layers) {}

  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_fast", platform::Key::LeftShift);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    spawnWorld();
    spawnLighting();
    spawnCamera();
    spawnExplosions();

    spdlog::info(
        "Explosion stress controls: hold RMB to look, WASD to move, Q/E vertical, Left Shift to boost");
    spdlog::info(
        "Explosion stress configured with {} controllers, replay period {:.2f}s, trigger interval {:.3f}s",
        explosion_count_,
        replay_period_seconds_,
        triggerStaggerSeconds());
    spdlog::info("Explosion stress disabled layers: {}", describeDisabledLayers(enabled_layers_));
    if (log_stats_) {
      spdlog::info("Explosion stress stats report the previous rendered frame.");
    }
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    time_ += dt;
    accumulatePerfSample(dt);

    if (!world->isAlive(camera_entity_)) {
      updateExplosions();
      maybeLogPerfStats();
      return;
    }

    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kMoveSpeed = 20.0f;
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
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
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
    camera_position.x += (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_position.y += (forward.y * forward_input + vertical_input) * move_speed * dt;
    camera_position.z += (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_transform.setPosition(camera_position);
    camera_transform.setRotation(camera_rotation);

    updateExplosions();
    maybeLogPerfStats();
  }

  void onShutdown() override {}

 private:
  int gridWidth() const {
    return std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(explosion_count_)))));
  }

  int gridDepth() const {
    const int width = gridWidth();
    return std::max(1, (explosion_count_ + width - 1) / width);
  }

  float triggerStaggerSeconds() const {
    return std::max(replay_period_seconds_ / static_cast<float>(explosion_count_), 0.001f);
  }

  float sceneHalfExtent() const {
    const float half_width = static_cast<float>(gridWidth() - 1) * 0.5f * kExplosionSpacing;
    const float half_depth = static_cast<float>(gridDepth() - 1) * 0.5f * kExplosionSpacing;
    return std::max(half_width, half_depth);
  }

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

    std::size_t active_explosion_visuals = 0u;
    std::size_t active_explosion_lights = 0u;
    std::size_t pending_restarts = 0u;
    for (const auto& explosion : explosions_) {
      if (time_ - explosion.last_trigger_time <= kExplosionVisualWindow) {
        active_explosion_visuals += 1u;
      }
      if (explosion.controller.light_active) {
        active_explosion_lights += 1u;
      }
      pending_restarts += explosion.controller.scheduled_restarts.size();
    }

    int fb_width = 0;
    int fb_height = 0;
    const std::size_t total_prefabs =
        world != nullptr ? world->view<components::PrefabInstanceComponent>().size() : 0u;
    const std::size_t total_emitters =
        world != nullptr ? world->view<components::ParticleEmitterComponent>().size() : 0u;
    const std::size_t total_lights =
        world != nullptr ? world->view<components::LightComponent>().size() : 0u;
    renderer::ForwardPlusStats stats{};
    renderer::ParticlePassStats particle_stats{};
    if (graphics != nullptr) {
      graphics->getFramebufferSize(fb_width, fb_height);
      stats = graphics->getForwardPlusStats();
      particle_stats = graphics->getParticlePassStats();
    }

    const float average_dt = perf_frame_time_sum_ / static_cast<float>(perf_frame_count_);
    const float average_fps = average_dt > 1.0e-6f ? 1.0f / average_dt : 0.0f;
    spdlog::info(
        "Explosion stress: t={:.2f}s fps={:.1f} avg_ms={:.2f} worst_ms={:.2f} configured={} active_visuals={} active_lights={} pending_restarts={} world_prefabs={} world_emitters={} world_lights={} trigger_interval_ms={:.0f} framebuffer={}x{} local_lights={} cpu_fallback={} fp_active={} part_sys_ms(sync/sim/pack)={:.2f}/{:.2f}/{:.2f} part_render_ms(add/asort/dsort/draw)={:.2f}/{:.2f}/{:.2f}/{:.2f} part_alpha_ms(collect/sort/span)={:.2f}/{:.2f}/{:.2f} part_dist_ms(collect/sort/span)={:.2f}/{:.2f}/{:.2f} fx_apply={} part_sys_emit(iter/vis/cull/sub)={}/{}/{}/{} part_sys_particles(sim/pack/cull/ground)={}/{}/{}/{} part_batches={}/{}/{} part_particles={}/{}/{} part_draws={}/{}/{} part_sorted={}/{} part_bad_depth={}/{} scene_samples={}/{} scene_copy={} post_copy={} alpha_half_res={}",
        time_,
        average_fps,
        average_dt * 1000.0f,
        perf_frame_time_max_ * 1000.0f,
        explosion_count_,
        active_explosion_visuals,
        active_explosion_lights,
        pending_restarts,
        total_prefabs,
        total_emitters,
        total_lights,
        triggerStaggerSeconds() * 1000.0f,
        fb_width,
        fb_height,
        static_cast<unsigned int>(stats.local_light_count),
        stats.cpu_fallback,
        stats.active,
        particle_stats.sync_effect_bindings_ms,
        particle_stats.simulation_ms,
        particle_stats.packing_ms,
        particle_stats.additive_grouping_ms,
        particle_stats.alpha_sort_ms,
        particle_stats.distortion_sort_ms,
        particle_stats.draw_submission_ms,
        particle_stats.alpha_collect_ms,
        particle_stats.alpha_sort_only_ms,
        particle_stats.alpha_span_ms,
        particle_stats.distortion_collect_ms,
        particle_stats.distortion_sort_only_ms,
        particle_stats.distortion_span_ms,
        particle_stats.effect_binding_updates,
        particle_stats.simulated_emitters,
        particle_stats.visible_emitters,
        particle_stats.culled_emitters,
        particle_stats.submitted_emitters,
        particle_stats.simulated_particles,
        particle_stats.packed_particles,
        particle_stats.culled_particles,
        particle_stats.ground_collision_particles,
        particle_stats.additive_batches,
        particle_stats.alpha_batches,
        particle_stats.distortion_batches,
        particle_stats.additive_particles,
        particle_stats.alpha_particles,
        particle_stats.distortion_particles,
        particle_stats.additive_draw_calls,
        particle_stats.alpha_draw_calls,
        particle_stats.distortion_draw_calls,
        particle_stats.alpha_sorted_particles,
        particle_stats.distortion_sorted_particles,
        particle_stats.alpha_invalid_depth_particles,
        particle_stats.distortion_invalid_depth_particles,
        particle_stats.pre_particle_scene_sample_draws,
        particle_stats.post_particle_scene_sample_draws,
        particle_stats.scene_color_copy,
        particle_stats.post_particle_scene_color_copy,
        particle_stats.alpha_half_res);

    perf_log_elapsed_ = 0.0f;
    perf_frame_time_sum_ = 0.0f;
    perf_frame_time_max_ = 0.0f;
    perf_frame_count_ = 0u;
  }

  void updateExplosions() {
    for (auto& explosion : explosions_) {
      if (time_ >= explosion.next_trigger_time) {
        triggerExplosionPrefab(*world, explosion.controller, time_);
        explosion.last_trigger_time = time_;
        do {
          explosion.next_trigger_time += replay_period_seconds_;
        } while (time_ >= explosion.next_trigger_time);
      }
      updateExplosionPrefab(*world, explosion.controller, time_);
    }
  }

  void disableExplosionEmitter(ecs::Entity& entity) {
    if (!entity.isValid()) {
      return;
    }
    particles::setEffectPlayback(*world, entity, false, false);
    if (world->has<components::VisibilityComponent>(entity)) {
      world->get<components::VisibilityComponent>(entity).visible = false;
    }
    entity = {};
  }

  void disableExplosionLight(ExplosionPrefabController& controller) {
    if (world->isAlive(controller.light) &&
        world->has<components::LightComponent>(controller.light)) {
      auto& light = world->get<components::LightComponent>(controller.light);
      light.intensity = 0.0f;
      light.range = controller.light_off_range;
    }
    if (world->has<components::VisibilityComponent>(controller.light)) {
      world->get<components::VisibilityComponent>(controller.light).visible = false;
    }
    controller.light_active = false;
    controller.light = {};
  }

  void applyLayerSelection(ExplosionPrefabController& controller) {
    if (!isLayerEnabled(enabled_layers_, kLayerFlash)) {
      disableExplosionEmitter(controller.flash);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerFireball)) {
      disableExplosionEmitter(controller.fireball);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerHeat)) {
      disableExplosionEmitter(controller.heat);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerCoreFlipbook)) {
      disableExplosionEmitter(controller.core_flipbook);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerSmokeFlipbook)) {
      disableExplosionEmitter(controller.smoke_flipbook);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerEmbers)) {
      disableExplosionEmitter(controller.embers);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerShockRing)) {
      disableExplosionEmitter(controller.shock_ring);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerDebris)) {
      disableExplosionEmitter(controller.debris);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerDustRing)) {
      disableExplosionEmitter(controller.dust_ring);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerSmoke)) {
      disableExplosionEmitter(controller.smoke);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerScorch)) {
      disableExplosionEmitter(controller.scorch);
    }
    if (!isLayerEnabled(enabled_layers_, kLayerLight)) {
      disableExplosionLight(controller);
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

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                 .environment_map = environment_map_,
                                 .intensity = 0.16f,
                                 .draw_skybox = false,
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
                        .color = {0.82f, 0.86f, 1.0f, 1.0f},
                        .intensity = 0.32f,
                        .casts_shadows = false,
                    });
  }

  void spawnCamera() {
    const float half_extent = sceneHalfExtent();
    const glm::vec3 target(0.0f, 2.0f, 0.0f);
    const glm::vec3 eye(0.0f,
                        10.0f + half_extent * 0.30f,
                        22.0f + half_extent * 1.20f);
    const LookAngles look = lookAnglesToTarget(eye, target);

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;

    components::TransformComponent camera_transform{};
    camera_transform.setPosition(toMath(eye));
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));

    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.05f;
    camera_component.far_clip = std::max(220.0f, 140.0f + half_extent * 4.0f);
    camera_component.render_shadows = false;
    camera_component.is_primary = true;

    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, camera_component);
  }

  void spawnExplosions() {
    if (prefab_registry == nullptr ||
        !registerExplosionPrefabPackage(*prefab_registry)) {
      spdlog::error("Explosion stress failed to register the explosion prefab package");
      return;
    }

    explosions_.clear();
    explosions_.reserve(static_cast<std::size_t>(explosion_count_));

    const int width = gridWidth();
    const int depth = gridDepth();
    const float half_width = static_cast<float>(width - 1) * 0.5f;
    const float half_depth = static_cast<float>(depth - 1) * 0.5f;
    const float trigger_stagger = triggerStaggerSeconds();

    for (int index = 0; index < explosion_count_; ++index) {
      const int x = index % width;
      const int z = index / width;
      const math::Vec3 position{
          (static_cast<float>(x) - half_width) * kExplosionSpacing,
          0.0f,
          (static_cast<float>(z) - half_depth) * kExplosionSpacing,
      };

      auto controller = instantiateExplosionPrefabController(
          *world,
          *prefab_registry,
          prefabs::PrefabInstantiateDesc{
              .name = "Explosion " + std::to_string(index + 1),
              .transform = makeTransform(position),
          });
      if (!controller.has_value()) {
        spdlog::error("Explosion stress failed to instantiate controller {}", index + 1);
        continue;
      }
      applyLayerSelection(*controller);

      explosions_.push_back(ExplosionStressItem{
          .controller = *controller,
          .next_trigger_time = 0.8f + static_cast<float>(index) * trigger_stagger,
          .last_trigger_time = -1000.0f,
      });
    }

    const ExplosionPrefabPackageDebugInfo debug_info =
        getExplosionPrefabPackageDebugInfo();
    spdlog::info("Explosion stress flipbooks: core={} smoke={}",
                 explosionFlipbookTextureSourceName(debug_info.core_flipbook_source),
                 explosionFlipbookTextureSourceName(debug_info.smoke_flipbook_source));
  }

  std::string world_mesh_;
  std::string environment_map_;
  ecs::Entity camera_entity_{};
  std::vector<ExplosionStressItem> explosions_{};
  int explosion_count_ = kDefaultExplosionCount;
  float replay_period_seconds_ = kDefaultReplayPeriod;
  bool log_stats_ = false;
  uint32_t enabled_layers_ = kAllExplosionLayers;
  float time_ = 0.0f;
  float perf_log_elapsed_ = 0.0f;
  float perf_frame_time_sum_ = 0.0f;
  float perf_frame_time_max_ = 0.0f;
  std::uint32_t perf_frame_count_ = 0u;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  karma::app::EngineApp engine;

  karma::demo::ExplosionStressOptions options{};
  options.log_stats = karma::demo::envFlagEnabled("KARMA_EXPLOSION_STRESS_STATS");
  if (const char* disabled_layers = std::getenv("KARMA_EXPLOSION_STRESS_DISABLE")) {
    std::string unknown_layer;
    if (!karma::demo::disableExplosionLayers(disabled_layers,
                                             options.enabled_layers,
                                             unknown_layer)) {
      spdlog::error("Unknown explosion layer '{}'", unknown_layer);
      return 1;
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--explosions" && i + 1 < argc) {
      options.explosion_count = std::max(std::atoi(argv[++i]), 1);
    } else if (arg == "--period" && i + 1 < argc) {
      options.replay_period_seconds =
          std::max(static_cast<float>(std::atof(argv[++i])), 0.2f);
    } else if (arg == "--disable" && i + 1 < argc) {
      std::string unknown_layer;
      if (!karma::demo::disableExplosionLayers(argv[++i],
                                               options.enabled_layers,
                                               unknown_layer)) {
        spdlog::error("Unknown explosion layer '{}'", unknown_layer);
        return 1;
      }
    } else if (arg == "--stats") {
      options.log_stats = true;
    } else if (arg == "--help" || arg == "-h") {
      spdlog::info(
          "Usage: {} [--explosions N] [--period SECONDS] [--stats] [--disable layers]",
          argv[0]);
      spdlog::info("  --explosions N  Explosion prefab controller count. Range: 1-{}.",
                   karma::demo::kMaxExplosionCount);
      spdlog::info("  --period SECONDS  Per-controller replay period. Default: {:.2f}s.",
                   karma::demo::kDefaultReplayPeriod);
      spdlog::info("  --stats  Log ongoing perf and renderer stats.");
      spdlog::info(
          "  --disable layers  Comma-separated layers to disable. Use 'all' to disable every explosion layer.");
      spdlog::info(
          "                   Available: flash, fireball, heat, core_flipbook, smoke_flipbook, embers, shock_ring, debris, dust_ring, smoke, scorch, light");
      spdlog::info(
          "Environment: KARMA_EXPLOSION_STRESS_STATS=1 enables periodic stats logging.");
      spdlog::info(
          "Environment: KARMA_EXPLOSION_STRESS_DISABLE=heat,smoke disables specific layers.");
      return 0;
    }
  }

  options.explosion_count =
      std::clamp(options.explosion_count, 1, karma::demo::kMaxExplosionCount);

  karma::demo::ExplosionStressExample game(options);

  karma::app::EngineConfig config;
  config.window.title = "Karma Explosion Stress Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 512;
  config.shadow_pcf_radius = 1;
  config.point_shadow_max_lights = 1;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 256;
  config.forward_plus_max_local_lights =
      std::max(256, std::min(options.explosion_count * 4, 2048));
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.05f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
