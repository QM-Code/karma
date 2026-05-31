#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "karma/core/math/types.h"

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

static constexpr unsigned char kNavAreaNull = 0;
static constexpr unsigned char kNavAreaDefault = 1;
static constexpr unsigned char kNavAreaMax = 63;
static constexpr size_t kNavAreaCount = 64;

static constexpr uint16_t kNavPolyFlagWalk = 1u << 0u;
static constexpr uint16_t kNavPolyFlagOffMesh = 1u << 1u;
static constexpr uint16_t kNavPolyFlagAll = 0xffffu;

enum NavPathPointFlag : uint8_t {
  NavPathPointFlagNone = 0,
  NavPathPointFlagStart = 1u << 0u,
  NavPathPointFlagEnd = 1u << 1u,
  NavPathPointFlagOffMeshConnection = 1u << 2u,
};

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

struct NavAreaConfig {
  unsigned char area = kNavAreaDefault;
  uint16_t flags = kNavPolyFlagWalk;
  float cost = 1.0f;
};

struct NavQueryFilter {
  NavQueryFilter();

  uint16_t include_flags = kNavPolyFlagAll;
  uint16_t exclude_flags = 0;
  std::array<float, kNavAreaCount> area_costs{};

  void setAreaCost(unsigned char area, float cost);
  float areaCost(unsigned char area) const;
};

struct NavMeshBuildConfig {
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
  std::vector<NavAreaConfig> area_configs;
};

NavQueryFilter makeQueryFilter(const NavMeshBuildConfig& config,
                               uint16_t include_flags = kNavPolyFlagAll,
                               uint16_t exclude_flags = 0);

struct NavOffMeshConnection {
  math::Vec3 start{};
  math::Vec3 end{};
  float radius = 0.4f;
  unsigned char area = kNavAreaDefault;
  uint16_t flags = kNavPolyFlagWalk | kNavPolyFlagOffMesh;
  bool bidirectional = true;
  uint32_t user_id = 0;
};

struct NavMeshInputGeometry {
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
  std::vector<unsigned char> triangle_areas;
  std::vector<NavOffMeshConnection> off_mesh_connections;

  bool empty() const { return vertices.empty() || indices.size() < 3; }
  uint32_t triangleCount() const { return static_cast<uint32_t>(indices.size() / 3u); }
};

struct NavMeshBuildResult {
  NavStatus status = NavStatus::BuildFailed;
  std::string message;
  uint32_t vertex_count = 0;
  uint32_t triangle_count = 0;
  uint32_t polygon_count = 0;
};

struct NavMeshSnapshot {
  std::vector<uint8_t> data;

  bool valid() const { return !data.empty(); }
};

struct NavPath {
  NavStatus status = NavStatus::QueryFailed;
  std::vector<math::Vec3> points;
  std::vector<uint8_t> point_flags;
  bool partial = false;

  bool success() const { return status == NavStatus::Success || status == NavStatus::PartialPath; }
};

const char* navStatusName(NavStatus status);

class NavMesh {
 public:
  NavMesh();
  ~NavMesh();

  NavMesh(const NavMesh&) = delete;
  NavMesh& operator=(const NavMesh&) = delete;
  NavMesh(NavMesh&&) noexcept;
  NavMesh& operator=(NavMesh&&) noexcept;

  bool build(const NavMeshInputGeometry& geometry,
             const NavMeshBuildConfig& config = {},
             NavMeshBuildResult* result = nullptr);
  void reset();

  bool isValid() const;
  const NavMeshBuildConfig& config() const { return config_; }
  const NavMeshBuildResult& lastBuildResult() const { return last_result_; }
  std::shared_ptr<const NavMeshSnapshot> snapshot() const { return snapshot_; }
  const math::Vec3& boundsMin() const { return bounds_min_; }
  const math::Vec3& boundsMax() const { return bounds_max_; }

  void debugDraw(renderer::GraphicsDevice& graphics,
                 const math::Color& color = {0.1f, 0.85f, 0.35f, 1.0f},
                 bool depth_test = true) const;

 private:
  friend class NavQuery;
  dtNavMesh* nav_mesh_ = nullptr;
  NavMeshBuildConfig config_{};
  NavMeshBuildResult last_result_{};
  math::Vec3 bounds_min_{};
  math::Vec3 bounds_max_{};
  std::vector<math::Vec3> debug_edges_;
  std::shared_ptr<const NavMeshSnapshot> snapshot_;
};

class NavQuery {
 public:
  explicit NavQuery(const NavMesh& nav_mesh, int max_nodes = 2048);
  explicit NavQuery(const NavMeshSnapshot& snapshot, int max_nodes = 2048);
  ~NavQuery();

  NavQuery(const NavQuery&) = delete;
  NavQuery& operator=(const NavQuery&) = delete;
  NavQuery(NavQuery&&) noexcept;
  NavQuery& operator=(NavQuery&&) noexcept;

  bool isValid() const;
  NavPath findPath(const math::Vec3& start,
                   const math::Vec3& end,
                   const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                   int max_points = 256,
                   const NavQueryFilter& filter = NavQueryFilter{}) const;
  NavPath raycast(const math::Vec3& start,
                  const math::Vec3& end,
                  const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                  int max_points = 256,
                  const NavQueryFilter& filter = NavQueryFilter{}) const;
  bool findNearestPoint(const math::Vec3& point,
                        math::Vec3& out_point,
                        const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                        const NavQueryFilter& filter = NavQueryFilter{}) const;
  bool moveAlongSurface(const math::Vec3& start,
                        const math::Vec3& end,
                        math::Vec3& out_point,
                        const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                        const NavQueryFilter& filter = NavQueryFilter{}) const;
  bool findHeight(const math::Vec3& point,
                  float& out_height,
                  const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                  const NavQueryFilter& filter = NavQueryFilter{}) const;
  bool findDistanceToWall(const math::Vec3& point,
                          float max_radius,
                          float& out_distance,
                          math::Vec3* out_hit_position = nullptr,
                          math::Vec3* out_hit_normal = nullptr,
                          const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                          const NavQueryFilter& filter = NavQueryFilter{}) const;
  bool findRandomPoint(math::Vec3& out_point, const NavQueryFilter& filter = NavQueryFilter{}) const;

  NavStatus beginSlicedPath(const math::Vec3& start,
                            const math::Vec3& end,
                            const math::Vec3& search_extents = {2.0f, 4.0f, 2.0f},
                            const NavQueryFilter& filter = NavQueryFilter{});
  NavStatus updateSlicedPath(int max_iterations, bool& out_done);
  NavPath finalizeSlicedPath(int max_points = 256);
  void cancelSlicedPath();
  bool hasActiveSlicedPath() const { return sliced_active_; }

  static void debugDrawPath(renderer::GraphicsDevice& graphics,
                            const NavPath& path,
                            const math::Color& color = {1.0f, 0.9f, 0.1f, 1.0f},
                            bool depth_test = true);

 private:
  const NavMesh* nav_mesh_ = nullptr;
  dtNavMesh* owned_nav_mesh_ = nullptr;
  dtNavMeshQuery* query_ = nullptr;
  std::unique_ptr<dtQueryFilter> sliced_filter_;
  uint64_t sliced_start_ref_ = 0;
  uint64_t sliced_end_ref_ = 0;
  math::Vec3 sliced_start_{};
  math::Vec3 sliced_end_{};
  bool sliced_active_ = false;
};

}  // namespace karma::navigation
