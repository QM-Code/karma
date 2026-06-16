#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_geometry_types.h"
#include "karma/simulation/navigation/nav_types.h"

class dtNavMesh;

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

class NavCrowd;
class NavQuery;
class NavTileCache;

/// \ingroup karma_navigation
/// Result metadata from a navmesh build.
struct NavMeshBuildResult {
  NavStatus status = NavStatus::BuildFailed;
  std::string message;
  uint32_t vertex_count = 0;
  uint32_t triangle_count = 0;
  uint32_t polygon_count = 0;
};

/// \ingroup karma_navigation
/// Serialized navmesh payload suitable for worker-thread queries.
struct NavMeshSnapshot {
  std::vector<uint8_t> data;

  /// Returns true when snapshot data is available.
  bool valid() const { return !data.empty(); }
};

/// \ingroup karma_navigation
/// Decoded Detour polygon reference fields.
struct NavPolyRefParts {
  uint32_t salt = 0;
  uint32_t tile = 0;
  uint32_t poly = 0;
};

/// \ingroup karma_navigation
/// Detour tile reference and coordinate metadata.
struct NavTileInfo {
  uint64_t ref = 0;
  int index = -1;
  int x = 0;
  int y = 0;
  int layer = 0;
  int poly_count = 0;
  int vert_count = 0;
};

/// \ingroup karma_navigation
/// Mutable Detour tile state payload from `storeTileState`.
struct NavTileStateSnapshot {
  uint64_t tile_ref = 0;
  std::vector<uint8_t> data;

  bool valid() const { return tile_ref != 0 && !data.empty(); }
};

/// \ingroup karma_navigation
/// Endpoints for a Detour off-mesh connection polygon.
struct NavOffMeshConnectionEndpoints {
  math::Vec3 start{};
  math::Vec3 end{};
};

/// \ingroup karma_navigation
/// Owns a Recast/Detour navmesh and debug draw data.
class NavMesh {
 public:
  NavMesh();
  ~NavMesh();

  NavMesh(const NavMesh&) = delete;
  NavMesh& operator=(const NavMesh&) = delete;
  NavMesh(NavMesh&&) noexcept;
  NavMesh& operator=(NavMesh&&) noexcept;

  /// Bakes a navmesh from triangle geometry.
  bool build(const NavMeshInputGeometry& geometry,
             const NavMeshBuildConfig& config = {},
             NavMeshBuildResult* result = nullptr);
  /// Bakes a tiled navmesh from triangle geometry.
  bool buildTiled(const NavMeshInputGeometry& geometry,
                  const NavMeshBuildConfig& config,
                  NavMeshBuildResult* result = nullptr);
  /// Rebuilds one tile containing `world_position`; valid only for tiled navmeshes.
  bool rebuildTile(const NavMeshInputGeometry& geometry,
                   const math::Vec3& world_position,
                   NavMeshBuildResult* result = nullptr);
  /// Removes one tile containing `world_position`; valid only for tiled navmeshes.
  bool removeTile(const math::Vec3& world_position);
  /// Removes all tiles from a tiled navmesh without changing its parameters.
  bool removeAllTiles();
  /// Rehydrates a navmesh from snapshot data.
  bool loadSnapshot(const NavMeshSnapshot& snapshot,
                    NavMeshBuildResult* result = nullptr);
  /// Sets Detour flags for one polygon reference.
  bool setPolyFlags(uint64_t poly_ref, uint16_t flags);
  /// Reads Detour flags for one polygon reference.
  bool getPolyFlags(uint64_t poly_ref, uint16_t& out_flags) const;
  /// Sets Detour area id for one polygon reference.
  bool setPolyArea(uint64_t poly_ref, unsigned char area);
  /// Reads Detour area id for one polygon reference.
  bool getPolyArea(uint64_t poly_ref, unsigned char& out_area) const;
  /// Decodes a Detour polygon reference into salt/tile/poly indices.
  bool decodePolyRef(uint64_t poly_ref, NavPolyRefParts& out_parts) const;
  /// Returns all active Detour tiles.
  std::vector<NavTileInfo> tiles() const;
  /// Returns the Detour tile reference at tile coordinates.
  uint64_t tileRefAt(int x, int y, int layer = 0) const;
  /// Stores mutable tile state such as polygon flags and areas.
  bool storeTileState(uint64_t tile_ref, NavTileStateSnapshot& out_state) const;
  /// Restores mutable tile state and refreshes snapshots/debug data.
  bool restoreTileState(const NavTileStateSnapshot& state);
  /// Returns off-mesh connection endpoints for a previous and off-mesh polygon pair.
  bool offMeshConnectionEndpoints(uint64_t previous_poly_ref,
                                  uint64_t off_mesh_poly_ref,
                                  NavOffMeshConnectionEndpoints& out_endpoints) const;
  /// Computes the center of one Detour polygon reference.
  bool polyCenter(uint64_t poly_ref, math::Vec3& out_center) const;
  /// Marks polygons unreachable from `start` with `disabled_flags`.
  uint32_t pruneUnreachable(const math::Vec3& start,
                            uint16_t disabled_flags,
                            const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                            const NavQueryFilter& filter = NavQueryFilter{});
  /// Releases Detour data and clears build metadata.
  void reset();

  /// Returns true after a successful build.
  bool isValid() const;
  /// Build configuration used by the last build.
  const NavMeshBuildConfig& config() const { return config_; }
  /// Metadata from the last build attempt.
  const NavMeshBuildResult& lastBuildResult() const { return last_result_; }
  /// Serialized snapshot for read-only query workers.
  std::shared_ptr<const NavMeshSnapshot> snapshot() const { return snapshot_; }
  /// World-space minimum navmesh bounds.
  const math::Vec3& boundsMin() const { return bounds_min_; }
  /// World-space maximum navmesh bounds.
  const math::Vec3& boundsMax() const { return bounds_max_; }
  /// Returns true when a debug draw layer has line data.
  bool hasDebugDrawMode(NavMeshDebugDrawMode mode) const;
  /// Returns captured line data for a debug draw layer.
  const std::vector<NavDebugLine>& debugDrawLines(NavMeshDebugDrawMode mode) const;

  /// Draws navmesh debug edges through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 const math::Color& color = {0.1f, 0.85f, 0.35f, 1.0f},
                 bool depth_test = true) const;
  /// Draws a selected Recast/Detour debug layer through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 NavMeshDebugDrawMode mode,
                 bool depth_test = true,
                 const math::Color& fallback_color = {0.1f, 0.85f, 0.35f, 1.0f}) const;
  /// Draws highlighted Detour polygon references through the graphics device.
  void debugDrawPolygons(renderer::GraphicsDevice& graphics,
                         const std::vector<uint64_t>& poly_refs,
                         const math::Color& color = {0.0f, 0.0f, 0.0f, 0.35f},
                         bool depth_test = false) const;

 private:
  friend class NavQuery;
  friend class NavCrowd;
  friend class NavTileCache;
  void refreshSnapshot();
  void refreshDetourDebugDraw();
  dtNavMesh* nav_mesh_ = nullptr;
  NavMeshBuildConfig config_{};
  NavMeshBuildResult last_result_{};
  math::Vec3 bounds_min_{};
  math::Vec3 bounds_max_{};
  std::vector<math::Vec3> debug_edges_;
  std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount> debug_draw_lines_;
  std::shared_ptr<const NavMeshSnapshot> snapshot_;
};

}  // namespace karma::navigation
