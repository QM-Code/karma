#include "demo_data.h"
#include "scene_helpers.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"
#include "karma/simulation/navigation/nav_geometry.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/transform.h"

namespace karma::demo {
namespace {

enum class PanelKind {
  Solo,
  Tile,
  TempObstacles,
  Crowd,
  Debug,
};

struct Panel {
  PanelKind kind = PanelKind::Solo;
  std::string name;
  ecs::Entity mesh_entity{};
  ecs::Entity nav_entity{};
  math::Vec3 mesh_offset{};
  uint32_t source_mask = 0;
  std::vector<QueryCase> path_queries;
  std::vector<QueryCase> ray_queries;
  std::vector<navigation::NavPath> debug_paths;
  std::vector<ecs::Entity> obstacles;
  std::vector<ecs::Entity> crowd_agents;
  math::Vec3 crowd_target_a{};
  math::Vec3 crowd_target_b{};
  float timer = 0.0f;
  bool initialized = false;
  bool tile_removed = false;
  bool crowd_target_b_active = false;
  bool temp_obstacle_enabled = true;
};

bool wantsPanel(std::string_view mode, PanelKind kind) {
  if (mode.empty() || mode == "all") {
    return true;
  }
  if (mode == "solo") {
    return kind == PanelKind::Solo;
  }
  if (mode == "tile") {
    return kind == PanelKind::Tile;
  }
  if (mode == "temp-obstacles") {
    return kind == PanelKind::TempObstacles;
  }
  if (mode == "crowd") {
    return kind == PanelKind::Crowd;
  }
  if (mode == "debug") {
    return kind == PanelKind::Debug;
  }
  return false;
}

navigation::NavQueryFilter filterFor(const QueryCase& query) {
  navigation::NavQueryFilter filter;
  filter.include_flags = query.include_flags;
  filter.exclude_flags = query.exclude_flags;
  return filter;
}

math::Vec3 localToWorld(const Panel& panel, const math::Vec3& point) {
  return offsetPoint(point, panel.mesh_offset);
}

math::Vec3 panelPoint(const Panel& panel, float x, float y, float z) {
  return localToWorld(panel, {x, y, z});
}

void registerPanelMaterial(content::AssetRegistry& assets,
                           const std::string& key,
                           const math::Color& tint) {
  renderer::MaterialDesc material{};
  material.base_color = tint;
  material.roughness = 0.72f;
  material.metallic = 0.0f;
  assets.registerMaterialAsset(key, material);
}

}  // namespace

class RecastNavigationGraphicalExample final : public app::GameInterface {
 public:
  explicit RecastNavigationGraphicalExample(std::string mode)
      : mode_(std::move(mode)) {}

  void onStart() override {
    bindInput();
    loadAssets();
    spawnLighting();
    spawnPanels();
    spawnCamera();
    spdlog::info(
        "Recast graphical navigation example: right mouse look, WASD move, Q/E vertical, Shift fast");
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    updateCamera(dt);
    updatePanels(dt);
    drawNavigation();
  }

  void onShutdown() override {}

 private:
  navigation::NavigationSystem* navigationSystem() const {
    return systems != nullptr ? systems->findSystem<navigation::NavigationSystem>() : nullptr;
  }

  void bindInput() {
    input->bindKey("forward", platform::Key::W);
    input->bindKey("back", platform::Key::S);
    input->bindKey("left", platform::Key::A);
    input->bindKey("right", platform::Key::D);
    input->bindKey("up", platform::Key::E);
    input->bindKey("down", platform::Key::Q);
    input->bindKey("fast", platform::Key::LeftShift);
    input->bindMouse("look", platform::MouseButton::Right);
  }

  void loadAssets() {
    nav_asset_ = loadMeshGeometry("nav_test.obj");
    dungeon_asset_ = loadMeshGeometry("dungeon.obj");
    undulating_asset_ = loadMeshGeometry("undulating.obj");
    nav_tests_ =
        loadTestCase(recastAssetPath("test_cases/nav_mesh_test.txt")).value_or(TestCaseFile{});
    ray_tests_ =
        loadTestCase(recastAssetPath("test_cases/raycast_test.txt")).value_or(TestCaseFile{});
  }

  uint32_t nextSourceMask() {
    const uint32_t mask = 1u << next_source_bit_;
    ++next_source_bit_;
    return mask;
  }

  Panel& createPanel(PanelKind kind,
                     std::string name,
                     const MeshGeometry& asset,
                     const math::Vec3& origin,
                     navigation::NavMeshBuildConfig config,
                     const math::Color& tint,
                     navigation::NavMeshDebugDrawMode debug_mode =
                         navigation::NavMeshDebugDrawMode::NavMeshEdges) {
    Panel panel;
    panel.kind = kind;
    panel.name = std::move(name);
    panel.source_mask = nextSourceMask();

    const Bounds bounds = computeBounds(asset.geometry);
    const math::Vec3 center = centerOf(bounds);
    panel.mesh_offset = {origin.x - center.x, origin.y - center.y, origin.z - center.z};

    const std::string mesh_key = asset.path.string();
    const std::string material_key = "recast/" + panel.name + "/mesh_tint";
    registerPanelMaterial(*assets, material_key, tint);
    panel.mesh_entity = helpers::spawnMeshAsset(*world, panel.name + " Mesh", mesh_key, panel.mesh_offset);
    world->get<components::MeshComponent>(panel.mesh_entity).materials = {
        components::MeshMaterialAssignment{.slot = 0, .material_key = material_key}};
    world->add(panel.mesh_entity,
               components::NavMeshSurfaceComponent{
                   .layer_mask = panel.source_mask,
                   .area = navigation::kNavAreaDefault,
                   .mesh_data = std::make_shared<geometry::MeshData>(combineMeshes(asset.meshes)),
                   .mesh_asset_key = mesh_key,
               });

    panel.nav_entity = world->createEntity();
    world->setName(panel.nav_entity, panel.name + " NavMesh");
    components::NavMeshComponent nav;
    nav.source_mask = panel.source_mask;
    nav.build_config = std::move(config);
    nav.debug_draw = true;
    nav.debug_draw_mode = debug_mode;
    world->add(panel.nav_entity, std::move(nav));

    panels_.push_back(std::move(panel));
    return panels_.back();
  }

  void spawnPanels() {
    if (wantsPanel(mode_, PanelKind::Solo)) {
      Panel& panel = createPanel(PanelKind::Solo,
                                 "Solo Mesh",
                                 nav_asset_,
                                 {-140.0f, 0.0f, 0.0f},
                                 recastBuildConfig(navigation::NavMeshBuildMode::Solo),
                                 {0.56f, 0.68f, 0.82f, 1.0f});
      panel.path_queries = nav_tests_.queries;
      addOffMeshAndVolume(panel);
    }

    if (wantsPanel(mode_, PanelKind::Tile)) {
      auto config = recastBuildConfig(navigation::NavMeshBuildMode::Tiled);
      config.partition_type = navigation::NavMeshPartitionType::Layers;
      Panel& panel = createPanel(PanelKind::Tile,
                                 "Tile Mesh",
                                 nav_asset_,
                                 {0.0f, 0.0f, 0.0f},
                                 config,
                                 {0.54f, 0.76f, 0.58f, 1.0f});
      panel.ray_queries = ray_tests_.queries;
    }

    if (wantsPanel(mode_, PanelKind::TempObstacles)) {
      auto config = recastBuildConfig(navigation::NavMeshBuildMode::Tiled);
      config.partition_type = navigation::NavMeshPartitionType::Layers;
      Panel& panel = createPanel(PanelKind::TempObstacles,
                                 "Temp Obstacles",
                                 dungeon_asset_,
                                 {140.0f, 0.0f, 0.0f},
                                 config,
                                 {0.72f, 0.62f, 0.52f, 1.0f});
      world->add(panel.nav_entity, components::NavTileCacheComponent{});
      addTempObstacles(panel);
    }

    if (wantsPanel(mode_, PanelKind::Crowd)) {
      auto config = recastBuildConfig(navigation::NavMeshBuildMode::Tiled);
      Panel& panel = createPanel(PanelKind::Crowd,
                                 "Crowd",
                                 nav_asset_,
                                 {-70.0f, 0.0f, 125.0f},
                                 config,
                                 {0.48f, 0.55f, 0.74f, 1.0f});
      components::NavCrowdComponent crowd;
      crowd.config.max_agents = 32;
      crowd.config.max_agent_radius = 0.8f;
      world->add(panel.nav_entity, std::move(crowd));
      panel.path_queries = nav_tests_.queries;
      addCrowdAgents(panel);
    }

    if (wantsPanel(mode_, PanelKind::Debug)) {
      if (mode_ == "debug") {
        addAllDebugDrawPanels();
      } else {
        addDebugPartitionPanel("Debug Watershed Regions",
                               {75.0f, 0.0f, 125.0f},
                               navigation::NavMeshPartitionType::Watershed,
                               navigation::NavMeshDebugDrawMode::CompactRegions,
                               {0.68f, 0.58f, 0.82f, 1.0f});
        addDebugPartitionPanel("Debug Monotone Contours",
                               {210.0f, 0.0f, 125.0f},
                               navigation::NavMeshPartitionType::Monotone,
                               navigation::NavMeshDebugDrawMode::Contours,
                               {0.74f, 0.56f, 0.62f, 1.0f});
        addDebugPartitionPanel("Debug Layers Detail",
                               {345.0f, 0.0f, 125.0f},
                               navigation::NavMeshPartitionType::Layers,
                               navigation::NavMeshDebugDrawMode::PolyMeshDetail,
                               {0.54f, 0.70f, 0.72f, 1.0f});
      }
    }
  }

  void addAllDebugDrawPanels() {
    struct DebugSpec {
      const char* name;
      navigation::NavMeshDebugDrawMode mode;
      navigation::NavMeshPartitionType partition;
      math::Color tint;
    };

    const DebugSpec specs[] = {
        {"Debug NavMesh",
         navigation::NavMeshDebugDrawMode::NavMesh,
         navigation::NavMeshPartitionType::Watershed,
         {0.58f, 0.68f, 0.78f, 1.0f}},
        {"Debug BVTree",
         navigation::NavMeshDebugDrawMode::NavMeshBVTree,
         navigation::NavMeshPartitionType::Watershed,
         {0.64f, 0.62f, 0.82f, 1.0f}},
        {"Debug Portals",
         navigation::NavMeshDebugDrawMode::NavMeshPortals,
         navigation::NavMeshPartitionType::Watershed,
         {0.48f, 0.72f, 0.74f, 1.0f}},
        {"Debug Voxels",
         navigation::NavMeshDebugDrawMode::Voxels,
         navigation::NavMeshPartitionType::Watershed,
         {0.76f, 0.58f, 0.52f, 1.0f}},
        {"Debug Walkable Voxels",
         navigation::NavMeshDebugDrawMode::WalkableVoxels,
         navigation::NavMeshPartitionType::Watershed,
         {0.60f, 0.76f, 0.54f, 1.0f}},
        {"Debug Compact",
         navigation::NavMeshDebugDrawMode::Compact,
         navigation::NavMeshPartitionType::Watershed,
         {0.52f, 0.66f, 0.82f, 1.0f}},
        {"Debug Compact Distance",
         navigation::NavMeshDebugDrawMode::CompactDistance,
         navigation::NavMeshPartitionType::Watershed,
         {0.70f, 0.64f, 0.48f, 1.0f}},
        {"Debug Compact Regions",
         navigation::NavMeshDebugDrawMode::CompactRegions,
         navigation::NavMeshPartitionType::Watershed,
         {0.68f, 0.58f, 0.82f, 1.0f}},
        {"Debug Region Connections",
         navigation::NavMeshDebugDrawMode::RegionConnections,
         navigation::NavMeshPartitionType::Watershed,
         {0.72f, 0.54f, 0.58f, 1.0f}},
        {"Debug Raw Contours",
         navigation::NavMeshDebugDrawMode::RawContours,
         navigation::NavMeshPartitionType::Monotone,
         {0.54f, 0.72f, 0.62f, 1.0f}},
        {"Debug Both Contours",
         navigation::NavMeshDebugDrawMode::BothContours,
         navigation::NavMeshPartitionType::Monotone,
         {0.70f, 0.56f, 0.70f, 1.0f}},
        {"Debug Contours",
         navigation::NavMeshDebugDrawMode::Contours,
         navigation::NavMeshPartitionType::Monotone,
         {0.74f, 0.56f, 0.62f, 1.0f}},
        {"Debug Poly Mesh",
         navigation::NavMeshDebugDrawMode::PolyMesh,
         navigation::NavMeshPartitionType::Layers,
         {0.56f, 0.70f, 0.78f, 1.0f}},
        {"Debug Poly Mesh Detail",
         navigation::NavMeshDebugDrawMode::PolyMeshDetail,
         navigation::NavMeshPartitionType::Layers,
         {0.54f, 0.70f, 0.72f, 1.0f}},
    };

    constexpr float start_x = -300.0f;
    constexpr float start_z = 120.0f;
    constexpr float spacing_x = 115.0f;
    constexpr float spacing_z = 92.0f;
    constexpr int columns = 4;
    for (int i = 0; i < static_cast<int>(sizeof(specs) / sizeof(specs[0])); ++i) {
      const float x = start_x + static_cast<float>(i % columns) * spacing_x;
      const float z = start_z + static_cast<float>(i / columns) * spacing_z;
      addDebugPartitionPanel(specs[i].name, {x, 0.0f, z}, specs[i].partition, specs[i].mode, specs[i].tint);
    }
  }

  void addDebugPartitionPanel(const std::string& name,
                              const math::Vec3& origin,
                              navigation::NavMeshPartitionType partition,
                              navigation::NavMeshDebugDrawMode debug_mode,
                              const math::Color& tint) {
    auto config = recastBuildConfig(navigation::NavMeshBuildMode::Solo);
    config.partition_type = partition;
    config.collect_build_debug_draw = true;
    createPanel(PanelKind::Debug, name, undulating_asset_, origin, config, tint, debug_mode);
  }

  void addOffMeshAndVolume(const Panel& panel) {
    if (nav_tests_.queries.empty()) {
      return;
    }

    const QueryCase& query = nav_tests_.queries.front();
    const ecs::Entity end = helpers::createDebugBoxMarker(*world,
                                                          graphics,
                                                          assets,
                                                          panel.name + " OffMesh End",
                                                          {0.95f, 0.90f, 0.20f, 1.0f},
                                                          localToWorld(panel, query.end),
                                                          {0.45f, 0.45f, 0.45f});
    const ecs::Entity start = helpers::createDebugBoxMarker(*world,
                                                            graphics,
                                                            assets,
                                                            panel.name + " OffMesh Start",
                                                            {0.15f, 0.75f, 0.95f, 1.0f},
                                                            localToWorld(panel, query.start),
                                                            {0.45f, 0.45f, 0.45f});
    world->add(start,
               components::NavOffMeshLinkComponent{
                   .layer_mask = panel.source_mask,
                   .end_entity = end,
                   .radius = 0.8f,
                   .area = navigation::kNavAreaDefault,
                   .flags = navigation::kNavPolyFlagWalk | navigation::kNavPolyFlagOffMesh,
                   .bidirectional = true,
                   .user_id = 1001,
               });

    const math::Vec3 mid = midpoint(query.start, query.end);
    const ecs::Entity volume = world->createEntity();
    world->setName(volume, panel.name + " Convex Volume");
    components::TransformComponent transform;
    transform.setPosition(panel.mesh_offset);
    world->add(volume, transform);
    world->add(volume,
               components::NavConvexVolumeComponent{
                   .layer_mask = panel.source_mask,
                   .vertices = {{mid.x - 4.0f, mid.y, mid.z - 4.0f},
                                {mid.x + 4.0f, mid.y, mid.z - 4.0f},
                                {mid.x + 4.0f, mid.y, mid.z + 4.0f},
                                {mid.x - 4.0f, mid.y, mid.z + 4.0f}},
                   .min_y = -2.0f,
                   .max_y = 5.0f,
                   .area = 2,
               });
  }

  void addTempObstacles(Panel& panel) {
    const math::Vec3 center = panelPoint(panel, 12.0f, 0.0f, -38.0f);
    panel.obstacles.push_back(createObstacle(panel,
                                             "Cylinder",
                                             center,
                                             navigation::NavTileCacheObstacleShape::Cylinder,
                                             {1.0f, 2.0f, 1.0f}));
    panel.obstacles.push_back(createObstacle(panel,
                                             "Box",
                                             {center.x + 4.0f, center.y + 0.8f, center.z + 3.0f},
                                             navigation::NavTileCacheObstacleShape::Box,
                                             {1.2f, 1.4f, 1.2f}));
    panel.obstacles.push_back(createObstacle(panel,
                                             "Oriented Box",
                                             {center.x - 4.0f, center.y + 0.8f, center.z + 2.0f},
                                             navigation::NavTileCacheObstacleShape::OrientedBox,
                                             {1.1f, 1.2f, 1.8f},
                                             0.65f));
  }

  ecs::Entity createObstacle(const Panel& panel,
                             std::string_view label,
                             const math::Vec3& position,
                             navigation::NavTileCacheObstacleShape shape,
                             const math::Vec3& half_extents,
                             float yaw = 0.0f) {
    const ecs::Entity entity = helpers::createDebugBoxMarker(*world,
                                                             graphics,
                                                             assets,
                                                             panel.name + " " + std::string(label),
                                                             {0.96f, 0.45f, 0.12f, 1.0f},
                                                             position,
                                                             {half_extents.x, half_extents.y, half_extents.z});
    components::NavTileCacheObstacleComponent obstacle;
    obstacle.nav_mesh_entity = panel.nav_entity;
    obstacle.shape = shape;
    obstacle.half_extents = half_extents;
    obstacle.bounds_min = {-half_extents.x, -half_extents.y, -half_extents.z};
    obstacle.bounds_max = {half_extents.x, half_extents.y, half_extents.z};
    obstacle.radius = std::max(half_extents.x, half_extents.z);
    obstacle.height = half_extents.y * 2.0f;
    obstacle.yaw_radians = yaw;
    world->add(entity, obstacle);
    return entity;
  }

  void addCrowdAgents(Panel& panel) {
    if (nav_tests_.queries.empty()) {
      return;
    }
    const QueryCase& query = nav_tests_.queries.front();
    panel.crowd_target_a = localToWorld(panel, query.start);
    panel.crowd_target_b = localToWorld(panel, query.end);

    for (int i = 0; i < 8; ++i) {
      const math::Vec3 offset{
          static_cast<float>(i % 4) * 0.9f,
          0.0f,
          static_cast<float>(i / 4) * 0.9f,
      };
      const math::Vec3 position = {
          panel.crowd_target_a.x + offset.x,
          panel.crowd_target_a.y,
          panel.crowd_target_a.z + offset.z,
      };
      const ecs::Entity entity = helpers::createDebugBoxMarker(*world,
                                                               graphics,
                                                               assets,
                                                               panel.name + " Agent",
                                                               {0.2f, 0.65f, 1.0f, 1.0f},
                                                               position,
                                                               {0.28f, 0.55f, 0.28f});
      components::NavCrowdAgentComponent agent;
      agent.crowd_entity = panel.nav_entity;
      agent.destination = panel.crowd_target_b;
      agent.has_destination = true;
      agent.destination_requested = true;
      agent.params.radius = 0.45f;
      agent.params.height = 1.8f;
      agent.params.max_speed = 3.0f + static_cast<float>(i % 3) * 0.2f;
      agent.params.update_flags = static_cast<uint8_t>(
          agent.params.update_flags | navigation::NavCrowdUpdateFlagSeparation);
      agent.height_offset = 0.0f;
      world->add(entity, agent);
      panel.crowd_agents.push_back(entity);
    }
  }

  void spawnCamera() {
    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {70.0f, 82.0f, 255.0f},
                                          math::fromYawPitch(camera_yaw_, camera_pitch_),
                                          components::CameraComponent{
                                              .near_clip = 0.05f,
                                              .far_clip = 900.0f,
                                              .is_primary = true,
                                          });
  }

  void spawnLighting() {
    helpers::spawnDirectionalLight(*world,
                                   "Recast Sun",
                                   {0.0f, 80.0f, 0.0f},
                                   math::fromYawPitch(0.45f, -0.95f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                       .intensity = 0.85f,
                                       .casts_shadows = true,
                                       .shadow_extent = 460.0f,
                                   });
    helpers::spawnEnvironment(*world, assets,
                              "Environment",
                              registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
                              0.35f,
                              true);
  }

  void updateCamera(float dt) {
    if (!world->isAlive(camera_entity_)) {
      return;
    }
    if (input->actionDown("look")) {
      camera_yaw_ -= input->mouseDeltaX() * 0.003f;
      camera_pitch_ =
          std::clamp(camera_pitch_ - input->mouseDeltaY() * 0.003f, -1.35f, -0.08f);
    }

    auto& transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward = math::rotateVec(rotation, {0.0f, 0.0f, -1.0f});
    const math::Vec3 right = math::rotateVec(rotation, {1.0f, 0.0f, 0.0f});
    math::Vec3 move{};
    if (input->actionDown("forward")) {
      move = math::add(move, forward);
    }
    if (input->actionDown("back")) {
      move = math::subtract(move, forward);
    }
    if (input->actionDown("right")) {
      move = math::add(move, right);
    }
    if (input->actionDown("left")) {
      move = math::subtract(move, right);
    }
    if (input->actionDown("up")) {
      move.y += 1.0f;
    }
    if (input->actionDown("down")) {
      move.y -= 1.0f;
    }

    if (math::lengthSquared(move) > 0.0001f) {
      const float speed = input->actionDown("fast") ? 95.0f : 38.0f;
      transform.setPosition(
          math::add(transform.getPosition(), math::scale(math::normalize(move), speed * dt)));
    }
    transform.setRotation(rotation);
  }

  void updatePanels(float dt) {
    for (Panel& panel : panels_) {
      if (!world->isAlive(panel.nav_entity) ||
          !world->has<components::NavMeshComponent>(panel.nav_entity)) {
        continue;
      }

      auto& nav = world->get<components::NavMeshComponent>(panel.nav_entity);
      if (!nav.built || !nav.nav_mesh.isValid()) {
        continue;
      }

      if (!panel.initialized) {
        initializePanelAfterBuild(panel, nav);
      }

      panel.timer += dt;
      if (panel.kind == PanelKind::Tile) {
        updateTileEdit(panel, nav);
      } else if (panel.kind == PanelKind::TempObstacles) {
        updateTempObstacleToggle(panel);
      } else if (panel.kind == PanelKind::Crowd) {
        updateCrowdTargets(panel);
      }
    }
  }

  void initializePanelAfterBuild(Panel& panel, const components::NavMeshComponent& nav) {
    panel.initialized = true;
    spdlog::info("{} baked {} polygons from {} triangles",
                 panel.name,
                 nav.last_build_result.polygon_count,
                 nav.last_build_result.triangle_count);

    if (panel.kind == PanelKind::Solo || panel.kind == PanelKind::Crowd) {
      navigation::NavQuery query(nav.nav_mesh);
      int count = 0;
      for (const QueryCase& path_query : panel.path_queries) {
        if (path_query.kind != "pf" || count >= 4) {
          continue;
        }
        const navigation::NavPath path =
            query.findSmoothPath(localToWorld(panel, path_query.start),
                                 localToWorld(panel, path_query.end),
                                 {2.0f, 4.0f, 2.0f},
                                 {},
                                 1024,
                                 filterFor(path_query));
        if (path.success() && !path.points.empty()) {
          panel.debug_paths.push_back(path);
          ++count;
        }
      }
    } else if (panel.kind == PanelKind::Tile) {
      navigation::NavQuery query(nav.nav_mesh);
      for (const QueryCase& ray_query : panel.ray_queries) {
        if (ray_query.kind != "rc") {
          continue;
        }
        const navigation::NavPath ray =
            query.raycast(localToWorld(panel, ray_query.start),
                          localToWorld(panel, ray_query.end),
                          {2.0f, 4.0f, 2.0f},
                          256,
                          filterFor(ray_query));
        if (ray.success() && !ray.points.empty()) {
          panel.debug_paths.push_back(ray);
        }
      }
    }
  }

  void updateTileEdit(Panel& panel, components::NavMeshComponent& nav) {
    if (panel.ray_queries.empty() || panel.timer < 4.0f) {
      return;
    }
    panel.timer = 0.0f;

    const math::Vec3 tile_position = localToWorld(panel, panel.ray_queries.front().start);
    if (panel.tile_removed) {
      const navigation::NavMeshInputGeometry geometry =
          navigation::collectNavMeshGeometry(*world, panel.source_mask);
      navigation::NavMeshBuildResult result;
      if (nav.nav_mesh.rebuildTile(geometry, tile_position, &result)) {
        panel.tile_removed = false;
        ++nav.build_version;
        spdlog::info("{} rebuilt one tile", panel.name);
      }
    } else if (nav.nav_mesh.removeTile(tile_position)) {
      panel.tile_removed = true;
      ++nav.build_version;
      spdlog::info("{} removed one tile", panel.name);
    }
  }

  void updateTempObstacleToggle(Panel& panel) {
    if (panel.obstacles.empty() || panel.timer < 5.0f) {
      return;
    }
    panel.timer = 0.0f;
    panel.temp_obstacle_enabled = !panel.temp_obstacle_enabled;
    const ecs::Entity obstacle_entity = panel.obstacles.front();
    if (!world->isAlive(obstacle_entity) ||
        !world->has<components::NavTileCacheObstacleComponent>(obstacle_entity) ||
        !world->has<components::MeshComponent>(obstacle_entity)) {
      return;
    }

    auto& obstacle = world->get<components::NavTileCacheObstacleComponent>(obstacle_entity);
    auto& mesh = world->get<components::MeshComponent>(obstacle_entity);
    obstacle.enabled = panel.temp_obstacle_enabled;
    obstacle.remove_requested = !panel.temp_obstacle_enabled;
    obstacle.dirty = panel.temp_obstacle_enabled;
    mesh.visible = panel.temp_obstacle_enabled;
    mesh.shadow_visible = panel.temp_obstacle_enabled;
  }

  void updateCrowdTargets(Panel& panel) {
    if (panel.crowd_agents.empty() || panel.timer < 7.0f) {
      return;
    }
    panel.timer = 0.0f;
    panel.crowd_target_b_active = !panel.crowd_target_b_active;
    const math::Vec3 target =
        panel.crowd_target_b_active ? panel.crowd_target_b : panel.crowd_target_a;
    for (const ecs::Entity agent : panel.crowd_agents) {
      navigation::NavigationSystem::requestCrowdMoveTo(*world, agent, target);
    }
  }

  void drawNavigation() {
    if (graphics == nullptr) {
      return;
    }
    if (auto* nav_system = navigationSystem()) {
      nav_system->debugDraw(*world, *graphics, false);
    }

    for (const Panel& panel : panels_) {
      const math::Color path_color = panel.kind == PanelKind::Tile
          ? math::Color{0.96f, 0.90f, 0.20f, 1.0f}
          : math::Color{0.18f, 0.95f, 0.90f, 1.0f};
      for (const navigation::NavPath& path : panel.debug_paths) {
        navigation::NavQuery::debugDrawPath(*graphics, path, path_color, false);
      }
    }
  }

  std::string mode_;
  MeshGeometry nav_asset_;
  MeshGeometry dungeon_asset_;
  MeshGeometry undulating_asset_;
  TestCaseFile nav_tests_;
  TestCaseFile ray_tests_;
  std::vector<Panel> panels_;
  ecs::Entity camera_entity_{};
  int next_source_bit_ = 0;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = -0.55f;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "all";

  karma::app::EngineApp engine;
  karma::demo::RecastNavigationGraphicalExample game(mode);

  karma::app::EngineConfig config;
  config.window.title = "Karma Recast Navigation Graphical Examples";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.lighting_exposure = 1.05f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return EXIT_SUCCESS;
}
