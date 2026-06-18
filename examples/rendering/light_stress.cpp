#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

constexpr float kUnsafeLightSpacing = 10.0f;
constexpr float kSafeLightSpacing = 8.0f;
constexpr float kLightHeight = 3.0f;
constexpr float kSafeLightRange = 36.0f;
constexpr float kUnsafeLightRange = 9.5f;
constexpr float kReceiverScale = 1.1f;
constexpr float kCasterScale = 0.65f;
constexpr float kLightMarkerScale = 0.2f;
constexpr int kMaxSafeLightCount = 16;
constexpr float kLightAnimationSpeedScale = 2.0f;

math::Color hsvToColor(float hue_degrees, float saturation, float value) {
  const float hue = std::fmod(std::max(hue_degrees, 0.0f), 360.0f) / 60.0f;
  const int sector = static_cast<int>(std::floor(hue)) % 6;
  const float fraction = hue - std::floor(hue);
  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * fraction);
  const float t = value * (1.0f - saturation * (1.0f - fraction));

  switch (sector) {
    case 0:
      return {value, t, p, 1.0f};
    case 1:
      return {q, value, p, 1.0f};
    case 2:
      return {p, value, t, 1.0f};
    case 3:
      return {p, q, value, 1.0f};
    case 4:
      return {t, p, value, 1.0f};
    default:
      return {value, p, q, 1.0f};
  }
}

bool envFlagEnabled(const char* name) {
  if (const char* value = std::getenv(name)) {
    return value[0] != '\0' && std::string(value) != "0";
  }
  return false;
}

void registerTintMaterial(renderer::MaterialLibrary& materials,
                          const std::string& key,
                          const math::Color& color) {
  renderer::MaterialDesc material{};
  material.base_color = color;
  material.roughness = 0.62f;
  material.metallic = 0.0f;
  materials.registerMaterialDesc(key, material);
}

}  // namespace

class LocalLightProbeExample final : public app::GameInterface {
 public:
  LocalLightProbeExample(bool unsafe_stress_mode, int safe_light_count, bool log_renderer_stats)
      : unsafe_stress_mode_(unsafe_stress_mode),
        safe_light_count_(std::clamp(safe_light_count, 1, kMaxSafeLightCount)),
        log_renderer_stats_(log_renderer_stats) {}

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
    marker_mesh_ = resolveExampleAssetPath("shot.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();
    registerTintMaterial(*materials, "light_receiver", math::Color{0.82f, 0.82f, 0.82f, 1.0f});
    registerTintMaterial(*materials, "shadow_caster", math::Color{0.22f, 0.24f, 0.28f, 1.0f});

    spawnBackdrop();
    spawnLights();
    spawnReceivers();
    spawnCamera();

    spdlog::info(
        "Local light probe controls: hold RMB to look, WASD to move, Q/E vertical, Left Shift to boost");
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    elapsed_time_ += dt;
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kMoveSpeed = 12.0f;
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

    auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat cam_rot = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    const math::Vec3 forward = math::normalize(math::rotateVec(cam_rot, {0.0f, 0.0f, -1.0f}));
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
    math::Vec3 camera_pos = camera_xform.getPosition();
    camera_pos.x += (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_pos.y += (forward.y * forward_input + vertical_input) * move_speed * dt;
    camera_pos.z += (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_xform.setPosition(camera_pos);
    camera_xform.setRotation(cam_rot);

    animateProbeLights();

    if (log_renderer_stats_ && !logged_renderer_stats_ && graphics != nullptr &&
        elapsed_time_ >= 0.5f) {
      int fb_width = 0;
      int fb_height = 0;
      graphics->getFramebufferSize(fb_width, fb_height);
      const renderer::ForwardPlusStats stats = graphics->getForwardPlusStats();
      spdlog::info(
          "Renderer stats: framebuffer={}x{}, active={}, cpu_fallback={}, overflow_risk={}, tile_size={}, max_lights_per_tile={}, max_local_lights={}, tiles={}x{}, local_light_count={}",
          fb_width,
          fb_height,
          stats.active,
          stats.cpu_fallback,
          stats.overflow_risk,
          stats.tile_size,
          stats.max_lights_per_tile,
          stats.max_local_lights,
          stats.tiles_x,
          stats.tiles_y,
          stats.local_light_count);
      logged_renderer_stats_ = true;
    }
  }

  void onShutdown() override {}

 private:
  int safeGridWidth() const {
    return std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(safe_light_count_)))));
  }

  int safeGridDepth() const {
    const int width = safeGridWidth();
    return std::max(1, (safe_light_count_ + width - 1) / width);
  }

  void animateProbeLights() {
    if (unsafe_stress_mode_ || animated_lights_.empty()) {
      return;
    }

    // Keep the probe scene lively so moving point-light shadows are easy to inspect.
    for (const AnimatedLight& animated_light : animated_lights_) {
      if (!world->isAlive(animated_light.entity)) {
        continue;
      }

      const float orbit_angle =
          elapsed_time_ * (animated_light.orbit_speed * kLightAnimationSpeedScale) +
          animated_light.phase_offset;
      const float bob_angle =
          elapsed_time_ * (animated_light.orbit_speed * 1.6f * kLightAnimationSpeedScale) +
          animated_light.phase_offset * 1.7f;

      math::Vec3 position = animated_light.anchor_position;
      position.x += std::cos(orbit_angle) * animated_light.orbit_radius;
      position.z += std::sin(orbit_angle) * animated_light.orbit_radius;
      position.y += std::sin(bob_angle) * animated_light.bob_amplitude;

      auto& light_xform = world->get<components::TransformComponent>(animated_light.entity);
      light_xform.setPosition(position);
      if (world->isAlive(animated_light.marker_entity)) {
        auto& marker_xform = world->get<components::TransformComponent>(animated_light.marker_entity);
        marker_xform.setPosition(position);
      }
    }
  }

  void spawnBackdrop() {
    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
                              .mesh_key = world_mesh_,
                              .visible = true,
                              .shadow_visible = true,
                          });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                 .environment_map = environment_map_,
                                 .intensity = unsafe_stress_mode_ ? 0.04f : 0.4f,
                                 .draw_skybox = !unsafe_stress_mode_,
                             });

    if (!unsafe_stress_mode_) {
      const ecs::Entity sun = world->createEntity();
      world->setName(sun, "Sun");
      components::TransformComponent sun_xform{};
      sun_xform.setPosition({0.0f, 50.0f, 0.0f});
      sun_xform.setRotation(math::fromYawPitch(0.5f, -0.9f));
      world->add(sun, sun_xform);
      world->add(sun, components::LightComponent{
                           .type = components::LightComponent::Type::Directional,
                           .color = {1.0f, 1.0f, 1.0f, 1.0f},
                           .intensity = 0.85f,
                           .casts_shadows = false,
                           .shadow_extent = 60.0f,
                       });
    }
  }

  void spawnLights() {
    if (!unsafe_stress_mode_) {
      const int grid_width = safeGridWidth();
      const int grid_depth = safeGridDepth();
      const float half_width = static_cast<float>(grid_width - 1) * 0.5f;
      const float half_depth = static_cast<float>(grid_depth - 1) * 0.5f;
      for (int index = 0; index < safe_light_count_; ++index) {
        const int x = index % grid_width;
        const int z = index / grid_width;
        const math::Vec3 position{
            (static_cast<float>(x) - half_width) * kSafeLightSpacing,
            kLightHeight,
            (static_cast<float>(z) - half_depth) * kSafeLightSpacing,
        };
        const float hue = std::fmod(35.0f + static_cast<float>(index) * 47.0f, 360.0f);
        const math::Color color = hsvToColor(hue, 0.62f, 1.0f);
        const std::string marker_material_key = "light_marker_" + std::to_string(index);
        registerTintMaterial(*materials, marker_material_key, color);

        const ecs::Entity light = world->createEntity();
        world->setName(light, "Probe Light " + std::to_string(index));
        components::TransformComponent light_xform{};
        light_xform.setPosition(position);
        world->add(light, light_xform);
        world->add(light, components::LightComponent{
                              .type = components::LightComponent::Type::Point,
                              .color = color,
                              .intensity = 28.0f,
                              .range = kSafeLightRange,
                              .casts_shadows = true,
                          });

        const ecs::Entity marker = world->createEntity();
        world->setName(marker, "Probe Light Marker " + std::to_string(index));
        components::TransformComponent marker_xform{};
        marker_xform.setPosition(position);
        marker_xform.setScale({kLightMarkerScale, kLightMarkerScale, kLightMarkerScale});
        world->add(marker, marker_xform);
        world->add(marker, components::MeshComponent{
                                .mesh_key = marker_mesh_,
                                .materials = {components::MeshMaterialBinding{
                                    .slot = 0,
                                    .material_key = marker_material_key,
                                }},
                                .visible = true,
                                .shadow_visible = false,
                            });

        animated_lights_.push_back(AnimatedLight{
            .entity = light,
            .marker_entity = marker,
            .anchor_position = position,
            .phase_offset = static_cast<float>(index) * 0.73f,
            .orbit_radius = 0.9f + 0.18f * static_cast<float>(index % 3),
            .bob_amplitude = 0.35f + 0.06f * static_cast<float>(index % 4),
            .orbit_speed = 0.7f + 0.08f * static_cast<float>(index % 5),
        });
      }
      return;
    }

    const int grid_width = 14;
    const int grid_depth = 14;
    const float light_range = kUnsafeLightRange;
    const float base_intensity = 64.0f;
    const float half_width = static_cast<float>(grid_width - 1) * 0.5f;
    const float half_depth = static_cast<float>(grid_depth - 1) * 0.5f;

    std::uint32_t light_index = 0;
    for (int z = 0; z < grid_depth; ++z) {
      for (int x = 0; x < grid_width; ++x) {
        const math::Vec3 position{
            (static_cast<float>(x) - half_width) * kUnsafeLightSpacing,
            kLightHeight,
            (static_cast<float>(z) - half_depth) * kUnsafeLightSpacing,
        };

        const float hue = static_cast<float>((x * 17 + z * 29) % 360);
        const math::Color color = hsvToColor(hue, 0.72f, 1.0f);
        const float radial_bias =
            1.0f + 0.25f * std::sin(static_cast<float>(x) * 0.55f) *
                         std::cos(static_cast<float>(z) * 0.45f);

        const ecs::Entity light = world->createEntity();
        world->setName(light, "Stress Light " + std::to_string(light_index++));
        components::TransformComponent light_xform{};
        light_xform.setPosition(position);
        world->add(light, light_xform);
        world->add(light, components::LightComponent{
                              .type = components::LightComponent::Type::Point,
                              .color = color,
                              .intensity = base_intensity * radial_bias,
                              .range = light_range,
                              .casts_shadows = false,
                          });

        const ecs::Entity marker = world->createEntity();
        world->setName(marker, "Stress Marker " + std::to_string(light_index));
        components::TransformComponent marker_xform{};
        marker_xform.setPosition(position);
        marker_xform.setScale({0.10f, 0.10f, 0.10f});
        world->add(marker, marker_xform);
        world->add(marker, components::MeshComponent{
                                .mesh_key = marker_mesh_,
                                .visible = true,
                            });
      }
    }
  }

  void spawnReceivers() {
    if (!unsafe_stress_mode_) {
      const int grid_width = safeGridWidth();
      const int grid_depth = safeGridDepth();
      const float half_width = static_cast<float>(grid_width - 1) * 0.5f;
      const float half_depth = static_cast<float>(grid_depth - 1) * 0.5f;
      for (int index = 0; index < safe_light_count_; ++index) {
        const int x = index % grid_width;
        const int z = index / grid_width;
        const math::Vec3 receiver_position{
            (static_cast<float>(x) - half_width) * kSafeLightSpacing,
            1.1f,
            (static_cast<float>(z) - half_depth) * kSafeLightSpacing,
        };
        const float offset_x = (x % 2 == 0) ? 1.65f : -1.65f;
        const float offset_z = (z % 2 == 0) ? 0.95f : -0.95f;
        const math::Vec3 caster_position{
            receiver_position.x + offset_x,
            0.7f,
            receiver_position.z + offset_z,
        };

        const ecs::Entity receiver = world->createEntity();
        world->setName(receiver, "Probe Receiver " + std::to_string(index));
        components::TransformComponent receiver_xform{};
        receiver_xform.setPosition(receiver_position);
        receiver_xform.setScale({kReceiverScale, kReceiverScale, kReceiverScale});
        world->add(receiver, receiver_xform);
        world->add(receiver, components::MeshComponent{
                                  .mesh_key = marker_mesh_,
                                  .materials = {components::MeshMaterialBinding{
                                      .slot = 0,
                                      .material_key = "light_receiver",
                                  }},
                                  .visible = true,
                                  .shadow_visible = false,
                              });

        const ecs::Entity caster = world->createEntity();
        world->setName(caster, "Probe Caster " + std::to_string(index));
        components::TransformComponent caster_xform{};
        caster_xform.setPosition(caster_position);
        caster_xform.setScale({kCasterScale, kCasterScale, kCasterScale});
        world->add(caster, caster_xform);
        world->add(caster, components::MeshComponent{
                                  .mesh_key = marker_mesh_,
                                  .materials = {components::MeshMaterialBinding{
                                      .slot = 0,
                                      .material_key = "shadow_caster",
                                  }},
                                  .visible = true,
                              });
      }
      return;
    }

    const int grid_width = 14;
    const int grid_depth = 14;
    if (grid_width < 2 || grid_depth < 2) {
      return;
    }
    const float half_width = static_cast<float>(grid_width - 2) * 0.5f;
    const float half_depth = static_cast<float>(grid_depth - 2) * 0.5f;

    std::uint32_t receiver_index = 0;
    for (int z = 0; z < grid_depth - 1; ++z) {
      for (int x = 0; x < grid_width - 1; ++x) {
        const math::Vec3 position{
            (static_cast<float>(x) - half_width) * kUnsafeLightSpacing + kUnsafeLightSpacing * 0.5f,
            0.55f,
            (static_cast<float>(z) - half_depth) * kUnsafeLightSpacing + kUnsafeLightSpacing * 0.5f,
        };

        const ecs::Entity receiver = world->createEntity();
        world->setName(receiver, "Receiver " + std::to_string(receiver_index++));
        components::TransformComponent receiver_xform{};
        receiver_xform.setPosition(position);
        receiver_xform.setScale({kReceiverScale, kReceiverScale, kReceiverScale});
        world->add(receiver, receiver_xform);
        world->add(receiver, components::MeshComponent{
                                  .mesh_key = marker_mesh_,
                                  .materials = {components::MeshMaterialBinding{
                                      .slot = 0,
                                      .material_key = "light_receiver",
                                  }},
                                  .visible = true,
                              });
      }
    }
  }

  void spawnCamera() {
    math::Vec3 eye{0.0f, 8.0f, 28.0f};
    float fov_y_degrees = 60.0f;
    math::Vec3 center{0.0f, unsafe_stress_mode_ ? kLightHeight : 1.5f, 0.0f};
    if (!unsafe_stress_mode_) {
      const int grid_width = safeGridWidth();
      const int grid_depth = safeGridDepth();
      const float max_span =
          std::max(static_cast<float>(grid_width), static_cast<float>(grid_depth)) * kSafeLightSpacing;
      const float depth_span = std::max(grid_depth - 1, 0) * kSafeLightSpacing;
      eye = {0.0f, 6.5f + 0.18f * max_span, std::max(20.0f, max_span * 1.5f)};
      fov_y_degrees = 72.0f;
      center.z = depth_span * 0.25f;
    }
    const math::Vec3 direction = math::normalize(math::Vec3{
        center.x - eye.x,
        center.y - eye.y,
        center.z - eye.z,
    });

    camera_yaw_ = std::atan2(direction.x, -direction.z);
    target_camera_yaw_ = camera_yaw_;
    camera_pitch_ = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
    target_camera_pitch_ = camera_pitch_;

    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Camera");
    camera_entity_ = camera;
    components::TransformComponent camera_xform{};
    camera_xform.setPosition(eye);
    camera_xform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera, camera_xform);
    components::CameraComponent camera_component{};
    camera_component.render_shadows = !unsafe_stress_mode_;
    camera_component.fov_y_degrees = fov_y_degrees;
    camera_component.near_clip = unsafe_stress_mode_ ? 0.05f : 0.03f;
    camera_component.far_clip = 300.0f;
    camera_component.is_primary = true;
    world->add(camera, camera_component);
  }

  std::string world_mesh_;
  std::string marker_mesh_;
  std::string environment_map_;
  struct AnimatedLight {
    ecs::Entity entity{};
    ecs::Entity marker_entity{};
    math::Vec3 anchor_position{};
    float phase_offset = 0.0f;
    float orbit_radius = 1.0f;
    float bob_amplitude = 0.35f;
    float orbit_speed = 0.8f;
  };
  std::vector<AnimatedLight> animated_lights_;
  ecs::Entity camera_entity_{};
  bool unsafe_stress_mode_ = false;
  int safe_light_count_ = 1;
  bool log_renderer_stats_ = false;
  float elapsed_time_ = 0.0f;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  bool logged_renderer_stats_ = false;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  karma::app::EngineApp engine;

  bool unsafe_stress_mode = karma::demo::envFlagEnabled("KARMA_LIGHT_STRESS_UNSAFE");
  bool log_renderer_stats = karma::demo::envFlagEnabled("KARMA_LIGHT_PROBE_STATS");
  int safe_light_count = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--unsafe") {
      unsafe_stress_mode = true;
    } else if (arg == "--stats") {
      log_renderer_stats = true;
    } else if (arg == "--lights" && i + 1 < argc) {
      safe_light_count = std::max(std::atoi(argv[++i]), 1);
    } else if (arg == "--help" || arg == "-h") {
      spdlog::info("Usage: {} [--lights N] [--stats] [--unsafe]", argv[0]);
      spdlog::info("  --lights N  Safe-mode shadowed light count. Range: 1-{}. Default: 1.",
                   karma::demo::kMaxSafeLightCount);
      spdlog::info("  --stats  Log renderer Forward+ stats after startup.");
      spdlog::info("  --unsafe  Enable the aggressive Forward+ light stress profile without point-light shadows.");
      spdlog::info(
          "Environment: KARMA_LIGHT_STRESS_UNSAFE=1 enables unsafe mode. KARMA_LIGHT_PROBE_STATS=1 enables stats logging.");
      return 0;
    }
  }
  safe_light_count = std::clamp(safe_light_count, 1, karma::demo::kMaxSafeLightCount);

  karma::demo::LocalLightProbeExample game(unsafe_stress_mode, safe_light_count, log_renderer_stats);

  karma::app::EngineConfig config;
  config.window.title = unsafe_stress_mode ? "Karma Light Stress Example" : "Karma Local Light Probe";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = unsafe_stress_mode ? 2048 : 512;
  config.point_shadow_max_lights = unsafe_stress_mode ? 2 : safe_light_count;
  config.shadow_pcf_radius = unsafe_stress_mode ? 0 : 1;
  config.point_shadow_constant_bias = unsafe_stress_mode ? 0.0012f : 0.0035f;
  config.point_shadow_slope_bias_scale = unsafe_stress_mode ? 2.0f : 3.0f;
  config.point_shadow_normal_bias_scale = unsafe_stress_mode ? 1.5f : 3.0f;
  config.point_shadow_receiver_bias_scale = unsafe_stress_mode ? 0.35f : 0.5f;
  // Keep the default probe profile below the renderer's Forward+ buffer cap on
  // common high-resolution displays so the sample remains usable on desktop GPUs.
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.forward_plus_max_local_lights = 512;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;

  if (unsafe_stress_mode) {
    config.forward_plus_tile_size = 8;
    config.forward_plus_max_lights_per_tile = 512;
    config.forward_plus_max_local_lights = 4096;
    spdlog::warn("Unsafe light stress mode enabled. This may destabilize high-resolution or low-VRAM systems.");
  } else {
    spdlog::info(
        "Local light probe enabled with {} moving shadowed light(s). Pass --lights N to scale up or --unsafe for the aggressive profile.",
        safe_light_count);
  }

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
