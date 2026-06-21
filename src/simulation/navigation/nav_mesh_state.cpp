#include "karma/navigation.h"

#include <unordered_set>
#include <vector>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "detail/detour_utils.h"
#include "detail/nav_mesh_debug.h"

namespace karma::navigation {

using detail::clearBuildDebugLines;
using detail::clearDebugLines;
using detail::failed;
using detail::kMaxPathPolys;
using detail::makeDetourFilter;
using detail::ptr;
using detail::succeeded;
using detail::toVec3;

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

}  // namespace karma::navigation
