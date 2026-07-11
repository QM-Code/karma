#include "navmesh_test_utils.h"

#include "karma/assets.h"

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
  karma::world::World world;
  const karma::world::Entity surface = world.createEntity();
  karma::components::TransformComponent transform{};
  transform.setPosition({10.0f, 2.0f, -3.0f});
  transform.setScale({2.0f, 1.0f, 2.0f});
  world.add(surface, transform);
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data =
                             std::make_shared<karma::world::MeshData>(makePlaneMesh(1.0f)),
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
  karma::world::World world;
  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .area = 2,
                         .mesh_data = std::make_shared<karma::world::MeshData>(makePlaneMesh()),
                     });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world);
  assert(geometry.triangleCount() == 2);
  assert(geometry.triangle_areas.size() == 2);
  assert(geometry.triangle_areas[0] == 2);
  assert(geometry.triangle_areas[1] == 2);
}

void testWorldSurfaceCollectionResolvesMeshAssetKey() {
  karma::assets::AssetRegistry assets;
  assets.registerMeshAsset("runtime/nav/plane", makePlaneMesh());

  karma::world::World world;
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

void testWorldSurfaceCollectionHonorsLayerMasks() {
  constexpr std::uint32_t ground_layer = 1u << 0u;
  constexpr std::uint32_t landing_layer = 1u << 1u;

  karma::world::World world;
  const auto ground_surface = world.createEntity();
  world.add(ground_surface, karma::components::TransformComponent{});
  world.add(ground_surface, karma::components::NavMeshSurfaceComponent{
                                .layer_mask = ground_layer | landing_layer,
                                .mesh_data =
                                    std::make_shared<karma::world::MeshData>(
                                        makePlaneMesh(1.0f)),
                            });

  const auto landing_surface = world.createEntity();
  karma::components::TransformComponent landing_transform{};
  landing_transform.setPosition({0.0f, 2.0f, 0.0f});
  world.add(landing_surface, landing_transform);
  world.add(landing_surface, karma::components::NavMeshSurfaceComponent{
                                 .layer_mask = landing_layer,
                                 .mesh_data =
                                     std::make_shared<karma::world::MeshData>(
                                         makePlaneMesh(1.0f)),
                             });

  const karma::navigation::NavMeshInputGeometry ground_geometry =
      karma::navigation::collectNavMeshGeometry(world, nullptr, ground_layer);
  assert(ground_geometry.triangleCount() == 2);
  for (const karma::math::Vec3& vertex : ground_geometry.vertices) {
    assert(std::abs(vertex.y) < 0.001f);
  }

  const karma::navigation::NavMeshInputGeometry landing_geometry =
      karma::navigation::collectNavMeshGeometry(world, nullptr, landing_layer);
  assert(landing_geometry.triangleCount() == 4);

  bool found_landing_height = false;
  for (const karma::math::Vec3& vertex : landing_geometry.vertices) {
    found_landing_height =
        found_landing_height || std::abs(vertex.y - 2.0f) < 0.001f;
  }
  assert(found_landing_height);
}

void testInstancedWorldSurfaceCollectionUsesActiveLayoutAndStaticFlags() {
  const auto nav_static = [] {
    karma::components::StaticComponent membership{};
    membership.enabled = true;
    membership.include_descendants = true;
    membership.flags = karma::components::StaticComponentNavigation;
    return membership;
  };
  const auto contains_vertex = [](const karma::navigation::NavMeshInputGeometry& geometry,
                                  float x,
                                  float y,
                                  float z) {
    for (const karma::math::Vec3& vertex : geometry.vertices) {
      if (std::abs(vertex.x - x) < 0.001f &&
          std::abs(vertex.y - y) < 0.001f &&
          std::abs(vertex.z - z) < 0.001f) {
        return true;
      }
    }
    return false;
  };

  karma::world::World world;

  const auto matrix_surface = world.createEntity();
  karma::components::TransformComponent matrix_transform{};
  matrix_transform.setPosition({1.0f, 0.0f, 2.0f});
  world.add(matrix_surface, matrix_transform);
  world.add(matrix_surface, nav_static());
  karma::components::InstancedMeshComponent matrix_instances{};
  matrix_instances.gpu_layout =
      karma::rendering::InstanceGpuLayout::Matrix4x4Params;
  matrix_instances.instances = {
      karma::components::MeshInstance{
          .position = {10.0f, 2.0f, -3.0f},
          .scale = {2.0f, 1.0f, 3.0f},
      },
      karma::components::MeshInstance{
          .position = {-5.0f, 1.0f, 4.0f},
      },
  };
  matrix_instances.planar_instances = {
      karma::components::PlanarMeshInstance{
          .position = {400.0f, 400.0f, 400.0f},
      },
  };
  world.add(matrix_surface, std::move(matrix_instances));
  world.add(matrix_surface,
            karma::components::NavMeshSurfaceComponent{
                .area = 2,
                .mesh_data = std::make_shared<karma::world::MeshData>(
                    makePlaneMesh(1.0f)),
            });

  const auto planar_surface = world.createEntity();
  karma::components::TransformComponent planar_transform{};
  planar_transform.setPosition({2.0f, 1.0f, -1.0f});
  world.add(planar_surface, planar_transform);
  world.add(planar_surface, nav_static());
  karma::components::InstancedMeshComponent planar_instances{};
  planar_instances.gpu_layout =
      karma::rendering::InstanceGpuLayout::PositionYawScaleParams;
  planar_instances.instances = {
      karma::components::MeshInstance{
          .position = {-400.0f, -400.0f, -400.0f},
      },
  };
  planar_instances.planar_instances = {
      karma::components::PlanarMeshInstance{
          .position = {20.0f, 4.0f, 10.0f},
          .yaw_radians = 1.57079632679f,
          .scale = {1.0f, 1.0f, 2.0f},
      },
  };
  world.add(planar_surface, std::move(planar_instances));
  world.add(planar_surface,
            karma::components::NavMeshSurfaceComponent{
                .area = 3,
                .mesh_data = std::make_shared<karma::world::MeshData>(
                    makePlaneMesh(1.0f)),
            });

  const auto disabled_surface = world.createEntity();
  world.add(disabled_surface, karma::components::TransformComponent{});
  karma::components::StaticComponent disabled_membership = nav_static();
  disabled_membership.flags = karma::components::StaticComponentLighting;
  world.add(disabled_surface, disabled_membership);
  karma::components::InstancedMeshComponent disabled_instances{};
  disabled_instances.instances.push_back(karma::components::MeshInstance{});
  world.add(disabled_surface, std::move(disabled_instances));
  world.add(disabled_surface,
            karma::components::NavMeshSurfaceComponent{
                .area = 4,
                .mesh_data = std::make_shared<karma::world::MeshData>(
                    makePlaneMesh(1.0f)),
            });

  // Explicit surfaces retain precedence over the legacy mesh-collider path.
  const auto legacy_fallback = world.createEntity();
  world.add(legacy_fallback, karma::components::TransformComponent{});
  world.add(legacy_fallback,
            karma::components::ColliderComponent::mesh(
                karma::components::MeshColliderShape{
                    .vertices = {
                        {-10.0f, 0.0f, -10.0f},
                        {10.0f, 0.0f, -10.0f},
                        {10.0f, 0.0f, 10.0f},
                        {-10.0f, 0.0f, 10.0f},
                    },
                    .indices = {0u, 2u, 1u, 0u, 3u, 2u},
                }));
  world.add(legacy_fallback, karma::components::MeshComponent{});

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world);
  assert(geometry.vertices.size() == 12u);
  assert(geometry.triangleCount() == 6u);
  assert(contains_vertex(geometry, 9.0f, 2.0f, -4.0f));
  assert(contains_vertex(geometry, -5.0f, 1.0f, 5.0f));
  assert(contains_vertex(geometry, 20.0f, 5.0f, 10.0f));

  size_t matrix_area_count = 0u;
  size_t planar_area_count = 0u;
  for (const unsigned char area : geometry.triangle_areas) {
    matrix_area_count += area == 2 ? 1u : 0u;
    planar_area_count += area == 3 ? 1u : 0u;
  }
  assert(matrix_area_count == 4u);
  assert(planar_area_count == 2u);
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
