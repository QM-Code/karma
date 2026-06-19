#include "navmesh_test_utils.h"

#include "karma/content/assets/asset_registry.h"

namespace karma::tests::navigation {

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

  const std::shared_ptr<const karma::navigation::NavMeshSnapshot> snapshot =
      nav_mesh.snapshot();
  assert(snapshot != nullptr);
  karma::navigation::NavQuery snapshot_query(*snapshot);
  const karma::navigation::NavPath snapshot_path =
      snapshot_query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(snapshot_path.success());
  assert(snapshot_path.points.size() >= 2);
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
  assert(std::abs(nearest.y) <= 0.25f);
}

void testNearestPolyFlagsAndSnapshotRefresh() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildConfig config;
  config.default_poly_flags = karma::navigation::kNavPolyFlagWalk;
  assert(nav_mesh.build(makePlaneGeometry(), config));

  karma::navigation::NavQuery query(nav_mesh);
  uint64_t poly_ref = 0;
  karma::math::Vec3 nearest{};
  assert(query.findNearestPoly({0.0f, 0.2f, 0.0f}, poly_ref, &nearest));
  assert(poly_ref != 0);

  karma::math::Vec3 center{};
  assert(nav_mesh.polyCenter(poly_ref, center));
  assert(std::isfinite(center.x));
  assert(std::isfinite(center.y));
  assert(std::isfinite(center.z));

  uint16_t flags = 0;
  assert(nav_mesh.getPolyFlags(poly_ref, flags));
  assert((flags & karma::navigation::kNavPolyFlagWalk) != 0);
  assert(nav_mesh.setPolyFlags(poly_ref, static_cast<uint16_t>(flags | (1u << 4u))));
  assert(nav_mesh.getPolyFlags(poly_ref, flags));
  assert((flags & (1u << 4u)) != 0);

  const std::shared_ptr<const karma::navigation::NavMeshSnapshot> snapshot = nav_mesh.snapshot();
  assert(snapshot != nullptr);
  karma::navigation::NavQuery snapshot_query(*snapshot);
  karma::navigation::NavQueryFilter filter;
  filter.exclude_flags = 1u << 4u;
  const karma::navigation::NavPath blocked =
      snapshot_query.findPath({0.0f, 0.2f, 0.0f},
                              {1.0f, 0.2f, 0.0f},
                              {2.0f, 4.0f, 2.0f},
                              256,
                              filter);
  assert(!blocked.success());
}

void testGlbPrefabCollectionAppliesWorldTransform() {
  karma::ecs::World world;
  const karma::ecs::Entity surface = world.createEntity();
  karma::components::TransformComponent transform{};
  transform.setPosition({10.0f, 2.0f, -3.0f});
  transform.setScale({2.0f, 1.0f, 2.0f});
  world.add(surface, transform);
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data =
                             std::make_shared<karma::geometry::MeshData>(makePlaneMesh(1.0f)),
                     });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world);
  assert(geometry.vertices.size() == 4);
  assert(geometry.indices.size() == 6);
  assert(std::abs(geometry.vertices[0].x - 8.0f) < 0.001f);
  assert(std::abs(geometry.vertices[0].y - 2.0f) < 0.001f);
  assert(std::abs(geometry.vertices[0].z + 5.0f) < 0.001f);
}

void testWorldSurfaceCollectionUsesNavMeshSurfaceArea() {
  karma::ecs::World world;
  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .area = 2,
                         .mesh_data = std::make_shared<karma::geometry::MeshData>(makePlaneMesh()),
                     });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world);
  assert(geometry.triangleCount() == 2);
  assert(geometry.triangle_areas.size() == 2);
  assert(geometry.triangle_areas[0] == 2);
  assert(geometry.triangle_areas[1] == 2);
}

void testWorldSurfaceCollectionResolvesMeshAssetKey() {
  karma::content::AssetRegistry assets;
  assets.registerMeshAsset("runtime/nav/plane", makePlaneMesh());

  karma::ecs::World world;
  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .area = 3,
                         .mesh_asset_key = "runtime/nav/plane",
                     });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world, &assets);
  assert(geometry.triangleCount() == 2);
  assert(geometry.triangle_areas.size() == 2);
  assert(geometry.triangle_areas[0] == 3);
  assert(geometry.triangle_areas[1] == 3);
}

void testAreaFlagsFilterQueries() {
  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry,
                                    makePlaneMesh(),
                                    {},
                                    {},
                                    {1.0f, 1.0f, 1.0f},
                                    2);

  karma::navigation::NavMeshBuildConfig config;
  config.area_configs.push_back({
      .area = 2,
      .flags = static_cast<uint16_t>(1u << 4u),
      .cost = 2.0f,
  });

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(geometry, config));
  karma::navigation::NavQuery query(nav_mesh);

  karma::navigation::NavQueryFilter matching_filter;
  matching_filter.include_flags = static_cast<uint16_t>(1u << 4u);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, -4.0f},
                     {4.0f, 0.1f, 4.0f},
                     {2.0f, 4.0f, 2.0f},
                     256,
                     matching_filter);
  assert(path.success());

  karma::navigation::NavQueryFilter rejected_filter;
  rejected_filter.include_flags = karma::navigation::kNavPolyFlagWalk;
  const karma::navigation::NavPath rejected_path =
      query.findPath({-4.0f, 0.1f, -4.0f},
                     {4.0f, 0.1f, 4.0f},
                     {2.0f, 4.0f, 2.0f},
                     256,
                     rejected_filter);
  assert(!rejected_path.success());
}

void testConvexVolumeMarksArea() {
  karma::navigation::NavMeshInputGeometry geometry = makePlaneGeometry();
  geometry.convex_volumes.push_back({
      .vertices = {{-5.0f, 0.0f, -5.0f},
                   {5.0f, 0.0f, -5.0f},
                   {5.0f, 0.0f, 5.0f},
                   {-5.0f, 0.0f, 5.0f}},
      .min_y = -1.0f,
      .max_y = 1.0f,
      .area = 2,
  });

  karma::navigation::NavMeshBuildConfig config;
  config.area_configs.push_back({
      .area = 2,
      .flags = static_cast<uint16_t>(1u << 4u),
      .cost = 1.0f,
  });

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(geometry, config));
  karma::navigation::NavQuery query(nav_mesh);

  karma::navigation::NavQueryFilter matching_filter;
  matching_filter.include_flags = static_cast<uint16_t>(1u << 4u);
  assert(query.findPath({-4.0f, 0.1f, -4.0f},
                        {4.0f, 0.1f, 4.0f},
                        {2.0f, 4.0f, 2.0f},
                        256,
                        matching_filter).success());

  karma::navigation::NavQueryFilter rejected_filter;
  rejected_filter.include_flags = karma::navigation::kNavPolyFlagWalk;
  assert(!query.findPath({-4.0f, 0.1f, -4.0f},
                         {4.0f, 0.1f, 4.0f},
                         {2.0f, 4.0f, 2.0f},
                         256,
                         rejected_filter).success());
}

void testPartitionModesAndTiledSnapshot() {
  for (const karma::navigation::NavMeshPartitionType partition :
       {karma::navigation::NavMeshPartitionType::Watershed,
        karma::navigation::NavMeshPartitionType::Monotone,
        karma::navigation::NavMeshPartitionType::Layers}) {
    karma::navigation::NavMeshBuildConfig config;
    config.partition_type = partition;
    config.agent_radius = 0.2f;
    karma::navigation::NavMesh nav_mesh;
    assert(nav_mesh.build(makeRingGeometry(), config));
    karma::navigation::NavQuery query(nav_mesh);
    assert(query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f}).success());
  }

  karma::navigation::NavMeshBuildConfig tiled_config;
  tiled_config.build_mode = karma::navigation::NavMeshBuildMode::Tiled;
  tiled_config.partition_type = karma::navigation::NavMeshPartitionType::Layers;
  tiled_config.tile_size = 16;
  tiled_config.agent_radius = 0.2f;

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry(), tiled_config));
  assert(nav_mesh.lastBuildResult().polygon_count > 0);

  const std::shared_ptr<const karma::navigation::NavMeshSnapshot> snapshot = nav_mesh.snapshot();
  assert(snapshot != nullptr);
  karma::navigation::NavQuery snapshot_query(*snapshot);
  assert(snapshot_query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f}).success());

  karma::navigation::NavMesh loaded;
  assert(loaded.loadSnapshot(*snapshot));
  karma::navigation::NavQuery loaded_query(loaded);
  karma::math::Vec3 loaded_nearest;
  assert(loaded_query.findNearestPoint({0.0f, 1.0f, 0.0f}, loaded_nearest));
}

void testBuildDebugDrawArtifacts() {
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  config.collect_build_debug_draw = true;

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry(), config));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::NavMeshEdges));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::NavMesh));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::Voxels));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::CompactRegions));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::Contours));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::PolyMeshDetail));
  assert(!nav_mesh.debugDrawLines(karma::navigation::NavMeshDebugDrawMode::Contours).empty());
  assert(karma::navigation::navMeshDebugDrawModeName(
             karma::navigation::NavMeshDebugDrawMode::WalkableVoxels)[0] != '\0');
}

}  // namespace karma::tests::navigation
