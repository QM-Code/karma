#include "karma/foliage.h"

#include "foliage_render_prototype.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

namespace karma::foliage {
namespace {

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

world::Entity entityFromKey(uint64_t key) {
  world::Entity entity{};
  entity.index = static_cast<uint32_t>(key >> 32u);
  entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
  return entity;
}

bool same(const math::Vec3& a, const math::Vec3& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool same(const math::Quat& a, const math::Quat& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool sameJson(const nlohmann::json& a, const nlohmann::json& b) {
  return a == b;
}

bool sameLod(const std::optional<components::LodComponent>& a,
             const std::optional<components::LodComponent>& b) {
  if (a.has_value() != b.has_value()) return false;
  if (!a.has_value()) return true;
  if (a->levels.size() != b->levels.size()) return false;
  for (std::size_t index = 0; index < a->levels.size(); ++index) {
    const auto& left = a->levels[index];
    const auto& right = b->levels[index];
    const bool same_start_distance =
        left.start_distance == right.start_distance ||
        (std::isnan(left.start_distance) &&
         std::isnan(right.start_distance));
    if (!same_start_distance ||
        left.mesh_asset_key != right.mesh_asset_key ||
        left.materials != right.materials ||
        left.render_mode != right.render_mode ||
        left.shadow_visible != right.shadow_visible) {
      return false;
    }
  }
  return true;
}

float yawFromQuaternion(const math::Quat& quaternion) {
  const math::Quat q = math::normalize(quaternion);
  return std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                    1.0f - 2.0f * (q.x * q.x + q.y * q.y));
}

math::Vec3 transformPosition(const FoliageInstance& instance,
                             const components::TransformComponent* transform) {
  if (transform == nullptr) {
    return instance.position;
  }
  const math::Vec3 scaled = math::multiply(instance.position, transform->getScale());
  return math::add(transform->getPosition(),
                   math::rotateVec(transform->getRotation(), scaled));
}

components::PlanarMeshInstance makeProxyInstance(
    const FoliageInstance& source,
    const components::TransformComponent* transform) {
  components::PlanarMeshInstance result{};
  result.position = transformPosition(source, transform);
  result.yaw_radians = source.yaw_radians +
                       (transform != nullptr
                            ? yawFromQuaternion(transform->getRotation())
                            : 0.0f);
  result.scale = transform != nullptr
                     ? math::multiply(source.scale, transform->getScale())
                     : source.scale;
  result.params = source.params;
  return result;
}

float chunkDistanceSquared(FoliageChunkCoord coord,
                           float chunk_size,
                           const math::Vec3& camera_position,
                           const components::TransformComponent* transform) {
  FoliageInstance center{};
  center.position = {
      (static_cast<float>(coord.x) + 0.5f) * chunk_size,
      0.0f,
      (static_cast<float>(coord.z) + 0.5f) * chunk_size,
  };
  const math::Vec3 world_center = transformPosition(center, transform);
  const float dx = world_center.x - camera_position.x;
  const float dz = world_center.z - camera_position.z;
  return dx * dx + dz * dz;
}

float transformedChunkRadius(float chunk_size,
                             const components::TransformComponent* transform) {
  float max_scale = 1.0f;
  if (transform != nullptr) {
    max_scale = std::max(std::abs(transform->getScale().x),
                         std::abs(transform->getScale().z));
  }
  return chunk_size * 0.7071067811865475244f * max_scale;
}

bool validOverrideDocument(const FoliageDocument& document,
                           std::string& error) {
  if (!std::isfinite(document.chunk_size) || document.chunk_size <= 0.0f) {
    error = "in-memory foliage override has an invalid chunk size";
    return false;
  }
  uint64_t instance_count = 0u;
  for (const FoliageChunk& chunk : document.chunks) {
    if (chunk.instances.empty()) {
      continue;
    }
    if (chunk.instances.size() >
        static_cast<uint64_t>(kMaxAuthoredFoliageInstances) - instance_count) {
      error = "in-memory foliage override exceeds the authored instance limit";
      return false;
    }
    instance_count += chunk.instances.size();
    for (const FoliageInstance& instance : chunk.instances) {
      if (!math::isFinite(instance.position) ||
          !std::isfinite(instance.yaw_radians) ||
          !math::isFinite(instance.scale) ||
          foliageChunkCoordForPosition(instance.position.x,
                                       instance.position.z,
                                       document.chunk_size) != chunk.coord ||
          !std::all_of(instance.params.begin(), instance.params.end(),
                       [](float value) { return std::isfinite(value); })) {
        error = "in-memory foliage override contains an invalid or misindexed instance";
        return false;
      }
    }
  }
  return true;
}

}  // namespace

class FoliageRuntimeModule::Impl {
 public:
  Impl() = default;

  ~Impl() {
    stopWorker();
  }

  void setReferenceRoot(std::filesystem::path root) {
    std::lock_guard lock(public_mutex_);
    reference_root_ = std::move(root);
    ++reference_root_revision_;
  }

  void setLayerOverride(world::Entity source,
                        std::shared_ptr<const FoliageDocument> document) {
    std::lock_guard lock(public_mutex_);
    const uint64_t key = entityKey(source);
    if (!document) {
      overrides_.erase(key);
      ++override_sequence_;
      return;
    }
    overrides_[key] = Override{
        .document = std::move(document),
        .revision = ++override_sequence_,
    };
  }

  void clearLayerOverride(world::Entity source) {
    std::lock_guard lock(public_mutex_);
    if (overrides_.erase(entityKey(source)) != 0u) {
      ++override_sequence_;
    }
  }

  void clearLayerOverrides() {
    std::lock_guard lock(public_mutex_);
    if (!overrides_.empty()) {
      overrides_.clear();
      ++override_sequence_;
    }
  }

  std::vector<FoliageDiagnostic> diagnostics() const {
    std::lock_guard lock(public_mutex_);
    return diagnostics_;
  }

  FoliageRuntimeStats stats() const {
    std::lock_guard lock(public_mutex_);
    return stats_;
  }

  void attach(assets::AssetRegistry* assets) {
    stopWorker();
    assets_ = assets;
    {
      std::lock_guard lock(queue_mutex_);
      stop_worker_ = false;
      requests_.clear();
    }
    worker_ = std::thread([this] { workerLoop(); });
  }

  void detach() {
    stopWorker();
    cleanupWorld();
    assets_ = nullptr;
  }

  void update(world::World& world, float interpolation_alpha) {
    if (last_world_id_ != 0u && last_world_id_ != world.instanceId()) {
      cleanupWorld();
      clearOverridesForWorldSwitch();
    }
    last_world_ = world.lifetimeHandle();
    last_world_id_ = world.instanceId();

    drainCompleted(world);
    math::Vec3 camera_position{};
    bool has_primary_camera = false;
    world.forEach<components::CameraComponent, components::TransformComponent>(
        [&](world::Entity entity) {
      const auto& camera = world.get<components::CameraComponent>(entity);
      if (!camera.is_primary) {
        return true;
      }
      camera_position = world.get<components::TransformComponent>(entity)
                            .getInterpolatedPosition(interpolation_alpha);
      has_primary_camera = true;
      return false;
    });

    const std::vector<world::Entity> sources =
        world.storage<components::FoliageComponent>().denseEntities();
    std::unordered_set<uint64_t> active;
    active.reserve(sources.size());
    uint32_t remaining_resident_budget =
        kDefaultMaxResidentFoliageInstances;
    for (world::Entity source : sources) {
      const uint64_t key = entityKey(source);
      active.insert(key);
      SourceState& state = ensureState(world, source);
      const auto& component = world.get<components::FoliageComponent>(source);
      syncSource(world,
                 source,
                 component,
                 state,
                 camera_position,
                 has_primary_camera,
                 interpolation_alpha,
                 remaining_resident_budget);
    }

    for (auto it = sources_.begin(); it != sources_.end();) {
      if (active.contains(it->first)) {
        ++it;
        continue;
      }
      destroyState(world, it->second);
      eraseOverrideForKey(it->first, it->second.override_revision);
      it = sources_.erase(it);
    }
    pruneDeadOverrides(world);
    publishStats();
  }

 private:
  struct Override {
    std::shared_ptr<const FoliageDocument> document;
    uint64_t revision = 0u;
  };

  struct PublicSnapshot {
    std::filesystem::path reference_root;
    uint64_t reference_root_revision = 0u;
    std::shared_ptr<const FoliageDocument> override_document;
    uint64_t override_revision = 0u;
  };

  struct SourceState {
    world::Entity source{};
    world::Entity instance_set_proxy{};
    std::vector<world::Entity> render_proxies;
    bool initialized = false;
    uint64_t generation = 0u;
    uint64_t source_revision = 0u;
    uint64_t reference_root_revision = 0u;
    uint64_t override_revision = 0u;
    std::filesystem::path authored_sidecar_path;
    std::filesystem::path authored_prefab_path;
    nlohmann::json authored_prefab_variables = nlohmann::json::object();
    std::optional<components::LodComponent> authored_direct_lod;
    std::filesystem::path resolved_path;
    std::filesystem::path resolved_prefab_path;
    std::filesystem::file_time_type prefab_modified =
        std::filesystem::file_time_type::min();
    std::optional<assets::AssetPackageHandle> prefab_package;
    std::vector<detail::FoliageRenderPrototypePart> prototype_parts;
    bool prototype_valid = false;
    std::shared_ptr<const FoliageFileIndex> file_index;
    std::shared_ptr<const FoliageDocument> override_document;
    bool override_valid = false;
    std::map<FoliageChunkCoord, std::vector<const FoliageChunk*>> override_chunks;
    std::map<FoliageChunkCoord, std::vector<FoliageInstance>> resident;
    std::map<FoliageChunkCoord, uint32_t> desired_counts;
    std::set<FoliageChunkCoord> queued;
    std::set<FoliageChunkCoord> failed;
    std::string component_validation_error;
    bool proxy_dirty = true;
    bool prototype_dirty = true;
    math::Vec3 last_position{};
    math::Quat last_rotation{};
    math::Vec3 last_scale{1.0f, 1.0f, 1.0f};
    bool had_transform = false;
  };

  struct LoadRequest {
    uint64_t source_key = 0u;
    uint64_t generation = 0u;
    std::filesystem::path path;
    std::shared_ptr<const FoliageFileIndex> index;
    FoliageChunkCoord coord{};
    uint32_t instance_limit = 0u;
  };

  struct CompletedLoad {
    uint64_t source_key = 0u;
    uint64_t generation = 0u;
    FoliageChunkCoord coord{};
    uint32_t requested_count = 0u;
    std::vector<FoliageInstance> instances;
    std::filesystem::path path;
    std::string error;
  };

  PublicSnapshot snapshotFor(world::Entity source) const {
    std::lock_guard lock(public_mutex_);
    PublicSnapshot snapshot{};
    snapshot.reference_root = reference_root_;
    snapshot.reference_root_revision = reference_root_revision_;
    const auto it = overrides_.find(entityKey(source));
    if (it != overrides_.end()) {
      snapshot.override_document = it->second.document;
      snapshot.override_revision = it->second.revision;
    }
    return snapshot;
  }

  uint64_t nextSourceGeneration() {
    ++source_generation_sequence_;
    if (source_generation_sequence_ == 0u) {
      ++source_generation_sequence_;
    }
    return source_generation_sequence_;
  }

  void eraseOverrideForKey(uint64_t key, uint64_t expected_revision) {
    std::lock_guard lock(public_mutex_);
    const auto it = overrides_.find(key);
    if (it != overrides_.end() && it->second.revision == expected_revision) {
      overrides_.erase(it);
      ++override_sequence_;
    }
  }

  void clearOverridesForWorldSwitch() {
    std::lock_guard lock(public_mutex_);
    if (!overrides_.empty()) {
      overrides_.clear();
      ++override_sequence_;
    }
  }

  void pruneDeadOverrides(const world::World& world) {
    std::lock_guard lock(public_mutex_);
    bool changed = false;
    for (auto it = overrides_.begin(); it != overrides_.end();) {
      if (!world.isAlive(entityFromKey(it->first))) {
        it = overrides_.erase(it);
        changed = true;
      } else {
        ++it;
      }
    }
    if (changed) {
      ++override_sequence_;
    }
  }

  void addDiagnostic(world::Entity source,
                     const std::filesystem::path& path,
                     std::string message) {
    spdlog::error("Foliage source {}:{} '{}': {}",
                  source.index,
                  source.generation,
                  path.string(),
                  message);
    std::lock_guard lock(public_mutex_);
    if (diagnostics_.size() >= 128u) {
      diagnostics_.erase(diagnostics_.begin());
    }
    diagnostics_.push_back(FoliageDiagnostic{
        .source = source,
        .path = path,
        .message = std::move(message),
    });
  }

  SourceState& ensureState(world::World& world, world::Entity source) {
    const uint64_t key = entityKey(source);
    auto [it, inserted] = sources_.try_emplace(key);
    SourceState& state = it->second;
    if (inserted) {
      state.source = source;
      state.instance_set_proxy = world.createEntity();
      world.setName(state.instance_set_proxy,
                    "__karma_foliage_instances_" +
                        std::to_string(source.index));
      components::InstanceSetComponent instances{};
      instances.gpu_layout =
          rendering::InstanceGpuLayout::PositionYawScaleParams;
      instances.dynamic = false;
      world.add(state.instance_set_proxy, std::move(instances));
    }
    return state;
  }

  void releasePrototypePackage(SourceState& state) {
    detail::releaseFoliageRenderPrototypePackage(
        assets_, state.prefab_package);
  }

  bool rebuildPrototype(world::World& world,
                        world::Entity source,
                        const components::FoliageComponent& component,
                        SourceState& state,
                        const PublicSnapshot& snapshot) {
    releasePrototypePackage(state);
    state.prototype_parts.clear();
    state.resolved_prefab_path.clear();
    state.prefab_modified = std::filesystem::file_time_type::min();
    detail::FoliageRenderPrototypeBuild built =
        detail::buildFoliageRenderPrototype(world,
                                            source,
                                            component,
                                            snapshot.reference_root,
                                            assets_);
    state.resolved_prefab_path = std::move(built.resolved_prefab_path);
    state.prefab_modified = built.prefab_modified;
    state.prefab_package = std::move(built.prefab_package);
    state.prototype_parts = std::move(built.parts);
    for (auto& diagnostic : built.diagnostics) {
      addDiagnostic(source,
                    std::move(diagnostic.path),
                    std::move(diagnostic.message));
    }
    state.prototype_valid = built.success;
    state.prototype_dirty = true;
    return built.success;
  }

  void resetSource(world::World& world,
                   world::Entity source,
                   const components::FoliageComponent& component,
                   SourceState& state,
                   const PublicSnapshot& snapshot) {
    state.generation = nextSourceGeneration();
    state.initialized = true;
    state.source_revision = component.source_revision;
    state.reference_root_revision = snapshot.reference_root_revision;
    state.override_revision = snapshot.override_revision;
    state.authored_sidecar_path = component.sidecar_path;
    state.authored_prefab_path = component.prefab_path;
    state.authored_prefab_variables = component.prefab_variables;
    state.authored_direct_lod =
        world.has<components::LodComponent>(source)
            ? std::optional<components::LodComponent>(
                  world.get<components::LodComponent>(source))
            : std::nullopt;
    state.override_document = snapshot.override_document;
    state.override_valid = false;
    state.file_index.reset();
    state.override_chunks.clear();
    state.resident.clear();
    state.desired_counts.clear();
    state.queued.clear();
    state.failed.clear();
    state.proxy_dirty = true;
    state.prototype_dirty = true;
    state.resolved_path.clear();

    if (!rebuildPrototype(world, source, component, state, snapshot)) {
      return;
    }

    if (snapshot.override_document != nullptr) {
      std::string error;
      if (!validOverrideDocument(*snapshot.override_document, error)) {
        addDiagnostic(source, {}, std::move(error));
        return;
      }
      for (const FoliageChunk& chunk : snapshot.override_document->chunks) {
        if (!chunk.instances.empty()) {
          state.override_chunks[chunk.coord].push_back(&chunk);
        }
      }
      state.override_valid = true;
      return;
    }

    state.resolved_path = component.sidecar_path;
    if (state.resolved_path.is_relative() && !snapshot.reference_root.empty()) {
      state.resolved_path = snapshot.reference_root / state.resolved_path;
    }
    if (state.resolved_path.empty()) {
      addDiagnostic(source, state.resolved_path, "foliage sidecar path is empty");
      return;
    }
    std::string error;
    auto index = readFoliageFileIndex(state.resolved_path, &error);
    if (!index.has_value()) {
      addDiagnostic(source, state.resolved_path, std::move(error));
      return;
    }
    if (std::isfinite(component.chunk_size) && component.chunk_size > 0.0f &&
        std::abs(component.chunk_size - index->chunk_size) > 1.0e-4f) {
      addDiagnostic(source,
                    state.resolved_path,
                    "component chunk_size does not match its foliage sidecar; "
                    "the sidecar value will be used");
    }
    state.file_index =
        std::make_shared<const FoliageFileIndex>(std::move(*index));
    (void)world;
  }

  void syncSource(world::World& world,
                  world::Entity source,
                  const components::FoliageComponent& component,
                  SourceState& state,
                  const math::Vec3& camera_position,
                  bool has_primary_camera,
                  float interpolation_alpha,
                  uint32_t& remaining_resident_budget) {
    std::string validation_error;
    if (!validateFoliageComponent(component, &validation_error)) {
      if (state.component_validation_error != validation_error) {
        state.component_validation_error = validation_error;
        state.generation = nextSourceGeneration();
        state.initialized = false;
        state.file_index.reset();
        state.override_document.reset();
        state.override_valid = false;
        state.override_chunks.clear();
        state.resident.clear();
        state.desired_counts.clear();
        state.queued.clear();
        state.failed.clear();
        state.resolved_path.clear();
        addDiagnostic(source, component.sidecar_path, validation_error);
      }
      suppressProxy(world, state);
      return;
    }
    if (!state.component_validation_error.empty()) {
      state.component_validation_error.clear();
      state.initialized = false;
    }

    const PublicSnapshot snapshot = snapshotFor(source);
    const std::optional<components::LodComponent> current_direct_lod =
        world.has<components::LodComponent>(source)
            ? std::optional<components::LodComponent>(
                  world.get<components::LodComponent>(source))
            : std::nullopt;
    std::filesystem::path resolved_prefab = component.prefab_path;
    if (!resolved_prefab.empty() && resolved_prefab.is_relative() &&
        !snapshot.reference_root.empty()) {
      resolved_prefab = snapshot.reference_root / resolved_prefab;
    }
    if (!resolved_prefab.empty()) {
      resolved_prefab =
          detail::resolveFoliagePrefabPath(std::move(resolved_prefab));
    }
    const bool prefab_changed_on_disk =
        !resolved_prefab.empty() &&
        (resolved_prefab != state.resolved_prefab_path ||
         detail::foliagePrefabModifiedTime(resolved_prefab) !=
             state.prefab_modified);
    if (!state.initialized || state.source_revision != component.source_revision ||
        state.authored_sidecar_path != component.sidecar_path ||
        state.authored_prefab_path != component.prefab_path ||
        !sameJson(state.authored_prefab_variables,
                  component.prefab_variables) ||
        !sameLod(state.authored_direct_lod, current_direct_lod) ||
        prefab_changed_on_disk ||
        state.reference_root_revision != snapshot.reference_root_revision ||
        state.override_revision != snapshot.override_revision ||
        state.override_document != snapshot.override_document) {
      resetSource(world, source, component, state, snapshot);
    }

    if (!state.prototype_valid) {
      suppressProxy(world, state);
      return;
    }

    components::TransformComponent interpolated{};
    const components::TransformComponent* transform = nullptr;
    if (world.has<components::TransformComponent>(source)) {
      const auto& source_transform =
          world.get<components::TransformComponent>(source);
      interpolated = components::TransformComponent{
          source_transform.getInterpolatedPosition(interpolation_alpha),
          source_transform.getInterpolatedRotation(interpolation_alpha),
          source_transform.getScale()};
      transform = &interpolated;
    }
    const bool transform_changed =
        state.had_transform != (transform != nullptr) ||
        (transform != nullptr &&
         (!same(state.last_position, transform->getPosition()) ||
          !same(state.last_rotation, transform->getRotation()) ||
          !same(state.last_scale, transform->getScale())));
    if (transform != nullptr) {
      state.last_position = transform->getPosition();
      state.last_rotation = transform->getRotation();
      state.last_scale = transform->getScale();
    }
    state.had_transform = transform != nullptr;
    state.proxy_dirty = state.proxy_dirty || transform_changed;

    bool visible = component.visible;
    if (world.has<components::VisibilityComponent>(source)) {
      visible = visible &&
                world.get<components::VisibilityComponent>(source).visible;
    }
    std::vector<std::tuple<float, FoliageChunkCoord, uint32_t>> candidates;
    float chunk_size = component.chunk_size;
    if (state.override_document != nullptr && state.override_valid) {
      chunk_size = state.override_document->chunk_size;
      candidates.reserve(state.override_chunks.size());
      for (const auto& [coord, chunks] : state.override_chunks) {
        uint64_t instance_count = 0u;
        for (const FoliageChunk* chunk : chunks) {
          instance_count += chunk->instances.size();
        }
        candidates.emplace_back(
            chunkDistanceSquared(coord, chunk_size, camera_position, transform),
            coord,
            static_cast<uint32_t>(std::min<uint64_t>(
                instance_count, std::numeric_limits<uint32_t>::max())));
      }
    } else if (state.file_index != nullptr) {
      chunk_size = state.file_index->chunk_size;
      candidates.reserve(state.file_index->chunks.size());
      for (const FoliageChunkInfo& info : state.file_index->chunks) {
        candidates.emplace_back(
            chunkDistanceSquared(info.coord, chunk_size, camera_position, transform),
            info.coord,
            info.instance_count);
      }
    }

    const float view_distance =
        std::isfinite(component.view_distance)
            ? std::max(component.view_distance, 0.0f)
            : 0.0f;
    const float distance_limit =
        view_distance + transformedChunkRadius(chunk_size, transform);
    const float distance_limit_squared = distance_limit * distance_limit;
    if (!has_primary_camera || !visible) {
      candidates.clear();
    } else {
      std::erase_if(candidates, [&](const auto& candidate) {
        return std::get<0>(candidate) > distance_limit_squared;
      });
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b)) {
          return std::get<0>(a) < std::get<0>(b);
        }
        return std::get<1>(a) < std::get<1>(b);
      });
    }

    const uint32_t hard_cap =
        std::min(component.max_resident_instances, remaining_resident_budget);
    std::map<FoliageChunkCoord, uint32_t> desired;
    uint32_t selected = 0u;
    for (const auto& [distance, coord, count] : candidates) {
      (void)distance;
      if (selected >= hard_cap) {
        break;
      }
      if (state.failed.contains(coord)) {
        continue;
      }
      const uint32_t accepted = std::min(count, hard_cap - selected);
      if (accepted == 0u) {
        continue;
      }
      desired[coord] = accepted;
      selected += accepted;
    }
    remaining_resident_budget -= selected;

    for (auto it = state.resident.begin(); it != state.resident.end();) {
      const auto desired_it = desired.find(it->first);
      if (desired_it == desired.end() ||
          it->second.size() != desired_it->second) {
        it = state.resident.erase(it);
        state.proxy_dirty = true;
      } else {
        ++it;
      }
    }
    state.desired_counts = desired;

    for (const auto& [coord, accepted] : desired) {
      if (state.resident.contains(coord) || state.queued.contains(coord) ||
          state.failed.contains(coord)) {
        continue;
      }
      if (state.override_document != nullptr) {
        const auto chunk_it = state.override_chunks.find(coord);
        if (chunk_it == state.override_chunks.end()) {
          continue;
        }
        auto& resident = state.resident[coord];
        resident.clear();
        resident.reserve(accepted);
        uint32_t remaining = accepted;
        for (const FoliageChunk* chunk : chunk_it->second) {
          const std::size_t copy_count = std::min<std::size_t>(
              chunk->instances.size(), remaining);
          resident.insert(resident.end(),
                          chunk->instances.begin(),
                          chunk->instances.begin() + copy_count);
          remaining -= static_cast<uint32_t>(copy_count);
          if (remaining == 0u) {
            break;
          }
        }
        state.proxy_dirty = true;
        continue;
      }
      if (state.file_index == nullptr) {
        continue;
      }
      {
        std::lock_guard lock(queue_mutex_);
        if (stop_worker_ ||
            outstanding_requests_ >= kMaxFoliageOutstandingChunkRequests) {
          continue;
        }
        requests_.push_back(LoadRequest{
            .source_key = entityKey(source),
            .generation = state.generation,
            .path = state.resolved_path,
            .index = state.file_index,
            .coord = coord,
            .instance_limit = accepted,
        });
        ++outstanding_requests_;
      }
      state.queued.insert(coord);
      queue_cv_.notify_one();
    }

    syncProxy(world, component, state, transform, visible);
  }

  void suppressProxy(world::World& world, SourceState& state) {
    if (world.isAlive(state.instance_set_proxy) &&
        world.has<components::InstanceSetComponent>(
            state.instance_set_proxy)) {
      auto& instances = world.get<components::InstanceSetComponent>(
          state.instance_set_proxy);
      const bool had_instances =
          !instances.instances.empty() || !instances.planar_instances.empty();
      instances.instances.clear();
      instances.planar_instances.clear();
      if (had_instances) {
        ++instances.instance_revision;
      }
    }
    for (world::Entity proxy_entity : state.render_proxies) {
      if (!world.isAlive(proxy_entity) ||
          !world.has<components::InstancedMeshComponent>(proxy_entity)) {
        continue;
      }
      auto& proxy =
          world.get<components::InstancedMeshComponent>(proxy_entity);
      proxy.visible = false;
      proxy.shadow_visible = false;
    }
    state.proxy_dirty = false;
  }

  void syncRenderProxyCount(world::World& world, SourceState& state) {
    while (state.render_proxies.size() > state.prototype_parts.size()) {
      const world::Entity entity = state.render_proxies.back();
      if (world.isAlive(entity)) {
        world.destroyEntity(entity);
      }
      state.render_proxies.pop_back();
    }
    while (state.render_proxies.size() < state.prototype_parts.size()) {
      const std::size_t index = state.render_proxies.size();
      const world::Entity entity = world.createEntity();
      world.setName(entity,
                    "__karma_foliage_batch_" +
                        std::to_string(state.source.index) + "_" +
                        std::to_string(index));
      components::InstancedMeshComponent batch{};
      batch.instance_source = state.instance_set_proxy;
      world.add(entity, std::move(batch));
      state.render_proxies.push_back(entity);
    }
  }

  void syncProxy(world::World& world,
                 const components::FoliageComponent& source,
                 SourceState& state,
                 const components::TransformComponent* transform,
                 bool visible) {
    if (!world.isAlive(state.instance_set_proxy) ||
        !world.has<components::InstanceSetComponent>(
            state.instance_set_proxy)) {
      state.instance_set_proxy = world.createEntity();
      components::InstanceSetComponent instances{};
      instances.gpu_layout =
          rendering::InstanceGpuLayout::PositionYawScaleParams;
      instances.dynamic = false;
      world.add(state.instance_set_proxy, std::move(instances));
      state.proxy_dirty = true;
    }
    syncRenderProxyCount(world, state);
    for (std::size_t index = 0; index < state.prototype_parts.size(); ++index) {
      const detail::FoliageRenderPrototypePart& part =
          state.prototype_parts[index];
      const world::Entity proxy_entity = state.render_proxies[index];
      auto& proxy =
          world.get<components::InstancedMeshComponent>(proxy_entity);
      proxy.mesh_asset_key = part.mesh.mesh_asset_key;
      proxy.materials = part.mesh.materials;
      proxy.instance_source = state.instance_set_proxy;
      proxy.local_position = part.local_position;
      proxy.local_rotation = part.local_rotation;
      proxy.local_scale = part.local_scale;
      proxy.visible = visible && part.mesh.visible;
      proxy.shadow_visible = proxy.visible && source.shadow_visible &&
                             part.mesh.shadow_visible;

      if (part.lod.has_value()) {
        world.add(proxy_entity, *part.lod);
      } else if (world.has<components::LodComponent>(proxy_entity)) {
        world.remove<components::LodComponent>(proxy_entity);
      }
      if (!part.render_tags.empty()) {
        components::RenderTagsComponent tags{};
        tags.tags = part.render_tags;
        world.add(proxy_entity, std::move(tags));
      } else if (world.has<components::RenderTagsComponent>(proxy_entity)) {
        world.remove<components::RenderTagsComponent>(proxy_entity);
      }
    }
    state.prototype_dirty = false;

    if (state.proxy_dirty) {
      auto& instances = world.get<components::InstanceSetComponent>(
          state.instance_set_proxy);
      instances.gpu_layout =
          rendering::InstanceGpuLayout::PositionYawScaleParams;
      instances.dynamic = false;
      instances.instances.clear();
      instances.planar_instances.clear();
      std::size_t count = 0u;
      for (const auto& [coord, resident] : state.resident) {
        (void)coord;
        count += resident.size();
      }
      instances.planar_instances.reserve(count);
      for (const auto& [coord, resident] : state.resident) {
        (void)coord;
        for (const FoliageInstance& instance : resident) {
          instances.planar_instances.push_back(
              makeProxyInstance(instance, transform));
        }
      }
      ++instances.instance_revision;
    }
    state.proxy_dirty = false;
  }

  void queueDiagnostic(const CompletedLoad& completed) {
    const auto it = sources_.find(completed.source_key);
    if (it == sources_.end() || it->second.generation != completed.generation) {
      return;
    }
    addDiagnostic(it->second.source, completed.path, completed.error);
  }

  void drainCompleted(world::World& world) {
    (void)world;
    std::vector<CompletedLoad> completed;
    {
      std::lock_guard lock(completed_mutex_);
      completed.swap(completed_);
    }
    {
      std::lock_guard lock(queue_mutex_);
      outstanding_requests_ -=
          std::min(outstanding_requests_, completed.size());
    }
    for (CompletedLoad& result : completed) {
      const auto state_it = sources_.find(result.source_key);
      if (state_it == sources_.end() ||
          state_it->second.generation != result.generation) {
        continue;
      }
      SourceState& state = state_it->second;
      state.queued.erase(result.coord);
      const auto desired_it = state.desired_counts.find(result.coord);
      if (desired_it == state.desired_counts.end()) {
        continue;
      }
      if (!result.error.empty()) {
        state.failed.insert(result.coord);
        queueDiagnostic(result);
        continue;
      }
      if (result.instances.size() != result.requested_count) {
        addDiagnostic(state.source,
                      result.path,
                      "streamed foliage chunk returned fewer instances than requested");
        state.failed.insert(result.coord);
        continue;
      }
      if (result.requested_count < desired_it->second) {
        // The desired limit grew while this request was in flight. Discard the
        // smaller result so the normal sync pass below can queue the new limit.
        continue;
      }
      if (result.instances.size() > desired_it->second) {
        result.instances.resize(desired_it->second);
      }
      if (result.instances.size() != desired_it->second) {
        addDiagnostic(state.source,
                      result.path,
                      "streamed foliage chunk returned fewer instances than indexed");
        state.failed.insert(result.coord);
        continue;
      }
      state.resident[result.coord] = std::move(result.instances);
      state.proxy_dirty = true;
    }
  }

  void workerLoop() {
    for (;;) {
      LoadRequest request{};
      {
        std::unique_lock lock(queue_mutex_);
        queue_cv_.wait(lock, [&] { return stop_worker_ || !requests_.empty(); });
        if (stop_worker_) {
          return;
        }
        request = std::move(requests_.front());
        requests_.pop_front();
      }
      CompletedLoad completed{};
      completed.source_key = request.source_key;
      completed.generation = request.generation;
      completed.coord = request.coord;
      completed.requested_count = request.instance_limit;
      completed.path = request.path;
      std::string error;
      auto chunk = readFoliageChunk(
          request.path,
          *request.index,
          request.coord,
          request.instance_limit,
          &error);
      if (!chunk.has_value()) {
        completed.error = std::move(error);
      } else {
        completed.instances = std::move(chunk->instances);
        if (completed.instances.size() > request.instance_limit) {
          completed.instances.resize(request.instance_limit);
        }
      }
      {
        std::lock_guard lock(completed_mutex_);
        completed_.push_back(std::move(completed));
      }
    }
  }

  void stopWorker() {
    {
      std::lock_guard lock(queue_mutex_);
      stop_worker_ = true;
      requests_.clear();
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
    {
      std::lock_guard lock(completed_mutex_);
      completed_.clear();
    }
    {
      std::lock_guard lock(queue_mutex_);
      outstanding_requests_ = 0u;
    }
  }

  void destroyState(world::World& world, SourceState& state) {
    for (world::Entity proxy : state.render_proxies) {
      if (world.isAlive(proxy)) {
        world.destroyEntity(proxy);
      }
    }
    state.render_proxies.clear();
    if (world.isAlive(state.instance_set_proxy)) {
      world.destroyEntity(state.instance_set_proxy);
    }
    releasePrototypePackage(state);
  }

  void cleanupWorld() {
    auto lease = last_world_.lock();
    world::World* world = lease.get();
    if (world != nullptr && world->instanceId() == last_world_id_) {
      for (auto& [key, state] : sources_) {
        (void)key;
        destroyState(*world, state);
      }
    } else {
      for (auto& [key, state] : sources_) {
        (void)key;
        releasePrototypePackage(state);
      }
    }
    sources_.clear();
    last_world_ = {};
    last_world_id_ = 0u;
    publishStats();
  }

  void publishStats() {
    FoliageRuntimeStats next{};
    next.source_count = sources_.size();
    for (const auto& [key, state] : sources_) {
      (void)key;
      next.resident_chunks += state.resident.size();
      next.queued_chunks += state.queued.size();
      for (const auto& [coord, instances] : state.resident) {
        (void)coord;
        next.resident_instances += instances.size();
      }
    }
    std::lock_guard lock(public_mutex_);
    stats_ = next;
  }

  mutable std::mutex public_mutex_;
  std::filesystem::path reference_root_;
  uint64_t reference_root_revision_ = 0u;
  uint64_t override_sequence_ = 0u;
  uint64_t source_generation_sequence_ = 0u;
  std::unordered_map<uint64_t, Override> overrides_;
  std::vector<FoliageDiagnostic> diagnostics_;
  FoliageRuntimeStats stats_{};

  std::unordered_map<uint64_t, SourceState> sources_;
  assets::AssetRegistry* assets_ = nullptr;
  world::World::LifetimeHandle last_world_;
  uint64_t last_world_id_ = 0u;

  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<LoadRequest> requests_;
  std::size_t outstanding_requests_ = 0u;
  bool stop_worker_ = true;
  std::thread worker_;

  std::mutex completed_mutex_;
  std::vector<CompletedLoad> completed_;
};

FoliageRuntimeModule::FoliageRuntimeModule()
    : impl_(std::make_unique<Impl>()) {}

FoliageRuntimeModule::~FoliageRuntimeModule() = default;

void FoliageRuntimeModule::setReferenceRoot(std::filesystem::path root) {
  impl_->setReferenceRoot(std::move(root));
}

void FoliageRuntimeModule::setLayerOverride(
    world::Entity source,
    std::shared_ptr<const FoliageDocument> document) {
  impl_->setLayerOverride(source, std::move(document));
}

void FoliageRuntimeModule::setLayerOverride(world::Entity source,
                                            const FoliageLayer& layer) {
  impl_->setLayerOverride(
      source, std::make_shared<const FoliageDocument>(layer.toDocument()));
}

void FoliageRuntimeModule::clearLayerOverride(world::Entity source) {
  impl_->clearLayerOverride(source);
}

void FoliageRuntimeModule::clearLayerOverrides() {
  impl_->clearLayerOverrides();
}

std::vector<FoliageDiagnostic> FoliageRuntimeModule::diagnostics() const {
  return impl_->diagnostics();
}

FoliageRuntimeStats FoliageRuntimeModule::stats() const {
  return impl_->stats();
}

void FoliageRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  impl_->attach(context.assets);
}

void FoliageRuntimeModule::onDetach() {
  impl_->detach();
}

void FoliageRuntimeModule::onUpdate(world::World& world,
                                    float,
                                    float interpolation_alpha) {
  impl_->update(world, interpolation_alpha);
}

}  // namespace karma::foliage
