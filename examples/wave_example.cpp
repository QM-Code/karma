#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace karma::demo {

namespace {

constexpr math::Vec3 kWaveCenter{0.0f, 2.2f, -5.3f};
constexpr float kWaveRadius = 5.7f;
constexpr math::Color kWaveColor{0.18f, 0.82f, 1.0f, 1.0f};
constexpr float kWaveCameraAspect = 16.0f / 9.0f;
constexpr float kWaveOverlayDepth = 0.12f;
constexpr int kWaveGlowTextureSize = 96;
constexpr int kWaveDistortionTextureSize = 96;

struct LookAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

math::Vec3 toMath(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

math::Quat toMath(const glm::quat& q) {
  return {q.x, q.y, q.z, q.w};
}

float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0f));
}

components::TransformComponent makeTransform(const math::Vec3& position) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  return transform;
}

components::TransformComponent makeScaledTransform(const math::Vec3& position,
                                                   float uniform_scale) {
  components::TransformComponent transform = makeTransform(position);
  transform.setScale({uniform_scale, uniform_scale, uniform_scale});
  return transform;
}

components::TransformComponent makeWaveTransform() {
  return makeScaledTransform(kWaveCenter, kWaveRadius);
}

components::TransformComponent makeWaveShellTransform(const math::Vec3& position) {
  return makeTransform(position);
}

components::TransformComponent makeScreenOverlayTransform(
    const components::TransformComponent& camera_transform,
    float fov_y_degrees,
    float aspect,
    float depth) {
  const float half_height = std::tan(glm::radians(fov_y_degrees) * 0.5f) * depth;
  const float half_width = half_height * aspect;
  const math::Quat rotation = camera_transform.getRotation();
  const math::Vec3 camera_position = camera_transform.getPosition();
  const math::Vec3 forward = math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));

  components::TransformComponent transform{};
  transform.setPosition(
      {camera_position.x + forward.x * depth,
       camera_position.y + forward.y * depth,
       camera_position.z + forward.z * depth});
  transform.setRotation(rotation);
  transform.setScale({half_width, half_height, 1.0f});
  return transform;
}

geometry::MeshData buildAuraQuadMesh() {
  geometry::MeshData mesh{};
  mesh.vertices = {
      {-1.0f, -1.0f, 0.0f},
      {1.0f, -1.0f, 0.0f},
      {1.0f, 1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f},
  };
  mesh.normals = {
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
  };
  mesh.uvs = {
      {0.0f, 0.0f},
      {1.0f, 0.0f},
      {1.0f, 1.0f},
      {0.0f, 1.0f},
  };
  mesh.tangents = {
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
  };
  mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  return mesh;
}

std::vector<std::uint8_t> buildWaveGlowTexture() {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWaveGlowTextureSize) *
                                       static_cast<std::size_t>(kWaveGlowTextureSize) * 4u,
                                   0u);
  for (int y = 0; y < kWaveGlowTextureSize; ++y) {
    for (int x = 0; x < kWaveGlowTextureSize; ++x) {
      const float px = (static_cast<float>(x) + 0.5f) /
                           static_cast<float>(kWaveGlowTextureSize) * 2.0f -
                       1.0f;
      const float py = 1.0f - (static_cast<float>(y) + 0.5f) /
                                  static_cast<float>(kWaveGlowTextureSize) * 2.0f;
      const float radius = std::sqrt(px * px + py * py);
      const float angle = std::atan2(py, px);
      const float base_alpha = std::exp(-radius * radius * 5.6f);
      const float core = std::exp(-radius * radius * 17.5f);
      const float shimmer = 0.84f + 0.16f * std::sin(angle * 5.0f + radius * 16.0f);
      const float alpha = saturate(base_alpha * shimmer);
      const float intensity = saturate(base_alpha * 0.72f + core * 0.58f);
      const std::uint8_t value = toByte(intensity);
      const std::size_t pixel_index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWaveGlowTextureSize) +
           static_cast<std::size_t>(x)) *
          4u;
      pixels[pixel_index + 0u] = value;
      pixels[pixel_index + 1u] = value;
      pixels[pixel_index + 2u] = value;
      pixels[pixel_index + 3u] = toByte(alpha);
    }
  }
  return pixels;
}

std::vector<std::uint8_t> buildWaveDistortionTexture() {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWaveDistortionTextureSize) *
                                       static_cast<std::size_t>(kWaveDistortionTextureSize) * 4u,
                                   0u);
  for (int y = 0; y < kWaveDistortionTextureSize; ++y) {
    for (int x = 0; x < kWaveDistortionTextureSize; ++x) {
      const float u =
          (static_cast<float>(x) + 0.5f) / static_cast<float>(kWaveDistortionTextureSize);
      const float v =
          (static_cast<float>(y) + 0.5f) / static_cast<float>(kWaveDistortionTextureSize);
      const float px = u * 2.0f - 1.0f;
      const float py = 1.0f - v * 2.0f;
      const float radius = std::sqrt(px * px + py * py);
      const float angle = std::atan2(py, px);
      const float alpha =
          saturate(std::exp(-radius * radius * 3.9f) * (1.0f - radius * 0.56f));
      const float swirl_a = std::sin(angle * 3.0f + radius * 10.0f);
      const float swirl_b = std::cos(angle * 5.0f - radius * 7.6f);
      const float flow_x = 0.5f + 0.5f * (0.74f * swirl_a + 0.26f * swirl_b);
      const float flow_y = 0.5f + 0.5f * (0.70f * swirl_b - 0.30f * swirl_a);
      const std::size_t pixel_index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWaveDistortionTextureSize) +
           static_cast<std::size_t>(x)) *
          4u;
      pixels[pixel_index + 0u] = toByte(flow_x);
      pixels[pixel_index + 1u] = toByte(flow_y);
      pixels[pixel_index + 2u] = 0u;
      pixels[pixel_index + 3u] = toByte(alpha);
    }
  }
  return pixels;
}

components::ParticleEmitterComponent buildWaveCoreGlowEmitter(std::string texture_key,
                                                             float wave_radius) {
  components::ParticleEmitterComponent emitter{};
  emitter.local_space = true;
  emitter.depth_test = true;
  emitter.blend_mode = components::ParticleBlendMode::Additive;
  emitter.use_soft_mask = false;
  emitter.texture_key = std::move(texture_key);
  emitter.loop = true;
  emitter.emit_burst_on_start = true;
  emitter.max_particles = 760u;
  emitter.burst_count = 760u;
  emitter.spawn_rate = 120.0f;
  emitter.particle_lifetime_min = 4.8f;
  emitter.particle_lifetime_max = 6.8f;
  emitter.start_size_min = 0.36f;
  emitter.start_size_max = 0.56f;
  emitter.end_size_min = 0.38f;
  emitter.end_size_max = 0.60f;
  emitter.initial_rotation_min = 0.0f;
  emitter.initial_rotation_max = 6.2831853f;
  emitter.angular_velocity_min = -0.1f;
  emitter.angular_velocity_max = 0.1f;
  emitter.spawn_shape = components::ParticleSpawnShape::SphereSurface;
  emitter.spawn_radius_min = wave_radius + 0.01f;
  emitter.spawn_radius_max = wave_radius + 0.08f;
  emitter.velocity_min = {0.0f, 0.0f, 0.0f};
  emitter.velocity_max = {0.0f, 0.0f, 0.0f};
  emitter.acceleration = {0.0f, 0.0f, 0.0f};
  emitter.drag = 0.0f;
  emitter.start_color = {1.0f, 1.0f, 1.0f, 0.34f};
  emitter.end_color = {0.92f, 0.98f, 1.0f, 0.18f};
  return emitter;
}

components::ParticleEmitterComponent buildWaveOuterGlowEmitter(std::string texture_key,
                                                              float wave_radius) {
  components::ParticleEmitterComponent emitter{};
  emitter.local_space = true;
  emitter.depth_test = true;
  emitter.blend_mode = components::ParticleBlendMode::Additive;
  emitter.use_soft_mask = false;
  emitter.texture_key = std::move(texture_key);
  emitter.loop = true;
  emitter.emit_burst_on_start = true;
  emitter.max_particles = 520u;
  emitter.burst_count = 520u;
  emitter.spawn_rate = 90.0f;
  emitter.particle_lifetime_min = 5.5f;
  emitter.particle_lifetime_max = 8.0f;
  emitter.start_size_min = 0.62f;
  emitter.start_size_max = 0.98f;
  emitter.end_size_min = 0.68f;
  emitter.end_size_max = 1.04f;
  emitter.initial_rotation_min = 0.0f;
  emitter.initial_rotation_max = 6.2831853f;
  emitter.angular_velocity_min = -0.06f;
  emitter.angular_velocity_max = 0.06f;
  emitter.spawn_shape = components::ParticleSpawnShape::SphereSurface;
  emitter.spawn_radius_min = wave_radius + 0.05f;
  emitter.spawn_radius_max = wave_radius + 0.16f;
  emitter.velocity_min = {0.0f, 0.0f, 0.0f};
  emitter.velocity_max = {0.0f, 0.0f, 0.0f};
  emitter.acceleration = {0.0f, 0.0f, 0.0f};
  emitter.drag = 0.0f;
  emitter.start_color = {kWaveColor.r, kWaveColor.g, kWaveColor.b, 0.16f};
  emitter.end_color = {kWaveColor.r, kWaveColor.g, kWaveColor.b, 0.08f};
  return emitter;
}

components::ParticleEmitterComponent buildWaveDistortionEmitter(std::string texture_key,
                                                              float wave_radius) {
  components::ParticleEmitterComponent emitter{};
  emitter.local_space = true;
  emitter.depth_test = true;
  emitter.blend_mode = components::ParticleBlendMode::Distortion;
  emitter.use_soft_mask = true;
  emitter.soft_particle_distance = 1.4f;
  emitter.distortion_strength = 22.0f;
  emitter.texture_key = std::move(texture_key);
  emitter.loop = true;
  emitter.emit_burst_on_start = true;
  emitter.max_particles = 260u;
  emitter.burst_count = 260u;
  emitter.spawn_rate = 42.0f;
  emitter.particle_lifetime_min = 4.0f;
  emitter.particle_lifetime_max = 6.0f;
  emitter.start_size_min = 0.56f;
  emitter.start_size_max = 0.84f;
  emitter.end_size_min = 0.60f;
  emitter.end_size_max = 0.92f;
  emitter.initial_rotation_min = 0.0f;
  emitter.initial_rotation_max = 6.2831853f;
  emitter.angular_velocity_min = -0.15f;
  emitter.angular_velocity_max = 0.15f;
  emitter.spawn_shape = components::ParticleSpawnShape::SphereSurface;
  emitter.spawn_radius_min = wave_radius + 0.03f;
  emitter.spawn_radius_max = wave_radius + 0.12f;
  emitter.velocity_min = {0.0f, 0.0f, 0.0f};
  emitter.velocity_max = {0.0f, 0.0f, 0.0f};
  emitter.acceleration = {0.0f, 0.0f, 0.0f};
  emitter.drag = 0.0f;
  emitter.start_color = {1.0f, 1.0f, 1.0f, 0.82f};
  emitter.end_color = {1.0f, 1.0f, 1.0f, 0.48f};
  return emitter;
}

LookAngles lookAnglesToTarget(const glm::vec3& eye, const glm::vec3& target) {
  const glm::vec3 direction = glm::normalize(target - eye);
  return {
      .yaw = std::atan2(direction.x, -direction.z),
      .pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)),
  };
}

renderer::MaterialDesc buildWaveOutsideMaterialDesc() {
  renderer::MaterialDesc desc{};
  desc.base_color = {kWaveColor.r, kWaveColor.g, kWaveColor.b, 0.55f};
  desc.emissive_color = {0.0f, 0.0f, 0.0f, 1.0f};
  desc.metallic = 0.0f;
  desc.roughness = 0.42f;
  desc.transparent = true;
  desc.double_sided = true;
  desc.depth_test = true;
  desc.depth_write = false;
  desc.blend_mode = renderer::MaterialDesc::BlendMode::Alpha;
  desc.shading_model = renderer::MaterialDesc::ShadingModel::Standard;
  return desc;
}

renderer::MaterialDesc buildWaveVolumeMaterialDesc() {
  renderer::MaterialDesc desc{};
  desc.base_color = {kWaveColor.r, kWaveColor.g, kWaveColor.b, 1.0f};
  desc.emissive_color = {0.18f, 0.44f, 0.52f, 1.0f};
  desc.metallic = 0.0f;
  desc.roughness = 0.0f;
  desc.transparent = true;
  desc.blend_mode = renderer::MaterialDesc::BlendMode::Alpha;
  desc.double_sided = true;
  desc.depth_test = false;
  desc.depth_write = false;
  desc.shading_model = renderer::MaterialDesc::ShadingModel::WaveVolume;
  desc.shell_fresnel_power = 3.6f;
  desc.shell_fresnel_strength = 1.5f;
  desc.shell_refraction_strength = 2.8f;
  desc.wave_tint_strength = 4.5f;
  desc.wave_distortion_strength = 6.5f;
  desc.wave_edge_strength = 1.45f;
  desc.wave_noise_strength = 1.35f;
  return desc;
}

renderer::MaterialDesc buildWaveOverlayMaterialDesc() {
  renderer::MaterialDesc desc{};
  desc.metallic = 0.0f;
  desc.roughness = 1.0f;
  desc.transparent = true;
  desc.blend_mode = renderer::MaterialDesc::BlendMode::Alpha;
  desc.double_sided = true;
  desc.depth_test = false;
  desc.depth_write = false;
  desc.shading_model = renderer::MaterialDesc::ShadingModel::ScreenWave;
  desc.base_color = {kWaveColor.r, kWaveColor.g, kWaveColor.b, 1.0f};
  desc.emissive_color = {0.04f, 0.11f, 0.14f, 1.0f};
  desc.wave_tint_strength = 1.45f;
  desc.wave_distortion_strength = 1.9f;
  desc.wave_edge_strength = 0.78f;
  desc.wave_noise_strength = 1.05f;
  desc.screen_center_x = 0.5f;
  desc.screen_center_y = 0.5f;
  desc.screen_radius_x = 1.5f;
  desc.screen_radius_y = 1.5f;
  return desc;
}

}  // namespace

class WaveExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    world_mesh_ = resolveExampleAssetPath("world.glb").string();
    wave_mesh_ = resolveExampleAssetPath("wave.glb").string();
    environment_map_ = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();
    aura_quad_mesh_key_ = "runtime/wave/aura_quad/mesh";
    wave_material_key_ = "runtime/wave/sphere/material";
    wave_volume_material_key_ = "runtime/wave/volume/material";
    wave_overlay_material_key_ = "runtime/wave/overlay/material";
    if (graphics != nullptr) {
      graphics->registerRuntimeMesh(aura_quad_mesh_key_, buildAuraQuadMesh());
    }

    spawnWorld();
    spawnLighting();
    spawnWave();
    spawnWaveVolume();
    spawnWaveShellParticles();
    spawnCamera();
    syncWaveAttachments();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

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

    syncWaveAttachments();
  }

  void onShutdown() override {
    if (particle_effects != nullptr) {
      particle_effects->unregisterTextureAlias("runtime/wave/glow_texture");
      particle_effects->unregisterTextureAlias("runtime/wave/distortion_texture");
    }
    if (graphics != nullptr) {
      if (wave_glow_texture_ != renderer::kInvalidTexture) {
        graphics->destroyTexture(wave_glow_texture_);
        wave_glow_texture_ = renderer::kInvalidTexture;
      }
      if (wave_distortion_texture_ != renderer::kInvalidTexture) {
        graphics->destroyTexture(wave_distortion_texture_);
        wave_distortion_texture_ = renderer::kInvalidTexture;
      }
    }
    if (graphics != nullptr && !aura_quad_mesh_key_.empty()) {
      graphics->unregisterRuntimeMesh(aura_quad_mesh_key_);
      aura_quad_mesh_key_.clear();
    }
  }

 private:
  void spawnWorld() {
    const ecs::Entity world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
                                 .mesh_key = world_mesh_,
                             });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                 .environment_map = environment_map_,
                                 .intensity = 0.28f,
                                 .draw_skybox = true,
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
                        .color = {1.0f, 0.97f, 0.92f, 1.0f},
                        .intensity = 0.74f,
                    });

    wave_light_entity_ = world->createEntity();
    world->setName(wave_light_entity_, "Wave Light");
    world->add(wave_light_entity_, makeTransform(kWaveCenter));
    world->add(wave_light_entity_, components::LightComponent{
                               .type = components::LightComponent::Type::Point,
                               .color = {kWaveColor.r, kWaveColor.g, kWaveColor.b, 1.0f},
                               .intensity = 1.8f,
                               .range = 8.5f,
                               .casts_shadows = false,
                           });
  }

  void spawnWave() {
    if (materials != nullptr) {
      materials->registerMaterialDesc(wave_material_key_, buildWaveOutsideMaterialDesc());
    }

    wave_entity_ = world->createEntity();
    world->setName(wave_entity_, "Wave Sphere");
    world->add(wave_entity_, makeWaveTransform());
    world->add(wave_entity_, components::MeshComponent{
                         .mesh_key = wave_mesh_,
                         .material_key = wave_material_key_,
                         .shadow_visible = false,
                     });
  }

  void spawnWaveVolume() {
    if (materials != nullptr) {
      materials->registerMaterialDesc(wave_volume_material_key_, buildWaveVolumeMaterialDesc());
    }

    wave_volume_entity_ = world->createEntity();
    world->setName(wave_volume_entity_, "Wave Volume");
    world->add(wave_volume_entity_, makeWaveTransform());
    world->add(wave_volume_entity_, components::MeshComponent{
                                .mesh_key = wave_mesh_,
                                .material_key = wave_volume_material_key_,
                                .visible = false,
                                .shadow_visible = false,
                            });
  }

  void spawnWaveShellParticles() {
    if (graphics == nullptr) {
      return;
    }

    const std::vector<std::uint8_t> glow_pixels = buildWaveGlowTexture();
    wave_glow_texture_ =
        graphics->createTextureRGBA8(kWaveGlowTextureSize, kWaveGlowTextureSize, glow_pixels.data());
    const std::vector<std::uint8_t> distortion_pixels = buildWaveDistortionTexture();
    wave_distortion_texture_ = graphics->createTextureRGBA8(
        kWaveDistortionTextureSize, kWaveDistortionTextureSize, distortion_pixels.data());
    if (wave_glow_texture_ == renderer::kInvalidTexture) {
      return;
    }
    const std::string glow_texture_key = "runtime/wave/glow_texture";
    const std::string distortion_texture_key = "runtime/wave/distortion_texture";
    if (particle_effects != nullptr) {
      particle_effects->registerTextureAlias(glow_texture_key, wave_glow_texture_);
      if (wave_distortion_texture_ != renderer::kInvalidTexture) {
        particle_effects->registerTextureAlias(distortion_texture_key, wave_distortion_texture_);
      }
    }

    auto spawn_emitter = [&](std::string_view name,
                             const components::ParticleEmitterComponent& emitter) -> ecs::Entity {
      const ecs::Entity entity = world->createEntity();
      world->setName(entity, std::string(name));
      world->add(entity, makeWaveShellTransform(kWaveCenter));
      world->add(entity, emitter);
      world->add(entity, components::VisibilityComponent{.visible = true});
      return entity;
    };

    wave_core_glow_entity_ =
        spawn_emitter("Wave Core Glow", buildWaveCoreGlowEmitter(glow_texture_key, kWaveRadius));
    wave_outer_glow_entity_ =
        spawn_emitter("Wave Outer Glow",
                      buildWaveOuterGlowEmitter(glow_texture_key, kWaveRadius));
    if (wave_distortion_texture_ != renderer::kInvalidTexture) {
      wave_distortion_entity_ =
          spawn_emitter("Wave Distortion Shell",
                        buildWaveDistortionEmitter(distortion_texture_key, kWaveRadius));
    }
  }

  void spawnWaveOverlay() {
    if (materials != nullptr) {
      materials->registerMaterialDesc(wave_overlay_material_key_, buildWaveOverlayMaterialDesc());
    }

    wave_overlay_entity_ = world->createEntity();
    world->setName(wave_overlay_entity_, "Wave Overlay");
    world->add(wave_overlay_entity_, components::TransformComponent{});
    world->add(wave_overlay_entity_, components::MeshComponent{
                                      .mesh_key = aura_quad_mesh_key_,
                                      .material_key = wave_overlay_material_key_,
                                      .shadow_visible = false,
                                  });
  }

  void syncWaveAttachments() {
    if (!world->isAlive(wave_entity_) || !world->has<components::TransformComponent>(wave_entity_) ||
        !world->isAlive(camera_entity_) || !world->has<components::TransformComponent>(camera_entity_) ||
        !world->has<components::CameraComponent>(camera_entity_)) {
      return;
    }

    const auto wave_transform = world->get<components::TransformComponent>(wave_entity_);
    const math::Vec3 wave_position = wave_transform.getPosition();
    const auto camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const auto camera_component = world->get<components::CameraComponent>(camera_entity_);
    const math::Vec3 camera_position = camera_transform.getPosition();
    const glm::vec3 camera_delta(camera_position.x - wave_position.x,
                                 camera_position.y - wave_position.y,
                                 camera_position.z - wave_position.z);
    const bool camera_inside_wave =
        glm::dot(camera_delta, camera_delta) <= (kWaveRadius * kWaveRadius);
    auto set_mesh_visibility = [&](ecs::Entity entity, bool visible) {
      if (!world->isAlive(entity) || !world->has<components::MeshComponent>(entity)) {
        return;
      }
      world->get<components::MeshComponent>(entity).visible = visible;
    };
    auto set_particle_visibility = [&](ecs::Entity entity, bool visible) {
      if (!world->isAlive(entity)) {
        return;
      }
      if (!world->has<components::VisibilityComponent>(entity)) {
        world->add(entity, components::VisibilityComponent{.visible = visible});
        return;
      }
      world->get<components::VisibilityComponent>(entity).visible = visible;
    };
    set_mesh_visibility(wave_overlay_entity_, false);
    set_mesh_visibility(wave_volume_entity_, camera_inside_wave);
    set_particle_visibility(wave_core_glow_entity_, !camera_inside_wave);
    set_particle_visibility(wave_outer_glow_entity_, !camera_inside_wave);
    set_particle_visibility(wave_distortion_entity_, !camera_inside_wave);

    auto sync_overlay_transform = [&](ecs::Entity entity) {
      if (!world->isAlive(entity) || !world->has<components::TransformComponent>(entity)) {
        return;
      }
      world->get<components::TransformComponent>(entity) =
          makeScreenOverlayTransform(camera_transform,
                                     camera_component.fov_y_degrees,
                                     kWaveCameraAspect,
                                     kWaveOverlayDepth);
    };

    auto sync_wave_shell_transform = [&](ecs::Entity entity) {
      if (!world->isAlive(entity) || !world->has<components::TransformComponent>(entity)) {
        return;
      }
      world->get<components::TransformComponent>(entity) = makeWaveShellTransform(wave_position);
    };

    auto sync_wave_volume_transform = [&](ecs::Entity entity) {
      if (!world->isAlive(entity) || !world->has<components::TransformComponent>(entity)) {
        return;
      }
      world->get<components::TransformComponent>(entity) = wave_transform;
    };

    sync_overlay_transform(wave_overlay_entity_);
    sync_wave_volume_transform(wave_volume_entity_);
    sync_wave_shell_transform(wave_core_glow_entity_);
    sync_wave_shell_transform(wave_outer_glow_entity_);
    sync_wave_shell_transform(wave_distortion_entity_);

    if (world->isAlive(wave_light_entity_) &&
        world->has<components::TransformComponent>(wave_light_entity_)) {
      auto& light_transform = world->get<components::TransformComponent>(wave_light_entity_);
      light_transform.setPosition(wave_position);
    }
  }

  void spawnCamera() {
    const glm::vec3 target = {kWaveCenter.x, kWaveCenter.y, kWaveCenter.z};
    const glm::vec3 eye = target + glm::vec3(0.0f, 0.45f, 6.4f);
    const LookAngles look = lookAnglesToTarget(eye, target);

    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Camera");
    camera_entity_ = camera;
    camera_yaw_ = look.yaw;
    target_camera_yaw_ = look.yaw;
    camera_pitch_ = look.pitch;
    target_camera_pitch_ = look.pitch;

    components::TransformComponent camera_transform{};
    camera_transform.setPosition(toMath(eye));
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));

    components::CameraComponent camera_component{};
    camera_component.near_clip = 0.03f;
    camera_component.far_clip = 220.0f;
    camera_component.render_shadows = false;
    camera_component.is_primary = true;

    world->add(camera, camera_transform);
    world->add(camera, camera_component);
  }

  std::string world_mesh_;
  std::string wave_mesh_;
  std::string environment_map_;
  std::string aura_quad_mesh_key_;
  std::string wave_material_key_;
  std::string wave_volume_material_key_;
  std::string wave_overlay_material_key_;
  renderer::TextureId wave_glow_texture_ = renderer::kInvalidTexture;
  renderer::TextureId wave_distortion_texture_ = renderer::kInvalidTexture;
  ecs::Entity wave_entity_{};
  ecs::Entity wave_volume_entity_{};
  ecs::Entity wave_core_glow_entity_{};
  ecs::Entity wave_outer_glow_entity_{};
  ecs::Entity wave_distortion_entity_{};
  ecs::Entity wave_overlay_entity_{};
  ecs::Entity wave_light_entity_{};
  ecs::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::WaveExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Wave Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.0f;
  config.lighting_exposure = 0.95f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
