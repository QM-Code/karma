#pragma once

#include "karma/app.h"
#include "karma/assets.h"
#include "karma/components.h"
#include "karma/rendering.h"
#include "karma/world.h"



#include <string_view>


namespace karma::visual {

/// \ingroup karma_features
/// Updates `LightPulseComponent` envelopes on paired lights.
class LightPulseSystem final : public world::ISystem {
 public:
  std::string_view name() const override { return "LightPulseSystem"; }
  void update(world::World& world, float dt) override;
};

}  // namespace karma::visual


#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace karma::visual::particles {

/// \ingroup karma_particles
/// Binding options for attaching a named particle effect to an entity.
struct ParticleEffectBindingDesc {
  std::string_view effect_key;
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  bool preserve_start_delay = false;
  std::optional<components::ParticleEffectOverrideComponent> effect_override;
};

/// \ingroup karma_particles
/// Creation options for a new particle effect entity.
struct ParticleEffectEntityDesc {
  std::string_view name;
  std::string_view effect_key;
  components::TransformComponent transform{};
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  bool preserve_start_delay = false;
  std::optional<components::ParticleEffectOverrideComponent> effect_override;
};

/// \ingroup karma_particles
/// Creation options for a textured particle beam/ribbon entity.
struct ParticleBeamEntityDesc {
  std::string_view name;
  components::TransformComponent transform{};
  bool enabled = true;
  bool visible = true;
  uint32_t layer = 0;
  bool depth_test = true;
  components::ParticleBlendMode blend_mode = components::ParticleBlendMode::Additive;
  std::string_view texture_key;
  std::vector<math::Vec3> local_path_points;
  float start_width = 0.2f;
  float end_width = 0.2f;
  math::Color start_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color end_color{1.0f, 1.0f, 1.0f, 1.0f};
  float edge_softness = 0.0f;
  float uv_repeat = 1.0f;
  float uv_scroll_speed = 0.0f;
  float time_scale = 1.0f;
};

/// Binds an existing entity to a named particle effect.
inline bool bindEffect(world::World& world,
                       world::Entity entity,
                       const ParticleEffectBindingDesc& desc) {
  if (!world.isAlive(entity) || desc.effect_key.empty()) {
    return false;
  }

  if (world.has<components::ParticleEmitterComponent>(entity)) {
    auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    emitter.enabled = desc.enabled;
    emitter.playing = desc.playing;
  } else {
    world.add(entity, components::ParticleEmitterComponent{
                          .enabled = desc.enabled,
                          .playing = desc.playing,
                      });
  }
  world.add(entity, components::ParticleEffectComponent{
                        .effect_key = std::string(desc.effect_key),
                        .auto_apply = desc.auto_apply,
                        .preserve_enabled = desc.preserve_enabled,
                        .preserve_playing = desc.preserve_playing,
                        .preserve_start_delay = desc.preserve_start_delay,
                    });
  if (desc.effect_override.has_value()) {
    world.add(entity, *desc.effect_override);
  }
  return true;
}

/// Creates a transform entity and binds it to a named particle effect.
inline world::Entity createEffectEntity(world::World& world,
                                      const ParticleEffectEntityDesc& desc) {
  world::Entity entity = world.createEntity();
  if (!desc.name.empty()) {
    world.setName(entity, std::string(desc.name));
  }
  world.add(entity, desc.transform);
  bindEffect(world,
             entity,
             ParticleEffectBindingDesc{
                 .effect_key = desc.effect_key,
                 .enabled = desc.enabled,
                 .playing = desc.playing,
                 .auto_apply = desc.auto_apply,
                 .preserve_enabled = desc.preserve_enabled,
                 .preserve_playing = desc.preserve_playing,
                 .preserve_start_delay = desc.preserve_start_delay,
                 .effect_override = desc.effect_override,
             });
  return entity;
}

/// Creates a transform entity with a particle beam component.
inline world::Entity createBeamEntity(world::World& world,
                                      const ParticleBeamEntityDesc& desc) {
  world::Entity entity = world.createEntity();
  if (!desc.name.empty()) {
    world.setName(entity, std::string(desc.name));
  }
  world.add(entity, desc.transform);
  world.add(entity, components::ParticleBeamComponent{
                        .enabled = desc.enabled,
                        .visible = desc.visible,
                        .layer = desc.layer,
                        .depth_test = desc.depth_test,
                        .blend_mode = desc.blend_mode,
                        .texture_key = std::string(desc.texture_key),
                        .local_path_points = desc.local_path_points,
                        .start_width = desc.start_width,
                        .end_width = desc.end_width,
                        .start_color = desc.start_color,
                        .end_color = desc.end_color,
                        .edge_softness = desc.edge_softness,
                        .uv_repeat = desc.uv_repeat,
                        .uv_scroll_speed = desc.uv_scroll_speed,
                        .time_scale = desc.time_scale,
                    });
  return entity;
}

/// Replaces the local path points for an existing beam entity.
inline bool setBeamPath(world::World& world,
                        world::Entity entity,
                        std::vector<math::Vec3> local_path_points) {
  if (!world.isAlive(entity) || !world.has<components::ParticleBeamComponent>(entity)) {
    return false;
  }
  world.get<components::ParticleBeamComponent>(entity).local_path_points =
      std::move(local_path_points);
  return true;
}

/// Enables or disables a beam and matching visibility when present.
inline bool setBeamEnabled(world::World& world, world::Entity entity, bool enabled) {
  if (!world.isAlive(entity) || !world.has<components::ParticleBeamComponent>(entity)) {
    return false;
  }
  auto& beam = world.get<components::ParticleBeamComponent>(entity);
  beam.enabled = enabled;
  beam.visible = enabled;
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = enabled;
  }
  return true;
}

/// Restarts beam UV time by incrementing its restart counter.
inline bool restartBeam(world::World& world, world::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::ParticleBeamComponent>(entity)) {
    return false;
  }
  auto& beam = world.get<components::ParticleBeamComponent>(entity);
  beam.enabled = true;
  beam.visible = true;
  beam.restart_count += 1u;
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = true;
  }
  return true;
}

/// Adds or replaces per-instance effect overrides.
inline bool setEffectOverrides(world::World& world,
                               world::Entity entity,
                               components::ParticleEffectOverrideComponent effect_override) {
  if (!world.isAlive(entity)) {
    return false;
  }
  world.add(entity, std::move(effect_override));
  return true;
}

/// Replaces the source path for one particle-effect instance.
inline bool setEffectSourcePath(world::World& world,
                                world::Entity entity,
                                std::vector<math::Vec3> points,
                                bool closed_loop = false) {
  if (!world.isAlive(entity)) {
    return false;
  }
  components::ParticleEffectOverrideComponent effect_override =
      world.has<components::ParticleEffectOverrideComponent>(entity)
          ? world.get<components::ParticleEffectOverrideComponent>(entity)
          : components::ParticleEffectOverrideComponent{};
  effect_override.source_shape = components::ParticleSourceShape::Path;
  effect_override.source_path_points = std::move(points);
  effect_override.source_closed_loop = closed_loop;
  world.add(entity, std::move(effect_override));
  return true;
}

/// Replaces the source box extents for one particle-effect instance.
inline bool setEffectSourceBoxExtents(world::World& world,
                                      world::Entity entity,
                                      const math::Vec3& extents) {
  if (!world.isAlive(entity)) {
    return false;
  }
  components::ParticleEffectOverrideComponent effect_override =
      world.has<components::ParticleEffectOverrideComponent>(entity)
          ? world.get<components::ParticleEffectOverrideComponent>(entity)
          : components::ParticleEffectOverrideComponent{};
  effect_override.source_shape = components::ParticleSourceShape::Box;
  effect_override.source_box_extents = extents;
  world.add(entity, std::move(effect_override));
  return true;
}

/// Removes per-instance effect overrides.
inline bool clearEffectOverrides(world::World& world, world::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEffectOverrideComponent>(entity)) {
    return false;
  }
  world.remove<components::ParticleEffectOverrideComponent>(entity);
  return true;
}

/// Enables or disables an effect and matching visibility when present.
inline bool setEffectEnabled(world::World& world, world::Entity entity, bool enabled) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  world.get<components::ParticleEmitterComponent>(entity).enabled = enabled;
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = enabled;
  }
  return true;
}

/// Sets whether an emitter is actively playing.
inline bool setEffectPlaying(world::World& world, world::Entity entity, bool playing) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  world.get<components::ParticleEmitterComponent>(entity).playing = playing;
  return true;
}

/// Sets both effect enabled and playback state.
inline bool setEffectPlayback(world::World& world,
                              world::Entity entity,
                              bool enabled,
                              bool playing) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEmitterComponent>(entity)) {
    return false;
  }
  auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
  emitter.enabled = enabled;
  emitter.playing = playing;
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = enabled;
  }
  return true;
}

/// Restarts an effect by incrementing its restart counter.
inline bool restartEffect(world::World& world, world::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::ParticleEffectComponent>(entity)) {
    return false;
  }

  if (world.has<components::ParticleEmitterComponent>(entity)) {
    auto& emitter = world.get<components::ParticleEmitterComponent>(entity);
    emitter.enabled = true;
    emitter.playing = true;
  }
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = true;
  }

  world.get<components::ParticleEffectComponent>(entity).restart_count += 1;
  return true;
}

}  // namespace karma::visual::particles


#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>


namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::assets {
class AssetRegistry;
}  // namespace karma::assets

namespace karma::visual::particles {

/// \ingroup karma_particles
/// Runtime particle binding and renderer submission system.
///
/// The system consumes `ParticleEmitterComponent`, `ParticleEffectComponent`,
/// and `ParticleEffectOverrideComponent`. Live particle state is owned by the
/// renderer backend for GPU-first effects.
class ParticleSystem {
 public:
  explicit ParticleSystem(rendering::GraphicsDevice* device,
                          const assets::AssetRegistry* assets = nullptr)
      : device_(device), assets_(assets) {}
  ~ParticleSystem();

  /// Updates effect bindings and submits emitter descriptors.
  void update(world::World& world, float dt, float interpolation_alpha);
  /// Returns current feature-owned live particle count for one entity.
  std::size_t liveParticleCount(world::Entity entity) const;
  /// Returns the most recent stats computed by this particle system.
  const rendering::ParticlePassStats& lastStats() const { return last_stats_; }

 private:
  uint32_t syncEffectBindings(world::World& world);
  rendering::TextureId resolveTextureAsset(const std::string& texture_key);
  void releaseTextureCache();
  void releaseMeshCache();

  rendering::GraphicsDevice* device_ = nullptr;
  const assets::AssetRegistry* assets_ = nullptr;
  std::unordered_map<std::string, rendering::MeshId> mesh_asset_cache_;
  std::unordered_map<std::string, rendering::TextureId> texture_asset_cache_;
  uint64_t last_mesh_version_ = 0;
  uint64_t last_texture_version_ = 0;
  rendering::ParticlePassStats last_stats_{};
};

}  // namespace karma::visual::particles


#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>


namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::assets {
class AssetRegistry;
}

namespace karma::visual::terrain {

using TileCoord = rendering::TerrainTileCoord;

struct TileCoordHash {
  std::size_t operator()(const TileCoord& coord) const noexcept;
};

int terrainTileRadius(float view_distance, float tile_size);
std::vector<TileCoord> terrainChunkCoordsAround(TileCoord center,
                                                float view_distance,
                                                float tile_size);
TileCoord terrainTileCoordForWorldPosition(
    const glm::vec3& world_position,
    const glm::vec3& terrain_origin,
    const components::TerrainComponent& terrain);
rendering::TerrainDesc terrainDescFromComponent(const components::TerrainComponent& terrain);
std::string formatTerrainTilePattern(std::string pattern,
                                     TileCoord coord,
                                     int32_t index_base = 0);
std::vector<float> convertHeightImageToNormalizedHeights(
    const assets::Rgba8Image& image,
    uint32_t output_resolution);
std::vector<float> convertScalarImageToNormalizedHeights(
    const assets::ScalarImage& image,
    uint32_t output_resolution);
rendering::TerrainTileData generateProceduralTerrainTile(
    const components::TerrainComponent& terrain,
    TileCoord coord);
std::optional<rendering::TerrainTileData> loadSingleImageTerrainTile(
    const components::TerrainComponent& terrain);
std::optional<rendering::TerrainTileData> loadImageTerrainTile(
    const components::TerrainComponent& terrain,
    TileCoord coord);
std::optional<rendering::TerrainMaterialLayerData> loadTerrainMaterialLayer(
    const components::TerrainMaterialLayer& layer,
    uint32_t layer_index);
std::optional<rendering::TerrainMaterialLayerData> loadTerrainMaterialLayer(
    const components::TerrainMaterialLayer& layer,
    uint32_t layer_index,
    const assets::AssetRegistry* assets);

/// Streams terrain chunks around the primary camera and submits loaded tiles.
class TerrainSystem {
 public:
  explicit TerrainSystem(rendering::GraphicsDevice* device,
                         const assets::AssetRegistry* assets = nullptr);
  ~TerrainSystem();

  TerrainSystem(const TerrainSystem&) = delete;
  TerrainSystem& operator=(const TerrainSystem&) = delete;

  void syncTerrainColliders(world::World& world);
  void update(world::World& world, float dt, float interpolation_alpha);

 private:
  struct TileRequest {
    uint64_t entity_key = 0u;
    uint64_t generation = 0u;
    components::TerrainComponent terrain{};
    TileCoord coord{};
  };

  struct CompletedTile {
    uint64_t entity_key = 0u;
    uint64_t generation = 0u;
    TileCoord coord{};
    std::optional<rendering::TerrainTileData> data;
  };

  struct TerrainSourceSettings {
    components::TerrainSourceType source = components::TerrainSourceType::Procedural;
    std::filesystem::path tile_directory;
    std::string height_pattern;
    std::string color_pattern;
    std::string control_pattern;
    std::filesystem::path height_image;
    std::filesystem::path heatmap_image;
    std::filesystem::path color_image;
    std::filesystem::path control_image;
    components::TerrainHeightFormat height_format = components::TerrainHeightFormat::Auto;
    uint32_t raw_width = 0u;
    uint32_t raw_height = 0u;
    bool raw_little_endian = true;
    bool flip_y = false;
    float height_value_min = 0.0f;
    float height_value_max = 1.0f;
    int32_t tile_index_base = 0;
    uint64_t asset_registry_version = 0u;
    std::vector<components::TerrainMaterialLayer> material_layers;
    std::vector<components::TerrainDataMapBinding> data_maps;

    friend bool operator==(const TerrainSourceSettings& lhs,
                           const TerrainSourceSettings& rhs) = default;
  };

  struct TerrainState {
    rendering::TerrainId terrain = rendering::kInvalidTerrain;
    rendering::TerrainDesc desc{};
    TerrainSourceSettings source_settings{};
    uint64_t generation = 1u;
    std::unordered_set<TileCoord, TileCoordHash> desired;
    std::unordered_set<TileCoord, TileCoordHash> loaded;
    std::unordered_set<TileCoord, TileCoordHash> queued;
  };

  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32u) |
           static_cast<uint64_t>(entity.generation);
  }
  static world::Entity entityFromKey(uint64_t key) {
    world::Entity entity{};
    entity.index = static_cast<uint32_t>(key >> 32u);
    entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
    return entity;
  }

  TerrainState& ensureState(uint64_t key, const components::TerrainComponent& terrain);
  void destroyState(TerrainState& state);
  void cleanupStaleStates(world::World& world);
  void queueTile(uint64_t key,
                 TerrainState& state,
                 const components::TerrainComponent& terrain,
                 TileCoord coord);
  void drainCompleted();
  void workerLoop();
  void stopWorker();

  rendering::GraphicsDevice* device_ = nullptr;
  const assets::AssetRegistry* assets_ = nullptr;
  std::unordered_map<uint64_t, TerrainState> states_;
  std::unordered_map<uint64_t, std::size_t> generated_collider_signatures_;
  std::mutex queue_mutex_;
  std::mutex completed_mutex_;
  std::condition_variable queue_cv_;
  std::deque<TileRequest> requests_;
  std::vector<CompletedTile> completed_;
  std::thread worker_;
  bool stop_worker_ = false;
};

}  // namespace karma::visual::terrain


#include <memory>


namespace karma::visual::terrain {

class TerrainSystem;

/// Runtime module that owns `TerrainSystem`.
class TerrainRuntimeModule final : public app::RuntimeModule {
 public:
  TerrainRuntimeModule();
  ~TerrainRuntimeModule() override;

  TerrainRuntimeModule(const TerrainRuntimeModule&) = delete;
  TerrainRuntimeModule& operator=(const TerrainRuntimeModule&) = delete;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onFrameBegin(world::World& world, float dt) override;
  void onUpdate(world::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<TerrainSystem> system_;
};

}  // namespace karma::visual::terrain


#include <cstdint>
#include <unordered_map>


namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::visual::volumes {

/// \ingroup karma_volumes
/// Per-frame analytic volumetric solid visual system.
///
/// The system creates/updates renderer proxy entities for source
/// `VolumetricComponent` entities.
class VolumeSystem {
 public:
  explicit VolumeSystem(rendering::GraphicsDevice* device);
  ~VolumeSystem();

  VolumeSystem(const VolumeSystem&) = delete;
  VolumeSystem& operator=(const VolumeSystem&) = delete;

  void update(world::World& world, float dt, float interpolation_alpha);

 private:
  struct RuntimeState {
    rendering::MaterialId material = rendering::kInvalidMaterial;
  };

  void ensureSharedResources();
  void destroySharedResources();
  void destroyRuntimeState(RuntimeState& state);
  RuntimeState& ensureRuntimeState(world::Entity source);

  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  rendering::GraphicsDevice* device_ = nullptr;
  rendering::MeshId overlay_mesh_ = rendering::kInvalidMesh;
  std::unordered_map<uint64_t, RuntimeState> runtime_;
};

}  // namespace karma::visual::volumes


#include <memory>


namespace karma::visual::volumes {

class VolumeSystem;

/// \ingroup karma_volumes
/// Runtime module that owns `VolumeSystem`.
class VolumeRuntimeModule final : public app::RuntimeModule {
 public:
  VolumeRuntimeModule();
  ~VolumeRuntimeModule() override;

  VolumeRuntimeModule(const VolumeRuntimeModule&) = delete;
  VolumeRuntimeModule& operator=(const VolumeRuntimeModule&) = delete;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onUpdate(world::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<VolumeSystem> system_;
};

}  // namespace karma::visual::volumes
