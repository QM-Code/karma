#include "detail/nav_mesh_debug.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include <DetourDebugDraw.h>
#include <DetourNavMesh.h>
#include <DebugDraw.h>
#include <RecastDebugDraw.h>

#include "karma/rendering.h"
#include "karma/navigation.h"
#include "detail/detour_utils.h"

namespace karma::navigation::detail {

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

}  // namespace karma::navigation::detail

namespace karma::navigation {

using detail::buildDebugEdges;
using detail::captureDetourDebugLines;
using detail::debugModeIndex;
using detail::failed;
using detail::validDebugMode;

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

void NavMesh::refreshDetourDebugDraw() {
  if (nav_mesh_ == nullptr) {
    return;
  }
  debug_edges_ = buildDebugEdges(*nav_mesh_);
  captureDetourDebugLines(*nav_mesh_, debug_draw_lines_);
}

void NavMesh::debugDraw(rendering::GraphicsDevice& graphics,
                        const math::Color& color,
                        bool depth_test) const {
  if (nav_mesh_ == nullptr) {
    return;
  }

  for (size_t i = 1; i < debug_edges_.size(); i += 2) {
    graphics.drawLine(debug_edges_[i - 1], debug_edges_[i], color, depth_test, 1.0f);
  }
}

void NavMesh::debugDraw(rendering::GraphicsDevice& graphics,
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

void NavMesh::debugDrawPolygons(rendering::GraphicsDevice& graphics,
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
