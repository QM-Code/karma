#include "karma/navigation/nav_mesh.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
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

bool failed(dtStatus status) {
  return dtStatusFailed(status) != 0;
}

float randomUnit() {
  return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

unsigned char sanitizeArea(unsigned char area) {
  if (area > kNavAreaMax) {
    return kNavAreaDefault;
  }
  return area;
}

uint16_t flagsForArea(const NavMeshBuildConfig& config, unsigned char area) {
  if (area == kNavAreaNull) {
    return 0;
  }
  for (const NavAreaConfig& area_config : config.area_configs) {
    if (area_config.area == area) {
      return area_config.flags;
    }
  }
  return config.default_poly_flags;
}

dtQueryFilter makeDetourFilter(const NavQueryFilter& filter) {
  dtQueryFilter out;
  out.setIncludeFlags(filter.include_flags);
  out.setExcludeFlags(filter.exclude_flags);
  for (size_t i = 0; i < filter.area_costs.size(); ++i) {
    out.setAreaCost(static_cast<int>(i), filter.area_costs[i]);
  }
  return out;
}

uint8_t mapStraightPathFlags(unsigned char flags) {
  uint8_t out = NavPathPointFlagNone;
  if ((flags & DT_STRAIGHTPATH_START) != 0) {
    out |= NavPathPointFlagStart;
  }
  if ((flags & DT_STRAIGHTPATH_END) != 0) {
    out |= NavPathPointFlagEnd;
  }
  if ((flags & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0) {
    out |= NavPathPointFlagOffMeshConnection;
  }
  return out;
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
  if (config.cell_size <= 0.0f ||
      config.cell_height <= 0.0f ||
      config.agent_height <= 0.0f ||
      config.agent_radius < 0.0f ||
      config.agent_max_climb < 0.0f ||
      config.agent_max_slope_degrees < 0.0f ||
      config.agent_max_slope_degrees > 90.0f ||
      config.verts_per_poly < 3 ||
      config.verts_per_poly > DT_VERTS_PER_POLYGON) {
    return false;
  }
  for (const NavAreaConfig& area_config : config.area_configs) {
    if (area_config.area == kNavAreaNull ||
        area_config.area > kNavAreaMax ||
        area_config.cost <= 0.0f ||
        !std::isfinite(area_config.cost)) {
      return false;
    }
  }
  return true;
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

NavQueryFilter::NavQueryFilter() {
  area_costs.fill(1.0f);
}

void NavQueryFilter::setAreaCost(unsigned char area, float cost) {
  if (area >= area_costs.size() || cost <= 0.0f || !std::isfinite(cost)) {
    return;
  }
  area_costs[area] = cost;
}

float NavQueryFilter::areaCost(unsigned char area) const {
  if (area >= area_costs.size()) {
    return 1.0f;
  }
  return area_costs[area];
}

NavQueryFilter makeQueryFilter(const NavMeshBuildConfig& config,
                               uint16_t include_flags,
                               uint16_t exclude_flags) {
  NavQueryFilter filter;
  filter.include_flags = include_flags;
  filter.exclude_flags = exclude_flags;
  for (const NavAreaConfig& area_config : config.area_configs) {
    filter.setAreaCost(area_config.area, area_config.cost);
  }
  return filter;
}

const char* navStatusName(NavStatus status) {
  switch (status) {
    case NavStatus::Success: return "Success";
    case NavStatus::InProgress: return "InProgress";
    case NavStatus::PartialPath: return "PartialPath";
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
  snapshot_ = std::move(other.snapshot_);
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
  snapshot_.reset();
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
  if (!geometry.triangle_areas.empty() &&
      geometry.triangle_areas.size() != static_cast<size_t>(geometry.triangleCount())) {
    setResult(result, NavStatus::BuildFailed, "Navigation triangle area count does not match triangle count.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Navigation triangle area count does not match triangle count."};
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

  std::vector<unsigned char> slope_areas(static_cast<size_t>(triangle_count), kNavAreaNull);
  rcMarkWalkableTriangles(&context,
                          cfg.walkableSlopeAngle,
                          vertices.data(),
                          vertex_count,
                          indices.data(),
                          triangle_count,
                          slope_areas.data());
  std::vector<unsigned char> triangle_areas(static_cast<size_t>(triangle_count), kNavAreaNull);
  const bool has_explicit_areas = !geometry.triangle_areas.empty();
  for (int i = 0; i < triangle_count; ++i) {
    if (slope_areas[static_cast<size_t>(i)] == RC_NULL_AREA) {
      continue;
    }
    const unsigned char requested_area =
        has_explicit_areas ? geometry.triangle_areas[static_cast<size_t>(i)] : kNavAreaDefault;
    triangle_areas[static_cast<size_t>(i)] = sanitizeArea(requested_area);
  }
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
    poly_mesh->flags[i] = flagsForArea(config, poly_mesh->areas[i]);
  }
  debug_edges_ = buildDebugEdges(*poly_mesh);

  unsigned char* nav_data = nullptr;
  int nav_data_size = 0;
  std::vector<float> off_mesh_vertices;
  std::vector<float> off_mesh_radii;
  std::vector<unsigned short> off_mesh_flags;
  std::vector<unsigned char> off_mesh_areas;
  std::vector<unsigned char> off_mesh_dirs;
  std::vector<unsigned int> off_mesh_user_ids;
  off_mesh_vertices.reserve(geometry.off_mesh_connections.size() * 6u);
  off_mesh_radii.reserve(geometry.off_mesh_connections.size());
  off_mesh_flags.reserve(geometry.off_mesh_connections.size());
  off_mesh_areas.reserve(geometry.off_mesh_connections.size());
  off_mesh_dirs.reserve(geometry.off_mesh_connections.size());
  off_mesh_user_ids.reserve(geometry.off_mesh_connections.size());
  for (const NavOffMeshConnection& connection : geometry.off_mesh_connections) {
    if (connection.radius <= 0.0f || connection.area == kNavAreaNull) {
      continue;
    }
    const unsigned char area = sanitizeArea(connection.area);
    off_mesh_vertices.insert(off_mesh_vertices.end(),
                             {connection.start.x,
                              connection.start.y,
                              connection.start.z,
                              connection.end.x,
                              connection.end.y,
                              connection.end.z});
    off_mesh_radii.push_back(connection.radius);
    off_mesh_flags.push_back(connection.flags != 0 ? connection.flags : config.off_mesh_poly_flags);
    off_mesh_areas.push_back(area);
    off_mesh_dirs.push_back(connection.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
    off_mesh_user_ids.push_back(connection.user_id);
  }

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
  params.offMeshConVerts = off_mesh_vertices.empty() ? nullptr : off_mesh_vertices.data();
  params.offMeshConRad = off_mesh_radii.empty() ? nullptr : off_mesh_radii.data();
  params.offMeshConFlags = off_mesh_flags.empty() ? nullptr : off_mesh_flags.data();
  params.offMeshConAreas = off_mesh_areas.empty() ? nullptr : off_mesh_areas.data();
  params.offMeshConDir = off_mesh_dirs.empty() ? nullptr : off_mesh_dirs.data();
  params.offMeshConUserID = off_mesh_user_ids.empty() ? nullptr : off_mesh_user_ids.data();
  params.offMeshConCount = static_cast<int>(off_mesh_radii.size());
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

  auto snapshot = std::make_shared<NavMeshSnapshot>();
  snapshot->data.resize(static_cast<size_t>(nav_data_size));
  std::memcpy(snapshot->data.data(), nav_data, static_cast<size_t>(nav_data_size));

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
  snapshot_ = std::move(snapshot);
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

NavQuery::NavQuery(const NavMeshSnapshot& snapshot, int max_nodes) {
  if (!snapshot.valid() ||
      snapshot.data.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return;
  }

  auto* nav_data = static_cast<unsigned char*>(
      dtAlloc(snapshot.data.size(), DT_ALLOC_PERM));
  if (nav_data == nullptr) {
    return;
  }
  std::memcpy(nav_data, snapshot.data.data(), snapshot.data.size());

  owned_nav_mesh_ = dtAllocNavMesh();
  if (owned_nav_mesh_ == nullptr ||
      !succeeded(owned_nav_mesh_->init(nav_data,
                                       static_cast<int>(snapshot.data.size()),
                                       DT_TILE_FREE_DATA))) {
    dtFree(nav_data);
    dtFreeNavMesh(owned_nav_mesh_);
    owned_nav_mesh_ = nullptr;
    return;
  }

  query_ = dtAllocNavMeshQuery();
  if (query_ == nullptr ||
      !succeeded(query_->init(owned_nav_mesh_, std::max(max_nodes, 1)))) {
    dtFreeNavMeshQuery(query_);
    query_ = nullptr;
    dtFreeNavMesh(owned_nav_mesh_);
    owned_nav_mesh_ = nullptr;
  }
}

NavQuery::~NavQuery() {
  dtFreeNavMeshQuery(query_);
  dtFreeNavMesh(owned_nav_mesh_);
}

NavQuery::NavQuery(NavQuery&& other) noexcept {
  *this = std::move(other);
}

NavQuery& NavQuery::operator=(NavQuery&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  dtFreeNavMeshQuery(query_);
  dtFreeNavMesh(owned_nav_mesh_);
  nav_mesh_ = other.nav_mesh_;
  owned_nav_mesh_ = other.owned_nav_mesh_;
  query_ = other.query_;
  sliced_filter_ = std::move(other.sliced_filter_);
  sliced_start_ref_ = other.sliced_start_ref_;
  sliced_end_ref_ = other.sliced_end_ref_;
  sliced_start_ = other.sliced_start_;
  sliced_end_ = other.sliced_end_;
  sliced_active_ = other.sliced_active_;
  other.nav_mesh_ = nullptr;
  other.owned_nav_mesh_ = nullptr;
  other.query_ = nullptr;
  other.sliced_start_ref_ = 0;
  other.sliced_end_ref_ = 0;
  other.sliced_active_ = false;
  return *this;
}

bool NavQuery::isValid() const {
  const bool has_nav_mesh = (nav_mesh_ != nullptr && nav_mesh_->isValid()) ||
                            owned_nav_mesh_ != nullptr;
  return has_nav_mesh && query_ != nullptr;
}

bool NavQuery::findNearestPoint(const math::Vec3& point,
                                math::Vec3& out_point,
                                const math::Vec3& search_extents,
                                const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }
  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  const dtStatus status = query_->findNearestPoly(ptr(point),
                                                  ptr(search_extents),
                                                  &detour_filter,
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
                           int max_points,
                           const NavQueryFilter& filter) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  dtPolyRef end_ref = 0;
  float nearest_start[3]{};
  float nearest_end[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &detour_filter, &start_ref, nearest_start)) ||
      start_ref == 0) {
    result.status = NavStatus::InvalidStart;
    return result;
  }
  if (!succeeded(query_->findNearestPoly(ptr(end), ptr(search_extents), &detour_filter, &end_ref, nearest_end)) ||
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
                                  &detour_filter,
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

  result.partial = polys[static_cast<size_t>(poly_count - 1)] != end_ref;
  result.status = result.partial ? NavStatus::PartialPath : NavStatus::Success;
  result.points.reserve(static_cast<size_t>(point_count));
  result.point_flags.reserve(static_cast<size_t>(point_count));
  for (int i = 0; i < point_count; ++i) {
    result.points.push_back(toVec3(&straight[static_cast<size_t>(i) * 3u]));
    result.point_flags.push_back(mapStraightPathFlags(flags[static_cast<size_t>(i)]));
  }
  return result;
}

NavPath NavQuery::raycast(const math::Vec3& start,
                          const math::Vec3& end,
                          const math::Vec3& search_extents,
                          int max_points,
                          const NavQueryFilter& filter) const {
  NavPath result{};
  if (!isValid()) {
    result.status = NavStatus::NoNavMesh;
    return result;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  float nearest_start[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start), ptr(search_extents), &detour_filter, &start_ref, nearest_start)) ||
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
                                          &detour_filter,
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
  result.point_flags.push_back(NavPathPointFlagStart);
  if (t > 1.0f || t == FLT_MAX) {
    result.points.push_back(end);
  } else {
    const math::Vec3 hit{
        nearest_start[0] + (end.x - nearest_start[0]) * t,
        nearest_start[1] + (end.y - nearest_start[1]) * t,
        nearest_start[2] + (end.z - nearest_start[2]) * t};
    result.points.push_back(hit);
  }
  result.point_flags.push_back(NavPathPointFlagEnd);
  return result;
}

bool NavQuery::moveAlongSurface(const math::Vec3& start,
                                const math::Vec3& end,
                                math::Vec3& out_point,
                                const math::Vec3& search_extents,
                                const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  float nearest_start[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &start_ref,
                                         nearest_start)) ||
      start_ref == 0) {
    return false;
  }

  float result[3]{};
  std::vector<dtPolyRef> visited(static_cast<size_t>(kMaxPathPolys));
  int visited_count = 0;
  const dtStatus status = query_->moveAlongSurface(start_ref,
                                                   nearest_start,
                                                   ptr(end),
                                                   &detour_filter,
                                                   result,
                                                   visited.data(),
                                                   &visited_count,
                                                   static_cast<int>(visited.size()));
  if (!succeeded(status)) {
    return false;
  }

  out_point = toVec3(result);
  return true;
}

bool NavQuery::findHeight(const math::Vec3& point,
                          float& out_height,
                          const math::Vec3& search_extents,
                          const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(point),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &ref,
                                         nearest)) ||
      ref == 0) {
    return false;
  }

  float height = 0.0f;
  if (!succeeded(query_->getPolyHeight(ref, nearest, &height))) {
    return false;
  }
  out_height = height;
  return true;
}

bool NavQuery::findDistanceToWall(const math::Vec3& point,
                                  float max_radius,
                                  float& out_distance,
                                  math::Vec3* out_hit_position,
                                  math::Vec3* out_hit_normal,
                                  const math::Vec3& search_extents,
                                  const NavQueryFilter& filter) const {
  if (!isValid() || max_radius <= 0.0f) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float nearest[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(point),
                                         ptr(search_extents),
                                         &detour_filter,
                                         &ref,
                                         nearest)) ||
      ref == 0) {
    return false;
  }

  float distance = 0.0f;
  float hit_position[3]{};
  float hit_normal[3]{};
  if (!succeeded(query_->findDistanceToWall(ref,
                                            nearest,
                                            max_radius,
                                            &detour_filter,
                                            &distance,
                                            hit_position,
                                            hit_normal))) {
    return false;
  }

  out_distance = distance;
  if (out_hit_position != nullptr) {
    *out_hit_position = toVec3(hit_position);
  }
  if (out_hit_normal != nullptr) {
    *out_hit_normal = toVec3(hit_normal);
  }
  return true;
}

bool NavQuery::findRandomPoint(math::Vec3& out_point, const NavQueryFilter& filter) const {
  if (!isValid()) {
    return false;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef ref = 0;
  float point[3]{};
  if (!succeeded(query_->findRandomPoint(&detour_filter, randomUnit, &ref, point)) || ref == 0) {
    return false;
  }
  out_point = toVec3(point);
  return true;
}

NavStatus NavQuery::beginSlicedPath(const math::Vec3& start,
                                    const math::Vec3& end,
                                    const math::Vec3& search_extents,
                                    const NavQueryFilter& filter) {
  cancelSlicedPath();
  if (!isValid()) {
    return NavStatus::NoNavMesh;
  }

  sliced_filter_ = std::make_unique<dtQueryFilter>(makeDetourFilter(filter));
  dtPolyRef start_ref = 0;
  dtPolyRef end_ref = 0;
  float nearest_start[3]{};
  float nearest_end[3]{};
  if (!succeeded(query_->findNearestPoly(ptr(start),
                                         ptr(search_extents),
                                         sliced_filter_.get(),
                                         &start_ref,
                                         nearest_start)) ||
      start_ref == 0) {
    sliced_filter_.reset();
    return NavStatus::InvalidStart;
  }
  if (!succeeded(query_->findNearestPoly(ptr(end),
                                         ptr(search_extents),
                                         sliced_filter_.get(),
                                         &end_ref,
                                         nearest_end)) ||
      end_ref == 0) {
    sliced_filter_.reset();
    return NavStatus::InvalidEnd;
  }

  const dtStatus status = query_->initSlicedFindPath(start_ref,
                                                     end_ref,
                                                     nearest_start,
                                                     nearest_end,
                                                     sliced_filter_.get());
  if (failed(status)) {
    sliced_filter_.reset();
    return NavStatus::QueryFailed;
  }

  sliced_start_ref_ = static_cast<uint64_t>(start_ref);
  sliced_end_ref_ = static_cast<uint64_t>(end_ref);
  sliced_start_ = toVec3(nearest_start);
  sliced_end_ = toVec3(nearest_end);
  sliced_active_ = true;
  return dtStatusInProgress(status) ? NavStatus::InProgress : NavStatus::Success;
}

NavStatus NavQuery::updateSlicedPath(int max_iterations, bool& out_done) {
  out_done = true;
  if (!isValid() || !sliced_active_) {
    return NavStatus::QueryFailed;
  }

  int done_iters = 0;
  const dtStatus status = query_->updateSlicedFindPath(std::max(max_iterations, 1), &done_iters);
  (void)done_iters;
  if (dtStatusInProgress(status)) {
    out_done = false;
    return NavStatus::InProgress;
  }
  if (failed(status)) {
    cancelSlicedPath();
    return NavStatus::QueryFailed;
  }
  return dtStatusDetail(status, DT_PARTIAL_RESULT) ? NavStatus::PartialPath : NavStatus::Success;
}

NavPath NavQuery::finalizeSlicedPath(int max_points) {
  NavPath result{};
  if (!isValid() || !sliced_active_) {
    result.status = NavStatus::QueryFailed;
    return result;
  }

  std::vector<dtPolyRef> polys(static_cast<size_t>(kMaxPathPolys));
  int poly_count = 0;
  const dtStatus status = query_->finalizeSlicedFindPath(polys.data(),
                                                        &poly_count,
                                                        static_cast<int>(polys.size()));
  const dtPolyRef end_ref = static_cast<dtPolyRef>(sliced_end_ref_);
  const math::Vec3 sliced_start = sliced_start_;
  const math::Vec3 sliced_end = sliced_end_;
  cancelSlicedPath();
  if (!succeeded(status) || poly_count <= 0) {
    result.status = failed(status) ? NavStatus::QueryFailed : NavStatus::NoPath;
    return result;
  }

  const int clamped_max_points = std::max(max_points, 2);
  std::vector<float> straight(static_cast<size_t>(clamped_max_points) * 3u);
  std::vector<unsigned char> flags(static_cast<size_t>(clamped_max_points));
  std::vector<dtPolyRef> straight_polys(static_cast<size_t>(clamped_max_points));
  int point_count = 0;
  if (!succeeded(query_->findStraightPath(ptr(sliced_start),
                                          ptr(sliced_end),
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

  result.partial = dtStatusDetail(status, DT_PARTIAL_RESULT) ||
                   polys[static_cast<size_t>(poly_count - 1)] != end_ref;
  result.status = result.partial ? NavStatus::PartialPath : NavStatus::Success;
  result.points.reserve(static_cast<size_t>(point_count));
  result.point_flags.reserve(static_cast<size_t>(point_count));
  for (int i = 0; i < point_count; ++i) {
    result.points.push_back(toVec3(&straight[static_cast<size_t>(i) * 3u]));
    result.point_flags.push_back(mapStraightPathFlags(flags[static_cast<size_t>(i)]));
  }
  return result;
}

void NavQuery::cancelSlicedPath() {
  sliced_filter_.reset();
  sliced_start_ref_ = 0;
  sliced_end_ref_ = 0;
  sliced_start_ = {};
  sliced_end_ = {};
  sliced_active_ = false;
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
