#include "karma/navigation.h"

#include <algorithm>

#include <DetourNavMesh.h>

#include "karma/rendering.h"
#include "detail/detour_utils.h"
#include "detail/nav_mesh_access.h"
#include "detail/nav_mesh_debug.h"

namespace karma::navigation {

void NavMesh::debugDraw(rendering::GraphicsDevice& graphics,
                        const math::Color& color,
                        bool depth_test) const {
  if (nav_mesh_ == nullptr) {
    return;
  }
  for (size_t index = 1u; index < debug_edges_.size(); index += 2u) {
    graphics.drawLine(debug_edges_[index - 1u],
                      debug_edges_[index],
                      color,
                      depth_test,
                      1.0f);
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
  for (const NavDebugLine& line :
       debug_draw_lines_[detail::debugModeIndex(mode)]) {
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
  for (const uint64_t poly_ref : poly_refs) {
    if (poly_ref == 0u) {
      continue;
    }
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    if (detail::failed(nav_mesh_->getTileAndPolyByRef(
            static_cast<dtPolyRef>(poly_ref), &tile, &poly)) ||
        tile == nullptr || poly == nullptr || poly->vertCount == 0) {
      continue;
    }

    math::Vec3 center{};
    if (!polyCenter(poly_ref, center)) {
      continue;
    }
    center.y += 0.06f;
    for (int index = 0; index < static_cast<int>(poly->vertCount); ++index) {
      const int next = (index + 1) % static_cast<int>(poly->vertCount);
      const float* a = &tile->verts[poly->verts[index] * 3];
      const float* b = &tile->verts[poly->verts[next] * 3];
      const math::Vec3 va{a[0], a[1] + 0.06f, a[2]};
      const math::Vec3 vb{b[0], b[1] + 0.06f, b[2]};
      graphics.drawLine(va, vb, color, depth_test, 2.0f);
      graphics.drawLine(center, va, color, depth_test, 1.0f);
    }
  }
}

}  // namespace karma::navigation
