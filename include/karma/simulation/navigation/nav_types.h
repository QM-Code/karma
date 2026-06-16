#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "karma/core/math/types.h"

namespace karma::navigation {

/// \ingroup karma_navigation
/// Navigation area id used for non-walkable geometry.
static constexpr unsigned char kNavAreaNull = 0;
/// Default walkable navigation area id.
static constexpr unsigned char kNavAreaDefault = 1;
static constexpr unsigned char kNavAreaMax = 63;
static constexpr size_t kNavAreaCount = 64;

/// Polygon flag for walkable navigation polygons.
static constexpr uint16_t kNavPolyFlagWalk = 1u << 0u;
/// Polygon flag for Detour off-mesh connection polygons.
static constexpr uint16_t kNavPolyFlagOffMesh = 1u << 1u;
/// Mask including every navigation polygon flag.
static constexpr uint16_t kNavPolyFlagAll = 0xffffu;

/// \ingroup karma_navigation
/// Path point flags emitted by Detour path queries.
enum NavPathPointFlag : uint8_t {
  NavPathPointFlagNone = 0,
  NavPathPointFlagStart = 1u << 0u,
  NavPathPointFlagEnd = 1u << 1u,
  NavPathPointFlagOffMeshConnection = 1u << 2u,
};

/// \ingroup karma_navigation
/// Straight-path vertex emission options used by Detour path queries.
enum NavStraightPathOption : int {
  NavStraightPathOptionNone = 0,
  NavStraightPathOptionAreaCrossings = 1,
  NavStraightPathOptionAllCrossings = 2,
};

/// \ingroup karma_navigation
/// Build/query status shared by navmesh operations.
enum class NavStatus {
  Success,
  InProgress,
  PartialPath,
  EmptyInput,
  InvalidConfig,
  BuildFailed,
  NoNavMesh,
  InvalidStart,
  InvalidEnd,
  NoPath,
  QueryFailed,
};

/// \ingroup karma_navigation
/// Recast region partitioning strategy used during navmesh baking.
enum class NavMeshPartitionType {
  Watershed,
  Monotone,
  Layers,
};

/// \ingroup karma_navigation
/// Recast/Detour navmesh build topology.
enum class NavMeshBuildMode {
  Solo,
  Tiled,
};

/// \ingroup karma_navigation
/// Navigation debug draw data layers inspired by RecastDemo draw modes.
enum class NavMeshDebugDrawMode : uint8_t {
  NavMeshEdges,
  NavMesh,
  NavMeshBVTree,
  NavMeshPortals,
  Voxels,
  WalkableVoxels,
  Compact,
  CompactDistance,
  CompactRegions,
  RegionConnections,
  RawContours,
  BothContours,
  Contours,
  PolyMesh,
  PolyMeshDetail,
};

static constexpr size_t kNavMeshDebugDrawModeCount = 15;

/// \ingroup karma_navigation
/// Renderer-neutral debug line emitted by Recast/Detour debug capture.
struct NavDebugLine {
  math::Vec3 start{};
  math::Vec3 end{};
  math::Color color{0.1f, 0.85f, 0.35f, 1.0f};
  float thickness = 1.0f;
};

/// \ingroup karma_navigation
/// Area flag/cost configuration used when building a navmesh.
struct NavAreaConfig {
  unsigned char area = kNavAreaDefault;
  uint16_t flags = kNavPolyFlagWalk;
  float cost = 1.0f;
};

/// \ingroup karma_navigation
/// Query filter controlling included polygon flags and area costs.
struct NavQueryFilter {
  NavQueryFilter();

  uint16_t include_flags = kNavPolyFlagAll;
  uint16_t exclude_flags = 0;
  std::array<float, kNavAreaCount> area_costs{};

  /// Sets traversal cost for an area id.
  void setAreaCost(unsigned char area, float cost);
  /// Returns traversal cost for an area id.
  float areaCost(unsigned char area) const;
};

/// \ingroup karma_navigation
/// Static navmesh build settings.
struct NavMeshBuildConfig {
  NavMeshBuildMode build_mode = NavMeshBuildMode::Solo;
  NavMeshPartitionType partition_type = NavMeshPartitionType::Watershed;
  float cell_size = 0.3f;
  float cell_height = 0.2f;
  float agent_height = 2.0f;
  float agent_radius = 0.6f;
  float agent_max_climb = 0.9f;
  float agent_max_slope_degrees = 45.0f;
  float edge_max_len = 12.0f;
  float edge_max_error = 1.3f;
  float region_min_size = 8.0f;
  float region_merge_size = 20.0f;
  int verts_per_poly = 6;
  float detail_sample_dist = 6.0f;
  float detail_sample_max_error = 1.0f;
  uint16_t default_poly_flags = kNavPolyFlagWalk;
  uint16_t off_mesh_poly_flags = kNavPolyFlagWalk | kNavPolyFlagOffMesh;
  int tile_size = 32;
  int max_tiles = 0;
  int max_polys_per_tile = 0;
  bool collect_build_debug_draw = false;
  std::vector<NavAreaConfig> area_configs;
};

/// Builds a query filter from build config area settings.
NavQueryFilter makeQueryFilter(const NavMeshBuildConfig& config,
                               uint16_t include_flags = kNavPolyFlagAll,
                               uint16_t exclude_flags = 0);

/// Human-readable name for a navigation status.
const char* navStatusName(NavStatus status);
/// Human-readable name for a navigation debug draw mode.
const char* navMeshDebugDrawModeName(NavMeshDebugDrawMode mode);

}  // namespace karma::navigation
