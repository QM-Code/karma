#pragma once

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

#include "karma/content/image/image.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/terrain.h"
#include "karma/world/components/terrain.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::content {
class AssetRegistry;
}

namespace karma::terrain {

using TileCoord = renderer::TerrainTileCoord;

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
renderer::TerrainDesc terrainDescFromComponent(const components::TerrainComponent& terrain);
std::string formatTerrainTilePattern(std::string pattern,
                                     TileCoord coord,
                                     int32_t index_base = 0);
std::vector<float> convertHeightImageToNormalizedHeights(
    const content::Rgba8Image& image,
    uint32_t output_resolution);
std::vector<float> convertScalarImageToNormalizedHeights(
    const content::ScalarImage& image,
    uint32_t output_resolution);
renderer::TerrainTileData generateProceduralTerrainTile(
    const components::TerrainComponent& terrain,
    TileCoord coord);
std::optional<renderer::TerrainTileData> loadSingleImageTerrainTile(
    const components::TerrainComponent& terrain);
std::optional<renderer::TerrainTileData> loadImageTerrainTile(
    const components::TerrainComponent& terrain,
    TileCoord coord);
std::optional<renderer::TerrainMaterialLayerData> loadTerrainMaterialLayer(
    const components::TerrainMaterialLayer& layer,
    uint32_t layer_index);
std::optional<renderer::TerrainMaterialLayerData> loadTerrainMaterialLayer(
    const components::TerrainMaterialLayer& layer,
    uint32_t layer_index,
    const content::AssetRegistry* assets);

/// Streams terrain chunks around the primary camera and submits loaded tiles.
class TerrainSystem {
 public:
  explicit TerrainSystem(renderer::GraphicsDevice* device,
                         const content::AssetRegistry* assets = nullptr);
  ~TerrainSystem();

  TerrainSystem(const TerrainSystem&) = delete;
  TerrainSystem& operator=(const TerrainSystem&) = delete;

  void syncTerrainColliders(ecs::World& world);
  void update(ecs::World& world, float dt, float interpolation_alpha);

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
    std::optional<renderer::TerrainTileData> data;
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
    renderer::TerrainId terrain = renderer::kInvalidTerrain;
    renderer::TerrainDesc desc{};
    TerrainSourceSettings source_settings{};
    uint64_t generation = 1u;
    std::unordered_set<TileCoord, TileCoordHash> desired;
    std::unordered_set<TileCoord, TileCoordHash> loaded;
    std::unordered_set<TileCoord, TileCoordHash> queued;
  };

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32u) |
           static_cast<uint64_t>(entity.generation);
  }
  static ecs::Entity entityFromKey(uint64_t key) {
    ecs::Entity entity{};
    entity.index = static_cast<uint32_t>(key >> 32u);
    entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
    return entity;
  }

  TerrainState& ensureState(uint64_t key, const components::TerrainComponent& terrain);
  void destroyState(TerrainState& state);
  void cleanupStaleStates(ecs::World& world);
  void queueTile(uint64_t key,
                 TerrainState& state,
                 const components::TerrainComponent& terrain,
                 TileCoord coord);
  void drainCompleted();
  void workerLoop();
  void stopWorker();

  renderer::GraphicsDevice* device_ = nullptr;
  const content::AssetRegistry* assets_ = nullptr;
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

}  // namespace karma::terrain
