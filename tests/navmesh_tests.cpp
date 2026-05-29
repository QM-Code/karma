#include <cassert>
#include <cmath>
#include <iostream>

#include "karma/navigation/nav_geometry.h"
#include "karma/navigation/nav_mesh.h"
#include "karma/scene/glb_scene_import.h"

namespace {

karma::renderer::MeshData makePlaneMesh(float half_extent = 5.0f) {
  karma::renderer::MeshData mesh;
  mesh.vertices = {
      {-half_extent, 0.0f, -half_extent},
      {half_extent, 0.0f, -half_extent},
      {half_extent, 0.0f, half_extent},
      {-half_extent, 0.0f, half_extent},
  };
  mesh.indices = {0, 2, 1, 0, 3, 2};
  return mesh;
}

karma::navigation::NavMeshInputGeometry makePlaneGeometry(float half_extent = 5.0f) {
  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, makePlaneMesh(half_extent));
  return geometry;
}

void appendQuad(karma::renderer::MeshData& mesh,
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
  karma::renderer::MeshData mesh;
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

void testFlatPlaneBuildAndPath() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildResult build_result;
  const bool built = nav_mesh.build(makePlaneGeometry(), {}, &build_result);
  assert(built);
  assert(build_result.status == karma::navigation::NavStatus::Success);
  assert(build_result.polygon_count > 0);

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(path.success());
  assert(path.points.size() >= 2);
}

void testHoleForcesDetour() {
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makeRingGeometry(), config));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
  assert(path.success());
  assert(path.points.size() > 2);
}

void testInvalidInputFailsCleanly() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildResult build_result;
  const bool built = nav_mesh.build({}, {}, &build_result);
  assert(!built);
  assert(build_result.status == karma::navigation::NavStatus::EmptyInput);
}

void testNearestPoint() {
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry()));

  karma::navigation::NavQuery query(nav_mesh);
  karma::math::Vec3 nearest;
  assert(query.findNearestPoint({0.0f, 3.0f, 0.0f}, nearest));
  assert(std::abs(nearest.y) < 0.05f);
}

void testGlbPrefabCollectionAppliesWorldTransform() {
  karma::scene::GlbScenePrefab prefab;
  prefab.root_node = 0;
  prefab.nodes.resize(1);
  prefab.nodes[0].world_position = {10.0f, 2.0f, -3.0f};
  prefab.nodes[0].world_scale = {2.0f, 1.0f, 2.0f};
  prefab.nodes[0].primitives.push_back({
      .name = "Plane",
      .mesh = makePlaneMesh(1.0f),
  });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(prefab);
  assert(geometry.vertices.size() == 4);
  assert(geometry.indices.size() == 6);
  assert(std::abs(geometry.vertices[0].x - 8.0f) < 0.001f);
  assert(std::abs(geometry.vertices[0].y - 2.0f) < 0.001f);
  assert(std::abs(geometry.vertices[0].z + 5.0f) < 0.001f);
}

}  // namespace

int main() {
  testFlatPlaneBuildAndPath();
  testHoleForcesDetour();
  testInvalidInputFailsCleanly();
  testNearestPoint();
  testGlbPrefabCollectionAppliesWorldTransform();
  std::cout << "navmesh tests passed\n";
  return 0;
}
