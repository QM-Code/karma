#include "navmesh_test_utils.h"

namespace karma::tests::navigation {

void testAdvancedQueryHelpers() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  assert(nav_mesh.build(makePlaneGeometry(), config));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath smooth =
      query.findSmoothPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(smooth.success());
  assert(smooth.points.size() > 2);

  karma::math::Vec3 random_point;
  assert(query.findRandomPoint(random_point));
  assert(query.findRandomPointAroundCircle({0.0f, 0.1f, 0.0f}, 2.0f, random_point));

  const karma::navigation::NavPolyQueryResult circle =
      query.findPolysAroundCircle({0.0f, 0.1f, 0.0f}, 2.0f);
  assert(circle.success());
  assert(!circle.polys.empty());

  const karma::navigation::NavPolyQueryResult shape =
      query.findPolysAroundShape({0.0f, 0.1f, 0.0f},
                                 {{-1.0f, 0.1f, -1.0f},
                                  {1.0f, 0.1f, -1.0f},
                                  {1.0f, 0.1f, 1.0f},
                                  {-1.0f, 0.1f, 1.0f}});
  assert(shape.success());
  assert(!shape.polys.empty());

  const karma::navigation::NavPolyQueryResult local =
      query.findLocalNeighbourhood({0.0f, 0.1f, 0.0f}, 2.0f);
  assert(local.success());
  assert(!local.polys.empty());

  const karma::navigation::NavWallSegments walls =
      query.getPolyWallSegments({0.0f, 0.1f, 0.0f});
  assert(walls.success());
  assert(!walls.segments.empty());

  const karma::navigation::NavPolyQueryResult aabb =
      query.queryPolygons({0.0f, 0.1f, 0.0f}, {2.0f, 2.0f, 2.0f});
  assert(aabb.success());
  assert(!aabb.polys.empty());

  const uint64_t poly_ref = aabb.polys.front();
  unsigned char area = 0;
  assert(nav_mesh.getPolyArea(poly_ref, area));
  assert(area == karma::navigation::kNavAreaDefault);
  assert(nav_mesh.setPolyArea(poly_ref, 2));
  assert(nav_mesh.getPolyArea(poly_ref, area));
  assert(area == 2);
  assert(nav_mesh.setPolyArea(poly_ref, karma::navigation::kNavAreaDefault));

  karma::navigation::NavPolyRefParts parts;
  assert(nav_mesh.decodePolyRef(poly_ref, parts));
  assert(parts.salt != 0);

  const auto mesh_tiles = nav_mesh.tiles();
  assert(!mesh_tiles.empty());
  assert(nav_mesh.tileRefAt(mesh_tiles.front().x, mesh_tiles.front().y, mesh_tiles.front().layer) ==
         mesh_tiles.front().ref);

  karma::navigation::NavTileStateSnapshot tile_state;
  assert(nav_mesh.storeTileState(mesh_tiles.front().ref, tile_state));
  uint16_t old_flags = 0;
  assert(nav_mesh.getPolyFlags(poly_ref, old_flags));
  assert(nav_mesh.setPolyFlags(poly_ref, static_cast<uint16_t>(old_flags | (1u << 5u))));
  assert(nav_mesh.restoreTileState(tile_state));
  uint16_t restored_flags = 0;
  assert(nav_mesh.getPolyFlags(poly_ref, restored_flags));
  assert(restored_flags == old_flags);

  const karma::navigation::NavClosestPointResult closest =
      query.closestPointOnPoly(poly_ref, {0.2f, 2.0f, 0.2f});
  assert(closest.success());
  const karma::navigation::NavClosestPointResult boundary =
      query.closestPointOnPolyBoundary(poly_ref, {10.0f, 0.1f, 10.0f});
  assert(boundary.success());

  const karma::navigation::NavRaycastResult ray =
      query.raycastDetailed({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(ray.success());
  assert(!ray.visited_polys.empty());

  if (aabb.polys.size() >= 2) {
    const karma::navigation::NavPortalPoints portal =
        query.portalPoints(aabb.polys[0], aabb.polys[1]);
    if (portal.success()) {
      karma::math::Vec3 midpoint;
      assert(query.edgeMidPoint(aabb.polys[0], aabb.polys[1], midpoint));
    }
  }
}

void testOffMeshConnectionBridgesGap() {
  karma::geometry::MeshData mesh;
  appendQuad(mesh, {-5.0f, 0.0f, -2.0f}, {-1.0f, 0.0f, -2.0f},
             {-1.0f, 0.0f, 2.0f}, {-5.0f, 0.0f, 2.0f});
  appendQuad(mesh, {1.0f, 0.0f, -2.0f}, {5.0f, 0.0f, -2.0f},
             {5.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 2.0f});

  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, mesh);
  geometry.off_mesh_connections.push_back({
      .start = {-1.15f, 0.0f, 0.0f},
      .end = {1.15f, 0.0f, 0.0f},
      .radius = 0.5f,
      .area = karma::navigation::kNavAreaDefault,
      .flags = karma::navigation::kNavPolyFlagWalk | karma::navigation::kNavPolyFlagOffMesh,
      .bidirectional = true,
      .user_id = 7,
  });

  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(geometry, config));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
  assert(path.success());
  bool saw_off_mesh = false;
  for (uint8_t flags : path.point_flags) {
    saw_off_mesh = saw_off_mesh ||
                   ((flags & karma::navigation::NavPathPointFlagOffMeshConnection) != 0);
  }
  assert(saw_off_mesh);
}

void testSlicedPathCompletes() {
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry()));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavStatus begin_status =
      query.beginSlicedPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(begin_status == karma::navigation::NavStatus::InProgress ||
         begin_status == karma::navigation::NavStatus::Success);

  bool done = begin_status == karma::navigation::NavStatus::Success;
  for (int i = 0; i < 64 && !done; ++i) {
    const karma::navigation::NavStatus status = query.updateSlicedPath(1, done);
    assert(status == karma::navigation::NavStatus::InProgress ||
           status == karma::navigation::NavStatus::Success ||
           status == karma::navigation::NavStatus::PartialPath);
  }
  assert(done);

  const karma::navigation::NavPath path = query.finalizeSlicedPath();
  assert(path.success());
  assert(path.points.size() >= 2);
}

}  // namespace karma::tests::navigation
