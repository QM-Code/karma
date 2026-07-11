#pragma once

/// \file
/// Deterministic foliage authoring, sidecar I/O, and runtime streaming.

#include "karma/app.h"
#include "karma/components.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace karma::components {

/// \ingroup karma_components
/// File-backed visual-only grass or tree instances.
///
/// Positions stored in `sidecar_path` are local to this entity's transform.
/// `FoliageRuntimeModule` streams nearby chunks into an internal
/// `InstancedMeshComponent` proxy. The component has no physics or navigation
/// behavior.
struct FoliageComponent : world::ComponentTag {
  std::filesystem::path sidecar_path;
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  std::vector<InstancedMeshLodLevel> lods;
  float chunk_size = 32.0f;
  float view_distance = 256.0f;
  uint32_t max_resident_instances = 100000u;
  uint64_t source_revision = 0u;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components

namespace karma::foliage {

/// `.kfoliage` v1 is entirely little-endian. Its 48-byte header contains the
/// eight-byte `KFOLIAGE` magic, version/header sizes, chunk size/count, total
/// instance count, directory offset, and record sizes. Each sorted 52-byte
/// directory entry contains X/Z, min/max bounds, count, offset, and byte size.
/// Each 44-byte instance stores position (3xf32), yaw (f32), scale (3xf32),
/// and four f32 parameters.
inline constexpr uint32_t kFoliageFileVersion = 1u;
inline constexpr uint32_t kFoliageInstanceRecordSize = 44u;
inline constexpr uint32_t kDefaultMaxResidentFoliageInstances = 100000u;
inline constexpr uint32_t kMaxAuthoredFoliageInstances = 1000000u;
inline constexpr uint32_t kMaxFoliageInstancesPerBrushStroke = 1000000u;
inline constexpr std::size_t kMaxFoliageOutstandingChunkRequests = 256u;

/// Validates renderer-facing foliage component state authored directly through
/// the library API. An empty sidecar is allowed because an in-memory layer
/// override can supply the instances; serialized components impose the
/// additional requirement that the sidecar be a portable relative path.
inline bool validateFoliageComponent(
    const components::FoliageComponent& component,
    std::string* error = nullptr) {
  if (error != nullptr) {
    error->clear();
  }
  const auto fail = [&](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };

  if (component.mesh_asset_key.empty()) {
    return fail("foliage mesh asset key must not be empty");
  }
  if (!std::isfinite(component.chunk_size) || component.chunk_size <= 0.0f) {
    return fail("foliage chunk size must be finite and positive");
  }
  if (!std::isfinite(component.view_distance) ||
      component.view_distance <= 0.0f) {
    return fail("foliage view distance must be finite and positive");
  }
  if (component.max_resident_instances == 0u) {
    return fail("foliage resident instance limit must be positive");
  }
  for (const components::MeshMaterialAssignment& material :
       component.materials) {
    if (material.material_key.empty()) {
      return fail("foliage material key must not be empty");
    }
  }
  if (component.lods.size() > components::kMaxInstancedMeshLodLevels) {
    return fail("foliage component has too many LOD levels");
  }

  float previous_start_distance = 0.0f;
  for (const components::InstancedMeshLodLevel& lod : component.lods) {
    if (!std::isfinite(lod.start_distance) ||
        lod.start_distance <= previous_start_distance) {
      return fail(
          "foliage LOD distances must be finite, positive, and strictly "
          "increasing");
    }
    if (lod.mesh_asset_key.empty()) {
      return fail("foliage LOD mesh asset key must not be empty");
    }
    if (lod.render_mode != rendering::InstanceLodRenderMode::Mesh &&
        lod.render_mode !=
            rendering::InstanceLodRenderMode::UprightBillboard) {
      return fail("foliage LOD render mode is invalid");
    }
    for (const components::MeshMaterialAssignment& material : lod.materials) {
      if (material.material_key.empty()) {
        return fail("foliage LOD material key must not be empty");
      }
    }
    previous_start_distance = lod.start_distance;
  }
  return true;
}

/// Integer coordinate of one square foliage chunk.
struct FoliageChunkCoord {
  int32_t x = 0;
  int32_t z = 0;

  bool operator==(const FoliageChunkCoord&) const = default;
  auto operator<=>(const FoliageChunkCoord&) const = default;
};

/// Compact upright instance stored in terrain-local space.
struct FoliageInstance {
  math::Vec3 position{};
  float yaw_radians = 0.0f;
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  std::array<float, 4> params{0.0f, 0.0f, 0.0f, 0.0f};

  bool operator==(const FoliageInstance& other) const {
    return position.x == other.position.x &&
           position.y == other.position.y &&
           position.z == other.position.z &&
           yaw_radians == other.yaw_radians &&
           scale.x == other.scale.x &&
           scale.y == other.scale.y &&
           scale.z == other.scale.z && params == other.params;
  }
};

/// Instances belonging to one chunk.
struct FoliageChunk {
  FoliageChunkCoord coord{};
  std::vector<FoliageInstance> instances;

  bool operator==(const FoliageChunk&) const = default;
};

/// Complete in-memory representation of a `.kfoliage` v1 sidecar.
struct FoliageDocument {
  float chunk_size = 32.0f;
  std::vector<FoliageChunk> chunks;

  std::size_t instanceCount() const;
};

/// Validated directory entry used for partial sidecar reads.
struct FoliageChunkInfo {
  FoliageChunkCoord coord{};
  math::Vec3 bounds_min{};
  math::Vec3 bounds_max{};
  uint32_t instance_count = 0u;
  uint64_t data_offset = 0u;
};

/// Header and sorted directory from a validated `.kfoliage` file.
struct FoliageFileIndex {
  float chunk_size = 32.0f;
  uint64_t instance_count = 0u;
  uint64_t file_size = 0u;
  std::vector<FoliageChunkInfo> chunks;

  const FoliageChunkInfo* find(FoliageChunkCoord coord) const;
};

/// Writes a canonical little-endian `.kfoliage` v1 file.
///
/// Chunks and instances are sorted before writing, so equivalent documents
/// produce byte-identical output regardless of insertion order.
bool writeFoliageFile(const std::filesystem::path& path,
                      const FoliageDocument& document,
                      std::string* error = nullptr);

/// Reads and validates an entire `.kfoliage` v1 file.
std::optional<FoliageDocument> readFoliageFile(
    const std::filesystem::path& path,
    std::string* error = nullptr);

/// Reads and validates only the header and sorted chunk directory.
std::optional<FoliageFileIndex> readFoliageFileIndex(
    const std::filesystem::path& path,
    std::string* error = nullptr);

/// Reads one chunk using a previously validated index.
std::optional<FoliageChunk> readFoliageChunk(
    const std::filesystem::path& path,
    const FoliageFileIndex& index,
    FoliageChunkCoord coord,
    std::string* error = nullptr);

/// Reads at most the first `max_instances` records from one indexed chunk.
/// This is useful for bounded runtime residency when a single authored chunk
/// is larger than the active instance budget.
std::optional<FoliageChunk> readFoliageChunk(
    const std::filesystem::path& path,
    const FoliageFileIndex& index,
    FoliageChunkCoord coord,
    uint32_t max_instances,
    std::string* error = nullptr);

/// Returns the chunk containing a terrain-local X/Z position.
FoliageChunkCoord foliageChunkCoordForPosition(float x,
                                               float z,
                                               float chunk_size);

/// Sample returned by an editable terrain or other placement surface.
struct FoliageSurfaceSample {
  float height = 0.0f;
  math::Vec3 normal{0.0f, 1.0f, 0.0f};
};

using FoliageSurfaceSampler =
    std::function<std::optional<FoliageSurfaceSample>(float x, float z)>;

/// Deterministic paint-stroke settings.
struct FoliagePaintBrush {
  math::Vec3 center{};
  float radius = 1.0f;
  /// Target candidates per square terrain unit.
  float density = 1.0f;
  float min_spacing = 0.25f;
  float min_yaw_radians = 0.0f;
  float max_yaw_radians = 6.28318530717958647692f;
  math::Vec3 min_scale{1.0f, 1.0f, 1.0f};
  math::Vec3 max_scale{1.0f, 1.0f, 1.0f};
  float min_height = -std::numeric_limits<float>::infinity();
  float max_height = std::numeric_limits<float>::infinity();
  float max_slope_degrees = 90.0f;
  std::array<float, 4> params{0.0f, 0.0f, 0.0f, 0.0f};
  uint64_t seed = 0u;
};

/// Deterministic erase-stroke settings.
struct FoliageEraseBrush {
  math::Vec3 center{};
  float radius = 1.0f;
  /// Fraction removed in [0, 1]. Selection is stable for a given seed.
  float strength = 1.0f;
  uint64_t seed = 0u;
};

/// One added or removed instance, including its owning chunk.
struct FoliageInstanceEdit {
  FoliageChunkCoord chunk{};
  FoliageInstance instance{};
};

/// Reversible result of one foliage brush operation.
struct FoliageEditResult {
  std::vector<FoliageChunkCoord> affected_chunks;
  std::vector<FoliageInstanceEdit> added;
  std::vector<FoliageInstanceEdit> removed;

  bool empty() const { return added.empty() && removed.empty(); }
};

/// Editable foliage layer with chunk-based spatial indexing.
class FoliageLayer {
 public:
  using ChunkMap = std::map<FoliageChunkCoord, std::vector<FoliageInstance>>;

  explicit FoliageLayer(float chunk_size = 32.0f);
  explicit FoliageLayer(const FoliageDocument& document);

  float chunkSize() const { return chunk_size_; }
  std::size_t instanceCount() const { return instance_count_; }
  const ChunkMap& chunks() const { return chunks_; }
  const std::vector<FoliageInstance>* instancesInChunk(
      FoliageChunkCoord coord) const;

  /// Reindexes every instance. Invalid sizes are rejected without mutation.
  bool setChunkSize(float chunk_size);
  /// Replaces one chunk after validating and reassigning each instance by X/Z.
  bool replaceChunk(FoliageChunkCoord coord,
                    std::vector<FoliageInstance> instances);
  /// Removes a chunk and returns its former contents.
  std::vector<FoliageInstance> clearChunk(FoliageChunkCoord coord);
  void clear();

  FoliageEditResult paint(const FoliagePaintBrush& brush,
                          const FoliageSurfaceSampler& surface);
  FoliageEditResult erase(const FoliageEraseBrush& brush);

  /// Reapplies or reverses a previously returned edit delta.
  bool applyEdit(const FoliageEditResult& edit, bool reverse = false);

  FoliageDocument toDocument() const;

 private:
  bool hasNeighborWithin(float x, float z, float spacing) const;

  float chunk_size_ = 32.0f;
  std::size_t instance_count_ = 0u;
  ChunkMap chunks_;
};

/// One actionable runtime load/validation failure.
struct FoliageDiagnostic {
  world::Entity source{};
  std::filesystem::path path;
  std::string message;
};

/// Current aggregate streaming state.
struct FoliageRuntimeStats {
  std::size_t source_count = 0u;
  std::size_t resident_chunks = 0u;
  std::size_t resident_instances = 0u;
  std::size_t queued_chunks = 0u;
};

/// Runtime module that streams foliage sidecars into instanced mesh proxies.
class FoliageRuntimeModule final : public app::RuntimeModule {
 public:
  FoliageRuntimeModule();
  ~FoliageRuntimeModule() override;

  FoliageRuntimeModule(const FoliageRuntimeModule&) = delete;
  FoliageRuntimeModule& operator=(const FoliageRuntimeModule&) = delete;

  /// Sets the root used to resolve relative sidecar paths.
  void setReferenceRoot(std::filesystem::path root);
  /// Replaces a file-backed source with an immutable in-memory document.
  void setLayerOverride(world::Entity source,
                        std::shared_ptr<const FoliageDocument> document);
  /// Copies an editable layer into an immutable runtime override.
  void setLayerOverride(world::Entity source, const FoliageLayer& layer);
  void clearLayerOverride(world::Entity source);
  void clearLayerOverrides();

  std::vector<FoliageDiagnostic> diagnostics() const;
  FoliageRuntimeStats stats() const;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onUpdate(world::World& world,
                float dt,
                float interpolation_alpha) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::foliage
