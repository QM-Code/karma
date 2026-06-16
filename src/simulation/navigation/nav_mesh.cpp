#include "karma/simulation/navigation/nav_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourDebugDraw.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DebugDraw.h>
#include <Recast.h>
#include <RecastDebugDraw.h>

#include "karma/rendering/renderer/device.h"
#include "detail/detour_utils.h"

namespace karma::navigation {
namespace {

constexpr int kMaxPathPolys = 1024;
constexpr uint32_t kSnapshotMagic = 0x4b4e4156u;  // KNAV
constexpr uint32_t kSnapshotVersion = 2;

using detail::failed;
using detail::flagsForArea;
using detail::makeDetourFilter;
using detail::ptr;
using detail::sanitizeArea;
using detail::succeeded;
using detail::toVec3;

struct SnapshotHeader {
  uint32_t magic = kSnapshotMagic;
  uint32_t version = kSnapshotVersion;
  float origin[3]{};
  float tile_width = 0.0f;
  float tile_height = 0.0f;
  int32_t max_tiles = 0;
  int32_t max_polys = 0;
  uint32_t tile_count = 0;
};

struct SnapshotTileHeader {
  uint64_t tile_ref = 0;
  uint32_t data_size = 0;
};

struct DebugVertex {
  math::Vec3 position{};
  math::Color color{};
};

size_t debugModeIndex(NavMeshDebugDrawMode mode) {
  return static_cast<size_t>(mode);
}

bool validDebugMode(NavMeshDebugDrawMode mode) {
  return debugModeIndex(mode) < kNavMeshDebugDrawModeCount;
}

math::Color debugColor(unsigned int color) {
  return {
      static_cast<float>(color & 0xffu) / 255.0f,
      static_cast<float>((color >> 8u) & 0xffu) / 255.0f,
      static_cast<float>((color >> 16u) & 0xffu) / 255.0f,
      static_cast<float>((color >> 24u) & 0xffu) / 255.0f,
  };
}

class LineCaptureDebugDraw final : public duDebugDraw {
 public:
  void depthMask(bool state) override { (void)state; }
  void texture(bool state) override { (void)state; }

  void begin(duDebugDrawPrimitives prim, float size = 1.0f) override {
    prim_ = prim;
    size_ = size;
    vertices_.clear();
  }

  void vertex(const float* pos, unsigned int color) override {
    vertex(pos[0], pos[1], pos[2], color);
  }

  void vertex(const float x, const float y, const float z, unsigned int color) override {
    vertices_.push_back({{x, y, z}, debugColor(color)});
  }

  void vertex(const float* pos, unsigned int color, const float* uv) override {
    (void)uv;
    vertex(pos, color);
  }

  void vertex(const float x,
              const float y,
              const float z,
              unsigned int color,
              const float u,
              const float v) override {
    (void)u;
    (void)v;
    vertex(x, y, z, color);
  }

  void end() override {
    switch (prim_) {
      case DU_DRAW_POINTS:
        for (const DebugVertex& vertex : vertices_) {
          drawPoint(vertex);
        }
        break;
      case DU_DRAW_LINES:
        for (size_t i = 1; i < vertices_.size(); i += 2) {
          addLine(vertices_[i - 1], vertices_[i]);
        }
        break;
      case DU_DRAW_TRIS:
        for (size_t i = 2; i < vertices_.size(); i += 3) {
          addLine(vertices_[i - 2], vertices_[i - 1]);
          addLine(vertices_[i - 1], vertices_[i]);
          addLine(vertices_[i], vertices_[i - 2]);
        }
        break;
      case DU_DRAW_QUADS:
        for (size_t i = 3; i < vertices_.size(); i += 4) {
          addLine(vertices_[i - 3], vertices_[i - 2]);
          addLine(vertices_[i - 2], vertices_[i - 1]);
          addLine(vertices_[i - 1], vertices_[i]);
          addLine(vertices_[i], vertices_[i - 3]);
        }
        break;
    }
    vertices_.clear();
  }

  std::vector<NavDebugLine> takeLines() { return std::move(lines_); }

 private:
  void addLine(const DebugVertex& a, const DebugVertex& b) {
    lines_.push_back({a.position, b.position, a.color, size_});
  }

  void drawPoint(const DebugVertex& vertex) {
    const float radius = std::max(0.04f, size_ * 0.025f);
    const math::Vec3& p = vertex.position;
    const DebugVertex px0{{p.x - radius, p.y, p.z}, vertex.color};
    const DebugVertex px1{{p.x + radius, p.y, p.z}, vertex.color};
    const DebugVertex py0{{p.x, p.y - radius, p.z}, vertex.color};
    const DebugVertex py1{{p.x, p.y + radius, p.z}, vertex.color};
    const DebugVertex pz0{{p.x, p.y, p.z - radius}, vertex.color};
    const DebugVertex pz1{{p.x, p.y, p.z + radius}, vertex.color};
    addLine(px0, px1);
    addLine(py0, py1);
    addLine(pz0, pz1);
  }

  duDebugDrawPrimitives prim_ = DU_DRAW_LINES;
  float size_ = 1.0f;
  std::vector<DebugVertex> vertices_;
  std::vector<NavDebugLine> lines_;
};

template <class DrawFn>
std::vector<NavDebugLine> captureDebugLines(DrawFn&& draw) {
  LineCaptureDebugDraw debug_draw;
  draw(debug_draw);
  return debug_draw.takeLines();
}

template <class DrawFn>
void appendDebugLines(std::vector<NavDebugLine>& out, DrawFn&& draw) {
  std::vector<NavDebugLine> lines = captureDebugLines(std::forward<DrawFn>(draw));
  out.insert(out.end(),
             std::make_move_iterator(lines.begin()),
             std::make_move_iterator(lines.end()));
}

void clearDebugLines(std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines) {
  for (auto& mode_lines : lines) {
    mode_lines.clear();
  }
}

void clearBuildDebugLines(std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines) {
  for (size_t i = debugModeIndex(NavMeshDebugDrawMode::Voxels);
       i < kNavMeshDebugDrawModeCount;
       ++i) {
    lines[i].clear();
  }
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

template <class T>
void appendValue(std::vector<uint8_t>& out, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

bool readBytes(const std::vector<uint8_t>& data,
               size_t& offset,
               void* out,
               size_t size) {
  if (offset > data.size() || size > data.size() - offset) {
    return false;
  }
  std::memcpy(out, data.data() + offset, size);
  offset += size;
  return true;
}

template <class T>
bool readValue(const std::vector<uint8_t>& data, size_t& offset, T& out) {
  return readBytes(data, offset, &out, sizeof(T));
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

std::vector<math::Vec3> buildDebugEdges(const dtNavMesh& nav_mesh) {
  std::vector<math::Vec3> edges;
  for (int tile_index = 0; tile_index < nav_mesh.getMaxTiles(); ++tile_index) {
    const dtMeshTile* tile = nav_mesh.getTile(tile_index);
    if (tile == nullptr || tile->header == nullptr || tile->verts == nullptr || tile->polys == nullptr) {
      continue;
    }
    for (int poly_index = 0; poly_index < tile->header->polyCount; ++poly_index) {
      const dtPoly& poly = tile->polys[poly_index];
      if (poly.vertCount < 2) {
        continue;
      }
      for (int edge = 0; edge < poly.vertCount; ++edge) {
        const int next_edge = (edge + 1) % poly.vertCount;
        const float* a = &tile->verts[poly.verts[edge] * 3];
        const float* b = &tile->verts[poly.verts[next_edge] * 3];
        edges.push_back(toVec3(a));
        edges.push_back(toVec3(b));
      }
    }
  }
  return edges;
}

void captureBuildDebugLines(const rcHeightfield& solid,
                            const rcCompactHeightfield& compact,
                            const rcContourSet& contours,
                            const rcPolyMesh& poly_mesh,
                            const rcPolyMeshDetail& detail_mesh,
                            std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines) {
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::Voxels)],
                   [&](duDebugDraw& draw) { duDebugDrawHeightfieldSolid(&draw, solid); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::WalkableVoxels)],
                   [&](duDebugDraw& draw) { duDebugDrawHeightfieldWalkable(&draw, solid); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::Compact)],
                   [&](duDebugDraw& draw) { duDebugDrawCompactHeightfieldSolid(&draw, compact); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::CompactDistance)],
                   [&](duDebugDraw& draw) { duDebugDrawCompactHeightfieldDistance(&draw, compact); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::CompactRegions)],
                   [&](duDebugDraw& draw) { duDebugDrawCompactHeightfieldRegions(&draw, compact); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::RegionConnections)], [&](duDebugDraw& draw) {
    duDebugDrawCompactHeightfieldRegions(&draw, compact);
    duDebugDrawRegionConnections(&draw, contours);
  });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::RawContours)],
                   [&](duDebugDraw& draw) { duDebugDrawRawContours(&draw, contours); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::BothContours)], [&](duDebugDraw& draw) {
    duDebugDrawRawContours(&draw, contours, 0.5f);
    duDebugDrawContours(&draw, contours);
  });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::Contours)],
                   [&](duDebugDraw& draw) { duDebugDrawContours(&draw, contours); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::PolyMesh)],
                   [&](duDebugDraw& draw) { duDebugDrawPolyMesh(&draw, poly_mesh); });
  appendDebugLines(lines[debugModeIndex(NavMeshDebugDrawMode::PolyMeshDetail)],
                   [&](duDebugDraw& draw) { duDebugDrawPolyMeshDetail(&draw, detail_mesh); });
}

void captureDetourDebugLines(const dtNavMesh& nav_mesh,
                             std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines) {
  lines[debugModeIndex(NavMeshDebugDrawMode::NavMesh)] =
      captureDebugLines([&](duDebugDraw& draw) {
        duDebugDrawNavMesh(&draw, nav_mesh, DU_DRAWNAVMESH_OFFMESHCONS);
      });
  lines[debugModeIndex(NavMeshDebugDrawMode::NavMeshBVTree)] =
      captureDebugLines([&](duDebugDraw& draw) { duDebugDrawNavMeshBVTree(&draw, nav_mesh); });
  lines[debugModeIndex(NavMeshDebugDrawMode::NavMeshPortals)] =
      captureDebugLines([&](duDebugDraw& draw) { duDebugDrawNavMeshPortals(&draw, nav_mesh); });
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

std::shared_ptr<const NavMeshSnapshot> makeSnapshot(const dtNavMesh& nav_mesh) {
  const dtNavMeshParams* params = nav_mesh.getParams();
  if (params == nullptr) {
    return {};
  }

  SnapshotHeader header{};
  std::memcpy(header.origin, params->orig, sizeof(header.origin));
  header.tile_width = params->tileWidth;
  header.tile_height = params->tileHeight;
  header.max_tiles = params->maxTiles;
  header.max_polys = params->maxPolys;
  for (int i = 0; i < nav_mesh.getMaxTiles(); ++i) {
    const dtMeshTile* tile = nav_mesh.getTile(i);
    if (tile != nullptr && tile->header != nullptr && tile->data != nullptr && tile->dataSize > 0) {
      ++header.tile_count;
    }
  }

  auto snapshot = std::make_shared<NavMeshSnapshot>();
  snapshot->data.reserve(sizeof(SnapshotHeader) + header.tile_count * sizeof(SnapshotTileHeader));
  appendValue(snapshot->data, header);
  for (int i = 0; i < nav_mesh.getMaxTiles(); ++i) {
    const dtMeshTile* tile = nav_mesh.getTile(i);
    if (tile == nullptr || tile->header == nullptr || tile->data == nullptr || tile->dataSize <= 0) {
      continue;
    }
    SnapshotTileHeader tile_header{};
    tile_header.tile_ref = static_cast<uint64_t>(nav_mesh.getTileRef(tile));
    tile_header.data_size = static_cast<uint32_t>(tile->dataSize);
    appendValue(snapshot->data, tile_header);
    snapshot->data.insert(snapshot->data.end(),
                          tile->data,
                          tile->data + static_cast<size_t>(tile->dataSize));
  }
  return snapshot;
}

dtNavMesh* navMeshFromSnapshot(const NavMeshSnapshot& snapshot) {
  if (!snapshot.valid() ||
      snapshot.data.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }

  size_t offset = 0;
  SnapshotHeader header{};
  if (readValue(snapshot.data, offset, header) && header.magic == kSnapshotMagic) {
    if (header.version != kSnapshotVersion ||
        header.max_tiles <= 0 ||
        header.max_polys <= 0) {
      return nullptr;
    }
    dtNavMeshParams params{};
    std::memcpy(params.orig, header.origin, sizeof(params.orig));
    params.tileWidth = header.tile_width;
    params.tileHeight = header.tile_height;
    params.maxTiles = header.max_tiles;
    params.maxPolys = header.max_polys;

    dtNavMesh* mesh = dtAllocNavMesh();
    if (mesh == nullptr || failed(mesh->init(&params))) {
      dtFreeNavMesh(mesh);
      return nullptr;
    }

    for (uint32_t i = 0; i < header.tile_count; ++i) {
      SnapshotTileHeader tile_header{};
      if (!readValue(snapshot.data, offset, tile_header) ||
          tile_header.data_size == 0 ||
          offset > snapshot.data.size() ||
          tile_header.data_size > snapshot.data.size() - offset) {
        dtFreeNavMesh(mesh);
        return nullptr;
      }
      auto* nav_data = static_cast<unsigned char*>(
          dtAlloc(tile_header.data_size, DT_ALLOC_PERM));
      if (nav_data == nullptr) {
        dtFreeNavMesh(mesh);
        return nullptr;
      }
      std::memset(nav_data, 0, tile_header.data_size);
      std::memcpy(nav_data, snapshot.data.data() + offset, tile_header.data_size);
      offset += tile_header.data_size;
      if (failed(mesh->addTile(nav_data,
                               static_cast<int>(tile_header.data_size),
                               DT_TILE_FREE_DATA,
                               static_cast<dtTileRef>(tile_header.tile_ref),
                               nullptr))) {
        dtFree(nav_data);
        dtFreeNavMesh(mesh);
        return nullptr;
      }
    }
    return mesh;
  }

  auto* nav_data = static_cast<unsigned char*>(
      dtAlloc(snapshot.data.size(), DT_ALLOC_PERM));
  if (nav_data == nullptr) {
    return nullptr;
  }
  std::memcpy(nav_data, snapshot.data.data(), snapshot.data.size());

  dtNavMesh* mesh = dtAllocNavMesh();
  if (mesh == nullptr ||
      failed(mesh->init(nav_data,
                        static_cast<int>(snapshot.data.size()),
                        DT_TILE_FREE_DATA))) {
    dtFree(nav_data);
    dtFreeNavMesh(mesh);
    return nullptr;
  }
  return mesh;
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

const char* navMeshDebugDrawModeName(NavMeshDebugDrawMode mode) {
  switch (mode) {
    case NavMeshDebugDrawMode::NavMeshEdges: return "NavMeshEdges";
    case NavMeshDebugDrawMode::NavMesh: return "NavMesh";
    case NavMeshDebugDrawMode::NavMeshBVTree: return "NavMeshBVTree";
    case NavMeshDebugDrawMode::NavMeshPortals: return "NavMeshPortals";
    case NavMeshDebugDrawMode::Voxels: return "Voxels";
    case NavMeshDebugDrawMode::WalkableVoxels: return "WalkableVoxels";
    case NavMeshDebugDrawMode::Compact: return "Compact";
    case NavMeshDebugDrawMode::CompactDistance: return "CompactDistance";
    case NavMeshDebugDrawMode::CompactRegions: return "CompactRegions";
    case NavMeshDebugDrawMode::RegionConnections: return "RegionConnections";
    case NavMeshDebugDrawMode::RawContours: return "RawContours";
    case NavMeshDebugDrawMode::BothContours: return "BothContours";
    case NavMeshDebugDrawMode::Contours: return "Contours";
    case NavMeshDebugDrawMode::PolyMesh: return "PolyMesh";
    case NavMeshDebugDrawMode::PolyMeshDetail: return "PolyMeshDetail";
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
  debug_draw_lines_ = std::move(other.debug_draw_lines_);
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
  clearDebugLines(debug_draw_lines_);
  snapshot_.reset();
}

bool NavMesh::isValid() const {
  return nav_mesh_ != nullptr;
}

bool NavMesh::hasDebugDrawMode(NavMeshDebugDrawMode mode) const {
  if (mode == NavMeshDebugDrawMode::NavMeshEdges) {
    return !debug_edges_.empty();
  }
  return validDebugMode(mode) && !debug_draw_lines_[debugModeIndex(mode)].empty();
}

const std::vector<NavDebugLine>& NavMesh::debugDrawLines(NavMeshDebugDrawMode mode) const {
  static const std::vector<NavDebugLine> empty;
  if (!validDebugMode(mode) || mode == NavMeshDebugDrawMode::NavMeshEdges) {
    return empty;
  }
  return debug_draw_lines_[debugModeIndex(mode)];
}

void NavMesh::refreshSnapshot() {
  snapshot_ = nav_mesh_ != nullptr ? makeSnapshot(*nav_mesh_) : nullptr;
}

void NavMesh::refreshDetourDebugDraw() {
  if (nav_mesh_ == nullptr) {
    return;
  }
  debug_edges_ = buildDebugEdges(*nav_mesh_);
  captureDetourDebugLines(*nav_mesh_, debug_draw_lines_);
}

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

bool NavMesh::removeTile(const math::Vec3& world_position) {
  if (nav_mesh_ == nullptr || config_.build_mode != NavMeshBuildMode::Tiled) {
    return false;
  }
  const dtNavMeshParams* params = nav_mesh_->getParams();
  if (params == nullptr || params->tileWidth <= 0.0f || params->tileHeight <= 0.0f) {
    return false;
  }
  const int tx = static_cast<int>((world_position.x - params->orig[0]) / params->tileWidth);
  const int ty = static_cast<int>((world_position.z - params->orig[2]) / params->tileHeight);
  const dtTileRef ref = nav_mesh_->getTileRefAt(tx, ty, 0);
  if (ref == 0 || failed(nav_mesh_->removeTile(ref, nullptr, nullptr))) {
    return false;
  }
  refreshSnapshot();
  if (config_.collect_build_debug_draw) {
    clearBuildDebugLines(debug_draw_lines_);
  }
  refreshDetourDebugDraw();
  return true;
}

bool NavMesh::removeAllTiles() {
  if (nav_mesh_ == nullptr || config_.build_mode != NavMeshBuildMode::Tiled) {
    return false;
  }
  bool removed = false;
  const dtNavMesh* nav = nav_mesh_;
  for (int i = 0; i < nav->getMaxTiles(); ++i) {
    const dtMeshTile* tile = nav->getTile(i);
    if (tile == nullptr || tile->header == nullptr) {
      continue;
    }
    const dtTileRef ref = nav->getTileRef(tile);
    if (ref != 0 && succeeded(nav_mesh_->removeTile(ref, nullptr, nullptr))) {
      removed = true;
    }
  }
  refreshSnapshot();
  debug_edges_.clear();
  clearDebugLines(debug_draw_lines_);
  return removed;
}

bool NavMesh::loadSnapshot(const NavMeshSnapshot& snapshot,
                           NavMeshBuildResult* result) {
  reset();
  dtNavMesh* mesh = navMeshFromSnapshot(snapshot);
  if (mesh == nullptr) {
    setResult(result, NavStatus::BuildFailed, "Failed to load navigation snapshot.");
    last_result_ = result != nullptr ? *result : NavMeshBuildResult{NavStatus::BuildFailed, "Failed to load navigation snapshot."};
    return false;
  }
  nav_mesh_ = mesh;
  snapshot_ = std::make_shared<NavMeshSnapshot>(snapshot);
  NavMeshBuildResult success{};
  success.status = NavStatus::Success;
  success.message = "Navigation mesh snapshot loaded.";
  const dtNavMesh* nav = nav_mesh_;
  for (int i = 0; i < nav->getMaxTiles(); ++i) {
    const dtMeshTile* tile = nav->getTile(i);
    if (tile != nullptr && tile->header != nullptr) {
      success.polygon_count += static_cast<uint32_t>(tile->header->polyCount);
    }
  }
  last_result_ = success;
  refreshDetourDebugDraw();
  if (result != nullptr) {
    *result = success;
  }
  return true;
}

bool NavMesh::setPolyFlags(uint64_t poly_ref, uint16_t flags) {
  if (nav_mesh_ == nullptr || poly_ref == 0) {
    return false;
  }
  if (!succeeded(nav_mesh_->setPolyFlags(static_cast<dtPolyRef>(poly_ref), flags))) {
    return false;
  }
  refreshSnapshot();
  return true;
}

bool NavMesh::getPolyFlags(uint64_t poly_ref, uint16_t& out_flags) const {
  if (nav_mesh_ == nullptr || poly_ref == 0) {
    return false;
  }
  unsigned short flags = 0;
  if (failed(nav_mesh_->getPolyFlags(static_cast<dtPolyRef>(poly_ref), &flags))) {
    return false;
  }
  out_flags = flags;
  return true;
}

bool NavMesh::setPolyArea(uint64_t poly_ref, unsigned char area) {
  if (nav_mesh_ == nullptr || poly_ref == 0 || area > kNavAreaMax) {
    return false;
  }
  if (!succeeded(nav_mesh_->setPolyArea(static_cast<dtPolyRef>(poly_ref), area))) {
    return false;
  }
  refreshSnapshot();
  return true;
}

bool NavMesh::getPolyArea(uint64_t poly_ref, unsigned char& out_area) const {
  if (nav_mesh_ == nullptr || poly_ref == 0) {
    return false;
  }
  unsigned char area = 0;
  if (failed(nav_mesh_->getPolyArea(static_cast<dtPolyRef>(poly_ref), &area))) {
    return false;
  }
  out_area = area;
  return true;
}

bool NavMesh::decodePolyRef(uint64_t poly_ref, NavPolyRefParts& out_parts) const {
  if (nav_mesh_ == nullptr || poly_ref == 0) {
    return false;
  }
  unsigned int salt = 0;
  unsigned int tile = 0;
  unsigned int poly = 0;
  nav_mesh_->decodePolyId(static_cast<dtPolyRef>(poly_ref), salt, tile, poly);
  out_parts.salt = salt;
  out_parts.tile = tile;
  out_parts.poly = poly;
  return true;
}

std::vector<NavTileInfo> NavMesh::tiles() const {
  std::vector<NavTileInfo> out;
  if (nav_mesh_ == nullptr) {
    return out;
  }
  const dtNavMesh* nav = nav_mesh_;
  for (int i = 0; i < nav->getMaxTiles(); ++i) {
    const dtMeshTile* tile = nav->getTile(i);
    if (tile == nullptr || tile->header == nullptr) {
      continue;
    }
    out.push_back({
        .ref = static_cast<uint64_t>(nav->getTileRef(tile)),
        .index = i,
        .x = tile->header->x,
        .y = tile->header->y,
        .layer = tile->header->layer,
        .poly_count = tile->header->polyCount,
        .vert_count = tile->header->vertCount,
    });
  }
  return out;
}

uint64_t NavMesh::tileRefAt(int x, int y, int layer) const {
  if (nav_mesh_ == nullptr) {
    return 0;
  }
  return static_cast<uint64_t>(nav_mesh_->getTileRefAt(x, y, layer));
}

bool NavMesh::storeTileState(uint64_t tile_ref, NavTileStateSnapshot& out_state) const {
  if (nav_mesh_ == nullptr || tile_ref == 0) {
    return false;
  }
  const dtMeshTile* tile = nav_mesh_->getTileByRef(static_cast<dtTileRef>(tile_ref));
  if (tile == nullptr || tile->header == nullptr) {
    return false;
  }
  const int size = nav_mesh_->getTileStateSize(tile);
  if (size <= 0) {
    return false;
  }
  out_state.tile_ref = tile_ref;
  out_state.data.assign(static_cast<size_t>(size), uint8_t{0});
  if (failed(nav_mesh_->storeTileState(tile,
                                       out_state.data.data(),
                                       static_cast<int>(out_state.data.size())))) {
    out_state = {};
    return false;
  }
  return true;
}

bool NavMesh::restoreTileState(const NavTileStateSnapshot& state) {
  if (nav_mesh_ == nullptr || !state.valid()) {
    return false;
  }
  dtMeshTile* tile = const_cast<dtMeshTile*>(
      nav_mesh_->getTileByRef(static_cast<dtTileRef>(state.tile_ref)));
  if (tile == nullptr || tile->header == nullptr) {
    return false;
  }
  if (failed(nav_mesh_->restoreTileState(tile,
                                         state.data.data(),
                                         static_cast<int>(state.data.size())))) {
    return false;
  }
  refreshSnapshot();
  refreshDetourDebugDraw();
  return true;
}

bool NavMesh::offMeshConnectionEndpoints(
    uint64_t previous_poly_ref,
    uint64_t off_mesh_poly_ref,
    NavOffMeshConnectionEndpoints& out_endpoints) const {
  if (nav_mesh_ == nullptr || previous_poly_ref == 0 || off_mesh_poly_ref == 0) {
    return false;
  }
  float start[3]{};
  float end[3]{};
  if (failed(nav_mesh_->getOffMeshConnectionPolyEndPoints(
          static_cast<dtPolyRef>(previous_poly_ref),
          static_cast<dtPolyRef>(off_mesh_poly_ref),
          start,
          end))) {
    return false;
  }
  out_endpoints.start = toVec3(start);
  out_endpoints.end = toVec3(end);
  return true;
}

bool NavMesh::polyCenter(uint64_t poly_ref, math::Vec3& out_center) const {
  if (nav_mesh_ == nullptr || poly_ref == 0) {
    return false;
  }
  const dtMeshTile* tile = nullptr;
  const dtPoly* poly = nullptr;
  if (failed(nav_mesh_->getTileAndPolyByRef(static_cast<dtPolyRef>(poly_ref), &tile, &poly)) ||
      tile == nullptr ||
      poly == nullptr ||
      poly->vertCount == 0) {
    return false;
  }

  math::Vec3 center{};
  for (int i = 0; i < static_cast<int>(poly->vertCount); ++i) {
    const float* vertex = &tile->verts[poly->verts[i] * 3];
    center.x += vertex[0];
    center.y += vertex[1];
    center.z += vertex[2];
  }
  const float inv_count = 1.0f / static_cast<float>(poly->vertCount);
  out_center = {center.x * inv_count, center.y * inv_count, center.z * inv_count};
  return true;
}

uint32_t NavMesh::pruneUnreachable(const math::Vec3& start,
                                   uint16_t disabled_flags,
                                   const math::Vec3& search_extents,
                                   const NavQueryFilter& filter) {
  if (nav_mesh_ == nullptr) {
    return 0;
  }

  dtNavMeshQuery* query = dtAllocNavMeshQuery();
  if (query == nullptr || failed(query->init(nav_mesh_, kMaxPathPolys))) {
    dtFreeNavMeshQuery(query);
    return 0;
  }

  dtQueryFilter detour_filter = makeDetourFilter(filter);
  dtPolyRef start_ref = 0;
  if (failed(query->findNearestPoly(ptr(start), ptr(search_extents), &detour_filter, &start_ref, nullptr)) ||
      start_ref == 0) {
    dtFreeNavMeshQuery(query);
    return 0;
  }
  dtFreeNavMeshQuery(query);

  std::unordered_set<dtPolyRef> visited;
  std::vector<dtPolyRef> open;
  visited.insert(start_ref);
  open.push_back(start_ref);
  while (!open.empty()) {
    const dtPolyRef ref = open.back();
    open.pop_back();
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    nav_mesh_->getTileAndPolyByRefUnsafe(ref, &tile, &poly);
    for (unsigned int link_index = poly->firstLink;
         link_index != DT_NULL_LINK;
         link_index = tile->links[link_index].next) {
      const dtPolyRef next_ref = tile->links[link_index].ref;
      if (next_ref == 0 || visited.find(next_ref) != visited.end()) {
        continue;
      }
      visited.insert(next_ref);
      open.push_back(next_ref);
    }
  }

  uint32_t disabled_count = 0;
  const dtNavMesh* nav = nav_mesh_;
  for (int tile_index = 0; tile_index < nav->getMaxTiles(); ++tile_index) {
    const dtMeshTile* tile = nav->getTile(tile_index);
    if (tile == nullptr || tile->header == nullptr) {
      continue;
    }
    const dtPolyRef base = nav->getPolyRefBase(tile);
    for (int poly_index = 0; poly_index < tile->header->polyCount; ++poly_index) {
      const dtPolyRef ref = base | static_cast<unsigned int>(poly_index);
      if (visited.find(ref) != visited.end()) {
        continue;
      }
      unsigned short flags = 0;
      if (succeeded(nav_mesh_->getPolyFlags(ref, &flags)) &&
          succeeded(nav_mesh_->setPolyFlags(ref, static_cast<unsigned short>(flags | disabled_flags)))) {
        ++disabled_count;
      }
    }
  }
  refreshSnapshot();
  return disabled_count;
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

void NavMesh::debugDraw(renderer::GraphicsDevice& graphics,
                        NavMeshDebugDrawMode mode,
                        bool depth_test,
                        const math::Color& fallback_color) const {
  if (mode == NavMeshDebugDrawMode::NavMeshEdges || !hasDebugDrawMode(mode)) {
    debugDraw(graphics, fallback_color, depth_test);
    return;
  }

  for (const NavDebugLine& line : debug_draw_lines_[debugModeIndex(mode)]) {
    graphics.drawLine(line.start,
                      line.end,
                      line.color,
                      depth_test,
                      std::max(1.0f, line.thickness));
  }
}

void NavMesh::debugDrawPolygons(renderer::GraphicsDevice& graphics,
                                const std::vector<uint64_t>& poly_refs,
                                const math::Color& color,
                                bool depth_test) const {
  if (nav_mesh_ == nullptr) {
    return;
  }
  for (uint64_t poly_ref : poly_refs) {
    if (poly_ref == 0) {
      continue;
    }
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    if (failed(nav_mesh_->getTileAndPolyByRef(static_cast<dtPolyRef>(poly_ref), &tile, &poly)) ||
        tile == nullptr ||
        poly == nullptr ||
        poly->vertCount == 0) {
      continue;
    }

    math::Vec3 center{};
    if (!polyCenter(poly_ref, center)) {
      continue;
    }
    center.y += 0.06f;
    for (int i = 0; i < static_cast<int>(poly->vertCount); ++i) {
      const int next = (i + 1) % static_cast<int>(poly->vertCount);
      const float* a = &tile->verts[poly->verts[i] * 3];
      const float* b = &tile->verts[poly->verts[next] * 3];
      const math::Vec3 va{a[0], a[1] + 0.06f, a[2]};
      const math::Vec3 vb{b[0], b[1] + 0.06f, b[2]};
      graphics.drawLine(va, vb, color, depth_test, 2.0f);
      graphics.drawLine(center, va, color, depth_test, 1.0f);
    }
  }
}


}  // namespace karma::navigation
