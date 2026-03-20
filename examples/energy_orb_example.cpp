#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace karma::demo {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kAtlasFrameSize = 128;
constexpr int kAtlasFrameCount = 6;
constexpr math::Vec3 kOrbBasePosition{0.0f, 1.85f, 0.0f};
constexpr math::Color kOrbAccentColor{0.18f, 1.0f, 0.28f, 1.0f};

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float smoothStep01(float value) {
  const float t = saturate(value);
  return t * t * (3.0f - 2.0f * t);
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0f));
}

std::uint32_t hashUint(std::uint32_t value) {
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  value ^= value >> 16u;
  return value;
}

float hash01(std::uint32_t value) {
  return static_cast<float>(hashUint(value) & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

Vec2 polarPoint(float angle_radians, float radius) {
  return {
      std::cos(angle_radians) * radius,
      std::sin(angle_radians) * radius,
  };
}

float distanceToSegmentSquared(Vec2 point, Vec2 a, Vec2 b) {
  const float ab_x = b.x - a.x;
  const float ab_y = b.y - a.y;
  const float ap_x = point.x - a.x;
  const float ap_y = point.y - a.y;
  const float ab_len_sq = ab_x * ab_x + ab_y * ab_y;
  if (ab_len_sq <= 1.0e-8f) {
    return ap_x * ap_x + ap_y * ap_y;
  }
  const float t = std::clamp((ap_x * ab_x + ap_y * ab_y) / ab_len_sq, 0.0f, 1.0f);
  const float dx = ap_x - ab_x * t;
  const float dy = ap_y - ab_y * t;
  return dx * dx + dy * dy;
}

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

void destroyTextureIfValid(renderer::GraphicsDevice* graphics, renderer::TextureId& texture) {
  if (graphics == nullptr || texture == renderer::kInvalidTexture) {
    return;
  }
  graphics->destroyTexture(texture);
  texture = renderer::kInvalidTexture;
}

std::vector<std::uint8_t> buildOrbCoreAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = static_cast<float>(frame) / static_cast<float>(std::max(frame_count, 1));
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        if (radius > 1.0f) {
          continue;
        }

        const float angle = std::atan2(py, px);
        const float core = std::pow(saturate(1.0f - radius / 0.92f), 0.55f);
        const float swirl_a =
            0.5f + 0.5f * std::sin(angle * 4.0f + radius * 11.0f - t * 9.0f);
        const float swirl_b =
            0.5f + 0.5f * std::cos(angle * 7.0f - radius * 8.5f + t * 7.5f);
        const float turbulence =
            0.5f + 0.5f * std::sin((px * 6.0f + py * 4.0f + t * 5.0f) * kPi) *
                       std::cos((px * 3.0f - py * 5.0f - t * 4.5f) * kPi);
        const float plasma = core * (0.28f + 0.38f * swirl_a + 0.24f * swirl_b + 0.22f * turbulence);
        const float hotspot =
            std::exp(-((px * 0.74f) * (px * 0.74f) + (py * 1.05f) * (py * 1.05f)) * 7.5f);
        const float filament = smoothStep01(
            saturate(1.0f - std::abs(std::sin(angle * 3.5f + radius * 10.0f - t * 10.5f)) *
                                 (0.45f + radius * 0.8f)));

        const float intensity = saturate(plasma + hotspot * 0.65f + filament * core * 0.18f);
        const float alpha = saturate(intensity * (0.75f + core * 0.35f));
        const float value = saturate(intensity * 0.72f + hotspot * 0.28f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(value);
        pixels[dst_index + 1u] = toByte(value);
        pixels[dst_index + 2u] = toByte(value);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildOrbArcAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const std::uint32_t frame_seed = 1009u + static_cast<std::uint32_t>(frame) * 917u;
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const Vec2 point{
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f,
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f,
        };
        const float radius = std::sqrt(point.x * point.x + point.y * point.y);
        if (radius > 1.05f) {
          continue;
        }

        float arc_intensity = 0.0f;
        for (int arc_index = 0; arc_index < 5; ++arc_index) {
          const std::uint32_t base_seed =
              frame_seed + static_cast<std::uint32_t>(arc_index) * 131u;
          const float a0 = hash01(base_seed + 1u) * kPi * 2.0f;
          const float a1 = a0 + (hash01(base_seed + 2u) * 1.1f - 0.55f);
          const float r0 = 0.10f + hash01(base_seed + 3u) * 0.20f;
          const float r1 = 0.48f + hash01(base_seed + 4u) * 0.26f;
          const float mid_angle = 0.5f * (a0 + a1) + (hash01(base_seed + 5u) * 0.9f - 0.45f);
          const float mid_radius = 0.26f + hash01(base_seed + 6u) * 0.34f;

          const Vec2 p0 = polarPoint(a0, r0);
          const Vec2 p1 = polarPoint(mid_angle, mid_radius);
          const Vec2 p2 = polarPoint(a1, r1);

          const float dist0 = distanceToSegmentSquared(point, p0, p1);
          const float dist1 = distanceToSegmentSquared(point, p1, p2);
          const float line = std::exp(-std::min(dist0, dist1) * 180.0f);
          const float endpoint =
              std::exp(-((point.x - p2.x) * (point.x - p2.x) +
                         (point.y - p2.y) * (point.y - p2.y)) *
                       90.0f);
          arc_intensity += line * (0.85f + endpoint * 0.45f);
        }

        const float central_glow = std::exp(-(point.x * point.x + point.y * point.y) * 7.5f);
        const float alpha = saturate(arc_intensity * 0.82f + central_glow * 0.08f);
        if (alpha <= 0.002f) {
          continue;
        }

        const float value = saturate(arc_intensity * 0.76f + central_glow * 0.24f);
        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(value);
        pixels[dst_index + 1u] = toByte(value);
        pixels[dst_index + 2u] = toByte(value);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildOrbHaloAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = static_cast<float>(frame) / static_cast<float>(std::max(frame_count, 1));
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        if (radius > 1.1f) {
          continue;
        }

        const float ring_radius = 0.54f + 0.05f * std::sin(t * kPi * 2.0f + radius * 2.5f);
        const float ring_width = 0.16f;
        const float halo =
            saturate(1.0f - std::abs(radius - ring_radius) / std::max(ring_width, 0.01f));
        const float outer = std::pow(saturate(1.0f - radius / 1.05f), 1.8f) * 0.36f;
        const float wisps =
            0.5f + 0.5f * std::sin((px * 4.5f - py * 6.5f + t * 4.8f) * kPi) *
                       std::cos((px * 7.0f + py * 3.8f - t * 4.0f) * kPi);
        const float alpha = saturate(halo * (0.34f + wisps * 0.18f) + outer * 0.12f);
        const float value = saturate(alpha * 1.1f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(value);
        pixels[dst_index + 1u] = toByte(value);
        pixels[dst_index + 2u] = toByte(value);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildOrbDistortionAtlas(int frame_size, int frame_count) {
  const int width = frame_size * frame_count;
  const int height = frame_size;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u,
                                   0u);

  for (int frame = 0; frame < frame_count; ++frame) {
    const float t = static_cast<float>(frame) / static_cast<float>(std::max(frame_count, 1));
    for (int y = 0; y < frame_size; ++y) {
      for (int x = 0; x < frame_size; ++x) {
        const float px =
            (static_cast<float>(x) + 0.5f) / static_cast<float>(frame_size) * 2.0f - 1.0f;
        const float py =
            1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(frame_size) * 2.0f;
        const float radius = std::sqrt(px * px + py * py);
        if (radius > 1.0f) {
          continue;
        }

        const float angle = std::atan2(py, px);
        const float body = std::pow(saturate(1.0f - radius / 0.98f), 0.72f);
        const float turbulence =
            0.5f + 0.5f * std::sin((px * 3.4f + py * 5.1f + t * 4.2f) * kPi) *
                       std::cos((px * 5.8f - py * 3.6f - t * 3.3f) * kPi);
        const float inner_swirl =
            0.5f + 0.5f * std::sin(angle * 3.0f - radius * 8.5f + t * kPi * 1.8f);
        const float alpha = saturate(body * (0.78f + turbulence * 0.18f + inner_swirl * 0.12f));

        Vec2 radial{radius > 1.0e-4f ? px / radius : 0.0f,
                    radius > 1.0e-4f ? py / radius : -1.0f};
        Vec2 tangent{-radial.y, radial.x};
        const float flow_tangent =
            std::sin(angle * 4.2f - radius * 7.0f + t * kPi * 2.4f) * 0.76f +
            std::cos(angle * 6.5f + radius * 5.0f - t * kPi * 1.7f) * 0.24f;
        const float flow_radial =
            std::cos(angle * 2.7f + radius * 9.2f - t * kPi * 2.0f) * 0.42f +
            std::sin(angle * 5.4f - radius * 6.4f + t * kPi * 1.2f) * 0.18f;

        const float vx = tangent.x * flow_tangent + radial.x * flow_radial;
        const float vy = tangent.y * flow_tangent + radial.y * flow_radial;

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(0.5f + 0.5f * vx);
        pixels[dst_index + 1u] = toByte(0.5f + 0.5f * vy);
        pixels[dst_index + 2u] = 0u;
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

void registerParticleEffects(particles::ParticleLibrary& particle_effects,
                             renderer::TextureId core_texture,
                             renderer::TextureId arc_texture,
                             renderer::TextureId halo_texture,
                             renderer::TextureId distortion_texture) {
  particle_effects.clear();
  particle_effects.clearTextureAliases();
  particle_effects.registerTextureAliases({
      {"orb_core_atlas", core_texture},
      {"orb_arc_atlas", arc_texture},
      {"orb_halo_atlas", halo_texture},
      {"orb_distortion_atlas", distortion_texture},
  });
  particle_effects.registerEffectFiles({
      {"energy_orb_core", resolveExampleAssetPath("particles/energy_orb_core.kpeffect")},
      {"energy_orb_arcs", resolveExampleAssetPath("particles/energy_orb_arcs.kpeffect")},
      {"energy_orb_halo", resolveExampleAssetPath("particles/energy_orb_halo.kpeffect")},
      {"energy_orb_distortion",
       resolveExampleAssetPath("particles/energy_orb_distortion.kpeffect")},
  });
}

}  // namespace

class EnergyOrbExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);
    input->bindKey("toggle_orb", platform::Key::Space, input::Trigger::Pressed);
    input->bindKey("restart_orb", platform::Key::R, input::Trigger::Pressed);

    const std::string world_mesh = resolveExampleAssetPath("world.glb").string();
    const std::string environment_map =
        resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    const std::vector<std::uint8_t> core_atlas = buildOrbCoreAtlas(kAtlasFrameSize, kAtlasFrameCount);
    core_texture_ = graphics->createTextureRGBA8(kAtlasFrameSize * kAtlasFrameCount,
                                                 kAtlasFrameSize,
                                                 core_atlas.data());

    const std::vector<std::uint8_t> arc_atlas = buildOrbArcAtlas(kAtlasFrameSize, kAtlasFrameCount);
    arc_texture_ = graphics->createTextureRGBA8(kAtlasFrameSize * kAtlasFrameCount,
                                                kAtlasFrameSize,
                                                arc_atlas.data());

    const std::vector<std::uint8_t> halo_atlas = buildOrbHaloAtlas(kAtlasFrameSize, kAtlasFrameCount);
    halo_texture_ = graphics->createTextureRGBA8(kAtlasFrameSize * kAtlasFrameCount,
                                                 kAtlasFrameSize,
                                                 halo_atlas.data());

    const std::vector<std::uint8_t> distortion_atlas =
        buildOrbDistortionAtlas(kAtlasFrameSize, kAtlasFrameCount);
    distortion_texture_ = graphics->createTextureRGBA8(kAtlasFrameSize * kAtlasFrameCount,
                                                       kAtlasFrameSize,
                                                       distortion_atlas.data());

    if (particle_effects != nullptr) {
      registerParticleEffects(*particle_effects,
                              core_texture_,
                              arc_texture_,
                              halo_texture_,
                              distortion_texture_);
    }

    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_key = world_mesh});

    const ecs::Entity environment_entity = world->createEntity();
    world->setName(environment_entity, "Environment");
    world->add(environment_entity,
               components::EnvironmentComponent{
                   .environment_map = environment_map,
                   .intensity = 0.18f,
                   .draw_skybox = false,
               });

    const ecs::Entity sun_entity = world->createEntity();
    world->setName(sun_entity, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 40.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.48f, -0.82f));
    world->add(sun_entity, sun_transform);
    world->add(sun_entity,
               components::LightComponent{
                   .type = components::LightComponent::Type::Directional,
                   .color = {0.80f, 0.84f, 1.0f, 1.0f},
                   .intensity = 0.36f,
                   .shadow_extent = 50.0f,
               });

    const auto orb = prefabs::instantiateEffectPrefab(
        *world,
        graphics,
        resolveExampleAssetPath("prefabs/energy_orb.kprefab"),
        prefabs::EffectPrefabInstantiateDesc{
            .name = "Energy Orb",
            .transform = makeTransform(kOrbBasePosition),
            .color_overrides = {{"accent", kOrbAccentColor}},
        });
    if (orb.has_value()) {
      orb_root_entity_ = orb->root;
    }

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    camera_yaw_ = 0.0f;
    target_camera_yaw_ = 0.0f;
    camera_pitch_ = -0.12f;
    target_camera_pitch_ = -0.12f;
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 2.35f, 6.6f});
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_,
               components::CameraComponent{
                   .near_clip = 0.05f,
                   .far_clip = 120.0f,
                   .is_primary = true,
               });
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    if (world->isAlive(camera_entity_)) {
      const float look_sensitivity = 0.0008f;
      const float move_speed = 14.0f;
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
      const math::Vec3 right = math::normalize(math::cross(forward, up));

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

    if (input->actionPressed("toggle_orb")) {
      orb_enabled_ = !orb_enabled_;
      prefabs::setPrefabPlayback(*world, orb_root_entity_, orb_enabled_);
    }

    if (input->actionPressed("restart_orb")) {
      prefabs::restartPrefab(*world, orb_root_entity_);
      orb_enabled_ = true;
      prefabs::setPrefabPlayback(*world, orb_root_entity_, true);
    }
  }

  void onShutdown() override {
    destroyTextureIfValid(graphics, core_texture_);
    destroyTextureIfValid(graphics, arc_texture_);
    destroyTextureIfValid(graphics, halo_texture_);
    destroyTextureIfValid(graphics, distortion_texture_);
  }

 private:
  ecs::Entity orb_root_entity_{};
  ecs::Entity camera_entity_{};
  renderer::TextureId core_texture_ = renderer::kInvalidTexture;
  renderer::TextureId arc_texture_ = renderer::kInvalidTexture;
  renderer::TextureId halo_texture_ = renderer::kInvalidTexture;
  renderer::TextureId distortion_texture_ = renderer::kInvalidTexture;
  bool orb_enabled_ = true;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::EnergyOrbExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Energy Orb Example";
  config.window.width = 1600;
  config.window.height = 900;
  config.vsync = true;
  config.generate_mipmaps = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0009f;
  config.shadow_pcf_radius = 1;
  config.local_light_distance_damping = 0.05f;
  config.local_light_range_falloff_exponent = 1.35f;
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
