#include "karma/simulation/navigation/nav_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include "detail/detour_utils.h"
#include "detail/nav_mesh_debug.h"
#include "detail/nav_mesh_result.h"

namespace karma::navigation {

using detail::buildDebugEdges;
using detail::captureBuildDebugLines;
using detail::clearBuildDebugLines;
using detail::failed;
using detail::flagsForArea;
using detail::ptr;
using detail::sanitizeArea;
using detail::setResult;
using detail::succeeded;

namespace {

bool validConfig(const NavMeshBuildConfig& config) {
  if (config.cell_size <= 0.0f ||
      config.cell_height <= 0.0f ||
      config.agent_height <= 0.0f ||
      config.agent_radius < 0.0f ||
      config.agent_max_climb < 0.0f ||
      config.agent_max_slope_degrees < 0.0f ||
      config.agent_max_slope_degrees > 90.0f ||
      config.verts_per_poly < 3 ||
      config.verts_per_poly > DT_VERTS_PER_POLYGON ||
      config.tile_size <= 0) {
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

void configureRecast(const NavMeshBuildConfig& config,
                     const math::Vec3& bounds_min,
                     const math::Vec3& bounds_max,
                     rcConfig& cfg) {
  cfg = {};
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
  cfg.detailSampleDist = config.detail_sample_dist < 0.9f
      ? 0.0f
      : cfg.cs * config.detail_sample_dist;
  cfg.detailSampleMaxError = cfg.ch * config.detail_sample_max_error;
  std::memcpy(cfg.bmin, ptr(bounds_min), sizeof(cfg.bmin));
  std::memcpy(cfg.bmax, ptr(bounds_max), sizeof(cfg.bmax));
  rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
}

bool buildRegions(rcContext& context,
                  const NavMeshBuildConfig& config,
                  const rcConfig& cfg,
                  rcCompactHeightfield& compact,
                  NavMeshBuildResult* result) {
  switch (config.partition_type) {
    case NavMeshPartitionType::Watershed:
      if (!rcBuildDistanceField(&context, compact) ||
          !rcBuildRegions(&context, compact, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
        setResult(result, NavStatus::BuildFailed, "Failed to build watershed navigation regions.");
        return false;
      }
      return true;
    case NavMeshPartitionType::Monotone:
      if (!rcBuildRegionsMonotone(&context, compact, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
        setResult(result, NavStatus::BuildFailed, "Failed to build monotone navigation regions.");
        return false;
      }
      return true;
    case NavMeshPartitionType::Layers:
      if (!rcBuildLayerRegions(&context, compact, cfg.borderSize, cfg.minRegionArea)) {
        setResult(result, NavStatus::BuildFailed, "Failed to build layered navigation regions.");
        return false;
      }
      return true;
  }
  setResult(result, NavStatus::InvalidConfig, "Unknown navigation partition type.");
  return false;
}

void markConvexVolumes(rcContext& context,
                       const NavMeshInputGeometry& geometry,
                       rcCompactHeightfield& compact) {
  for (const NavConvexVolume& volume : geometry.convex_volumes) {
    if (volume.vertices.size() < 3 || volume.area == kNavAreaNull) {
      continue;
    }
    std::vector<float> verts;
    verts.reserve(volume.vertices.size() * 3u);
    for (const math::Vec3& vertex : volume.vertices) {
      verts.push_back(vertex.x);
      verts.push_back(vertex.y);
      verts.push_back(vertex.z);
    }
    rcMarkConvexPolyArea(&context,
                         verts.data(),
                         static_cast<int>(volume.vertices.size()),
                         volume.min_y,
                         volume.max_y,
                         sanitizeArea(volume.area),
                         compact);
  }
}

void fillOffMeshConnectionArrays(const NavMeshInputGeometry& geometry,
                                 const NavMeshBuildConfig& config,
                                 std::vector<float>& vertices,
                                 std::vector<float>& radii,
                                 std::vector<unsigned short>& flags,
                                 std::vector<unsigned char>& areas,
                                 std::vector<unsigned char>& dirs,
                                 std::vector<unsigned int>& user_ids) {
  vertices.clear();
  radii.clear();
  flags.clear();
  areas.clear();
  dirs.clear();
  user_ids.clear();
  vertices.reserve(geometry.off_mesh_connections.size() * 6u);
  radii.reserve(geometry.off_mesh_connections.size());
  flags.reserve(geometry.off_mesh_connections.size());
  areas.reserve(geometry.off_mesh_connections.size());
  dirs.reserve(geometry.off_mesh_connections.size());
  user_ids.reserve(geometry.off_mesh_connections.size());
  for (const NavOffMeshConnection& connection : geometry.off_mesh_connections) {
    if (connection.radius <= 0.0f || connection.area == kNavAreaNull) {
      continue;
    }
    const unsigned char area = sanitizeArea(connection.area);
    vertices.insert(vertices.end(),
                    {connection.start.x,
                     connection.start.y,
                     connection.start.z,
                     connection.end.x,
                     connection.end.y,
                     connection.end.z});
    radii.push_back(connection.radius);
    flags.push_back(connection.flags != 0 ? connection.flags : config.off_mesh_poly_flags);
    areas.push_back(area);
    dirs.push_back(connection.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
    user_ids.push_back(connection.user_id);
  }
}

int nextPow2(int value) {
  int out = 1;
  while (out < value) {
    out <<= 1;
  }
  return out;
}

int ilog2(int value) {
  int log = 0;
  while ((value >>= 1) != 0) {
    ++log;
  }
  return log;
}

unsigned char* buildTileData(const NavMeshInputGeometry& geometry,
                             const NavMeshBuildConfig& config,
                             const math::Vec3& bounds_min,
                             const math::Vec3& bounds_max,
                             int tx,
                             int ty,
                             int& out_data_size,
                             uint32_t& out_poly_count,
                             std::vector<math::Vec3>* out_debug_edges,
                             std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>* out_debug_lines,
                             NavMeshBuildResult* result) {
  out_data_size = 0;
  out_poly_count = 0;

  std::vector<float> vertices = flattenVertices(geometry.vertices);
  std::vector<int> indices;
  if (!flattenIndices(geometry.indices, indices)) {
    setResult(result, NavStatus::BuildFailed, "Navigation indices exceed Recast limits.");
    return nullptr;
  }

  rcContext context;
  rcConfig cfg{};
  configureRecast(config, bounds_min, bounds_max, cfg);
  cfg.tileSize = config.tile_size;
  cfg.borderSize = cfg.walkableRadius + 3;
  cfg.width = cfg.tileSize + cfg.borderSize * 2;
  cfg.height = cfg.tileSize + cfg.borderSize * 2;

  const float tile_world_size = static_cast<float>(config.tile_size) * config.cell_size;
  cfg.bmin[0] = bounds_min.x + static_cast<float>(tx) * tile_world_size;
  cfg.bmin[1] = bounds_min.y;
  cfg.bmin[2] = bounds_min.z + static_cast<float>(ty) * tile_world_size;
  cfg.bmax[0] = bounds_min.x + static_cast<float>(tx + 1) * tile_world_size;
  cfg.bmax[1] = bounds_max.y;
  cfg.bmax[2] = bounds_min.z + static_cast<float>(ty + 1) * tile_world_size;
  cfg.bmin[0] -= static_cast<float>(cfg.borderSize) * cfg.cs;
  cfg.bmin[2] -= static_cast<float>(cfg.borderSize) * cfg.cs;
  cfg.bmax[0] += static_cast<float>(cfg.borderSize) * cfg.cs;
  cfg.bmax[2] += static_cast<float>(cfg.borderSize) * cfg.cs;

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
    setResult(result, NavStatus::BuildFailed, "Failed to create tiled Recast heightfield.");
    return nullptr;
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
    setResult(result, NavStatus::BuildFailed, "Failed to rasterize tiled navigation triangles.");
    return nullptr;
  }

  rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid);
  rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
  rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);

  compact = rcAllocCompactHeightfield();
  if (compact == nullptr ||
      !rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid, *compact)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build tiled compact heightfield.");
    return nullptr;
  }

  if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to erode tiled navigation regions.");
    return nullptr;
  }

  markConvexVolumes(context, geometry, *compact);
  if (!buildRegions(context, config, cfg, *compact, result)) {
    cleanup_recast();
    return nullptr;
  }

  contours = rcAllocContourSet();
  if (contours == nullptr ||
      !rcBuildContours(&context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build tiled navigation contours.");
    return nullptr;
  }
  if (contours->nconts == 0) {
    cleanup_recast();
    return nullptr;
  }

  poly_mesh = rcAllocPolyMesh();
  if (poly_mesh == nullptr ||
      !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *poly_mesh)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to build tiled navigation polygon mesh.");
    return nullptr;
  }
  if (poly_mesh->nverts >= 0xffff) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Tiled navigation mesh has too many vertices in one tile.");
    return nullptr;
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
    setResult(result, NavStatus::BuildFailed, "Failed to build tiled navigation detail mesh.");
    return nullptr;
  }

  for (int i = 0; i < poly_mesh->npolys; ++i) {
    poly_mesh->flags[i] = flagsForArea(config, poly_mesh->areas[i]);
  }
  if (out_debug_edges != nullptr) {
    std::vector<math::Vec3> tile_edges = buildDebugEdges(*poly_mesh);
    out_debug_edges->insert(out_debug_edges->end(), tile_edges.begin(), tile_edges.end());
  }
  if (out_debug_lines != nullptr && config.collect_build_debug_draw) {
    captureBuildDebugLines(*solid,
                           *compact,
                           *contours,
                           *poly_mesh,
                           *detail_mesh,
                           *out_debug_lines);
  }

  std::vector<float> off_mesh_vertices;
  std::vector<float> off_mesh_radii;
  std::vector<unsigned short> off_mesh_flags;
  std::vector<unsigned char> off_mesh_areas;
  std::vector<unsigned char> off_mesh_dirs;
  std::vector<unsigned int> off_mesh_user_ids;
  fillOffMeshConnectionArrays(geometry,
                              config,
                              off_mesh_vertices,
                              off_mesh_radii,
                              off_mesh_flags,
                              off_mesh_areas,
                              off_mesh_dirs,
                              off_mesh_user_ids);

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
  params.offMeshConVerts = off_mesh_vertices.empty() ? nullptr : off_mesh_vertices.data();
  params.offMeshConRad = off_mesh_radii.empty() ? nullptr : off_mesh_radii.data();
  params.offMeshConFlags = off_mesh_flags.empty() ? nullptr : off_mesh_flags.data();
  params.offMeshConAreas = off_mesh_areas.empty() ? nullptr : off_mesh_areas.data();
  params.offMeshConDir = off_mesh_dirs.empty() ? nullptr : off_mesh_dirs.data();
  params.offMeshConUserID = off_mesh_user_ids.empty() ? nullptr : off_mesh_user_ids.data();
  params.offMeshConCount = static_cast<int>(off_mesh_radii.size());
  params.tileX = tx;
  params.tileY = ty;
  params.tileLayer = 0;
  rcVcopy(params.bmin, poly_mesh->bmin);
  rcVcopy(params.bmax, poly_mesh->bmax);
  params.cs = cfg.cs;
  params.ch = cfg.ch;
  params.buildBvTree = true;

  if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to create tiled Detour navmesh data.");
    return nullptr;
  }

  out_data_size = nav_data_size;
  out_poly_count = static_cast<uint32_t>(poly_mesh->npolys);
  cleanup_recast();
  return nav_data;
}

}  // namespace

bool NavMesh::build(const NavMeshInputGeometry& geometry,
                    const NavMeshBuildConfig& config,
                    NavMeshBuildResult* result) {
  if (config.build_mode == NavMeshBuildMode::Tiled) {
    return buildTiled(geometry, config, result);
  }

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
  configureRecast(config, bounds_min_, bounds_max_, cfg);

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

  if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact)) {
    cleanup_recast();
    setResult(result, NavStatus::BuildFailed, "Failed to erode navigation regions.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to erode navigation regions."};
    return false;
  }

  markConvexVolumes(context, geometry, *compact);
  if (!buildRegions(context, config, cfg, *compact, result)) {
    cleanup_recast();
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
  if (config.collect_build_debug_draw) {
    captureBuildDebugLines(*solid,
                           *compact,
                           *contours,
                           *poly_mesh,
                           *detail_mesh,
                           debug_draw_lines_);
  }

  unsigned char* nav_data = nullptr;
  int nav_data_size = 0;
  std::vector<float> off_mesh_vertices;
  std::vector<float> off_mesh_radii;
  std::vector<unsigned short> off_mesh_flags;
  std::vector<unsigned char> off_mesh_areas;
  std::vector<unsigned char> off_mesh_dirs;
  std::vector<unsigned int> off_mesh_user_ids;
  fillOffMeshConnectionArrays(geometry,
                              config,
                              off_mesh_vertices,
                              off_mesh_radii,
                              off_mesh_flags,
                              off_mesh_areas,
                              off_mesh_dirs,
                              off_mesh_user_ids);

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
  refreshDetourDebugDraw();
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

bool NavMesh::buildTiled(const NavMeshInputGeometry& geometry,
                         const NavMeshBuildConfig& config,
                         NavMeshBuildResult* result) {
  reset();
  config_ = config;
  config_.build_mode = NavMeshBuildMode::Tiled;

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

  computeBounds(geometry, bounds_min_, bounds_max_);
  int grid_width = 0;
  int grid_height = 0;
  const float bmin[3]{bounds_min_.x, bounds_min_.y, bounds_min_.z};
  const float bmax[3]{bounds_max_.x, bounds_max_.y, bounds_max_.z};
  rcCalcGridSize(bmin, bmax, config.cell_size, &grid_width, &grid_height);
  const int tile_width = (grid_width + config.tile_size - 1) / config.tile_size;
  const int tile_height = (grid_height + config.tile_size - 1) / config.tile_size;
  if (tile_width <= 0 || tile_height <= 0) {
    setResult(result, NavStatus::BuildFailed, "Navigation tile grid is empty.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Navigation tile grid is empty."};
    return false;
  }

  const int tile_count = tile_width * tile_height;
  const int tile_bits = std::min(ilog2(nextPow2(tile_count)), 14);
  const int poly_bits = 22 - tile_bits;
  const int max_tiles = config.max_tiles > 0 ? config.max_tiles : (1 << tile_bits);
  const int max_polys_per_tile = config.max_polys_per_tile > 0
      ? config.max_polys_per_tile
      : (1 << poly_bits);

  dtNavMeshParams params{};
  params.orig[0] = bounds_min_.x;
  params.orig[1] = bounds_min_.y;
  params.orig[2] = bounds_min_.z;
  params.tileWidth = static_cast<float>(config.tile_size) * config.cell_size;
  params.tileHeight = static_cast<float>(config.tile_size) * config.cell_size;
  params.maxTiles = max_tiles;
  params.maxPolys = max_polys_per_tile;

  dtNavMesh* mesh = dtAllocNavMesh();
  if (mesh == nullptr || failed(mesh->init(&params))) {
    dtFreeNavMesh(mesh);
    setResult(result, NavStatus::BuildFailed, "Failed to initialize tiled Detour navmesh.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to initialize tiled Detour navmesh."};
    return false;
  }

  uint32_t polygon_count = 0;
  debug_edges_.clear();
  for (int y = 0; y < tile_height; ++y) {
    for (int x = 0; x < tile_width; ++x) {
      int data_size = 0;
      uint32_t tile_polys = 0;
      unsigned char* data = buildTileData(geometry,
                                          config_,
                                          bounds_min_,
                                          bounds_max_,
                                          x,
                                          y,
                                          data_size,
                                          tile_polys,
                                          &debug_edges_,
                                          config_.collect_build_debug_draw ? &debug_draw_lines_ : nullptr,
                                          result);
      if (data == nullptr) {
        continue;
      }
      mesh->removeTile(mesh->getTileRefAt(x, y, 0), nullptr, nullptr);
      if (failed(mesh->addTile(data, data_size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(data);
        dtFreeNavMesh(mesh);
        setResult(result, NavStatus::BuildFailed, "Failed to add tiled Detour navmesh data.");
        last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to add tiled Detour navmesh data."};
        return false;
      }
      polygon_count += tile_polys;
    }
  }

  nav_mesh_ = mesh;
  refreshSnapshot();
  refreshDetourDebugDraw();
  NavMeshBuildResult success{};
  success.status = NavStatus::Success;
  success.message = "Tiled navigation mesh built.";
  success.vertex_count = static_cast<uint32_t>(geometry.vertices.size());
  success.triangle_count = geometry.triangleCount();
  success.polygon_count = polygon_count;
  last_result_ = success;
  if (result != nullptr) {
    *result = success;
  }
  return true;
}

bool NavMesh::rebuildTile(const NavMeshInputGeometry& geometry,
                          const math::Vec3& world_position,
                          NavMeshBuildResult* result) {
  if (nav_mesh_ == nullptr || config_.build_mode != NavMeshBuildMode::Tiled) {
    setResult(result, NavStatus::NoNavMesh, "Navigation mesh is not a tiled navmesh.");
    return false;
  }
  const dtNavMeshParams* params = nav_mesh_->getParams();
  if (params == nullptr || params->tileWidth <= 0.0f || params->tileHeight <= 0.0f) {
    setResult(result, NavStatus::NoNavMesh, "Tiled navmesh has invalid tile parameters.");
    return false;
  }
  const int tx = static_cast<int>((world_position.x - params->orig[0]) / params->tileWidth);
  const int ty = static_cast<int>((world_position.z - params->orig[2]) / params->tileHeight);

  int data_size = 0;
  uint32_t tile_polys = 0;
  std::vector<math::Vec3> tile_edges;
  if (config_.collect_build_debug_draw) {
    clearBuildDebugLines(debug_draw_lines_);
  }
  unsigned char* data = buildTileData(geometry,
                                      config_,
                                      bounds_min_,
                                      bounds_max_,
                                      tx,
                                      ty,
                                      data_size,
                                      tile_polys,
                                      &tile_edges,
                                      config_.collect_build_debug_draw ? &debug_draw_lines_ : nullptr,
                                      result);
  nav_mesh_->removeTile(nav_mesh_->getTileRefAt(tx, ty, 0), nullptr, nullptr);
  if (data != nullptr &&
      failed(nav_mesh_->addTile(data, data_size, DT_TILE_FREE_DATA, 0, nullptr))) {
    dtFree(data);
    setResult(result, NavStatus::BuildFailed, "Failed to add rebuilt tiled Detour navmesh data.");
    return false;
  }
  refreshSnapshot();
  refreshDetourDebugDraw();
  setResult(result,
            NavStatus::Success,
            "Navigation tile rebuilt.",
            static_cast<uint32_t>(geometry.vertices.size()),
            geometry.triangleCount(),
            tile_polys);
  return true;
}

}  // namespace karma::navigation
