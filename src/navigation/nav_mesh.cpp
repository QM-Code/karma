#include "karma/navigation/nav_mesh.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include "karma/renderer/device.h"

namespace karma::navigation {
namespace {

constexpr uint16_t kWalkablePolyFlag = 0x1;
constexpr int kMaxPathPolys = 1024;

float* ptr(math::Vec3& v) {
  return &v.x;
}

const float* ptr(const math::Vec3& v) {
  return &v.x;
}

math::Vec3 toVec3(const float* v) {
  return {v[0], v[1], v[2]};
}

bool succeeded(dtStatus status) {
  return dtStatusSucceed(status) != 0;
}

void setResult(NavMeshBuildResult* result,
               NavStatus status,
               std::string message,
               uint32_t vertex_count = 0,
               uint32_t triangle_count = 0,
               uint32_t polygon_count = 0) {
  if (result == nullptr) {
    return;
  }
  result->status = status;
  result->message = std::move(message);
  result->vertex_count = vertex_count;
  result->triangle_count = triangle_count;
  result->polygon_count = polygon_count;
}

math::Vec3 polyMeshVertexToWorld(const rcPolyMesh& poly_mesh, int vertex_index) {
  const unsigned short* vertex = &poly_mesh.verts[vertex_index * 3];
  return {
      poly_mesh.bmin[0] + static_cast<float>(vertex[0]) * poly_mesh.cs,
      poly_mesh.bmin[1] + static_cast<float>(vertex[1]) * poly_mesh.ch,
      poly_mesh.bmin[2] + static_cast<float>(vertex[2]) * poly_mesh.cs,
  };
}

std::vector<math::Vec3> buildDebugEdges(const rcPolyMesh& poly_mesh) {
  std::vector<math::Vec3> edges;
  for (int poly_index = 0; poly_index < poly_mesh.npolys; ++poly_index) {
    const unsigned short* poly = &poly_mesh.polys[poly_index * poly_mesh.nvp * 2];
    int vertex_count = 0;
    while (vertex_count < poly_mesh.nvp && poly[vertex_count] != RC_MESH_NULL_IDX) {
      ++vertex_count;
    }
    if (vertex_count < 2) {
      continue;
    }

    for (int edge = 0; edge < vertex_count; ++edge) {
      const int next_edge = (edge + 1) % vertex_count;
      edges.push_back(polyMeshVertexToWorld(poly_mesh, poly[edge]));
      edges.push_back(polyMeshVertexToWorld(poly_mesh, poly[next_edge]));
    }
  }
  return edges;
}

bool validConfig(const NavMeshBuildConfig& config) {
  return config.cell_size > 0.0f &&
         config.cell_height > 0.0f &&
         config.agent_height > 0.0f &&
         config.agent_radius >= 0.0f &&
         config.agent_max_climb >= 0.0f &&
         config.agent_max_slope_degrees >= 0.0f &&
         config.agent_max_slope_degrees <= 90.0f &&
         config.verts_per_poly >= 3 &&
         config.verts_per_poly <= DT_VERTS_PER_POLYGON;
}

void computeBounds(const NavMeshInputGeometry& geometry, math::Vec3& min, math::Vec3& max) {
  min = geometry.vertices.front();
  max = geometry.vertices.front();
  for (const math::Vec3& v : geometry.vertices) {
    min.x = std::min(min.x, v.x);
    min.y = std::min(min.y, v.y);
    min.z = std::min(min.z, v.z);
    max.x = std::max(max.x, v.x);
    max.y = std::max(max.y, v.y);
    max.z = std::max(max.z, v.z);
  }
}

std::vector<float> flattenVertices(const std::vector<math::Vec3>& vertices) {
  std::vector<float> out;
  out.reserve(vertices.size() * 3u);
  for (const math::Vec3& vertex : vertices) {
    out.push_back(vertex.x);
    out.push_back(vertex.y);
    out.push_back(vertex.z);
  }
  return out;
}

bool flattenIndices(const std::vector<uint32_t>& indices, std::vector<int>& out) {
  out.clear();
  out.reserve(indices.size());
  for (const uint32_t index : indices) {
    if (index > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    out.push_back(static_cast<int>(index));
  }
  return true;
}

}  // namespace

const char* navStatusName(NavStatus status) {
  switch (status) {
    case NavStatus::Success: return "Success";
    case NavStatus::EmptyInput: return "EmptyInput";
    case NavStatus::InvalidConfig: return "InvalidConfig";
    case NavStatus::BuildFailed: return "BuildFailed";
    case NavStatus::NoNavMesh: return "NoNavMesh";
    case NavStatus::InvalidStart: return "InvalidStart";
    case NavStatus::InvalidEnd: return "InvalidEnd";
    case NavStatus::NoPath: return "NoPath";
    case NavStatus::QueryFailed: return "QueryFailed";
    default: return "Unknown";
  }
}

NavMesh::NavMesh() = default;

NavMesh::~NavMesh() {
  reset();
}

NavMesh::NavMesh(NavMesh&& other) noexcept {
  *this = std::move(other);
}

NavMesh& NavMesh::operator=(NavMesh&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  nav_mesh_ = other.nav_mesh_;
  config_ = other.config_;
  last_result_ = std::move(other.last_result_);
  bounds_min_ = other.bounds_min_;
  bounds_max_ = other.bounds_max_;
  debug_edges_ = std::move(other.debug_edges_);
  other.nav_mesh_ = nullptr;
  return *this;
}

void NavMesh::reset() {
  if (nav_mesh_ != nullptr) {
    dtFreeNavMesh(nav_mesh_);
    nav_mesh_ = nullptr;
  }
  last_result_ = {};
  debug_edges_.clear();
}

bool NavMesh::isValid() const {
  return nav_mesh_ != nullptr;
}

bool NavMesh::build(const NavMeshInputGeometry& geometry,
                    const NavMeshBuildConfig& config,
                    NavMeshBuildResult* result) {
  reset();
  config_ = config;

  if (geometry.empty() || geometry.indices.size() % 3u != 0u) {
    setResult(result, NavStatus::EmptyInput, "Navigation geometry has no triangles.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::EmptyInput, "Navigation geometry has no triangles."};
    return false;
  }
  if (!validConfig(config)) {
    setResult(result, NavStatus::InvalidConfig, "Navigation build config is invalid.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::InvalidConfig, "Navigation build config is invalid."};
    return false;
  }

  std::vector<float> vertices = flattenVertices(geometry.vertices);
  std::vector<int> indices;
  if (!flattenIndices(geometry.indices, indices)) {
    setResult(result, NavStatus::BuildFailed, "Navigation indices exceed Recast limits.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Navigation indices exceed Recast limits."};
    return false;
  }

  computeBounds(geometry, bounds_min_, bounds_max_);

  rcContext context;
  rcConfig cfg{};
  cfg.cs = config.cell_size;
  cfg.ch = config.cell_height;
  cfg.walkableSlopeAngle = config.agent_max_slope_degrees;
  cfg.walkableHeight = static_cast<int>(std::ceil(config.agent_height / cfg.ch));
  cfg.walkableClimb = static_cast<int>(std::floor(config.agent_max_climb / cfg.ch));
  cfg.walkableRadius = static_cast<int>(std::ceil(config.agent_radius / cfg.cs));
  cfg.maxEdgeLen = static_cast<int>(config.edge_max_len / cfg.cs);
  cfg.maxSimplificationError = config.edge_max_error;
  cfg.minRegionArea = static_cast<int>(rcSqr(config.region_min_size));
  cfg.mergeRegionArea = static_cast<int>(rcSqr(config.region_merge_size));
  cfg.maxVertsPerPoly = config.verts_per_poly;
  cfg.detailSampleDist = config.detail_sample_dist < 0.9f ? 0.0f : cfg.cs * config.detail_sample_dist;
  cfg.detailSampleMaxError = cfg.ch * config.detail_sample_max_error;
  std::memcpy(cfg.bmin, ptr(bounds_min_), sizeof(cfg.bmin));
  std::memcpy(cfg.bmax, ptr(bounds_max_), sizeof(cfg.bmax));
  rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

  rcHeightfield* solid = rcAllocHeightfield();
  rcCompactHeightfield* compact = nullptr;
  rcContourSet* contours = nullptr;
  rcPolyMesh* poly_mesh = nullptr;
  rcPolyMeshDetail* detail_mesh = nullptr;

  auto cleanup_recast = [&]() {
    rcFreeHeightField(solid);
    rcFreeCompactHeightfield(compact);
    rcFreeContourSet(contours);
    rcFreePolyMesh(poly_mesh);
    rcFreePolyMeshDetail(detail_mesh);
  };

  const int vertex_count = static_cast<int>(geometry.vertices.size());
  const int triangle_count = static_cast<int>(geometry.triangleCount());

  if (solid == nullptr ||
      !rcCreateHeightfield(&context, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to create Recast heightfield.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to create Recast heightfield."};
    return false;
  }

  std::vector<unsigned char> triangle_areas(static_cast<size_t>(triangle_count), 0);
  rcMarkWalkableTriangles(&context,
                          cfg.walkableSlopeAngle,
                          vertices.data(),
                          vertex_count,
                          indices.data(),
                          triangle_count,
                          triangle_areas.data());
  if (!rcRasterizeTriangles(&context,
                            vertices.data(),
                            vertex_count,
                            indices.data(),
                            triangle_areas.data(),
                            triangle_count,
                            *solid,
                            cfg.walkableClimb)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to rasterize navigation triangles.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to rasterize navigation triangles."};
    return false;
  }

  rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid);
  rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
  rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);

  compact = rcAllocCompactHeightfield();
  if (compact == nullptr ||
      !rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid, *compact)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build compact navigation heightfield.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to build compact navigation heightfield."};
    return false;
  }

  if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact) ||
      !rcBuildDistanceField(&context, *compact) ||
      !rcBuildRegions(&context, *compact, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build navigation regions.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to build navigation regions."};
    return false;
  }

  contours = rcAllocContourSet();
  if (contours == nullptr ||
      !rcBuildContours(&context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build navigation contours.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to build navigation contours."};
    return false;
  }

  poly_mesh = rcAllocPolyMesh();
  if (poly_mesh == nullptr ||
      !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *poly_mesh)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build navigation polygon mesh.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to build navigation polygon mesh."};
    return false;
  }

  detail_mesh = rcAllocPolyMeshDetail();
  if (detail_mesh == nullptr ||
      !rcBuildPolyMeshDetail(&context,
                             *poly_mesh,
                             *compact,
                             cfg.detailSampleDist,
                             cfg.detailSampleMaxError,
                             *detail_mesh)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build navigation detail mesh.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to build navigation detail mesh."};
    return false;
  }

  for (int i = 0; i < poly_mesh->npolys; ++i) {
    if (poly_mesh->areas[i] != RC_NULL_AREA) {
      poly_mesh->flags[i] = kWalkablePolyFlag;
    }
  }
  debug_edges_ = buildDebugEdges(*poly_mesh);

  unsigned char* nav_data = nullptr;
  int nav_data_size = 0;
  dtNavMeshCreateParams params{};
  params.verts = poly_mesh->verts;
  params.vertCount = poly_mesh->nverts;
  params.polys = poly_mesh->polys;
  params.polyAreas = poly_mesh->areas;
  params.polyFlags = poly_mesh->flags;
  params.polyCount = poly_mesh->npolys;
  params.nvp = poly_mesh->nvp;
  params.detailMeshes = detail_mesh->meshes;
  params.detailVerts = detail_mesh->verts;
  params.detailVertsCount = detail_mesh->nverts;
  params.detailTris = detail_mesh->tris;
  params.detailTriCount = detail_mesh->ntris;
  params.walkableHeight = config.agent_height;
  params.walkableRadius = config.agent_radius;
  params.walkableClimb = config.agent_max_climb;
  rcVcopy(params.bmin, poly_mesh->bmin);
  rcVcopy(params.bmax, poly_mesh->bmax);
  params.cs = cfg.cs;
  params.ch = cfg.ch;
  params.buildBvTree = true;

  if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to create Detour navmesh data.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to create Detour navmesh data."};
    return false;
  }

  dtNavMesh* mesh = dtAllocNavMesh();
  if (mesh == nullptr || !succeeded(mesh->init(nav_data, nav_data_size, DT_TILE_FREE_DATA))) {
    dtFree(nav_data);
    dtFreeNavMesh(mesh);
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to initialize Detour navmesh.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to initialize Detour navmesh."};
    return false;
  }

  nav_mesh_ = mesh;
  NavMeshBuildResult success{};
  success.status = NavStatus::Success;
  success.message = "Navigation mesh built.";
  success.vertex_count = static_cast<uint32_t>(geometry.vertices.size());
  success.triangle_count = geometry.triangleCount();
  success.polygon_count = static_cast<uint32_t>(poly_mesh->npolys);
  last_result_ = success;
  if (result != nullptr) {
    *result = success;
  }

  cleanup_recast();
  return true;
}

void NavMesh::debugDraw(renderer::GraphicsDevice& graphics,
                        const math::Color& color,
                        bool depth_test) const {
  if (nav_mesh_ == nullptr) {
    return;
  }

  for (size_t i = 1; i < debug_edges_.size(); i += 2) {
    graphics.drawLine(debug_edges_[i - 1], debug_edges_[i], color, depth_test, 1.0f);
  }
}

NavQuery::NavQuery(const NavMesh& nav_mesh, int max_nodes)
    : nav_mesh_(&nav_mesh) {
  if (!nav_mesh.isValid()) {
    return;
  }
  query_ = dtAllocNavMeshQuery();
  if (query_ == nullptr ||
      !succeeded(query_->init(nav_mesh.nav_mesh_, std::max(max_nodes, 1)))) {
    dtFreeNavMeshQuery(query_);
    query_ = nullptr;
  }
}

NavQuery::~NavQuery() {
  dtFreeNavMeshQuery(query_);
}

NavQuery::NavQuery(NavQuery&& other) noexcept {
  *this = std::move(other);
}

NavQuery& NavQuery::operator=(NavQuery&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  dtFreeNavMeshQuery(query_);
  nav_mesh_ = other.nav_mesh_;
  query_ = other.query_;
  other.nav_mesh_ = nullptr;
  other.query_ = nullptr;
  return *this;
}

bool NavQuery::isValid() const {
  return nav_mesh_ != nullptr && nav_mesh_->isValid() && query_ != nullptr;
}

bool NavQuery::findNearestPoint(const math::Vec3& point,
                                math::Vec3& out_point,
                                const math::Vec3& search_extents) const {
  if (!isValid()) {
    return false;
  }
  dtQueryFilter filter;
  dtPolyRef ref = 0;
  float nearest[3]{};
  const dtStatus status = query_->findNearestPoly(ptr(point),
                                                  ptr(search_extents),
                                                  &filter,
                                                  &ref,
                                                  nearest);
  if (!succeeded(status) || ref == 0) {
    return false;
  }
  out_point = toVec3(nearest);
  return true;
}

NavPath NavQuery::findPath(const math::Vec3& start,
                           const math::Vec3& end,
                           const math::Vec3& search_extents,
                           int max_points) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter filter;
  dtPolyRef start_ref = 0;
  dtPolyRef end_ref = 0;
  float nearest_start[3]{};
  float nearest_end[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &filter, &start_ref, nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }
  if (!succeeded(query_->findNearestPoly(ptr(end), ptr(search_extents), &filter, &end_ref, nearest_end)) ||
      end_ref == 0) {
    result.status = NavStatus::InvalidEnd;
    return result;
  }

  std::vector<dtPolyRef> polys(static_cast<size_t>(kMaxPathPolys));
  int poly_count = 0;
  if (!succeeded(query_->findPath(start_ref,
                                  end_ref,
                                  nearest_start,
                                  nearest_end,
                                  &filter,
                                  polys.data(),
                                  &poly_count,
                                  static_cast<int>(polys.size()))) ||
      poly_count <= 0) {
    result.status = NavStatus::NoPath;
    return result;
  }

  const int clamped_max_points = std::max(max_points, 2);
  std::vector<float> straight(static_cast<size_t>(clamped_max_points) * 3u);
  std::vector<unsigned char> flags(static_cast<size_t>(clamped_max_points));
  std::vector<dtPolyRef> straight_polys(static_cast<size_t>(clamped_max_points));
  int point_count = 0;
  if (!succeeded(query_->findStraightPath(nearest_start,
                                          nearest_end,
                                          polys.data(),
                                          poly_count,
                                          straight.data(),
                                          flags.data(),
                                          straight_polys.data(),
                                          &point_count,
                                          clamped_max_points)) ||
      point_count <= 0) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.status = NavStatus::Success;
  result.points.reserve(static_cast<size_t>(point_count));
  for (int i = 0; i < point_count; ++i) {
    result.points.push_back(toVec3(&straight[static_cast<size_t>(i) * 3u]));
  }
  return result;
}

NavPath NavQuery::raycast(const math::Vec3& start,
                          const math::Vec3& end,
                          const math::Vec3& search_extents,
                          int max_points) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter filter;
  dtPolyRef start_ref = 0;
  float nearest_start[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &filter, &start_ref, nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }

  const int clamped_max_points = std::clamp(max_points, 2, kMaxPathPolys);
  std::vector<dtPolyRef> polys(static_cast<size_t>(clamped_max_points));
  int poly_count = 0;
  float t = 0.0f;
  float hit_normal[3]{};
  const dtStatus status = query_->raycast(start_ref,
                                          nearest_start,
                                          ptr(end),
                                          &filter,
                                          &t,
                                          hit_normal,
                                          polys.data(),
                                          &poly_count,
                                          clamped_max_points);
  if (!succeeded(status)) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  result.status = NavStatus::Success;
  result.points.push_back(toVec3(nearest_start));
  if (t > 1.0f || t == FLT_MAX) {
    result.points.push_back(end);
  } else {
    const math::Vec3 hit{
        nearest_start[0] + (end.x - nearest_start[0]) * t,
        nearest_start[1] + (end.y - nearest_start[1]) * t,
        nearest_start[2] + (end.z - nearest_start[2]) * t};
    result.points.push_back(hit);
  }
  return result;
}

void NavQuery::debugDrawPath(renderer::GraphicsDevice& graphics,
                             const NavPath& path,
                             const math::Color& color,
                             bool depth_test) {
  if (path.points.size() < 2) {
    return;
  }
  for (size_t i = 1; i < path.points.size(); ++i) {
    graphics.drawLine(path.points[i - 1], path.points[i], color, depth_test, 2.0f);
  }
}

}  // namespace karma::navigation
