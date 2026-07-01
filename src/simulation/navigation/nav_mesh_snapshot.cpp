#include "karma/navigation.h"

#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>

#include "detail/detour_utils.h"
#include "detail/nav_mesh_debug.h"
#include "detail/nav_mesh_result.h"

namespace karma::navigation {

using detail::captureDetourDebugLines;
using detail::failed;
using detail::setResult;

namespace {

constexpr uint32_t kSnapshotMagic = 0x4b4e4156u;  // KNAV
constexpr uint32_t kSnapshotVersion = 2;

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

}  // namespace

void NavMesh::refreshSnapshot() {
  snapshot_ = nav_mesh_ != nullptr ? makeSnapshot(*nav_mesh_) : nullptr;
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

bool NavMesh::loadSnapshot(const NavMeshSnapshot& snapshot,
                           const NavMeshSnapshotMetadata& metadata,
                           NavMeshBuildResult* result) {
  NavMeshBuildResult loaded_result;
  if (!loadSnapshot(snapshot, &loaded_result)) {
    if (result != nullptr) {
      *result = loaded_result;
    }
    return false;
  }

  config_ = metadata.build_config;
  bounds_min_ = metadata.bounds_min;
  bounds_max_ = metadata.bounds_max;
  last_result_ = metadata.build_result;
  if (last_result_.message.empty()) {
    last_result_.message = loaded_result.message;
  }
  if (last_result_.polygon_count == 0) {
    last_result_.polygon_count = loaded_result.polygon_count;
  }
  if (result != nullptr) {
    *result = last_result_;
  }
  return true;
}

}  // namespace karma::navigation
