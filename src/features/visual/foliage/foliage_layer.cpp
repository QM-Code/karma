#include "karma/foliage.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <set>
#include <tuple>
#include <utility>

namespace karma::foliage {
namespace {

bool validChunkSize(float value) {
  return std::isfinite(value) && value > 0.0f;
}

bool validInstance(const FoliageInstance& instance) {
  if (!math::isFinite(instance.position) ||
      !std::isfinite(instance.yaw_radians) ||
      !math::isFinite(instance.scale)) {
    return false;
  }
  return std::all_of(instance.params.begin(), instance.params.end(), [](float value) {
    return std::isfinite(value);
  });
}

class SplitMix64 {
 public:
  explicit SplitMix64(uint64_t seed) : state_(seed) {}

  uint64_t next() {
    uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27u)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31u);
  }

  double unitDouble() {
    constexpr double kInverse53 = 1.0 / 9007199254740992.0;
    return static_cast<double>(next() >> 11u) * kInverse53;
  }

  float unitFloat() {
    return static_cast<float>(unitDouble());
  }

 private:
  uint64_t state_;
};

uint64_t mixHash(uint64_t state, uint64_t value) {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
  value ^= value >> 31u;
  return state ^ (value + 0x9e3779b97f4a7c15ull + (state << 6u) + (state >> 2u));
}

uint64_t instanceHash(const FoliageInstance& instance, uint64_t seed) {
  uint64_t hash = seed;
  const auto add_float = [&](float value) {
    if (value == 0.0f) {
      value = 0.0f;
    }
    hash = mixHash(hash, std::bit_cast<uint32_t>(value));
  };
  add_float(instance.position.x);
  add_float(instance.position.y);
  add_float(instance.position.z);
  add_float(instance.yaw_radians);
  add_float(instance.scale.x);
  add_float(instance.scale.y);
  add_float(instance.scale.z);
  for (float value : instance.params) {
    add_float(value);
  }
  return hash;
}

float randomRange(SplitMix64& random, float a, float b) {
  const double low = static_cast<double>(std::min(a, b));
  const double high = static_cast<double>(std::max(a, b));
  const double value =
      std::clamp(low + (high - low) * random.unitDouble(), low, high);
  return static_cast<float>(value);
}

bool sameInstance(const FoliageInstance& a, const FoliageInstance& b) {
  return a == b;
}

bool removeOne(std::vector<FoliageInstance>& instances,
               const FoliageInstance& wanted) {
  const auto reverse_it = std::find_if(
      instances.rbegin(), instances.rend(),
      [&](const FoliageInstance& candidate) { return sameInstance(candidate, wanted); });
  if (reverse_it == instances.rend()) {
    return false;
  }
  instances.erase(std::next(reverse_it).base());
  return true;
}

}  // namespace

FoliageLayer::FoliageLayer(float chunk_size)
    : chunk_size_(validChunkSize(chunk_size) ? chunk_size : 32.0f) {}

FoliageLayer::FoliageLayer(const FoliageDocument& document)
    : FoliageLayer(document.chunk_size) {
  for (const FoliageChunk& chunk : document.chunks) {
    for (const FoliageInstance& instance : chunk.instances) {
      if (instance_count_ >= kMaxAuthoredFoliageInstances) {
        return;
      }
      if (!validInstance(instance)) {
        continue;
      }
      const FoliageChunkCoord coord = foliageChunkCoordForPosition(
          instance.position.x, instance.position.z, chunk_size_);
      chunks_[coord].push_back(instance);
      ++instance_count_;
    }
  }
}

const std::vector<FoliageInstance>* FoliageLayer::instancesInChunk(
    FoliageChunkCoord coord) const {
  const auto it = chunks_.find(coord);
  return it == chunks_.end() ? nullptr : &it->second;
}

bool FoliageLayer::setChunkSize(float chunk_size) {
  if (!validChunkSize(chunk_size)) {
    return false;
  }
  if (chunk_size == chunk_size_) {
    return true;
  }
  ChunkMap reindexed;
  for (const auto& [coord, instances] : chunks_) {
    (void)coord;
    for (const FoliageInstance& instance : instances) {
      reindexed[foliageChunkCoordForPosition(
          instance.position.x, instance.position.z, chunk_size)]
          .push_back(instance);
    }
  }
  chunk_size_ = chunk_size;
  chunks_ = std::move(reindexed);
  return true;
}

bool FoliageLayer::replaceChunk(FoliageChunkCoord coord,
                                std::vector<FoliageInstance> instances) {
  for (const FoliageInstance& instance : instances) {
    if (!validInstance(instance) ||
        foliageChunkCoordForPosition(
            instance.position.x, instance.position.z, chunk_size_) != coord) {
      return false;
    }
  }
  const auto it = chunks_.find(coord);
  const std::size_t previous_count =
      it == chunks_.end() ? 0u : it->second.size();
  if (instances.size() > kMaxAuthoredFoliageInstances -
                             (instance_count_ - previous_count)) {
    return false;
  }
  instance_count_ -= previous_count;
  if (instances.empty()) {
    chunks_.erase(coord);
    return true;
  }
  instance_count_ += instances.size();
  chunks_[coord] = std::move(instances);
  return true;
}

std::vector<FoliageInstance> FoliageLayer::clearChunk(FoliageChunkCoord coord) {
  const auto it = chunks_.find(coord);
  if (it == chunks_.end()) {
    return {};
  }
  std::vector<FoliageInstance> removed = std::move(it->second);
  instance_count_ -= removed.size();
  chunks_.erase(it);
  return removed;
}

void FoliageLayer::clear() {
  chunks_.clear();
  instance_count_ = 0u;
}

bool FoliageLayer::hasNeighborWithin(float x, float z, float spacing) const {
  if (!(spacing > 0.0f) || !std::isfinite(spacing)) {
    return false;
  }
  const FoliageChunkCoord min_coord = foliageChunkCoordForPosition(
      x - spacing, z - spacing, chunk_size_);
  const FoliageChunkCoord max_coord = foliageChunkCoordForPosition(
      x + spacing, z + spacing, chunk_size_);
  const int64_t width = static_cast<int64_t>(max_coord.x) - min_coord.x + 1;
  const int64_t height = static_cast<int64_t>(max_coord.z) - min_coord.z + 1;
  const float spacing_squared = spacing * spacing;
  const auto has_neighbor = [&](const std::vector<FoliageInstance>& instances) {
    return std::any_of(instances.begin(), instances.end(), [&](const auto& instance) {
      const float dx = instance.position.x - x;
      const float dz = instance.position.z - z;
      return dx * dx + dz * dz < spacing_squared;
    });
  };

  if (width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= 4096u) {
    for (int64_t chunk_x = min_coord.x; chunk_x <= max_coord.x; ++chunk_x) {
      for (int64_t chunk_z = min_coord.z; chunk_z <= max_coord.z; ++chunk_z) {
        const auto it = chunks_.find(FoliageChunkCoord{
            static_cast<int32_t>(chunk_x), static_cast<int32_t>(chunk_z)});
        if (it != chunks_.end() && has_neighbor(it->second)) {
          return true;
        }
      }
    }
    return false;
  }

  for (const auto& [coord, instances] : chunks_) {
    if (coord.x < min_coord.x || coord.x > max_coord.x ||
        coord.z < min_coord.z || coord.z > max_coord.z) {
      continue;
    }
    if (has_neighbor(instances)) {
      return true;
    }
  }
  return false;
}

FoliageEditResult FoliageLayer::paint(
    const FoliagePaintBrush& brush,
    const FoliageSurfaceSampler& surface) {
  FoliageEditResult result{};
  if (!surface || !math::isFinite(brush.center) ||
      !std::isfinite(brush.radius) || brush.radius <= 0.0f ||
      !std::isfinite(brush.density) || brush.density <= 0.0f ||
      !std::isfinite(brush.min_spacing) || brush.min_spacing < 0.0f ||
      !std::isfinite(brush.min_yaw_radians) ||
      !std::isfinite(brush.max_yaw_radians) ||
      !math::isFinite(brush.min_scale) || !math::isFinite(brush.max_scale) ||
      std::isnan(brush.min_height) || std::isnan(brush.max_height) ||
      !std::isfinite(brush.max_slope_degrees)) {
    return result;
  }
  for (float value : brush.params) {
    if (!std::isfinite(value)) {
      return result;
    }
  }

  const double target_double = std::ceil(
      std::numbers::pi * static_cast<double>(brush.radius) * brush.radius *
      brush.density);
  const uint32_t requested = static_cast<uint32_t>(std::clamp(
      target_double,
      0.0,
      static_cast<double>(kMaxFoliageInstancesPerBrushStroke)));
  const uint32_t target = static_cast<uint32_t>(std::min<std::size_t>(
      requested, kMaxAuthoredFoliageInstances - instance_count_));
  if (target == 0u) {
    return result;
  }
  const uint64_t attempts = std::min<uint64_t>(
      std::max<uint64_t>(static_cast<uint64_t>(target) * 16u, 64u),
      static_cast<uint64_t>(kMaxFoliageInstancesPerBrushStroke) * 16u);
  const float slope = std::clamp(brush.max_slope_degrees, 0.0f, 90.0f);
  const float min_normal_y = std::cos(slope * std::numbers::pi_v<float> / 180.0f);
  const float min_height = std::min(brush.min_height, brush.max_height);
  const float max_height = std::max(brush.min_height, brush.max_height);
  std::set<FoliageChunkCoord> affected;
  SplitMix64 random(brush.seed);

  for (uint64_t attempt = 0u;
       attempt < attempts && result.added.size() < target;
       ++attempt) {
    const double radial =
        std::sqrt(random.unitDouble()) * static_cast<double>(brush.radius);
    const double angle =
        random.unitDouble() * 2.0 * std::numbers::pi_v<double>;
    const double x_double =
        static_cast<double>(brush.center.x) + std::cos(angle) * radial;
    const double z_double =
        static_cast<double>(brush.center.z) + std::sin(angle) * radial;
    constexpr double kMaxFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(x_double) || !std::isfinite(z_double) ||
        std::abs(x_double) > kMaxFloat || std::abs(z_double) > kMaxFloat) {
      continue;
    }
    const float x = static_cast<float>(x_double);
    const float z = static_cast<float>(z_double);
    const auto sample = surface(x, z);
    if (!sample.has_value() || !std::isfinite(sample->height) ||
        !math::isFinite(sample->normal) ||
        sample->height < min_height || sample->height > max_height) {
      continue;
    }
    const math::Vec3 normal = math::normalize(sample->normal);
    if (math::lengthSquared(normal) == 0.0f || normal.y < min_normal_y ||
        hasNeighborWithin(x, z, brush.min_spacing)) {
      continue;
    }

    FoliageInstance instance{};
    instance.position = {x, sample->height, z};
    instance.yaw_radians = randomRange(
        random, brush.min_yaw_radians, brush.max_yaw_radians);
    instance.scale = {
        randomRange(random, brush.min_scale.x, brush.max_scale.x),
        randomRange(random, brush.min_scale.y, brush.max_scale.y),
        randomRange(random, brush.min_scale.z, brush.max_scale.z),
    };
    instance.params = brush.params;
    if (!validInstance(instance)) {
      continue;
    }
    const FoliageChunkCoord coord = foliageChunkCoordForPosition(x, z, chunk_size_);
    chunks_[coord].push_back(instance);
    ++instance_count_;
    result.added.push_back({.chunk = coord, .instance = instance});
    affected.insert(coord);
  }
  result.affected_chunks.assign(affected.begin(), affected.end());
  return result;
}

FoliageEditResult FoliageLayer::erase(const FoliageEraseBrush& brush) {
  FoliageEditResult result{};
  if (!math::isFinite(brush.center) || !std::isfinite(brush.radius) ||
      brush.radius <= 0.0f || !std::isfinite(brush.strength) ||
      brush.strength <= 0.0f) {
    return result;
  }
  const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
  const float radius_squared = brush.radius * brush.radius;
  std::set<FoliageChunkCoord> affected;
  for (auto chunk_it = chunks_.begin(); chunk_it != chunks_.end();) {
    auto& instances = chunk_it->second;
    const FoliageChunkCoord coord = chunk_it->first;
    for (auto instance_it = instances.begin(); instance_it != instances.end();) {
      const float dx = instance_it->position.x - brush.center.x;
      const float dz = instance_it->position.z - brush.center.z;
      if (dx * dx + dz * dz > radius_squared) {
        ++instance_it;
        continue;
      }
      const uint64_t hash = instanceHash(*instance_it, brush.seed);
      constexpr double kInverse53 = 1.0 / 9007199254740992.0;
      const float selection = static_cast<float>(
          static_cast<double>(hash >> 11u) * kInverse53);
      if (selection >= strength) {
        ++instance_it;
        continue;
      }
      result.removed.push_back({.chunk = coord, .instance = *instance_it});
      instance_it = instances.erase(instance_it);
      --instance_count_;
      affected.insert(coord);
    }
    if (instances.empty()) {
      chunk_it = chunks_.erase(chunk_it);
    } else {
      ++chunk_it;
    }
  }
  result.affected_chunks.assign(affected.begin(), affected.end());
  return result;
}

bool FoliageLayer::applyEdit(const FoliageEditResult& edit, bool reverse) {
  FoliageLayer next = *this;
  const auto& removals = reverse ? edit.added : edit.removed;
  const auto& additions = reverse ? edit.removed : edit.added;
  for (const FoliageInstanceEdit& removal : removals) {
    auto it = next.chunks_.find(removal.chunk);
    if (it == next.chunks_.end() || !removeOne(it->second, removal.instance)) {
      return false;
    }
    --next.instance_count_;
    if (it->second.empty()) {
      next.chunks_.erase(it);
    }
  }
  for (const FoliageInstanceEdit& addition : additions) {
    if (!validInstance(addition.instance) ||
        foliageChunkCoordForPosition(addition.instance.position.x,
                                     addition.instance.position.z,
                                     next.chunk_size_) != addition.chunk) {
      return false;
    }
    if (next.instance_count_ >= kMaxAuthoredFoliageInstances) {
      return false;
    }
    next.chunks_[addition.chunk].push_back(addition.instance);
    ++next.instance_count_;
  }
  *this = std::move(next);
  return true;
}

FoliageDocument FoliageLayer::toDocument() const {
  FoliageDocument document{};
  document.chunk_size = chunk_size_;
  document.chunks.reserve(chunks_.size());
  for (const auto& [coord, instances] : chunks_) {
    document.chunks.push_back(FoliageChunk{.coord = coord, .instances = instances});
  }
  return document;
}

}  // namespace karma::foliage
