#include "karma/features/visual/beams/beam_path_system.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include <glm/glm.hpp>

#include "karma/world/components/beam_path.h"
#include "karma/world/components/light.h"
#include "karma/world/components/transform.h"
#include "karma/core/math/quat.h"
#include "karma/rendering/renderer/device.h"

namespace karma::beams {

namespace {

constexpr double kWrappedBeamTimeSeconds = 4096.0;

constexpr int kEndpointTextureSize = 96;
constexpr int kElectricTextureSize = 96;
constexpr int kDistortionTextureSize = 96;
// Beam helper lights are intentionally budgeted so authored beam paths do not
// explode the local-light cost or push simple scenes onto a heavier path.
constexpr std::size_t kMaxBeamPathLightCount = 8u;

struct EndpointBatchGroup {
  renderer::LayerId layer = 0;
  bool depth_test = true;
  std::vector<renderer::ParticleInstance> core_particles;
  std::vector<renderer::ParticleInstance> glow_particles;
  std::vector<renderer::ParticleInstance> electric_core_particles;
  std::vector<renderer::ParticleInstance> electric_glow_particles;
};

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0f));
}

math::Vec3 scalePoint(const math::Vec3& point, const math::Vec3& scale) {
  return {point.x * scale.x, point.y * scale.y, point.z * scale.z};
}

math::Vec3 add(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 lerpPoint(const math::Vec3& a, const math::Vec3& b, float t) {
  const float s = std::clamp(t, 0.0f, 1.0f);
  return {
      a.x + (b.x - a.x) * s,
      a.y + (b.y - a.y) * s,
      a.z + (b.z - a.z) * s,
  };
}

math::Vec3 transformLocalPoint(const components::TransformComponent* transform,
                               const math::Vec3& point,
                               float interpolation_alpha) {
  if (transform == nullptr) {
    return point;
  }

  const math::Vec3 scaled = scalePoint(point, transform->getScale());
  const math::Quat rotation = transform->getInterpolatedRotation(interpolation_alpha);
  const math::Vec3 rotated = math::rotateVec(rotation, scaled);
  return add(transform->getInterpolatedPosition(interpolation_alpha), rotated);
}

std::vector<std::uint8_t> buildEndpointTexture() {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kEndpointTextureSize) *
                                       static_cast<std::size_t>(kEndpointTextureSize) * 4u,
                                   0u);
  for (int y = 0; y < kEndpointTextureSize; ++y) {
    for (int x = 0; x < kEndpointTextureSize; ++x) {
      const float px =
          (static_cast<float>(x) + 0.5f) / static_cast<float>(kEndpointTextureSize) * 2.0f - 1.0f;
      const float py =
          1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kEndpointTextureSize) * 2.0f;
      const float radius = std::sqrt(px * px + py * py);
      const float alpha = std::exp(-radius * radius * 5.5f);
      const float core = std::exp(-radius * radius * 18.0f);
      const float intensity = saturate(alpha * 0.72f + core * 0.55f);
      const std::uint8_t value = toByte(intensity);
      const std::size_t pixel_index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(kEndpointTextureSize) +
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

std::vector<std::uint8_t> buildElectricTexture() {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kElectricTextureSize) *
                                       static_cast<std::size_t>(kElectricTextureSize) * 4u,
                                   0u);
  for (int y = 0; y < kElectricTextureSize; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(kElectricTextureSize - 1);
    const float center =
        0.5f + 0.11f * std::sin(v * 8.0f) + 0.05f * std::sin(v * 23.0f + 0.65f) +
        0.02f * std::sin(v * 41.0f + 1.7f);
    const float width = 0.035f + 0.015f * (0.5f + 0.5f * std::sin(v * 13.0f + 0.2f));
    const float height_fade = std::pow(std::sin(v * 3.14159265f), 0.55f);
    for (int x = 0; x < kElectricTextureSize; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(kElectricTextureSize - 1);
      const float line = std::exp(-std::pow((u - center) / std::max(width, 1.0e-3f), 2.0f) * 4.2f);
      const float halo = std::exp(-std::pow((u - center) / std::max(width * 2.6f, 1.0e-3f), 2.0f) * 2.0f);
      const float alpha = saturate((line * 0.92f + halo * 0.28f) * height_fade);
      const float intensity = saturate(line * 0.85f + halo * 0.20f);
      const std::size_t pixel_index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(kElectricTextureSize) +
           static_cast<std::size_t>(x)) *
          4u;
      pixels[pixel_index + 0u] = toByte(intensity);
      pixels[pixel_index + 1u] = toByte(intensity);
      pixels[pixel_index + 2u] = toByte(intensity);
      pixels[pixel_index + 3u] = toByte(alpha);
    }
  }
  return pixels;
}

std::vector<std::uint8_t> buildDistortionTexture() {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kDistortionTextureSize) *
                                       static_cast<std::size_t>(kDistortionTextureSize) * 4u,
                                   0u);
  for (int y = 0; y < kDistortionTextureSize; ++y) {
    for (int x = 0; x < kDistortionTextureSize; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kDistortionTextureSize);
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kDistortionTextureSize);
      const float px = u * 2.0f - 1.0f;
      const float py = 1.0f - v * 2.0f;
      const float radius = std::sqrt(px * px + py * py);
      const float angle = std::atan2(py, px);

      const float alpha = saturate(std::exp(-radius * radius * 3.8f) * (1.0f - radius * 0.58f));
      const float swirl_a = std::sin(angle * 3.0f + radius * 10.5f);
      const float swirl_b = std::cos(angle * 5.0f - radius * 7.0f);
      const float flow_x = 0.5f + 0.5f * (0.72f * swirl_a + 0.28f * swirl_b);
      const float flow_y = 0.5f + 0.5f * (0.68f * swirl_b - 0.32f * swirl_a);

      const std::size_t pixel_index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(kDistortionTextureSize) +
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

std::vector<math::Vec3> resolveWorldPoints(const ecs::World& world,
                                           ecs::Entity entity,
                                           const components::BeamPathComponent& beam,
                                           float interpolation_alpha) {
  std::vector<math::Vec3> world_points;
  world_points.reserve(beam.points.size());
  const components::TransformComponent* transform =
      (!beam.world_space && world.has<components::TransformComponent>(entity))
          ? &world.get<components::TransformComponent>(entity)
          : nullptr;

  for (const math::Vec3& point : beam.points) {
    world_points.push_back(
        beam.world_space ? point : transformLocalPoint(transform, point, interpolation_alpha));
  }
  return world_points;
}

uint64_t endpointBatchKey(renderer::LayerId layer, bool depth_test) {
  return (static_cast<uint64_t>(layer) << 32u) | static_cast<uint64_t>(depth_test ? 1u : 0u);
}

renderer::ParticleInstance makeParticle(const math::Vec3& position,
                                        float size,
                                        const math::Color& color,
                                        float rotation_radians = 0.0f) {
  renderer::ParticleInstance particle{};
  particle.position = toGlm(position);
  particle.size = size;
  particle.color = color;
  particle.rotation_radians = rotation_radians;
  particle.uv_min = {0.0f, 0.0f};
  particle.uv_max = {1.0f, 1.0f};
  particle.uv_min_next = particle.uv_min;
  particle.uv_max_next = particle.uv_max;
  particle.frame_blend = 0.0f;
  return particle;
}

float segmentLength(const math::Vec3& start, const math::Vec3& end) {
  return glm::length(toGlm(end) - toGlm(start));
}

float pathLength(const std::vector<math::Vec3>& points, std::size_t segment_count) {
  float total = 0.0f;
  const std::size_t point_count = points.size();
  for (std::size_t i = 0; i < segment_count; ++i) {
    total += segmentLength(points[i], points[(i + 1u) % point_count]);
  }
  return total;
}

math::Vec3 samplePathPosition(const std::vector<math::Vec3>& points,
                              std::size_t segment_count,
                              float distance_along_path) {
  if (points.empty()) {
    return {};
  }
  if (segment_count == 0u) {
    return points.front();
  }

  const std::size_t point_count = points.size();
  float remaining = std::max(distance_along_path, 0.0f);
  for (std::size_t i = 0; i < segment_count; ++i) {
    const math::Vec3& start = points[i];
    const math::Vec3& end = points[(i + 1u) % point_count];
    const float length = segmentLength(start, end);
    if (length <= 1.0e-4f) {
      continue;
    }
    if (remaining <= length || i + 1u == segment_count) {
      return lerpPoint(start, end, remaining / length);
    }
    remaining -= length;
  }
  return points.back();
}

math::Color lerpColor(const math::Color& a, const math::Color& b, float t) {
  const float s = saturate(t);
  return {
      a.r + (b.r - a.r) * s,
      a.g + (b.g - a.g) * s,
      a.b + (b.b - a.b) * s,
      a.a + (b.a - a.a) * s,
  };
}

void makePerpendicularBasis(const glm::vec3& direction, glm::vec3& right, glm::vec3& up) {
  const glm::vec3 normalized = glm::length(direction) > 1.0e-4f
                                   ? glm::normalize(direction)
                                   : glm::vec3{0.0f, 0.0f, 1.0f};
  glm::vec3 reference = std::abs(normalized.y) < 0.95f ? glm::vec3{0.0f, 1.0f, 0.0f}
                                                       : glm::vec3{1.0f, 0.0f, 0.0f};
  right = glm::normalize(glm::cross(normalized, reference));
  if (glm::length(right) <= 1.0e-4f) {
    reference = glm::vec3{0.0f, 0.0f, 1.0f};
    right = glm::normalize(glm::cross(normalized, reference));
  }
  up = glm::normalize(glm::cross(right, normalized));
}

math::Color beamLightColor(const components::BeamPathComponent& beam) {
  math::Color white_hot{1.0f, 0.97f, 0.92f, 1.0f};
  math::Color mixed = lerpColor(beam.glow_color, white_hot, 0.38f);
  mixed.a = 1.0f;
  return mixed;
}

math::Color electricCoreColor(const components::BeamPathComponent& beam) {
  math::Color hot = lerpColor(beam.core_color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.72f);
  hot.r *= beam.core_intensity * 0.82f;
  hot.g *= beam.core_intensity * 0.82f;
  hot.b *= beam.core_intensity * 0.82f;
  hot.a *= beam.electric_intensity * 0.42f;
  return hot;
}

math::Color electricGlowColor(const components::BeamPathComponent& beam) {
  math::Color glow = lerpColor(beam.glow_color, beam.core_color, 0.20f);
  glow.r *= beam.glow_intensity * 0.92f;
  glow.g *= beam.glow_intensity * 0.92f;
  glow.b *= beam.glow_intensity * 0.92f;
  glow.a *= beam.electric_intensity * 0.24f;
  return glow;
}

}  // namespace

BeamPathSystem::BeamPathSystem(renderer::GraphicsDevice* device) : device_(device) {}

BeamPathSystem::~BeamPathSystem() {
  destroySharedResources();
}

void BeamPathSystem::destroyRuntimeState(RuntimeState& state) {
  state.light_entities.clear();
}

BeamPathSystem::RuntimeState& BeamPathSystem::ensureRuntimeState(uint64_t beam_key) {
  return beams_[beam_key];
}

void BeamPathSystem::resizeLightEntities(ecs::World& world,
                                         RuntimeState& state,
                                         std::size_t desired_count) {
  while (state.light_entities.size() > desired_count) {
    const ecs::Entity light = state.light_entities.back();
    if (world.isAlive(light)) {
      world.destroyEntity(light);
    }
    state.light_entities.pop_back();
  }

  while (state.light_entities.size() < desired_count) {
    const ecs::Entity light = world.createEntity();
    world.setName(light, "Beam Light");
    world.add(light, components::TransformComponent{});
    world.add(light, components::LightComponent{});
    state.light_entities.push_back(light);
  }
}

void BeamPathSystem::ensureSharedResources() {
  if (device_ == nullptr) {
    return;
  }
  if (endpoint_texture_ == renderer::kInvalidTexture) {
    endpoint_texture_pixels_ = buildEndpointTexture();
    endpoint_texture_ = device_->createTextureRGBA8(
        kEndpointTextureSize, kEndpointTextureSize, endpoint_texture_pixels_.data());
  }
  if (electric_texture_ == renderer::kInvalidTexture) {
    electric_texture_pixels_ = buildElectricTexture();
    electric_texture_ = device_->createTextureRGBA8(
        kElectricTextureSize, kElectricTextureSize, electric_texture_pixels_.data());
  }
  if (distortion_texture_ == renderer::kInvalidTexture) {
    distortion_texture_pixels_ = buildDistortionTexture();
    distortion_texture_ = device_->createTextureRGBA8(
        kDistortionTextureSize, kDistortionTextureSize, distortion_texture_pixels_.data());
  }
}

void BeamPathSystem::destroySharedResources() {
  if (device_ != nullptr) {
    for (auto& [key, state] : beams_) {
      (void)key;
      destroyRuntimeState(state);
    }
    if (endpoint_texture_ != renderer::kInvalidTexture) {
      device_->destroyTexture(endpoint_texture_);
      endpoint_texture_ = renderer::kInvalidTexture;
    }
    if (electric_texture_ != renderer::kInvalidTexture) {
      device_->destroyTexture(electric_texture_);
      electric_texture_ = renderer::kInvalidTexture;
    }
    if (distortion_texture_ != renderer::kInvalidTexture) {
      device_->destroyTexture(distortion_texture_);
      distortion_texture_ = renderer::kInvalidTexture;
    }
  }
  beams_.clear();
  endpoint_texture_pixels_.clear();
  electric_texture_pixels_.clear();
  distortion_texture_pixels_.clear();
}

void BeamPathSystem::update(ecs::World& world, float dt, float interpolation_alpha) {
  if (device_ == nullptr) {
    return;
  }

  time_ += static_cast<double>(std::max(dt, 0.0f));
  if (time_ >= kWrappedBeamTimeSeconds) {
    time_ = std::fmod(time_, kWrappedBeamTimeSeconds);
  }
  ensureSharedResources();
  if (endpoint_texture_ == renderer::kInvalidTexture) {
    return;
  }

  std::unordered_set<uint64_t> seen_beams;
  std::unordered_map<uint64_t, EndpointBatchGroup> endpoint_batches;
  std::vector<renderer::ParticleBatch> distortion_batches;

  world.forEach<components::BeamPathComponent>([&](ecs::Entity entity) {
    const auto& beam = world.get<components::BeamPathComponent>(entity);
    const uint64_t beam_key = entityKey(entity);
    seen_beams.insert(beam_key);
    RuntimeState& state = ensureRuntimeState(beam_key);

    if (beam.points.size() < 2u) {
      resizeLightEntities(world, state, 0u);
      return;
    }

    std::vector<math::Vec3> world_points =
        resolveWorldPoints(world, entity, beam, interpolation_alpha);
    const std::size_t point_count = world_points.size();
    const std::size_t segment_count =
        beam.closed_loop ? point_count : (point_count > 0u ? point_count - 1u : 0u);

    if (!beam.visible || segment_count == 0u) {
      resizeLightEntities(world, state, 0u);
      return;
    }

    const uint64_t batch_key = endpointBatchKey(beam.layer, beam.depth_test);
    auto& group = endpoint_batches[batch_key];
    group.layer = beam.layer;
    group.depth_test = beam.depth_test;
    std::vector<renderer::ParticleInstance> distortion_particles;

    math::Color fill_core = beam.core_color;
    fill_core.r *= beam.core_intensity * 1.10f;
    fill_core.g *= beam.core_intensity * 1.10f;
    fill_core.b *= beam.core_intensity * 1.10f;
    fill_core.a *= 0.92f;

    math::Color fill_glow = beam.glow_color;
    fill_glow.r *= beam.glow_intensity * 1.05f;
    fill_glow.g *= beam.glow_intensity * 1.05f;
    fill_glow.b *= beam.glow_intensity * 1.05f;
    fill_glow.a *= 0.58f;

    const float core_fill_size =
        std::max(beam.endpoint_core_size * 0.86f, std::max(beam.core_radius * 2.2f, 0.18f));
    const float glow_fill_size = std::max(
        beam.endpoint_glow_size * 0.72f, std::max(beam.glow_radius * 1.65f, core_fill_size * 1.9f));

    for (std::size_t i = 0; i < segment_count; ++i) {
      const math::Vec3& start = world_points[i];
      const math::Vec3& end = world_points[(i + 1u) % point_count];
      const float current_segment_length = segmentLength(start, end);
      const float spacing = std::max(core_fill_size * 0.18f, 0.03f);
      const std::size_t sample_count =
          std::max<std::size_t>(1u, static_cast<std::size_t>(std::ceil(current_segment_length / spacing)));

      for (std::size_t sample_index = 0; sample_index <= sample_count; ++sample_index) {
        const float t = sample_count > 0u
                            ? static_cast<float>(sample_index) /
                                  static_cast<float>(sample_count)
                            : 0.0f;
        const math::Vec3 position = lerpPoint(start, end, t);
        group.glow_particles.push_back(makeParticle(position, glow_fill_size, fill_glow));
        group.core_particles.push_back(makeParticle(position, core_fill_size, fill_core));
      }

      if (electric_texture_ != renderer::kInvalidTexture && beam.electric_intensity > 0.0f &&
          beam.electric_size > 0.0f && beam.electric_spacing > 0.0f) {
        const glm::vec3 direction = toGlm(end) - toGlm(start);
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        makePerpendicularBasis(direction, right, up);

        const float electric_spacing =
            std::max(beam.electric_spacing, beam.electric_size * 0.65f);
        const std::size_t electric_sample_count = std::max<std::size_t>(
            1u, static_cast<std::size_t>(std::ceil(current_segment_length / electric_spacing)));
        const math::Color electric_core = electricCoreColor(beam);
        const math::Color electric_glow = electricGlowColor(beam);

        for (std::size_t sample_index = 0; sample_index <= electric_sample_count; ++sample_index) {
          const float t = electric_sample_count > 0u
                              ? static_cast<float>(sample_index) /
                                    static_cast<float>(electric_sample_count)
                              : 0.0f;
          const math::Vec3 base_position = lerpPoint(start, end, t);
          const float electric_time =
              static_cast<float>(time_ * static_cast<double>(std::max(beam.electric_speed, 0.0f)));
          const float phase =
              electric_time * 9.0f + static_cast<float>(i) * 1.27f +
              static_cast<float>(sample_index) * 0.91f;
          const float phase_b =
              electric_time * 14.5f - static_cast<float>(i) * 0.73f +
              static_cast<float>(sample_index) * 1.61f;
          const float jitter_a = std::sin(phase) * 0.72f + std::sin(phase_b * 0.63f) * 0.28f;
          const float jitter_b = std::cos(phase_b) * 0.68f + std::sin(phase * 1.19f) * 0.32f;
          const glm::vec3 offset =
              right * (jitter_a * beam.electric_jitter_radius) +
              up * (jitter_b * beam.electric_jitter_radius);
          const math::Vec3 electric_position = add(base_position, {offset.x, offset.y, offset.z});
          const float rotation = phase * 0.21f + phase_b * 0.09f;
          group.electric_glow_particles.push_back(
              makeParticle(electric_position,
                           beam.electric_size * 1.42f,
                           electric_glow,
                           rotation));
          group.electric_core_particles.push_back(
              makeParticle(electric_position,
                           beam.electric_size * 0.82f,
                           electric_core,
                           rotation + 0.35f));
        }
      }

      if (distortion_texture_ != renderer::kInvalidTexture && beam.distortion_intensity > 0.0f &&
          beam.distortion_size > 0.0f && beam.distortion_spacing > 0.0f &&
          beam.distortion_strength > 0.0f) {
        const glm::vec3 direction = toGlm(end) - toGlm(start);
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        makePerpendicularBasis(direction, right, up);

        const float distortion_spacing =
            std::max(beam.distortion_spacing, beam.distortion_size * 0.22f);
        const std::size_t distortion_sample_count = std::max<std::size_t>(
            1u, static_cast<std::size_t>(std::ceil(current_segment_length / distortion_spacing)));

        for (std::size_t sample_index = 0; sample_index <= distortion_sample_count; ++sample_index) {
          const float t = distortion_sample_count > 0u
                              ? static_cast<float>(sample_index) /
                                    static_cast<float>(distortion_sample_count)
                              : 0.0f;
          const math::Vec3 base_position = lerpPoint(start, end, t);
          const float distortion_time = static_cast<float>(
              time_ * static_cast<double>(std::max(beam.distortion_speed, 0.0f)));
          const float phase =
              distortion_time * 3.8f + static_cast<float>(i) * 0.87f +
              static_cast<float>(sample_index) * 0.49f;
          const float phase_b =
              distortion_time * 5.9f - static_cast<float>(i) * 0.42f +
              static_cast<float>(sample_index) * 0.71f;
          const float jitter_a = std::sin(phase) * 0.62f + std::cos(phase_b * 0.81f) * 0.38f;
          const float jitter_b = std::cos(phase_b) * 0.64f + std::sin(phase * 1.11f) * 0.36f;
          const glm::vec3 offset =
              right * (jitter_a * beam.distortion_jitter_radius * 0.55f) +
              up * (jitter_b * beam.distortion_jitter_radius * 0.55f);
          const math::Vec3 distortion_position =
              add(base_position, {offset.x, offset.y, offset.z});

          renderer::ParticleInstance particle =
              makeParticle(distortion_position,
                           beam.distortion_size,
                           {1.0f, 1.0f, 1.0f, beam.distortion_intensity},
                           phase * 0.24f + phase_b * 0.11f);
          distortion_particles.push_back(particle);
        }
      }
    }

    if (beam.endpoint_flares) {
      for (const math::Vec3& point : world_points) {
        if (beam.endpoint_core_size > 0.0f) {
          math::Color hot = beam.core_color;
          hot.r *= beam.core_intensity;
          hot.g *= beam.core_intensity;
          hot.b *= beam.core_intensity;
          group.core_particles.push_back(makeParticle(point, beam.endpoint_core_size, hot));
        }
        if (beam.endpoint_glow_size > 0.0f) {
          math::Color glow = beam.glow_color;
          glow.r *= beam.glow_intensity;
          glow.g *= beam.glow_intensity;
          glow.b *= beam.glow_intensity;
          glow.a *= 0.72f;
          group.glow_particles.push_back(makeParticle(point, beam.endpoint_glow_size, glow));
        }
      }
    }

    const float total_length = pathLength(world_points, segment_count);
    std::size_t desired_light_count = 0u;
    if (beam.light_intensity > 0.0f && beam.light_range > 0.0f) {
      if (beam.light_spacing > 0.0f && total_length > 1.0e-4f) {
        desired_light_count = std::max<std::size_t>(
            2u, static_cast<std::size_t>(std::ceil(total_length / beam.light_spacing)) + 1u);
      } else if (beam.light_count > 0u) {
        desired_light_count = static_cast<std::size_t>(beam.light_count);
      }
    }
    desired_light_count = std::min(desired_light_count, kMaxBeamPathLightCount);
    resizeLightEntities(world, state, desired_light_count);
    if (!state.light_entities.empty()) {
      const math::Color light_color = beamLightColor(beam);
      for (std::size_t light_index = 0; light_index < state.light_entities.size(); ++light_index) {
        const ecs::Entity light_entity = state.light_entities[light_index];
        if (!world.isAlive(light_entity)) {
          continue;
        }
        const float fraction = (static_cast<float>(light_index) + 0.5f) /
                               static_cast<float>(state.light_entities.size());
        const math::Vec3 light_position =
            samplePathPosition(world_points, segment_count, total_length * fraction);
        auto& transform = world.get<components::TransformComponent>(light_entity);
        transform.setPosition(light_position);
        auto& light = world.get<components::LightComponent>(light_entity);
        light.type = components::LightComponent::Type::Point;
        light.color = light_color;
        light.intensity = beam.light_intensity *
                          (0.92f + 0.08f * std::sin(static_cast<float>(
                                               time_ * 6.0 + static_cast<double>(fraction) * 9.0)));
        light.range = beam.light_range;
        light.casts_shadows = false;
      }
    }

    if (!distortion_particles.empty()) {
      renderer::ParticleBatch distortion_batch{};
      distortion_batch.layer = beam.layer;
      distortion_batch.depth_test = beam.depth_test;
      distortion_batch.texture = distortion_texture_;
      distortion_batch.blend_mode = renderer::ParticleBlendMode::Distortion;
      distortion_batch.use_soft_mask = true;
      distortion_batch.soft_particle_distance =
          std::max(beam.distortion_soft_particle_distance, 0.0f);
      distortion_batch.distortion_strength = std::max(beam.distortion_strength, 0.0f);
      distortion_batch.particles = std::move(distortion_particles);
      distortion_batches.push_back(std::move(distortion_batch));
    }
  });

  for (auto it = beams_.begin(); it != beams_.end();) {
    if (seen_beams.find(it->first) == seen_beams.end()) {
      resizeLightEntities(world, it->second, 0u);
      destroyRuntimeState(it->second);
      it = beams_.erase(it);
      continue;
    }
    ++it;
  }

  for (auto& [key, group] : endpoint_batches) {
    (void)key;
    if (!group.glow_particles.empty()) {
      renderer::ParticleBatch glow_batch{};
      glow_batch.layer = group.layer;
      glow_batch.depth_test = group.depth_test;
      glow_batch.texture = endpoint_texture_;
      glow_batch.blend_mode = renderer::ParticleBlendMode::Additive;
      glow_batch.use_soft_mask = false;
      glow_batch.particles = std::move(group.glow_particles);
      device_->submitParticles(std::move(glow_batch));
    }
    if (!group.core_particles.empty()) {
      renderer::ParticleBatch core_batch{};
      core_batch.layer = group.layer;
      core_batch.depth_test = group.depth_test;
      core_batch.texture = endpoint_texture_;
      core_batch.blend_mode = renderer::ParticleBlendMode::Additive;
      core_batch.use_soft_mask = false;
      core_batch.particles = std::move(group.core_particles);
      device_->submitParticles(std::move(core_batch));
    }
    if (!group.electric_glow_particles.empty()) {
      renderer::ParticleBatch electric_glow_batch{};
      electric_glow_batch.layer = group.layer;
      electric_glow_batch.depth_test = group.depth_test;
      electric_glow_batch.texture = electric_texture_;
      electric_glow_batch.blend_mode = renderer::ParticleBlendMode::Additive;
      electric_glow_batch.use_soft_mask = false;
      electric_glow_batch.particles = std::move(group.electric_glow_particles);
      device_->submitParticles(std::move(electric_glow_batch));
    }
    if (!group.electric_core_particles.empty()) {
      renderer::ParticleBatch electric_core_batch{};
      electric_core_batch.layer = group.layer;
      electric_core_batch.depth_test = group.depth_test;
      electric_core_batch.texture = electric_texture_;
      electric_core_batch.blend_mode = renderer::ParticleBlendMode::Additive;
      electric_core_batch.use_soft_mask = false;
      electric_core_batch.particles = std::move(group.electric_core_particles);
      device_->submitParticles(std::move(electric_core_batch));
    }
  }

  for (auto& batch : distortion_batches) {
    device_->submitParticles(std::move(batch));
  }
}

}  // namespace karma::beams
