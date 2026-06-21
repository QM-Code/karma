#include "karma/navigation.h"
#include "karma/navigation.h"

#include <cmath>
#include <vector>

#include <DetourNavMesh.h>
#include <DetourStatus.h>
#include <DetourTileCache.h>

#include "detail/detour_utils.h"
#include "detail/nav_mesh_access.h"
#include "detail/nav_mesh_debug.h"
#include "detail/nav_tile_cache_impl.h"

namespace karma::navigation {

using detail::buildDebugEdges;
using detail::failed;
using detail::mapObstacleShape;
using detail::mapObstacleState;
using detail::ptr;
using detail::succeeded;
using detail::toVec3;

bool NavTileCache::addCylinderObstacle(const math::Vec3& position,
                                       float radius,
                                       float height,
                                       uint64_t* out_ref) {
  if (!isValid() || radius <= 0.0f || height <= 0.0f) {
    return false;
  }
  dtObstacleRef ref = 0;
  if (failed(impl_->tile_cache->addObstacle(ptr(position), radius, height, &ref))) {
    return false;
  }
  impl_->pending_changes = true;
  if (out_ref != nullptr) {
    *out_ref = static_cast<uint64_t>(ref);
  }
  return true;
}

bool NavTileCache::addBoxObstacle(const math::Vec3& bounds_min,
                                  const math::Vec3& bounds_max,
                                  uint64_t* out_ref) {
  if (!isValid() ||
      bounds_max.x <= bounds_min.x ||
      bounds_max.y <= bounds_min.y ||
      bounds_max.z <= bounds_min.z) {
    return false;
  }
  dtObstacleRef ref = 0;
  if (failed(impl_->tile_cache->addBoxObstacle(ptr(bounds_min), ptr(bounds_max), &ref))) {
    return false;
  }
  impl_->pending_changes = true;
  if (out_ref != nullptr) {
    *out_ref = static_cast<uint64_t>(ref);
  }
  return true;
}

bool NavTileCache::addOrientedBoxObstacle(const math::Vec3& center,
                                          const math::Vec3& half_extents,
                                          float yaw_radians,
                                          uint64_t* out_ref) {
  if (!isValid() ||
      half_extents.x <= 0.0f ||
      half_extents.y <= 0.0f ||
      half_extents.z <= 0.0f) {
    return false;
  }
  dtObstacleRef ref = 0;
  if (failed(impl_->tile_cache->addBoxObstacle(ptr(center), ptr(half_extents), yaw_radians, &ref))) {
    return false;
  }
  impl_->pending_changes = true;
  if (out_ref != nullptr) {
    *out_ref = static_cast<uint64_t>(ref);
  }
  return true;
}

bool NavTileCache::removeObstacle(uint64_t ref) {
  if (!isValid() || ref == 0) {
    return false;
  }
  if (failed(impl_->tile_cache->removeObstacle(static_cast<dtObstacleRef>(ref)))) {
    return false;
  }
  impl_->pending_changes = true;
  return true;
}

void NavTileCache::clearObstacles() {
  if (!isValid()) {
    return;
  }
  for (int i = 0; i < impl_->tile_cache->getObstacleCount(); ++i) {
    const dtTileCacheObstacle* obstacle = impl_->tile_cache->getObstacle(i);
    if (obstacle == nullptr || obstacle->state == DT_OBSTACLE_EMPTY) {
      continue;
    }
    const dtObstacleRef ref = impl_->tile_cache->getObstacleRef(obstacle);
    if (ref != 0 && succeeded(impl_->tile_cache->removeObstacle(ref))) {
      impl_->pending_changes = true;
    }
  }
}

bool NavTileCache::update(float dt, NavMesh& nav_mesh, bool* up_to_date) {
  dtNavMesh* detour_nav_mesh = detail::NavMeshAccess::detour(nav_mesh);
  if (!isValid() || detour_nav_mesh == nullptr) {
    if (up_to_date != nullptr) {
      *up_to_date = true;
    }
    return false;
  }

  bool detour_up_to_date = true;
  const bool had_pending_changes = impl_->pending_changes;
  const dtStatus status = impl_->tile_cache->update(dt, detour_nav_mesh, &detour_up_to_date);
  if (up_to_date != nullptr) {
    *up_to_date = detour_up_to_date;
  }
  if (failed(status)) {
    return false;
  }
  if (had_pending_changes || !detour_up_to_date) {
    detail::NavMeshAccess::setDebugEdges(nav_mesh, buildDebugEdges(*detour_nav_mesh));
    detail::NavMeshAccess::refreshSnapshot(nav_mesh);
  }
  impl_->pending_changes = !detour_up_to_date;
  return true;
}

uint32_t NavTileCache::obstacleCapacity() const {
  return isValid() ? static_cast<uint32_t>(impl_->tile_cache->getObstacleCount()) : 0;
}

uint32_t NavTileCache::obstacleCount() const {
  if (!isValid()) {
    return 0;
  }
  uint32_t count = 0;
  for (int i = 0; i < impl_->tile_cache->getObstacleCount(); ++i) {
    const dtTileCacheObstacle* obstacle = impl_->tile_cache->getObstacle(i);
    if (obstacle != nullptr && obstacle->state != DT_OBSTACLE_EMPTY) {
      ++count;
    }
  }
  return count;
}

std::vector<NavTileCacheObstacleInfo> NavTileCache::obstacles() const {
  std::vector<NavTileCacheObstacleInfo> out;
  if (!isValid()) {
    return out;
  }

  for (int i = 0; i < impl_->tile_cache->getObstacleCount(); ++i) {
    const dtTileCacheObstacle* obstacle = impl_->tile_cache->getObstacle(i);
    if (obstacle == nullptr || obstacle->state == DT_OBSTACLE_EMPTY) {
      continue;
    }
    NavTileCacheObstacleInfo info;
    info.ref = static_cast<uint64_t>(impl_->tile_cache->getObstacleRef(obstacle));
    info.shape = mapObstacleShape(obstacle->type);
    info.state = mapObstacleState(obstacle->state);
    switch (obstacle->type) {
      case DT_OBSTACLE_BOX:
        info.position = {
            (obstacle->box.bmin[0] + obstacle->box.bmax[0]) * 0.5f,
            (obstacle->box.bmin[1] + obstacle->box.bmax[1]) * 0.5f,
            (obstacle->box.bmin[2] + obstacle->box.bmax[2]) * 0.5f,
        };
        info.half_extents = {
            (obstacle->box.bmax[0] - obstacle->box.bmin[0]) * 0.5f,
            (obstacle->box.bmax[1] - obstacle->box.bmin[1]) * 0.5f,
            (obstacle->box.bmax[2] - obstacle->box.bmin[2]) * 0.5f,
        };
        break;
      case DT_OBSTACLE_ORIENTED_BOX:
        info.position = toVec3(obstacle->orientedBox.center);
        info.half_extents = toVec3(obstacle->orientedBox.halfExtents);
        info.yaw_radians = std::atan2(-2.0f * obstacle->orientedBox.rotAux[0],
                                      2.0f * obstacle->orientedBox.rotAux[1]);
        break;
      case DT_OBSTACLE_CYLINDER:
      default:
        info.position = toVec3(obstacle->cylinder.pos);
        info.radius = obstacle->cylinder.radius;
        info.height = obstacle->cylinder.height;
        break;
    }
    out.push_back(info);
  }
  return out;
}

uint32_t NavTileCache::tileCapacity() const {
  return isValid() ? static_cast<uint32_t>(impl_->tile_cache->getTileCount()) : 0;
}

uint32_t NavTileCache::tileCount() const {
  if (!isValid()) {
    return 0;
  }
  uint32_t count = 0;
  for (int i = 0; i < impl_->tile_cache->getTileCount(); ++i) {
    const dtCompressedTile* tile = impl_->tile_cache->getTile(i);
    if (tile != nullptr && tile->header != nullptr) {
      ++count;
    }
  }
  return count;
}

std::vector<NavTileCacheTileInfo> NavTileCache::tiles() const {
  std::vector<NavTileCacheTileInfo> out;
  if (!isValid()) {
    return out;
  }
  for (int i = 0; i < impl_->tile_cache->getTileCount(); ++i) {
    const dtCompressedTile* tile = impl_->tile_cache->getTile(i);
    if (tile == nullptr || tile->header == nullptr) {
      continue;
    }
    out.push_back({
        .ref = static_cast<uint64_t>(impl_->tile_cache->getTileRef(tile)),
        .x = tile->header->tx,
        .y = tile->header->ty,
        .layer = tile->header->tlayer,
        .bounds_min = toVec3(tile->header->bmin),
        .bounds_max = toVec3(tile->header->bmax),
        .data_size = static_cast<uint32_t>(tile->dataSize),
    });
  }
  return out;
}

}  // namespace karma::navigation
