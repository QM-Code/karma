#include "karma/scene_authoring.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <numeric>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace karma::scene_authoring {
namespace {

constexpr uint32_t kMaxTerrainResolution =
    rendering::kMaxTerrainTileResolution;

bool isPowerOfTwoPlusOne(uint32_t value) {
  if (value < 3u || value > kMaxTerrainResolution) {
    return false;
  }
  const uint32_t power = value - 1u;
  return (power & (power - 1u)) == 0u;
}

bool resolveDesc(const TerrainCanvasDesc& source, TerrainCanvasDesc& out) {
  out = source;
  if (out.control_resolution == 0u) {
    out.control_resolution = out.resolution;
  }
  return isPowerOfTwoPlusOne(out.resolution) &&
         out.control_resolution >= 2u &&
         out.control_resolution <= kMaxTerrainResolution &&
         std::isfinite(out.terrain_size) && out.terrain_size > 0.0f &&
         std::isfinite(out.height_scale) && out.height_scale > 0.0f &&
         std::isfinite(out.height_offset);
}

std::size_t squareSampleCount(uint32_t resolution) {
  return static_cast<std::size_t>(resolution) *
         static_cast<std::size_t>(resolution);
}

bool storageValid(const TerrainCanvasDesc& desc,
                  const std::vector<float>& heights,
                  const std::vector<uint8_t>& control) {
  TerrainCanvasDesc resolved{};
  return resolveDesc(desc, resolved) && resolved.control_resolution == desc.control_resolution &&
         heights.size() == squareSampleCount(desc.resolution) &&
         control.size() == squareSampleCount(desc.control_resolution) * 4u;
}

float finiteNormalized(float value) {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float sampleScalarBilinear(const assets::ScalarImage& image, float u, float v) {
  const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
  const float z = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
  const int x0 = static_cast<int>(std::floor(x));
  const int z0 = static_cast<int>(std::floor(z));
  const int x1 = std::min(x0 + 1, image.width - 1);
  const int z1 = std::min(z0 + 1, image.height - 1);
  const float tx = x - static_cast<float>(x0);
  const float tz = z - static_cast<float>(z0);
  const auto at = [&](int sx, int sz) {
    const std::size_t index = static_cast<std::size_t>(sz) *
                                  static_cast<std::size_t>(image.width) +
                              static_cast<std::size_t>(sx);
    return finiteNormalized(image.values[index]);
  };
  return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx),
                   std::lerp(at(x0, z1), at(x1, z1), tx),
                   tz);
}

float brushWeight(float normalized_distance, TerrainBrushFalloff falloff) {
  if (normalized_distance > 1.0f) {
    return 0.0f;
  }
  const float remaining = std::clamp(1.0f - normalized_distance, 0.0f, 1.0f);
  switch (falloff) {
    case TerrainBrushFalloff::Constant:
      return 1.0f;
    case TerrainBrushFalloff::Linear:
      return remaining;
    case TerrainBrushFalloff::Smooth:
      return remaining * remaining * (3.0f - 2.0f * remaining);
  }
  return 0.0f;
}

bool brushValid(float center_x,
                float center_z,
                const TerrainBrush& brush,
                float terrain_size) {
  return std::isfinite(center_x) && std::isfinite(center_z) &&
         std::isfinite(brush.radius) && brush.radius > 0.0f &&
         std::isfinite(brush.strength) && brush.strength > 0.0f &&
         center_x + brush.radius >= 0.0f && center_z + brush.radius >= 0.0f &&
         center_x - brush.radius <= terrain_size &&
         center_z - brush.radius <= terrain_size;
}

struct SampleBounds {
  uint32_t min_x = 0u;
  uint32_t max_x = 0u;
  uint32_t min_z = 0u;
  uint32_t max_z = 0u;
};

SampleBounds brushBounds(float center_x,
                         float center_z,
                         float radius,
                         float terrain_size,
                         uint32_t resolution) {
  const float samples_per_unit =
      static_cast<float>(resolution - 1u) / terrain_size;
  const auto lower = [&](float value) {
    return static_cast<uint32_t>(std::clamp(
        std::floor(value * samples_per_unit),
        0.0f,
        static_cast<float>(resolution - 1u)));
  };
  const auto upper = [&](float value) {
    return static_cast<uint32_t>(std::clamp(
        std::ceil(value * samples_per_unit),
        0.0f,
        static_cast<float>(resolution - 1u)));
  };
  return {
      .min_x = lower(center_x - radius),
      .max_x = upper(center_x + radius),
      .min_z = lower(center_z - radius),
      .max_z = upper(center_z + radius),
  };
}

std::array<uint8_t, 4u> quantizeWeights(const std::array<float, 4u>& source) {
  std::array<float, 4u> normalized{};
  float sum = 0.0f;
  for (uint32_t channel = 0u; channel < 4u; ++channel) {
    normalized[channel] = finiteNormalized(source[channel]);
    sum += normalized[channel];
  }
  if (sum <= 0.0f) {
    return {255u, 0u, 0u, 0u};
  }
  for (float& weight : normalized) {
    weight /= sum;
  }

  std::array<uint8_t, 4u> result{};
  std::array<float, 4u> remainders{};
  uint32_t assigned = 0u;
  for (uint32_t channel = 0u; channel < 4u; ++channel) {
    const float scaled = normalized[channel] * 255.0f;
    const uint32_t integral = static_cast<uint32_t>(std::floor(scaled));
    result[channel] = static_cast<uint8_t>(integral);
    remainders[channel] = scaled - static_cast<float>(integral);
    assigned += integral;
  }
  while (assigned < 255u) {
    uint32_t best = 0u;
    for (uint32_t channel = 1u; channel < 4u; ++channel) {
      if (remainders[channel] > remainders[best]) {
        best = channel;
      }
    }
    ++result[best];
    remainders[best] = -1.0f;
    ++assigned;
  }
  return result;
}

void setDiagnostic(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
}

std::filesystem::path temporaryPath(const std::filesystem::path& path) {
  static std::atomic<uint64_t> sequence{0u};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return path.parent_path() /
         (path.filename().string() + ".tmp." + std::to_string(stamp) + "." +
          std::to_string(sequence.fetch_add(1u)));
}

void removeTemporary(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

bool commitTemporary(const std::filesystem::path& temporary,
                     const std::filesystem::path& destination,
                     std::string* diagnostic) {
  std::error_code ec;
#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(),
                   destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()),
                         std::system_category());
  }
#else
  std::filesystem::rename(temporary, destination, ec);
#endif
  if (!ec) return true;
  removeTemporary(temporary);
  setDiagnostic(diagnostic,
                "could not atomically replace terrain output: " + ec.message());
  return false;
}

bool intersectSlab(float origin,
                   float direction,
                   float min_value,
                   float max_value,
                   float& enter,
                   float& exit) {
  constexpr float kDirectionEpsilon = 1.0e-7f;
  if (std::abs(direction) <= kDirectionEpsilon) {
    return origin >= min_value && origin <= max_value;
  }
  float a = (min_value - origin) / direction;
  float b = (max_value - origin) / direction;
  if (a > b) {
    std::swap(a, b);
  }
  enter = std::max(enter, a);
  exit = std::min(exit, b);
  return enter <= exit;
}

}  // namespace

std::optional<TerrainCanvas> TerrainCanvas::create(
    const TerrainCanvasDesc& desc,
    float initial_normalized_height) {
  TerrainCanvasDesc resolved{};
  if (!resolveDesc(desc, resolved) || !std::isfinite(initial_normalized_height)) {
    return std::nullopt;
  }
  TerrainCanvas canvas;
  canvas.desc_ = resolved;
  canvas.heights_.assign(squareSampleCount(resolved.resolution),
                         finiteNormalized(initial_normalized_height));
  canvas.control_rgba8_.assign(
      squareSampleCount(resolved.control_resolution) * 4u, 0u);
  for (std::size_t offset = 0u; offset < canvas.control_rgba8_.size(); offset += 4u) {
    canvas.control_rgba8_[offset] = 255u;
  }
  return canvas;
}

std::optional<TerrainCanvas> TerrainCanvas::import(
    const TerrainCanvasDesc& desc,
    const assets::ScalarImage& image) {
  if (!image.valid()) {
    return std::nullopt;
  }
  std::optional<TerrainCanvas> canvas = create(desc);
  if (!canvas.has_value()) {
    return std::nullopt;
  }
  const float denominator = static_cast<float>(canvas->desc_.resolution - 1u);
  for (uint32_t z = 0u; z < canvas->desc_.resolution; ++z) {
    for (uint32_t x = 0u; x < canvas->desc_.resolution; ++x) {
      canvas->heights_[static_cast<std::size_t>(z) * canvas->desc_.resolution + x] =
          sampleScalarBilinear(image,
                               static_cast<float>(x) / denominator,
                               static_cast<float>(z) / denominator);
    }
  }
  return canvas;
}

bool TerrainCanvas::valid() const {
  if (!storageValid(desc_, heights_, control_rgba8_)) {
    return false;
  }
  for (float height : heights_) {
    if (!std::isfinite(height) || height < 0.0f || height > 1.0f) {
      return false;
    }
  }
  for (std::size_t offset = 0u; offset < control_rgba8_.size(); offset += 4u) {
    const uint32_t sum = static_cast<uint32_t>(control_rgba8_[offset]) +
                         static_cast<uint32_t>(control_rgba8_[offset + 1u]) +
                         static_cast<uint32_t>(control_rgba8_[offset + 2u]) +
                         static_cast<uint32_t>(control_rgba8_[offset + 3u]);
    if (sum != 255u) {
      return false;
    }
  }
  return true;
}

std::optional<float> TerrainCanvas::sampleNormalizedHeight(float local_x,
                                                           float local_z) const {
  if (!storageValid(desc_, heights_, control_rgba8_) ||
      !std::isfinite(local_x) || !std::isfinite(local_z) ||
      local_x < 0.0f || local_z < 0.0f ||
      local_x > desc_.terrain_size || local_z > desc_.terrain_size) {
    return std::nullopt;
  }
  const float scale =
      static_cast<float>(desc_.resolution - 1u) / desc_.terrain_size;
  const float sample_x = std::clamp(local_x * scale,
                                    0.0f,
                                    static_cast<float>(desc_.resolution - 1u));
  const float sample_z = std::clamp(local_z * scale,
                                    0.0f,
                                    static_cast<float>(desc_.resolution - 1u));
  const uint32_t x0 = static_cast<uint32_t>(std::floor(sample_x));
  const uint32_t z0 = static_cast<uint32_t>(std::floor(sample_z));
  const uint32_t x1 = std::min(x0 + 1u, desc_.resolution - 1u);
  const uint32_t z1 = std::min(z0 + 1u, desc_.resolution - 1u);
  const float tx = sample_x - static_cast<float>(x0);
  const float tz = sample_z - static_cast<float>(z0);
  const auto at = [&](uint32_t x, uint32_t z) {
    return finiteNormalized(heights_[static_cast<std::size_t>(z) * desc_.resolution + x]);
  };
  return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx),
                   std::lerp(at(x0, z1), at(x1, z1), tx),
                   tz);
}

std::optional<float> TerrainCanvas::sampleWorldHeight(
    float world_x,
    float world_z,
    const math::Vec3& terrain_origin) const {
  if (!math::isFinite(terrain_origin) || !std::isfinite(world_x) ||
      !std::isfinite(world_z)) {
    return std::nullopt;
  }
  const std::optional<float> normalized = sampleNormalizedHeight(
      world_x - terrain_origin.x, world_z - terrain_origin.z);
  if (!normalized.has_value()) {
    return std::nullopt;
  }
  return terrain_origin.y + desc_.height_offset +
         *normalized * desc_.height_scale;
}

std::optional<TerrainRaycastHit> TerrainCanvas::raycast(
    const math::Vec3& ray_origin,
    const math::Vec3& ray_direction,
    const math::Vec3& terrain_origin,
    float max_distance) const {
  if (!storageValid(desc_, heights_, control_rgba8_) ||
      !math::isFinite(ray_origin) || !math::isFinite(ray_direction) ||
      !math::isFinite(terrain_origin) || std::isnan(max_distance) ||
      max_distance <= 0.0f) {
    return std::nullopt;
  }
  const math::Vec3 direction = math::normalize(ray_direction);
  if (math::lengthSquared(direction) <= 0.0f) {
    return std::nullopt;
  }

  float enter = 0.0f;
  float exit = max_distance;
  const float min_y = terrain_origin.y + desc_.height_offset;
  const float max_y = min_y + desc_.height_scale;
  if (!intersectSlab(ray_origin.x,
                     direction.x,
                     terrain_origin.x,
                     terrain_origin.x + desc_.terrain_size,
                     enter,
                     exit) ||
      !intersectSlab(ray_origin.y, direction.y, min_y, max_y, enter, exit) ||
      !intersectSlab(ray_origin.z,
                     direction.z,
                     terrain_origin.z,
                     terrain_origin.z + desc_.terrain_size,
                     enter,
                     exit) ||
      exit < 0.0f || !std::isfinite(enter)) {
    return std::nullopt;
  }
  enter = std::max(enter, 0.0f);
  if (!std::isfinite(exit)) {
    return std::nullopt;
  }

  auto pointAt = [&](float distance) {
    return math::add(ray_origin, math::scale(direction, distance));
  };
  auto difference = [&](float distance, float& out_normalized) -> std::optional<float> {
    const math::Vec3 point = pointAt(distance);
    const float clamped_x = std::clamp(point.x,
                                       terrain_origin.x,
                                       terrain_origin.x + desc_.terrain_size);
    const float clamped_z = std::clamp(point.z,
                                       terrain_origin.z,
                                       terrain_origin.z + desc_.terrain_size);
    const std::optional<float> normalized = sampleNormalizedHeight(
        clamped_x - terrain_origin.x, clamped_z - terrain_origin.z);
    if (!normalized.has_value()) {
      return std::nullopt;
    }
    out_normalized = *normalized;
    const float surface_y = terrain_origin.y + desc_.height_offset +
                            *normalized * desc_.height_scale;
    return point.y - surface_y;
  };

  constexpr float kSurfaceEpsilon = 1.0e-4f;
  float previous_t = enter;
  float previous_height = 0.0f;
  std::optional<float> previous_difference = difference(previous_t, previous_height);
  if (!previous_difference.has_value()) {
    return std::nullopt;
  }
  float hit_t = previous_t;
  float hit_height = previous_height;
  bool found = std::abs(*previous_difference) <= kSurfaceEpsilon;

  if (!found) {
    const float horizontal_speed =
        std::sqrt(direction.x * direction.x + direction.z * direction.z);
    const float cell_size =
        desc_.terrain_size / static_cast<float>(desc_.resolution - 1u);
    const float preferred_step = horizontal_speed > 1.0e-6f
                                     ? (cell_size * 0.25f) / horizontal_speed
                                     : std::max(exit - enter, 1.0e-4f);
    const uint32_t max_steps = desc_.resolution * 8u + 64u;
    const uint32_t step_count = std::clamp(
        static_cast<uint32_t>(std::ceil((exit - enter) /
                                        std::max(preferred_step, 1.0e-5f))),
        1u,
        max_steps);
    const float step = (exit - enter) / static_cast<float>(step_count);
    for (uint32_t index = 1u; index <= step_count; ++index) {
      const float current_t = index == step_count
                                  ? exit
                                  : enter + step * static_cast<float>(index);
      float current_height = 0.0f;
      const std::optional<float> current_difference =
          difference(current_t, current_height);
      if (!current_difference.has_value()) {
        break;
      }
      if (std::abs(*current_difference) <= kSurfaceEpsilon ||
          (*previous_difference < 0.0f && *current_difference > 0.0f) ||
          (*previous_difference > 0.0f && *current_difference < 0.0f)) {
        float low = previous_t;
        float high = current_t;
        float low_difference = *previous_difference;
        for (uint32_t iteration = 0u; iteration < 20u; ++iteration) {
          const float middle = (low + high) * 0.5f;
          float middle_height = 0.0f;
          const std::optional<float> middle_difference =
              difference(middle, middle_height);
          if (!middle_difference.has_value()) {
            break;
          }
          hit_t = middle;
          hit_height = middle_height;
          if (std::abs(*middle_difference) <= kSurfaceEpsilon) {
            break;
          }
          if ((low_difference < 0.0f && *middle_difference > 0.0f) ||
              (low_difference > 0.0f && *middle_difference < 0.0f)) {
            high = middle;
          } else {
            low = middle;
            low_difference = *middle_difference;
          }
        }
        found = true;
        break;
      }
      previous_t = current_t;
      previous_difference = current_difference;
      previous_height = current_height;
    }
  }
  if (!found) {
    return std::nullopt;
  }

  math::Vec3 hit_position = pointAt(hit_t);
  hit_position.x = std::clamp(hit_position.x,
                              terrain_origin.x,
                              terrain_origin.x + desc_.terrain_size);
  hit_position.z = std::clamp(hit_position.z,
                              terrain_origin.z,
                              terrain_origin.z + desc_.terrain_size);
  const std::optional<float> final_height = sampleNormalizedHeight(
      hit_position.x - terrain_origin.x, hit_position.z - terrain_origin.z);
  if (!final_height.has_value()) {
    return std::nullopt;
  }
  hit_height = *final_height;
  hit_position.y = terrain_origin.y + desc_.height_offset +
                   hit_height * desc_.height_scale;

  const float cell_size =
      desc_.terrain_size / static_cast<float>(desc_.resolution - 1u);
  const float local_x = hit_position.x - terrain_origin.x;
  const float local_z = hit_position.z - terrain_origin.z;
  const float x0 = std::max(local_x - cell_size, 0.0f);
  const float x1 = std::min(local_x + cell_size, desc_.terrain_size);
  const float z0 = std::max(local_z - cell_size, 0.0f);
  const float z1 = std::min(local_z + cell_size, desc_.terrain_size);
  const float hx0 = sampleNormalizedHeight(x0, local_z).value_or(hit_height) *
                    desc_.height_scale;
  const float hx1 = sampleNormalizedHeight(x1, local_z).value_or(hit_height) *
                    desc_.height_scale;
  const float hz0 = sampleNormalizedHeight(local_x, z0).value_or(hit_height) *
                    desc_.height_scale;
  const float hz1 = sampleNormalizedHeight(local_x, z1).value_or(hit_height) *
                    desc_.height_scale;
  const math::Vec3 tangent_x{x1 - x0, hx1 - hx0, 0.0f};
  const math::Vec3 tangent_z{0.0f, hz1 - hz0, z1 - z0};
  math::Vec3 normal = math::normalize(math::cross(tangent_z, tangent_x));
  if (math::lengthSquared(normal) <= 0.0f) {
    normal = {0.0f, 1.0f, 0.0f};
  }
  return TerrainRaycastHit{
      .position = hit_position,
      .normal = normal,
      .distance = hit_t,
      .normalized_height = hit_height,
  };
}

bool TerrainCanvas::applySculpt(float local_x,
                                float local_z,
                                TerrainSculptMode mode,
                                const TerrainBrush& brush,
                                float target_normalized_height) {
  if (!storageValid(desc_, heights_, control_rgba8_) ||
      !brushValid(local_x, local_z, brush, desc_.terrain_size) ||
      !std::isfinite(target_normalized_height)) {
    return false;
  }
  const SampleBounds bounds = brushBounds(local_x,
                                          local_z,
                                          brush.radius,
                                          desc_.terrain_size,
                                          desc_.resolution);
  const float spacing =
      desc_.terrain_size / static_cast<float>(desc_.resolution - 1u);
  const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
  const float target = finiteNormalized(target_normalized_height);
  const uint32_t width = bounds.max_x - bounds.min_x + 1u;
  const uint32_t height = bounds.max_z - bounds.min_z + 1u;
  std::vector<float> smooth_targets;
  if (mode == TerrainSculptMode::Smooth) {
    smooth_targets.resize(static_cast<std::size_t>(width) * height);
    for (uint32_t z = bounds.min_z; z <= bounds.max_z; ++z) {
      for (uint32_t x = bounds.min_x; x <= bounds.max_x; ++x) {
        float sum = 0.0f;
        uint32_t count = 0u;
        const uint32_t neighbor_min_z = z > 0u ? z - 1u : 0u;
        const uint32_t neighbor_max_z = std::min(z + 1u, desc_.resolution - 1u);
        const uint32_t neighbor_min_x = x > 0u ? x - 1u : 0u;
        const uint32_t neighbor_max_x = std::min(x + 1u, desc_.resolution - 1u);
        for (uint32_t nz = neighbor_min_z; nz <= neighbor_max_z; ++nz) {
          for (uint32_t nx = neighbor_min_x; nx <= neighbor_max_x; ++nx) {
            sum += finiteNormalized(
                heights_[static_cast<std::size_t>(nz) * desc_.resolution + nx]);
            ++count;
          }
        }
        smooth_targets[static_cast<std::size_t>(z - bounds.min_z) * width +
                       (x - bounds.min_x)] = sum / static_cast<float>(count);
      }
    }
  }

  bool changed = false;
  for (uint32_t z = bounds.min_z; z <= bounds.max_z; ++z) {
    for (uint32_t x = bounds.min_x; x <= bounds.max_x; ++x) {
      const float sample_x = static_cast<float>(x) * spacing;
      const float sample_z = static_cast<float>(z) * spacing;
      const float dx = sample_x - local_x;
      const float dz = sample_z - local_z;
      const float distance = std::sqrt(dx * dx + dz * dz);
      const float alpha = strength * brushWeight(distance / brush.radius, brush.falloff);
      if (alpha <= 0.0f) {
        continue;
      }
      const std::size_t sample_index =
          static_cast<std::size_t>(z) * desc_.resolution + x;
      const float current = finiteNormalized(heights_[sample_index]);
      float result = current;
      switch (mode) {
        case TerrainSculptMode::Raise:
          result = current + alpha;
          break;
        case TerrainSculptMode::Lower:
          result = current - alpha;
          break;
        case TerrainSculptMode::Smooth:
          result = std::lerp(
              current,
              smooth_targets[static_cast<std::size_t>(z - bounds.min_z) * width +
                             (x - bounds.min_x)],
              alpha);
          break;
        case TerrainSculptMode::Flatten: {
          const float delta = target - current;
          result = current + std::clamp(delta, -alpha, alpha);
          break;
        }
        case TerrainSculptMode::SetHeight:
          result = std::lerp(current, target, alpha);
          break;
      }
      result = finiteNormalized(result);
      if (result != heights_[sample_index]) {
        heights_[sample_index] = result;
        changed = true;
      }
    }
  }
  return changed;
}

bool TerrainCanvas::paintLayer(float local_x,
                               float local_z,
                               uint32_t layer,
                               const TerrainBrush& brush) {
  if (!storageValid(desc_, heights_, control_rgba8_) || layer >= 4u ||
      !brushValid(local_x, local_z, brush, desc_.terrain_size)) {
    return false;
  }
  const SampleBounds bounds = brushBounds(local_x,
                                          local_z,
                                          brush.radius,
                                          desc_.terrain_size,
                                          desc_.control_resolution);
  const float spacing =
      desc_.terrain_size / static_cast<float>(desc_.control_resolution - 1u);
  const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
  bool changed = false;
  for (uint32_t z = bounds.min_z; z <= bounds.max_z; ++z) {
    for (uint32_t x = bounds.min_x; x <= bounds.max_x; ++x) {
      const float dx = static_cast<float>(x) * spacing - local_x;
      const float dz = static_cast<float>(z) * spacing - local_z;
      const float distance = std::sqrt(dx * dx + dz * dz);
      const float alpha = strength * brushWeight(distance / brush.radius, brush.falloff);
      if (alpha <= 0.0f) {
        continue;
      }
      const std::size_t offset =
          (static_cast<std::size_t>(z) * desc_.control_resolution + x) * 4u;
      std::array<float, 4u> weights{};
      float sum = 0.0f;
      for (uint32_t channel = 0u; channel < 4u; ++channel) {
        weights[channel] = static_cast<float>(control_rgba8_[offset + channel]) / 255.0f;
        sum += weights[channel];
      }
      if (sum <= 0.0f) {
        weights = {1.0f, 0.0f, 0.0f, 0.0f};
        sum = 1.0f;
      }
      for (float& weight : weights) {
        weight /= sum;
      }
      const float old_selected = weights[layer];
      const float new_selected = old_selected + (1.0f - old_selected) * alpha;
      const float old_others = 1.0f - old_selected;
      if (old_others > 1.0e-7f) {
        const float scale = (1.0f - new_selected) / old_others;
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
          if (channel != layer) {
            weights[channel] *= scale;
          }
        }
      } else {
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
          if (channel != layer) {
            weights[channel] = 0.0f;
          }
        }
      }
      weights[layer] = new_selected;
      const std::array<uint8_t, 4u> quantized = quantizeWeights(weights);
      for (uint32_t channel = 0u; channel < 4u; ++channel) {
        if (control_rgba8_[offset + channel] != quantized[channel]) {
          control_rgba8_[offset + channel] = quantized[channel];
          changed = true;
        }
      }
    }
  }
  return changed;
}

rendering::TerrainTileData TerrainCanvas::buildTileData(
    rendering::TerrainTileCoord coord) const {
  rendering::TerrainTileData tile{};
  if (!valid()) {
    return tile;
  }
  tile.coord = coord;
  tile.resolution = desc_.resolution;
  tile.heights = heights_;
  tile.color_width = 1u;
  tile.color_height = 1u;
  tile.color_rgba8 = {255u, 255u, 255u, 255u};
  tile.control_width = desc_.control_resolution;
  tile.control_height = desc_.control_resolution;
  tile.control_rgba8 = control_rgba8_;
  return tile;
}

bool TerrainCanvas::saveHeightR32(const std::filesystem::path& path,
                                  std::string* diagnostic) const {
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!valid()) {
    setDiagnostic(diagnostic, "terrain canvas is invalid");
    return false;
  }
  if (path.empty()) {
    setDiagnostic(diagnostic, "height output path is empty");
    return false;
  }
  const std::filesystem::path temporary = temporaryPath(path);
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) {
    setDiagnostic(diagnostic, "could not open height output");
    return false;
  }
  constexpr std::size_t kSamplesPerWrite = 4096u;
  std::array<uint8_t, kSamplesPerWrite * sizeof(float)> bytes{};
  for (std::size_t first = 0u; first < heights_.size(); first += kSamplesPerWrite) {
    const std::size_t sample_count =
        std::min(kSamplesPerWrite, heights_.size() - first);
    for (std::size_t index = 0u; index < sample_count; ++index) {
      const uint32_t bits = std::bit_cast<uint32_t>(heights_[first + index]);
      bytes[index * 4u + 0u] = static_cast<uint8_t>(bits & 0xFFu);
      bytes[index * 4u + 1u] = static_cast<uint8_t>((bits >> 8u) & 0xFFu);
      bytes[index * 4u + 2u] = static_cast<uint8_t>((bits >> 16u) & 0xFFu);
      bytes[index * 4u + 3u] = static_cast<uint8_t>((bits >> 24u) & 0xFFu);
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(sample_count * sizeof(float)));
    if (!stream) {
      break;
    }
  }
  stream.close();
  if (!stream) {
    removeTemporary(temporary);
    setDiagnostic(diagnostic, "could not write height output");
    return false;
  }
  return commitTemporary(temporary, path, diagnostic);
}

bool TerrainCanvas::saveControlTga(const std::filesystem::path& path,
                                   std::string* diagnostic) const {
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!valid()) {
    setDiagnostic(diagnostic, "terrain canvas is invalid");
    return false;
  }
  if (path.empty()) {
    setDiagnostic(diagnostic, "control output path is empty");
    return false;
  }
  const std::filesystem::path temporary = temporaryPath(path);
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) {
    setDiagnostic(diagnostic, "could not open control output");
    return false;
  }
  std::array<uint8_t, 18u> header{};
  header[2] = 2u;
  header[12] = static_cast<uint8_t>(desc_.control_resolution & 0xFFu);
  header[13] = static_cast<uint8_t>((desc_.control_resolution >> 8u) & 0xFFu);
  header[14] = static_cast<uint8_t>(desc_.control_resolution & 0xFFu);
  header[15] = static_cast<uint8_t>((desc_.control_resolution >> 8u) & 0xFFu);
  header[16] = 32u;
  header[17] = 0x28u;
  stream.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
  constexpr std::size_t kPixelsPerWrite = 4096u;
  std::array<uint8_t, kPixelsPerWrite * 4u> bgra{};
  const std::size_t pixel_count = control_rgba8_.size() / 4u;
  for (std::size_t first = 0u; first < pixel_count; first += kPixelsPerWrite) {
    const std::size_t chunk_pixels =
        std::min(kPixelsPerWrite, pixel_count - first);
    for (std::size_t index = 0u; index < chunk_pixels; ++index) {
      const std::size_t source = (first + index) * 4u;
      const std::size_t destination = index * 4u;
      bgra[destination + 0u] = control_rgba8_[source + 2u];
      bgra[destination + 1u] = control_rgba8_[source + 1u];
      bgra[destination + 2u] = control_rgba8_[source + 0u];
      bgra[destination + 3u] = control_rgba8_[source + 3u];
    }
    stream.write(reinterpret_cast<const char*>(bgra.data()),
                 static_cast<std::streamsize>(chunk_pixels * 4u));
    if (!stream) {
      break;
    }
  }
  stream.close();
  if (!stream) {
    removeTemporary(temporary);
    setDiagnostic(diagnostic, "could not write control output");
    return false;
  }
  return commitTemporary(temporary, path, diagnostic);
}

}  // namespace karma::scene_authoring
