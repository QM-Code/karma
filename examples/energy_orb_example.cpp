#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace karma::demo {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kAtlasFrameSize = 128;
constexpr int kAtlasFrameCount = 6;
constexpr math::Vec3 kOrbBasePosition{0.0f, 1.85f, 0.0f};
constexpr float kOrbScale = 0.25f * 0.75f;
constexpr float kOrbDistortionScale = 0.5f;
constexpr float kOrbLightScale = 0.75f * 0.5f;
constexpr float kOrbShellUniformScale = 7.0f * kOrbScale;
constexpr math::Color kOrbAccentColor{0.18f, 1.0f, 0.28f, 1.0f};
constexpr math::Color kOrbHotCoreColor{1.0f, 1.0f, 1.0f, 1.0f};

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

float lerpFloat(float a, float b, float t) {
  return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

math::Color lerpColor(const math::Color& a, const math::Color& b, float t) {
  const float s = std::clamp(t, 0.0f, 1.0f);
  return {
      lerpFloat(a.r, b.r, s),
      lerpFloat(a.g, b.g, s),
      lerpFloat(a.b, b.b, s),
      lerpFloat(a.a, b.a, s),
  };
}

math::Color scaleColor(const math::Color& color, float scale, float alpha = 1.0f) {
  return {
      saturate(color.r * scale),
      saturate(color.g * scale),
      saturate(color.b * scale),
      saturate(alpha),
  };
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

components::TransformComponent makeScaledTransform(const math::Vec3& position, float uniform_scale) {
  components::TransformComponent transform = makeTransform(position);
  transform.setScale({uniform_scale, uniform_scale, uniform_scale});
  return transform;
}

ecs::Entity createParticleEffectEntity(ecs::World& world,
                                       std::string_view name,
                                       std::string_view effect_key,
                                       const math::Vec3& position,
                                       std::optional<components::ParticleEffectOverrideComponent>
                                           effect_override = std::nullopt) {
  return particles::createEffectEntity(world,
                                       particles::ParticleEffectEntityDesc{
                                           .name = name,
                                           .effect_key = effect_key,
                                           .transform = makeTransform(position),
                                           .enabled = true,
                                           .playing = true,
                                           .effect_override = std::move(effect_override),
                                       });
}

void setEntityPositionIfAlive(ecs::World& world, ecs::Entity entity, const math::Vec3& position) {
  if (!world.isAlive(entity) || !world.has<components::TransformComponent>(entity)) {
    return;
  }
  world.get<components::TransformComponent>(entity).setPosition(position);
}

void setMeshVisibilityIfAlive(ecs::World& world, ecs::Entity entity, bool visible) {
  if (!world.isAlive(entity) || !world.has<components::MeshComponent>(entity)) {
    return;
  }
  world.get<components::MeshComponent>(entity).visible = visible;
}

void destroyTextureIfValid(renderer::GraphicsDevice* graphics, renderer::TextureId& texture) {
  if (graphics == nullptr || texture == renderer::kInvalidTexture) {
    return;
  }
  graphics->destroyTexture(texture);
  texture = renderer::kInvalidTexture;
}

std::vector<std::uint8_t> buildOrbCoreAtlas(int frame_size,
                                            int frame_count,
                                            const math::Color& accent_color) {
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
        const float filament =
            smoothStep01(saturate(1.0f - std::abs(std::sin(angle * 3.5f + radius * 10.0f - t * 10.5f)) * (0.45f + radius * 0.8f)));

        const float intensity = saturate(plasma + hotspot * 0.65f + filament * core * 0.18f);
        const float alpha = saturate(intensity * (0.75f + core * 0.35f));
        const float hot_mix = saturate(hotspot * 0.82f + core * 0.34f + filament * 0.16f);
        const math::Color edge_color = scaleColor(accent_color, 0.36f + intensity * 0.64f);
        const math::Color final_color = lerpColor(edge_color, kOrbHotCoreColor, hot_mix);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(final_color.r);
        pixels[dst_index + 1u] = toByte(final_color.g);
        pixels[dst_index + 2u] = toByte(final_color.b);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildOrbArcAtlas(int frame_size,
                                           int frame_count,
                                           const math::Color& accent_color) {
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
              std::exp(-((point.x - p2.x) * (point.x - p2.x) + (point.y - p2.y) * (point.y - p2.y)) * 90.0f);
          arc_intensity += line * (0.85f + endpoint * 0.45f);
        }

        const float central_glow = std::exp(-(point.x * point.x + point.y * point.y) * 7.5f);
        const float alpha = saturate(arc_intensity * 0.82f + central_glow * 0.08f);
        if (alpha <= 0.002f) {
          continue;
        }
        const float hot_mix = saturate(arc_intensity * 0.74f + central_glow * 0.18f);
        const math::Color edge_color = scaleColor(accent_color, 0.52f + alpha * 0.48f);
        const math::Color final_color = lerpColor(edge_color, kOrbHotCoreColor, hot_mix);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(final_color.r);
        pixels[dst_index + 1u] = toByte(final_color.g);
        pixels[dst_index + 2u] = toByte(final_color.b);
        pixels[dst_index + 3u] = toByte(alpha);
      }
    }
  }

  return pixels;
}

std::vector<std::uint8_t> buildOrbHaloAtlas(int frame_size,
                                            int frame_count,
                                            const math::Color& accent_color) {
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
        const float outer =
            std::pow(saturate(1.0f - radius / 1.05f), 1.8f) * 0.36f;
        const float wisps =
            0.5f + 0.5f * std::sin((px * 4.5f - py * 6.5f + t * 4.8f) * kPi) *
                       std::cos((px * 7.0f + py * 3.8f - t * 4.0f) * kPi);
        const float alpha = saturate(halo * (0.34f + wisps * 0.18f) + outer * 0.12f);
        const math::Color halo_color =
            lerpColor(scaleColor(accent_color, 0.46f + alpha * 0.34f),
                      kOrbHotCoreColor,
                      alpha * 0.12f);

        const size_t dst_x = static_cast<size_t>(frame * frame_size + x);
        const size_t dst_index =
            (static_cast<size_t>(y) * static_cast<size_t>(width) + dst_x) * 4u;
        pixels[dst_index + 0u] = toByte(halo_color.r);
        pixels[dst_index + 1u] = toByte(halo_color.g);
        pixels[dst_index + 2u] = toByte(halo_color.b);
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

        Vec2 radial{radius > 1.0e-4f ? px / radius : 0.0f, radius > 1.0e-4f ? py / radius : -1.0f};
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

renderer::MaterialId createOrbShellMaterial(renderer::GraphicsDevice& graphics,
                                            const math::Color& accent_color) {
  renderer::MaterialDesc desc{};
  const math::Color shell_tint = lerpColor(accent_color, kOrbHotCoreColor, 0.16f);
  desc.base_color = {shell_tint.r, shell_tint.g, shell_tint.b, 0.16f};
  desc.emissive_color = {
      accent_color.r * 0.12f,
      accent_color.g * 0.16f,
      accent_color.b * 0.12f,
      1.0f,
  };
  desc.metallic = 0.0f;
  desc.roughness = 0.015f;
  desc.occlusion_strength = 0.0f;
  desc.shading_model = renderer::MaterialDesc::ShadingModel::EnergyShell;
  desc.shell_fresnel_power = 6.1f;
  desc.shell_fresnel_strength = 1.55f;
  desc.shell_refraction_strength = 0.24f;
  desc.shell_interior_strength = 0.60f;
  desc.shell_highlight_strength = 1.34f;
  desc.shell_alpha_boost = 0.15f;
  desc.shell_swirl_strength = 0.82f;
  desc.transparent = true;
  desc.depth_write = false;
  desc.double_sided = true;
  return graphics.createMaterial(desc);
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
    const std::string orb_shell_mesh = resolveExampleAssetPath("shot.glb").string();
    const std::string environment_map =
        resolveExampleAssetPath("golden_gate_hills_4k.hdr").string();

    const std::vector<std::uint8_t> core_atlas =
        buildOrbCoreAtlas(kAtlasFrameSize, kAtlasFrameCount, kOrbAccentColor);
    core_texture_ = graphics->createTextureRGBA8(kAtlasFrameSize * kAtlasFrameCount,
                                                 kAtlasFrameSize,
                                                 core_atlas.data());

    const std::vector<std::uint8_t> arc_atlas =
        buildOrbArcAtlas(kAtlasFrameSize, kAtlasFrameCount, kOrbAccentColor);
    arc_texture_ = graphics->createTextureRGBA8(kAtlasFrameSize * kAtlasFrameCount,
                                                kAtlasFrameSize,
                                                arc_atlas.data());

    const std::vector<std::uint8_t> halo_atlas =
        buildOrbHaloAtlas(kAtlasFrameSize, kAtlasFrameCount, kOrbAccentColor);
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

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_key = world_mesh});

    auto environment_entity = world->createEntity();
    world->setName(environment_entity, "Environment");
    world->add(environment_entity, components::EnvironmentComponent{
        .environment_map = environment_map,
        .intensity = 0.18f,
        .draw_skybox = false,
    });

    auto sun_entity = world->createEntity();
    world->setName(sun_entity, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 40.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.48f, -0.82f));
    world->add(sun_entity, sun_transform);
    world->add(sun_entity, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {0.80f, 0.84f, 1.0f, 1.0f},
        .intensity = 0.36f,
        .shadow_extent = 50.0f,
    });

    auto orb_light_entity = world->createEntity();
    world->setName(orb_light_entity, "Orb Light");
    world->add(orb_light_entity, makeTransform(kOrbBasePosition));
    world->add(orb_light_entity, components::LightComponent{
                                       .type = components::LightComponent::Type::Point,
                                       .color = {0.62f, 1.0f, 0.70f, 1.0f},
                                       .intensity = 26.0f * kOrbLightScale,
                                       .range = 18.0f * kOrbScale,
                                       .casts_shadows = false,
                                   });
    orb_light_entity_ = orb_light_entity;

    const renderer::MaterialId orb_shell_material =
        graphics != nullptr ? createOrbShellMaterial(*graphics, kOrbAccentColor)
                            : renderer::kInvalidMaterial;
    auto orb_shell_mesh_entity = world->createEntity();
    world->setName(orb_shell_mesh_entity, "Energy Orb Shell Mesh");
    world->add(orb_shell_mesh_entity,
               makeScaledTransform(kOrbBasePosition, kOrbShellUniformScale));
    world->add(orb_shell_mesh_entity,
               components::MeshComponent{
                   .mesh_key = orb_shell_mesh,
                   .material_id = orb_shell_material,
                   .owns_material_id = orb_shell_material != renderer::kInvalidMaterial,
                   .shadow_visible = false,
               });
    orb_shell_mesh_entity_ = orb_shell_mesh_entity;

    if (particle_effects != nullptr) {
      const components::ParticleEffectOverrideComponent core_override{
          .size_scale = kOrbScale,
          .radius_scale = kOrbScale,
          .velocity_scale = kOrbScale,
          .start_color = math::Color{kOrbHotCoreColor.r, kOrbHotCoreColor.g, kOrbHotCoreColor.b, 0.98f},
          .end_color = math::Color{kOrbAccentColor.r * 0.85f,
                                   kOrbAccentColor.g * 0.92f,
                                   kOrbAccentColor.b * 0.85f,
                                   0.0f},
      };
      const components::ParticleEffectOverrideComponent arc_override{
          .size_scale = kOrbScale,
          .radius_scale = kOrbScale,
          .velocity_scale = kOrbScale,
          .start_color = math::Color{kOrbHotCoreColor.r, kOrbHotCoreColor.g, kOrbHotCoreColor.b, 1.0f},
          .end_color = math::Color{kOrbAccentColor.r * 0.92f,
                                   kOrbAccentColor.g * 0.98f,
                                   kOrbAccentColor.b * 0.92f,
                                   0.0f},
      };
      const components::ParticleEffectOverrideComponent halo_override{
          .size_scale = kOrbScale,
          .radius_scale = kOrbScale,
          .velocity_scale = kOrbScale,
          .start_color = math::Color{kOrbAccentColor.r * 0.28f,
                                     kOrbAccentColor.g * 0.48f,
                                     kOrbAccentColor.b * 0.28f,
                                     0.08f},
          .end_color = math::Color{kOrbAccentColor.r * 0.12f,
                                   kOrbAccentColor.g * 0.22f,
                                   kOrbAccentColor.b * 0.12f,
                                   0.0f},
      };
      const components::ParticleEffectOverrideComponent distortion_override{
          .size_scale = kOrbScale * kOrbDistortionScale,
          .radius_scale = kOrbScale,
          .velocity_scale = kOrbScale,
          .start_color = math::Color{kOrbHotCoreColor.r, kOrbHotCoreColor.g, kOrbHotCoreColor.b, 0.92f},
          .end_color = math::Color{kOrbHotCoreColor.r, kOrbHotCoreColor.g, kOrbHotCoreColor.b, 0.0f},
      };

      orb_core_entity_ = createParticleEffectEntity(*world,
                                                    "Energy Orb Core",
                                                    "energy_orb_core",
                                                    kOrbBasePosition,
                                                    core_override);
      orb_arc_entity_ = createParticleEffectEntity(*world,
                                                   "Energy Orb Arcs",
                                                   "energy_orb_arcs",
                                                   kOrbBasePosition,
                                                   arc_override);
      orb_halo_entity_ = createParticleEffectEntity(*world,
                                                    "Energy Orb Halo",
                                                    "energy_orb_halo",
                                                    kOrbBasePosition,
                                                    halo_override);
      orb_distortion_entity_ = createParticleEffectEntity(*world,
                                                          "Energy Orb Distortion",
                                                          "energy_orb_distortion",
                                                          kOrbBasePosition,
                                                          distortion_override);
    }

    auto camera_entity = world->createEntity();
    world->setName(camera_entity, "Camera");
    camera_entity_ = camera_entity;
    camera_yaw_ = 0.0f;
    target_camera_yaw_ = 0.0f;
    camera_pitch_ = -0.12f;
    target_camera_pitch_ = -0.12f;
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 2.35f, 6.6f});
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera_entity, camera_transform);
    world->add(camera_entity, components::CameraComponent{
        .near_clip = 0.05f,
        .far_clip = 120.0f,
        .is_primary = true,
    });
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    time_ += dt;

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
      math::Vec3 right = math::normalize(math::cross(forward, up));

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
      setOrbPlayback(orb_enabled_);
    }

    if (input->actionPressed("restart_orb")) {
      restartOrbEffects();
      orb_enabled_ = true;
      setOrbPlayback(true);
    }

    const math::Vec3 orb_position = kOrbBasePosition;

    setEntityPositionIfAlive(*world, orb_shell_mesh_entity_, orb_position);
    setEntityPositionIfAlive(*world, orb_core_entity_, orb_position);
    setEntityPositionIfAlive(*world, orb_halo_entity_, orb_position);
    setEntityPositionIfAlive(*world, orb_distortion_entity_, orb_position);

    if (world->isAlive(orb_arc_entity_) && world->has<components::TransformComponent>(orb_arc_entity_)) {
      auto& transform = world->get<components::TransformComponent>(orb_arc_entity_);
      transform.setPosition(orb_position);
      transform.setRotation(
          math::fromYawPitch(time_ * 0.72f, std::sin(time_ * 0.62f) * 0.12f));
    }

    if (world->isAlive(orb_light_entity_) &&
        world->has<components::TransformComponent>(orb_light_entity_) &&
        world->has<components::LightComponent>(orb_light_entity_)) {
      auto& transform = world->get<components::TransformComponent>(orb_light_entity_);
      auto& light = world->get<components::LightComponent>(orb_light_entity_);
      transform.setPosition({orb_position.x, orb_position.y + 0.02f, orb_position.z});

      if (!orb_enabled_) {
        light.intensity = 0.0f;
        light.range = 0.1f;
      } else {
        const float pulse = 0.60f + 0.25f * (0.5f + 0.5f * std::sin(time_ * 2.1f)) +
                            0.15f * (0.5f + 0.5f * std::sin(time_ * 5.3f + 1.1f));
        const math::Color pulsed_light_color =
            lerpColor(kOrbAccentColor, kOrbHotCoreColor, 0.34f + pulse * 0.22f);
        light.color = {
            pulsed_light_color.r,
            pulsed_light_color.g,
            pulsed_light_color.b,
            1.0f,
        };
        light.intensity = (18.0f + pulse * 16.0f) * kOrbLightScale;
        light.range = (12.0f + pulse * 8.0f) * kOrbScale;
      }
    }
  }

  void onShutdown() override {
    destroyTextureIfValid(graphics, core_texture_);
    destroyTextureIfValid(graphics, arc_texture_);
    destroyTextureIfValid(graphics, halo_texture_);
    destroyTextureIfValid(graphics, distortion_texture_);
  }

 private:
  void setOrbPlayback(bool playing) {
    setMeshVisibilityIfAlive(*world, orb_shell_mesh_entity_, playing);
    particles::setEffectPlaying(*world, orb_core_entity_, playing);
    particles::setEffectPlaying(*world, orb_arc_entity_, playing);
    particles::setEffectPlaying(*world, orb_halo_entity_, playing);
    particles::setEffectPlaying(*world, orb_distortion_entity_, playing);
  }

  void restartOrbEffects() {
    particles::restartEffect(*world, orb_core_entity_);
    particles::restartEffect(*world, orb_arc_entity_);
    particles::restartEffect(*world, orb_halo_entity_);
    particles::restartEffect(*world, orb_distortion_entity_);
  }

  ecs::Entity orb_shell_mesh_entity_{};
  ecs::Entity orb_core_entity_{};
  ecs::Entity orb_arc_entity_{};
  ecs::Entity orb_halo_entity_{};
  ecs::Entity orb_distortion_entity_{};
  ecs::Entity orb_light_entity_{};
  ecs::Entity camera_entity_{};
  renderer::TextureId core_texture_ = renderer::kInvalidTexture;
  renderer::TextureId arc_texture_ = renderer::kInvalidTexture;
  renderer::TextureId halo_texture_ = renderer::kInvalidTexture;
  renderer::TextureId distortion_texture_ = renderer::kInvalidTexture;
  bool orb_enabled_ = true;
  float time_ = 0.0f;
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
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.shadow_bias = 0.0006f;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.55f;
  config.lighting_exposure = 1.0f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
