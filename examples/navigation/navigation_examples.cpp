#include "navigation_examples.h"
#include "navigation_example_scene.h"

#include "demo_asset_paths.h"
#include "scene_helpers.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "karma/content/navigation/nav_tile_cache.h"
#include "karma/features/ui/imgui/imgui_layer.h"
#include "karma/karma.h"
#include "karma/rendering/renderer/camera_picking.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/character_controller.h"
#include "karma/world/components/transform.h"

namespace karma::demo::navigation_examples {
namespace {

class NavigationExampleApp final : public app::GameInterface {
 public:
  explicit NavigationExampleApp(ExampleKind kind) : kind_(kind) {}

  void onStart() override {
    input->bindMouse("primary", platform::MouseButton::Left, input::Trigger::Pressed);
    input->bindMouse("secondary", platform::MouseButton::Right, input::Trigger::Pressed);
    input->bindKey("rebuild", platform::Key::R, input::Trigger::Pressed);
    input->bindKey("reset", platform::Key::C, input::Trigger::Pressed);
    build_config_ = defaultBuildConfig(kind_);
    setupScene();
    rebuild();
  }

  void onFixedUpdate(float dt) override { (void)dt; }
  void onShutdown() override {}

  void onUpdate(float dt) override {
    if (input->actionPressed("rebuild")) {
      rebuild_requested_ = true;
    }
    if (input->actionPressed("reset")) {
      resetScenario();
    }
    if (rebuild_requested_) {
      rebuild();
    }
    handleClicks();
    updateScenario(dt);
    drawDebug();
  }

  void drawUi(app::UIContext&) {
    ImGui::SetNextWindowSize(ImVec2(390.0f, 620.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(exampleName(kind_));
    ImGui::Text("Status: %s", status_.c_str());
    ImGui::Separator();
    drawBuildUi();
    ImGui::Separator();
    switch (kind_) {
      case ExampleKind::PointClick: drawPointClickUi(); break;
      case ExampleKind::Crowds: drawCrowdUi(); break;
      case ExampleKind::TileCache: drawTileCacheUi(); break;
      case ExampleKind::QueryLab: drawQueryUi(); break;
      case ExampleKind::OffMeshAreas: drawOffMeshUi(); break;
      case ExampleKind::PhysicsBridge: drawPhysicsBridgeUi(); break;
    }
    ImGui::Separator();
    ImGui::Checkbox("Draw navmesh", &draw_navmesh_);
    ImGui::Checkbox("Draw path", &draw_path_);
    ImGui::Checkbox("Draw query data", &draw_query_);
    ImGui::Checkbox("Draw build layer", &draw_build_layer_);
    ImGui::Combo("Build layer", &debug_layer_index_,
                 "Edges\0Navmesh\0BVTree\0Portals\0Voxels\0Compact\0Contours\0Detail\0");
    ImGui::End();
  }

 private:
  navigation::NavigationSystem* navigationSystem() const {
    return systems != nullptr ? systems->findSystem<navigation::NavigationSystem>() : nullptr;
  }

  void setupScene() {
    surface_ = kind_ == ExampleKind::QueryLab ? makeRingSurface()
        : kind_ == ExampleKind::OffMeshAreas ? makeOffMeshSurface()
        : makeOpenSurface();
    bounds_ = computeBounds(surface_.geometry);

    const std::string mesh_key = std::string("runtime/navigation/") + exampleName(kind_) + "/surface";
    const std::string material_key = std::string("runtime/navigation/") + exampleName(kind_) + "/surface_material";
    if (graphics != nullptr) {
      assets->registerMeshAsset(mesh_key, surface_.mesh);
    }
    if (assets != nullptr) {
      renderer::MaterialDesc material;
      material.base_color = {0.42f, 0.44f, 0.40f, 1.0f};
      material.roughness = 0.85f;
      assets->registerMaterialAsset(material_key, material);
    }
    surface_entity_ = helpers::spawnMesh(*world, "Navigation Surface", mesh_key, material_key, {}, true);
    world->add(surface_entity_, components::NavMeshSurfaceComponent{
                                    .area = navigation::kNavAreaDefault,
                                    .mesh_data = std::make_shared<geometry::MeshData>(surface_.mesh),
                                });

    spawnCameraAndLights();
    start_ = {bounds_.min.x + 1.0f, 0.1f, bounds_.min.z + 1.0f};
    end_ = {bounds_.max.x - 1.0f, 0.1f, bounds_.max.z - 1.0f};
    target_ = end_;

    start_marker_ = helpers::createDebugBoxMarker(*world, graphics, assets, "Start",
                                                  {0.2f, 0.8f, 1.0f, 1.0f},
                                                  start_, {0.15f, 0.15f, 0.15f}, true);
    end_marker_ = helpers::createDebugBoxMarker(*world, graphics, assets, "End",
                                                {1.0f, 0.8f, 0.15f, 1.0f},
                                                end_, {0.15f, 0.15f, 0.15f}, true);
  }

  void spawnCameraAndLights() {
    const math::Vec3 center = midpoint(bounds_.min, bounds_.max);
    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {center.x, 13.0f, center.z + 15.0f},
                                          math::fromYawPitch(0.0f, -0.7f),
                                          components::CameraComponent{
                                              .near_clip = 0.05f,
                                              .far_clip = 220.0f,
                                              .is_primary = true,
                                          });
    helpers::spawnDirectionalLight(*world,
                                   "Sun",
                                   {0.0f, 30.0f, 0.0f},
                                   math::fromYawPitch(0.4f, -0.85f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .intensity = 0.9f,
                                       .casts_shadows = true,
                                       .shadow_extent = 35.0f,
                                   });
    helpers::spawnPointLight(*world,
                             "Warm Fill",
                             {-5.0f, 4.0f, -5.0f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.7f, 0.45f, 1.0f},
                                 .intensity = 12.0f,
                                 .range = 20.0f,
                             });
    helpers::spawnEnvironment(*world, assets,
                              "Environment",
                              registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
                              0.25f,
                              true);
  }

  bool screenToGround(math::Vec3& out) const {
    if (graphics == nullptr || !world->isAlive(camera_entity_)) {
      return false;
    }
    double x = 0.0;
    double y = 0.0;
    if (!input->mousePosition(x, y)) {
      return false;
    }
    int width = 0;
    int height = 0;
    graphics->getFramebufferSize(width, height);
    const auto& transform = world->get<components::TransformComponent>(camera_entity_);
    const auto& camera = world->get<components::CameraComponent>(camera_entity_);
    renderer::ScreenRay ray;
    if (!renderer::screenPointToWorldRay(x,
                                         y,
                                         width,
                                         height,
                                         transform.getPosition(),
                                         transform.getRotation(),
                                         camera.fov_y_degrees,
                                         ray) ||
        std::abs(ray.direction.y) < 0.0001f) {
      return false;
    }
    const float t = (kGroundY - ray.origin.y) / ray.direction.y;
    if (t < 0.0f) {
      return false;
    }
    out = math::add(ray.origin, math::scale(ray.direction, t));
    return true;
  }

  void rebuild() {
    rebuild_requested_ = false;
    path_ = {};
    smooth_path_ = {};
    ray_ = {};
    polys_ = {};
    walls_ = {};
    closest_ = {};
    tile_state_ = {};
    if (kind_ == ExampleKind::PointClick || kind_ == ExampleKind::PhysicsBridge) {
      rebuildEcsScenario();
      return;
    }
    if (kind_ == ExampleKind::TileCache) {
      navigation::NavTileCacheBuildConfig cache_config;
      cache_config.compression = compression_index_ == 0
          ? navigation::NavTileCacheCompression::FastLz
          : navigation::NavTileCacheCompression::None;
      navigation::NavTileCacheBuildResult result;
      const bool ok = tile_cache_.build(nav_mesh_, surface_.geometry, build_config_, cache_config, &result);
      status_ = ok ? "tile cache built" : result.message;
      return;
    }
    navigation::NavMeshBuildResult result;
    const bool ok = build_config_.build_mode == navigation::NavMeshBuildMode::Tiled
        ? nav_mesh_.buildTiled(surface_.geometry, build_config_, &result)
        : nav_mesh_.build(surface_.geometry, build_config_, &result);
    status_ = ok ? "navmesh built" : result.message;
    if (ok && kind_ == ExampleKind::Crowds) {
      rebuildCrowd();
    }
    if (ok && kind_ == ExampleKind::QueryLab) {
      runQuerySuite();
    }
    if (ok && kind_ == ExampleKind::OffMeshAreas) {
      runOffMeshQuery();
    }
  }

  void rebuildEcsScenario() {
    if (!nav_mesh_entity_.isValid()) {
      nav_mesh_entity_ = world->createEntity();
      world->setName(nav_mesh_entity_, "Navigation Mesh");
      world->add(nav_mesh_entity_, components::NavMeshComponent{});
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_mesh_entity_);
    nav.build_config = build_config_;
    nav.rebuild_requested = true;
    nav.built = false;

    if (kind_ == ExampleKind::PointClick) {
      if (!actor_entity_.isValid()) {
        actor_entity_ = helpers::spawnMeshAsset(*world,
                                                "Click Agent",
                                                importExampleMeshAsset(assets, "tank_final.glb"),
                                                start_);
        components::NavMeshAgentComponent agent;
        agent.nav_mesh_entity = nav_mesh_entity_;
        agent.speed = point_speed_;
        agent.stopping_distance = 0.25f;
        agent.search_extents = {3.0f, 6.0f, 3.0f};
        world->add(actor_entity_, agent);
      }
    } else {
      if (!nav_crowd_added_) {
        components::NavCrowdComponent crowd;
        crowd.config.max_agents = 8;
        crowd.config.max_agent_radius = 0.5f;
        crowd.debug_request.enabled = true;
        world->add(nav_mesh_entity_, std::move(crowd));
        nav_crowd_added_ = true;
      }
      if (!actor_entity_.isValid()) {
        actor_entity_ = helpers::createDebugBoxMarker(*world,
                                                      graphics,
                                                      assets,
                                                      "Physics Bridge Agent",
                                                      {0.25f, 0.9f, 0.55f, 1.0f},
                                                      start_,
                                                      {0.28f, 0.5f, 0.28f},
                                                      true);
        world->add(actor_entity_, components::ColliderComponent::box());
        world->add(actor_entity_, components::CharacterControllerComponent{});
        components::NavCrowdAgentComponent agent;
        agent.crowd_entity = nav_mesh_entity_;
        agent.params.radius = 0.28f;
        agent.params.height = 1.0f;
        agent.params.max_speed = crowd_speed_;
        agent.params.max_acceleration = 12.0f;
        agent.movement_mode = bridge_velocity_mode_
            ? components::NavCrowdMovementMode::CharacterControllerVelocity
            : components::NavCrowdMovementMode::Transform;
        world->add(actor_entity_, agent);
      }
    }
    status_ = "ECS rebuild requested";
  }

  void rebuildCrowd() {
    crowd_ = navigation::NavCrowd{};
    navigation::NavCrowdConfig config;
    config.max_agents = std::max(1, crowd_count_);
    config.max_agent_radius = 0.5f;
    navigation::NavCrowdBuildResult result;
    if (!crowd_.init(nav_mesh_, config, &result)) {
      status_ = result.message;
      return;
    }
    agent_visuals_.clear();
    for (int i = 0; i < crowd_count_; ++i) {
      const float angle = (static_cast<float>(i) / static_cast<float>(crowd_count_)) * 6.2831853f;
      const math::Vec3 position{std::cos(angle) * 5.0f, 0.15f, std::sin(angle) * 5.0f};
      navigation::NavCrowdAgentParams params;
      params.radius = 0.28f;
      params.height = 1.0f;
      params.max_speed = crowd_speed_;
      params.max_acceleration = 10.0f;
      params.update_flags = crowd_update_flags_;
      const int id = crowd_.addAgent(position, params);
      if (id < 0) {
        continue;
      }
      const ecs::Entity marker = helpers::createDebugBoxMarker(*world,
                                                               graphics,
                                                               assets,
                                                               "Crowd Agent",
                                                               {0.25f, 0.55f, 1.0f, 1.0f},
                                                               position,
                                                               {0.18f, 0.45f, 0.18f},
                                                               true);
      agent_visuals_.push_back({id, marker});
      crowd_.requestMoveTarget(id, target_);
    }
    status_ = "crowd initialized";
  }

  void resetScenario() {
    if (kind_ == ExampleKind::Crowds) {
      rebuildCrowd();
    } else if (kind_ == ExampleKind::TileCache) {
      tile_cache_.clearObstacles();
    } else if (kind_ == ExampleKind::PhysicsBridge && actor_entity_.isValid()) {
      world->get<components::TransformComponent>(actor_entity_).setPosition(start_);
    }
  }

  void handleClicks() {
    if (imguiCapturesMouse()) {
      return;
    }
    math::Vec3 hit;
    if (input->actionPressed("secondary") && screenToGround(hit)) {
      start_ = {hit.x, 0.1f, hit.z};
      if (world->isAlive(start_marker_)) {
        world->get<components::TransformComponent>(start_marker_).setPosition(start_);
      }
      runActiveQueryAfterEndpointChange();
    }
    if (!input->actionPressed("primary") || !screenToGround(hit)) {
      return;
    }
    end_ = {hit.x, 0.1f, hit.z};
    target_ = end_;
    if (world->isAlive(end_marker_)) {
      world->get<components::TransformComponent>(end_marker_).setPosition(end_);
    }
    switch (kind_) {
      case ExampleKind::PointClick:
        if (actor_entity_.isValid()) {
          navigation::NavigationSystem::requestMoveTo(*world, actor_entity_, end_);
        }
        break;
      case ExampleKind::Crowds:
        for (const AgentVisual& agent : agent_visuals_) {
          crowd_.requestMoveTarget(agent.id, end_);
        }
        break;
      case ExampleKind::TileCache:
        addObstacleAt(end_);
        break;
      case ExampleKind::QueryLab:
        runQuerySuite();
        break;
      case ExampleKind::OffMeshAreas:
        runOffMeshQuery();
        break;
      case ExampleKind::PhysicsBridge:
        if (actor_entity_.isValid()) {
          navigation::NavigationSystem::requestCrowdMoveTo(*world, actor_entity_, end_);
        }
        break;
    }
  }

  void runActiveQueryAfterEndpointChange() {
    if (kind_ == ExampleKind::QueryLab) {
      runQuerySuite();
    } else if (kind_ == ExampleKind::OffMeshAreas) {
      runOffMeshQuery();
    }
  }

  void updateScenario(float dt) {
    if (kind_ == ExampleKind::Crowds && crowd_.isValid()) {
      crowd_.update(dt);
      crowd_debug_ = crowd_.debugSnapshot(crowd_debug_request_);
      for (const AgentVisual& visual : agent_visuals_) {
        navigation::NavCrowdAgentInfo info;
        if (crowd_.agentInfo(visual.id, info) && world->isAlive(visual.marker)) {
          world->get<components::TransformComponent>(visual.marker).setPosition(info.position);
        }
      }
    }
    if (kind_ == ExampleKind::TileCache && tile_cache_.isValid()) {
      bool up_to_date = false;
      tile_cache_.update(dt, nav_mesh_, &up_to_date);
      (void)up_to_date;
    }
    if (kind_ == ExampleKind::PointClick && actor_entity_.isValid() &&
        world->has<components::NavMeshAgentComponent>(actor_entity_)) {
      auto& agent = world->get<components::NavMeshAgentComponent>(actor_entity_);
      agent.speed = point_speed_;
    }
    if (kind_ == ExampleKind::PhysicsBridge && actor_entity_.isValid() &&
        world->has<components::NavCrowdAgentComponent>(actor_entity_)) {
      auto& agent = world->get<components::NavCrowdAgentComponent>(actor_entity_);
      agent.params.max_speed = crowd_speed_;
      agent.params_dirty = true;
      agent.movement_mode = bridge_velocity_mode_
          ? components::NavCrowdMovementMode::CharacterControllerVelocity
          : components::NavCrowdMovementMode::Transform;
    }
  }

  navigation::NavQueryFilter currentFilter() const {
    navigation::NavQueryFilter filter;
    filter.include_flags = include_flags_;
    filter.exclude_flags = exclude_flags_;
    filter.setAreaCost(kAreaWater, water_cost_);
    filter.setAreaCost(kAreaDoor, door_cost_);
    return filter;
  }

  void runQuerySuite() {
    if (!nav_mesh_.isValid()) {
      return;
    }
    navigation::NavQuery query(nav_mesh_);
    path_ = query.findPath(start_, end_, {3.0f, 6.0f, 3.0f}, 256, currentFilter());
    smooth_path_ = query.findSmoothPath(start_, end_, {3.0f, 6.0f, 3.0f}, {}, 1024, currentFilter());
    ray_ = query.raycastDetailed(start_, end_, {3.0f, 6.0f, 3.0f}, max_query_polys_, currentFilter());
    polys_ = query.queryPolygons(midpoint(start_, end_), query_half_extents_, max_query_polys_, currentFilter());
    circle_polys_ = query.findPolysAroundCircle(start_, query_radius_, {3.0f, 6.0f, 3.0f}, max_query_polys_, currentFilter());
    local_polys_ = query.findLocalNeighbourhood(start_, query_radius_, {3.0f, 6.0f, 3.0f}, max_query_polys_, currentFilter());
    walls_ = query.getPolyWallSegments(start_, {3.0f, 6.0f, 3.0f}, max_query_polys_, currentFilter());
    uint64_t nearest = 0;
    if (query.findNearestPoly(start_, nearest, nullptr, {3.0f, 6.0f, 3.0f}, currentFilter())) {
      closest_ = query.closestPointOnPoly(nearest, {start_.x, start_.y + 3.0f, start_.z});
      boundary_ = query.closestPointOnPolyBoundary(nearest, end_);
      closed_list_contains_start_ = query.isInClosedList(nearest);
    }
    dijkstra_path_ = {};
    if (!circle_polys_.polys.empty()) {
      dijkstra_path_ = query.pathFromDijkstraSearch(circle_polys_.polys.back(), max_query_polys_);
    }
    sliced_path_ = {};
    if (query.beginSlicedPath(start_, end_, {3.0f, 6.0f, 3.0f}, currentFilter()) != navigation::NavStatus::QueryFailed) {
      bool done = false;
      for (int i = 0; i < 64 && !done; ++i) {
        query.updateSlicedPath(4, done);
      }
      sliced_path_ = query.finalizeSlicedPath();
    }
    status_ = "query suite updated";
  }

  void runOffMeshQuery() {
    if (!nav_mesh_.isValid()) {
      return;
    }
    navigation::NavQuery query(nav_mesh_);
    path_ = query.findPath(start_, end_, {3.0f, 6.0f, 3.0f}, 256, currentFilter());
    polys_ = query.queryPolygons({0.0f, 0.1f, 0.0f}, {3.0f, 3.0f, 5.0f}, 128, currentFilter());
    offmesh_endpoints_ = {};
    for (uint64_t ref : polys_.polys) {
      uint16_t flags = 0;
      if (!nav_mesh_.getPolyFlags(ref, flags) || (flags & navigation::kNavPolyFlagOffMesh) == 0) {
        continue;
      }
      for (uint64_t previous : polys_.polys) {
        if (previous == ref) {
          continue;
        }
        if (nav_mesh_.offMeshConnectionEndpoints(previous, ref, offmesh_endpoints_)) {
          break;
        }
      }
    }
    status_ = "off-mesh query updated";
  }

  void addObstacleAt(const math::Vec3& point) {
    if (!tile_cache_.isValid()) {
      return;
    }
    uint64_t ref = 0;
    if (obstacle_shape_ == 0) {
      tile_cache_.addCylinderObstacle(point, obstacle_radius_, obstacle_height_, &ref);
    } else if (obstacle_shape_ == 1) {
      tile_cache_.addBoxObstacle({point.x - obstacle_radius_, -0.1f, point.z - obstacle_radius_},
                                 {point.x + obstacle_radius_, obstacle_height_, point.z + obstacle_radius_},
                                 &ref);
    } else {
      tile_cache_.addOrientedBoxObstacle({point.x, obstacle_height_ * 0.5f, point.z},
                                         {obstacle_radius_, obstacle_height_ * 0.5f, obstacle_radius_ * 0.65f},
                                         obstacle_yaw_,
                                         &ref);
    }
    if (ref != 0) {
      status_ = "obstacle added";
    }
  }

  void saveTileCache() {
    if (!tile_cache_.isValid() || !nav_mesh_.isValid()) {
      return;
    }
    const navigation::NavTileCacheSnapshot snapshot = tile_cache_.snapshot(nav_mesh_);
    if (content::saveNavTileCacheSnapshot(snapshot_path_, snapshot)) {
      status_ = "saved " + snapshot_path_.string();
    }
  }

  void loadTileCache() {
    navigation::NavTileCacheSnapshot snapshot = content::loadNavTileCacheSnapshot(snapshot_path_);
    navigation::NavTileCacheBuildResult result;
    if (tile_cache_.loadSnapshot(nav_mesh_, snapshot, &result)) {
      status_ = "loaded " + snapshot_path_.string();
    } else {
      status_ = result.message;
    }
  }

  void mutateNearestPoly() {
    if (!nav_mesh_.isValid()) {
      return;
    }
    navigation::NavQuery query(nav_mesh_);
    uint64_t ref = 0;
    if (query.findNearestPoly(start_, ref)) {
      uint16_t flags = 0;
      nav_mesh_.getPolyFlags(ref, flags);
      nav_mesh_.setPolyFlags(ref, static_cast<uint16_t>(flags ^ kFlagDisabled));
      nav_mesh_.setPolyArea(ref, kAreaWater);
      status_ = "nearest poly mutated";
      runOffMeshQuery();
    }
  }

  void storeFirstTileState() {
    const auto tiles = nav_mesh_.tiles();
    if (!tiles.empty() && nav_mesh_.storeTileState(tiles.front().ref, tile_state_)) {
      status_ = "stored tile state";
    }
  }

  void restoreTileState() {
    if (tile_state_.valid() && nav_mesh_.restoreTileState(tile_state_)) {
      status_ = "restored tile state";
      runOffMeshQuery();
    }
  }

  navigation::NavMeshDebugDrawMode selectedDebugMode() const {
    switch (debug_layer_index_) {
      case 1: return navigation::NavMeshDebugDrawMode::NavMesh;
      case 2: return navigation::NavMeshDebugDrawMode::NavMeshBVTree;
      case 3: return navigation::NavMeshDebugDrawMode::NavMeshPortals;
      case 4: return navigation::NavMeshDebugDrawMode::Voxels;
      case 5: return navigation::NavMeshDebugDrawMode::Compact;
      case 6: return navigation::NavMeshDebugDrawMode::Contours;
      case 7: return navigation::NavMeshDebugDrawMode::PolyMeshDetail;
      case 0:
      default:
        return navigation::NavMeshDebugDrawMode::NavMeshEdges;
    }
  }

  void drawDebug() {
    if (graphics == nullptr) {
      return;
    }
    if ((kind_ == ExampleKind::PointClick || kind_ == ExampleKind::PhysicsBridge) && navigationSystem() != nullptr) {
      navigationSystem()->debugDraw(*world, *graphics, false);
    } else if (nav_mesh_.isValid()) {
      if (draw_build_layer_) {
        nav_mesh_.debugDraw(*graphics, selectedDebugMode(), false);
      } else if (draw_navmesh_) {
        nav_mesh_.debugDraw(*graphics, {0.1f, 0.85f, 0.35f, 1.0f}, false);
      }
    }
    if (draw_path_) {
      navigation::NavQuery::debugDrawPath(*graphics, path_, {1.0f, 0.85f, 0.1f, 1.0f}, false);
      navigation::NavQuery::debugDrawPath(*graphics, smooth_path_, {0.25f, 0.9f, 1.0f, 1.0f}, false);
      navigation::NavQuery::debugDrawPath(*graphics, sliced_path_, {0.9f, 0.35f, 1.0f, 1.0f}, false);
    }
    if (draw_query_) {
      if (nav_mesh_.isValid()) {
        nav_mesh_.debugDrawPolygons(*graphics, polys_.polys, {0.05f, 0.2f, 0.95f, 0.5f}, false);
        nav_mesh_.debugDrawPolygons(*graphics, circle_polys_.polys, {0.9f, 0.2f, 0.1f, 0.45f}, false);
        nav_mesh_.debugDrawPolygons(*graphics, local_polys_.polys, {0.2f, 0.9f, 0.25f, 0.35f}, false);
      }
      for (const navigation::NavWallSegment& segment : walls_.segments) {
        graphics->drawLine(segment.start, segment.end, {1.0f, 0.25f, 0.1f, 1.0f}, false, 2.0f);
      }
      if (ray_.success()) {
        graphics->drawLine(start_, ray_.hit_position, {1.0f, 1.0f, 1.0f, 1.0f}, false, 2.0f);
        graphics->drawLine(ray_.hit_position,
                           math::add(ray_.hit_position, math::scale(ray_.hit_normal, 1.0f)),
                           {1.0f, 0.1f, 0.1f, 1.0f},
                           false,
                           2.0f);
      }
      if (closest_.success()) {
        graphics->drawLine(start_, closest_.point, {0.2f, 1.0f, 0.8f, 1.0f}, false, 2.0f);
      }
      if (offmesh_endpoints_.start.x != 0.0f || offmesh_endpoints_.end.x != 0.0f) {
        graphics->drawLine(offmesh_endpoints_.start,
                           offmesh_endpoints_.end,
                           {1.0f, 0.45f, 0.0f, 1.0f},
                           false,
                           4.0f);
      }
    }
    if (kind_ == ExampleKind::TileCache) {
      tile_cache_.debugDraw(*graphics, {1.0f, 0.45f, 0.1f, 1.0f}, false, true);
    }
    if (kind_ == ExampleKind::Crowds) {
      for (const navigation::NavCrowdDebugAgent& agent : crowd_debug_.agents) {
        for (const navigation::NavCrowdDebugSegment& segment : agent.boundary_segments) {
          graphics->drawLine(segment.start, segment.end, {1.0f, 0.1f, 0.1f, 1.0f}, false, 1.0f);
        }
        for (size_t i = 1; i < agent.corners.size(); ++i) {
          graphics->drawLine(agent.corners[i - 1].position,
                             agent.corners[i].position,
                             {1.0f, 0.85f, 0.1f, 1.0f},
                             false,
                             1.5f);
        }
      }
    }
  }

  void drawBuildUi() {
    if (ImGui::CollapsingHeader("Build", ImGuiTreeNodeFlags_DefaultOpen)) {
      bool changed = false;
      changed |= ImGui::SliderFloat("Cell Size", &build_config_.cell_size, 0.1f, 0.8f, "%.2f");
      changed |= ImGui::SliderFloat("Agent Radius", &build_config_.agent_radius, 0.05f, 1.5f, "%.2f");
      changed |= ImGui::SliderFloat("Agent Height", &build_config_.agent_height, 0.5f, 3.5f, "%.2f");
      changed |= ImGui::SliderFloat("Max Climb", &build_config_.agent_max_climb, 0.1f, 2.0f, "%.2f");
      changed |= ImGui::SliderFloat("Max Slope", &build_config_.agent_max_slope_degrees, 0.0f, 80.0f, "%.0f");
      changed |= ImGui::SliderInt("Tile Size", &build_config_.tile_size, 16, 96);
      int partition = static_cast<int>(build_config_.partition_type);
      if (ImGui::Combo("Partition", &partition, "Watershed\0Monotone\0Layers\0")) {
        build_config_.partition_type = static_cast<navigation::NavMeshPartitionType>(partition);
        changed = true;
      }
      if (ImGui::Button("Rebuild") || changed) {
        rebuild_requested_ = true;
      }
    }
  }

  void drawPointClickUi() {
    ImGui::TextUnformatted("Left-click a destination. Right-click moves the query start marker.");
    ImGui::SliderFloat("Agent Speed", &point_speed_, 0.5f, 12.0f, "%.1f");
    if (actor_entity_.isValid() && world->has<components::NavMeshAgentComponent>(actor_entity_)) {
      const auto& agent = world->get<components::NavMeshAgentComponent>(actor_entity_);
      ImGui::Text("Path points: %zu", agent.path.size());
      ImGui::Text("Velocity: %.2f %.2f %.2f",
                  agent.current_velocity.x,
                  agent.current_velocity.y,
                  agent.current_velocity.z);
    }
  }

  void drawCrowdUi() {
    ImGui::SliderInt("Agents", &crowd_count_, 1, 32);
    ImGui::SliderFloat("Speed", &crowd_speed_, 0.5f, 8.0f, "%.1f");
    bool avoid = (crowd_update_flags_ & navigation::NavCrowdUpdateFlagObstacleAvoidance) != 0;
    bool separate = (crowd_update_flags_ & navigation::NavCrowdUpdateFlagSeparation) != 0;
    if (ImGui::Checkbox("Obstacle Avoidance", &avoid)) {
      crowd_update_flags_ ^= navigation::NavCrowdUpdateFlagObstacleAvoidance;
      rebuildCrowd();
    }
    if (ImGui::Checkbox("Separation", &separate)) {
      crowd_update_flags_ ^= navigation::NavCrowdUpdateFlagSeparation;
      rebuildCrowd();
    }
    ImGui::Checkbox("Corners", &crowd_debug_request_.include_corners);
    ImGui::Checkbox("Collision Segments", &crowd_debug_request_.include_collision_segments);
    ImGui::Checkbox("Neighbours", &crowd_debug_request_.include_neighbours);
    if (ImGui::Button("Respawn Agents")) {
      rebuildCrowd();
    }
    ImGui::Text("Debug agents: %zu", crowd_debug_.agents.size());
  }

  void drawTileCacheUi() {
    ImGui::Combo("Compression", &compression_index_, "FastLZ\0None\0");
    ImGui::Combo("Obstacle Shape", &obstacle_shape_, "Cylinder\0Box\0Oriented Box\0");
    ImGui::SliderFloat("Obstacle Radius", &obstacle_radius_, 0.1f, 2.0f, "%.2f");
    ImGui::SliderFloat("Obstacle Height", &obstacle_height_, 0.2f, 4.0f, "%.2f");
    ImGui::SliderFloat("Obstacle Yaw", &obstacle_yaw_, -3.14f, 3.14f, "%.2f");
    if (ImGui::Button("Clear Obstacles")) {
      tile_cache_.clearObstacles();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save .kntc")) {
      saveTileCache();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .kntc")) {
      loadTileCache();
    }
    ImGui::Text("Tiles: %u / %u", tile_cache_.tileCount(), tile_cache_.tileCapacity());
    ImGui::Text("Obstacles: %u / %u", tile_cache_.obstacleCount(), tile_cache_.obstacleCapacity());
  }

  void drawQueryUi() {
    ImGui::TextUnformatted("Left-click end, right-click start.");
    ImGui::SliderFloat("Radius", &query_radius_, 0.5f, 8.0f, "%.1f");
    ImGui::SliderInt("Max Polys", &max_query_polys_, 8, 512);
    ImGui::DragFloat3("AABB Half Extents", &query_half_extents_.x, 0.1f, 0.1f, 8.0f);
    if (ImGui::Button("Run Query Suite")) {
      runQuerySuite();
    }
    ImGui::Text("Path: %s (%zu points)", navigation::navStatusName(path_.status), path_.points.size());
    ImGui::Text("Smooth: %s (%zu points)", navigation::navStatusName(smooth_path_.status), smooth_path_.points.size());
    ImGui::Text("Sliced: %s (%zu points)", navigation::navStatusName(sliced_path_.status), sliced_path_.points.size());
    ImGui::Text("AABB polys: %zu", polys_.polys.size());
    ImGui::Text("Circle polys: %zu", circle_polys_.polys.size());
    ImGui::Text("Local polys: %zu", local_polys_.polys.size());
    ImGui::Text("Wall segments: %zu", walls_.segments.size());
    ImGui::Text("Ray t: %.3f visited: %zu", ray_.hit_fraction, ray_.visited_polys.size());
    ImGui::Text("Dijkstra path polys: %zu", dijkstra_path_.polys.size());
    ImGui::Text("Closed list contains nearest start: %s", closed_list_contains_start_ ? "yes" : "no");
  }

  void drawOffMeshUi() {
    ImGui::TextUnformatted("Gap uses an off-mesh link; water uses a separate area flag.");
    auto flagCheckbox = [](const char* label, uint16_t& flags, uint16_t flag) {
      bool enabled = (flags & flag) != 0;
      if (ImGui::Checkbox(label, &enabled)) {
        if (enabled) {
          flags = static_cast<uint16_t>(flags | flag);
        } else {
          flags = static_cast<uint16_t>(flags & ~flag);
        }
      }
    };
    flagCheckbox("Walk", include_flags_, navigation::kNavPolyFlagWalk);
    flagCheckbox("Water", include_flags_, kFlagWater);
    flagCheckbox("Door / Link", include_flags_, kFlagDoor);
    flagCheckbox("Exclude Disabled", exclude_flags_, kFlagDisabled);
    ImGui::SliderFloat("Water Cost", &water_cost_, 1.0f, 20.0f, "%.1f");
    ImGui::SliderFloat("Door Cost", &door_cost_, 1.0f, 20.0f, "%.1f");
    if (ImGui::Button("Run Path")) {
      runOffMeshQuery();
    }
    ImGui::SameLine();
    if (ImGui::Button("Mutate Nearest Poly")) {
      mutateNearestPoly();
    }
    if (ImGui::Button("Store Tile State")) {
      storeFirstTileState();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore Tile State")) {
      restoreTileState();
    }
    ImGui::Text("Path: %s (%zu points)", navigation::navStatusName(path_.status), path_.points.size());
    ImGui::Text("Queried polys: %zu", polys_.polys.size());
  }

  void drawPhysicsBridgeUi() {
    ImGui::Checkbox("CharacterController velocity mode", &bridge_velocity_mode_);
    ImGui::SliderFloat("Crowd Speed", &crowd_speed_, 0.5f, 8.0f, "%.1f");
    if (actor_entity_.isValid() && world->has<components::CharacterControllerComponent>(actor_entity_)) {
      const auto& controller = world->get<components::CharacterControllerComponent>(actor_entity_);
      ImGui::Text("Desired velocity: %.2f %.2f %.2f",
                  controller.desiredVelocity().x,
                  controller.desiredVelocity().y,
                  controller.desiredVelocity().z);
    }
    if (actor_entity_.isValid() && world->has<components::NavCrowdAgentComponent>(actor_entity_)) {
      const auto& agent = world->get<components::NavCrowdAgentComponent>(actor_entity_);
      ImGui::Text("Crowd velocity: %.2f %.2f %.2f",
                  agent.current_velocity.x,
                  agent.current_velocity.y,
                  agent.current_velocity.z);
    }
  }

  ExampleKind kind_;
  SurfaceBuild surface_;
  Bounds bounds_;
  navigation::NavMeshBuildConfig build_config_{};
  navigation::NavMesh nav_mesh_;
  navigation::NavTileCache tile_cache_;
  navigation::NavCrowd crowd_;
  navigation::NavCrowdDebugRequest crowd_debug_request_{.enabled = true};
  navigation::NavCrowdDebugSnapshot crowd_debug_;
  navigation::NavPath path_;
  navigation::NavPath smooth_path_;
  navigation::NavPath sliced_path_;
  navigation::NavRaycastResult ray_;
  navigation::NavPolyQueryResult polys_;
  navigation::NavPolyQueryResult circle_polys_;
  navigation::NavPolyQueryResult local_polys_;
  navigation::NavPolyQueryResult dijkstra_path_;
  navigation::NavWallSegments walls_;
  navigation::NavClosestPointResult closest_;
  navigation::NavClosestPointResult boundary_;
  navigation::NavOffMeshConnectionEndpoints offmesh_endpoints_;
  navigation::NavTileStateSnapshot tile_state_;
  std::vector<AgentVisual> agent_visuals_;
  std::filesystem::path snapshot_path_ =
      std::filesystem::temp_directory_path() / "karma_navigation_tile_cache.kntc";
  math::Vec3 start_{};
  math::Vec3 end_{};
  math::Vec3 target_{};
  math::Vec3 query_half_extents_{2.0f, 3.0f, 2.0f};
  ecs::Entity surface_entity_{};
  ecs::Entity camera_entity_{};
  ecs::Entity nav_mesh_entity_{};
  ecs::Entity actor_entity_{};
  ecs::Entity start_marker_{};
  ecs::Entity end_marker_{};
  std::string status_ = "not built";
  float point_speed_ = 6.0f;
  float crowd_speed_ = 3.0f;
  float query_radius_ = 3.0f;
  float water_cost_ = 4.0f;
  float door_cost_ = 1.2f;
  float obstacle_radius_ = 0.65f;
  float obstacle_height_ = 2.0f;
  float obstacle_yaw_ = 0.5f;
  int crowd_count_ = 12;
  int max_query_polys_ = 128;
  int debug_layer_index_ = 0;
  int compression_index_ = 0;
  int obstacle_shape_ = 0;
  uint8_t crowd_update_flags_ = navigation::NavCrowdUpdateFlagAnticipateTurns |
                                navigation::NavCrowdUpdateFlagObstacleAvoidance |
                                navigation::NavCrowdUpdateFlagOptimizeVisibility |
                                navigation::NavCrowdUpdateFlagOptimizeTopology;
  uint16_t include_flags_ = navigation::kNavPolyFlagWalk | kFlagWater | kFlagDoor;
  uint16_t exclude_flags_ = 0;
  bool bridge_velocity_mode_ = true;
  bool draw_navmesh_ = true;
  bool draw_path_ = true;
  bool draw_query_ = true;
  bool draw_build_layer_ = false;
  bool rebuild_requested_ = false;
  bool nav_crowd_added_ = false;
  bool closed_list_contains_start_ = false;
};

}  // namespace

const char* exampleName(ExampleKind kind) {
  switch (kind) {
    case ExampleKind::PointClick: return "Point Click";
    case ExampleKind::Crowds: return "Crowds";
    case ExampleKind::TileCache: return "Tile Cache";
    case ExampleKind::QueryLab: return "Query Lab";
    case ExampleKind::OffMeshAreas: return "Off-Mesh Areas";
    case ExampleKind::PhysicsBridge: return "Physics Bridge";
    default: return "Navigation";
  }
}

int runExample(ExampleKind kind) {
  app::EngineApp engine;
  NavigationExampleApp game(kind);
  engine.setUi(imgui::createUiLayer([&](app::UIContext& ctx) {
    game.drawUi(ctx);
  }));

  app::EngineConfig config;
  config.window.title = std::string("Navigation / ") + exampleName(kind);
  config.window.width = 1280;
  config.window.height = 720;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.forward_plus_max_lights_per_tile = 128;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.lighting_exposure = 1.08f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}

}  // namespace karma::demo::navigation_examples
