#include "navmesh_test_utils.h"

namespace karma::tests::navigation {

std::filesystem::path resolveRepoPath(const std::filesystem::path& relative) {
  std::filesystem::path cwd = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    const std::filesystem::path candidate = cwd / relative;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (!cwd.has_parent_path()) {
      break;
    }
    cwd = cwd.parent_path();
  }
  return relative;
}

karma::world::MeshData makePlaneMesh(float half_extent) {
  karma::world::MeshData mesh;
  mesh.vertices = {
      {-half_extent, 0.0f, -half_extent},
      {half_extent, 0.0f, -half_extent},
      {half_extent, 0.0f, half_extent},
      {-half_extent, 0.0f, half_extent},
  };
  mesh.indices = {0, 2, 1, 0, 3, 2};
  return mesh;
}

karma::world::MeshData combineMeshes(const std::vector<karma::world::MeshData>& meshes) {
  karma::world::MeshData combined;
  for (const karma::world::MeshData& mesh : meshes) {
    const uint32_t base = static_cast<uint32_t>(combined.vertices.size());
    combined.vertices.insert(combined.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    combined.indices.reserve(combined.indices.size() + mesh.indices.size());
    for (const uint32_t index : mesh.indices) {
      combined.indices.push_back(base + index);
    }
  }
  return combined;
}

karma::navigation::NavMeshInputGeometry makePlaneGeometry(float half_extent) {
  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, makePlaneMesh(half_extent));
  return geometry;
}

void appendQuad(karma::world::MeshData& mesh,
                const karma::math::Vec3& a,
                const karma::math::Vec3& b,
                const karma::math::Vec3& c,
                const karma::math::Vec3& d) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({a.x, a.y, a.z});
  mesh.vertices.push_back({b.x, b.y, b.z});
  mesh.vertices.push_back({c.x, c.y, c.z});
  mesh.vertices.push_back({d.x, d.y, d.z});
  mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

karma::navigation::NavMeshInputGeometry makeRingGeometry() {
  karma::world::MeshData mesh;
  appendQuad(mesh, {-5.0f, 0.0f, -5.0f}, {-1.0f, 0.0f, -5.0f},
             {-1.0f, 0.0f, 5.0f}, {-5.0f, 0.0f, 5.0f});
  appendQuad(mesh, {1.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f},
             {5.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 5.0f});
  appendQuad(mesh, {-1.0f, 0.0f, -5.0f}, {1.0f, 0.0f, -5.0f},
             {1.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, -1.0f});
  appendQuad(mesh, {-1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
             {1.0f, 0.0f, 5.0f}, {-1.0f, 0.0f, 5.0f});

  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, mesh);
  return geometry;
}

karma::navigation::NavMeshInputGeometry makeCorridorGeometry() {
  karma::world::MeshData mesh;
  appendQuad(mesh,
             {-5.0f, 0.0f, -1.0f},
             {5.0f, 0.0f, -1.0f},
             {5.0f, 0.0f, 1.0f},
             {-5.0f, 0.0f, 1.0f});

  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, mesh);
  return geometry;
}

void updateTileCacheUntilReady(karma::navigation::NavTileCache& tile_cache,
                               karma::navigation::NavMesh& nav_mesh) {
  for (int i = 0; i < 16; ++i) {
    bool up_to_date = false;
    assert(tile_cache.update(0.0f, nav_mesh, &up_to_date));
    if (up_to_date) {
      return;
    }
  }
  assert(false && "tile cache did not become ready");
}

}  // namespace karma::tests::navigation
