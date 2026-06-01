#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <cmath>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr float kFloorY = 0.0f;
constexpr float kTargetMarkerY = 0.45f;

math::Vec3 toMarkerVisualPoint(const math::Vec3& nav_point) {
  return {nav_point.x, kTargetMarkerY, nav_point.z};
}

}  // namespace

class NavMeshSceneExample final : public app::GameInterface {
 public:
  void onStart() override {
    nav_diag_.initializeFromEnvironment();
    input->bindMouse("move_to", platform::MouseButton::Left, input::Trigger::Pressed);
    buildScene();
    createNavigation();
    spawnCameraRig();
    spawnLighting();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    const auto frame_start = core::SteadyClock::now();

    auto section_start = frame_start;
    reportBakeResult();
    handleMoveClick();
    auto section_end = core::SteadyClock::now();
    const double click_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    updateCamera();
    section_end = core::SteadyClock::now();
    const double camera_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    drawNavigationDebug();
    section_end = core::SteadyClock::now();
    const double debug_draw_ms = core::elapsedMilliseconds(section_start, section_end);
    const double on_update_ms = core::elapsedMilliseconds(frame_start, section_end);

    if (const auto* nav_system = navigationSystem()) {
      nav_diag_.logIfChanged(nav_system->stats(),
                             navigation::NavigationDiagnosticsFrame{
                                 .dt = dt,
                                 .on_update_ms = on_update_ms,
                                 .click_ms = click_ms,
                                 .camera_ms = camera_ms,
                                 .debug_draw_ms = debug_draw_ms,
                             });
    }
  }

  void onShutdown() override {}

 private:
  navigation::NavigationSystem* navigationSystem() const {
    return systems != nullptr ? systems->findSystem<navigation::NavigationSystem>() : nullptr;
  }

  void buildScene() {
    const std::string world_mesh_key = resolveExampleAssetPath("world.glb").string();
    const std::string tank_mesh_key = resolveExampleAssetPath("tank_final.glb").string();

    world_entity_ = runtime::spawnMeshAsset(*world, "World", world_mesh_key, {});
    world->add(world_entity_, components::NavMeshSurfaceComponent{
                                .area = navigation::kNavAreaDefault,
                                .mesh_key = world_mesh_key,
                            });

    player_entity_ = runtime::spawnMeshAsset(*world, "Click Move Tank", tank_mesh_key, start_);
    target_marker_entity_ = runtime::createDebugBoxMarker(*world,
                                                          graphics,
                                                          "Move Target",
                                                          {0.98f, 0.72f, 0.1f, 1.0f},
                                                          toMarkerVisualPoint(start_),
                                                          {0.18f, 0.18f, 0.18f},
                                                          false);
  }

  void createNavigation() {
    navigation::NavMeshBuildConfig config;
    config.cell_size = 0.25f;
    config.cell_height = 0.1f;
    config.agent_height = 1.8f;
    config.agent_radius = 0.55f;
    config.agent_max_climb = 0.7f;
    config.region_min_size = 6.0f;
    config.region_merge_size = 18.0f;
    config.area_configs = {
        {.area = navigation::kNavAreaDefault, .flags = navigation::kNavPolyFlagWalk, .cost = 1.0f},
    };

    nav_mesh_entity_ = world->createEntity();
    world->setName(nav_mesh_entity_, "Navigation Mesh");
    world->add(nav_mesh_entity_, components::NavMeshComponent{
                                     .build_config = config,
                                 });

    components::NavMeshAgentComponent agent;
    agent.speed = 7.0f;
    agent.stopping_distance = 0.2f;
    agent.nav_mesh_entity = nav_mesh_entity_;
    agent.search_extents = {3.0f, 6.0f, 3.0f};
    agent.query_filter = navigation::makeQueryFilter(config);
    world->add(player_entity_, std::move(agent));
  }

  void reportBakeResult() {
    if (bake_result_reported_ ||
        !world->isAlive(nav_mesh_entity_) ||
        !world->has<components::NavMeshComponent>(nav_mesh_entity_)) {
      return;
    }

    const auto& nav = world->get<components::NavMeshComponent>(nav_mesh_entity_);
    if (nav.build_version == 0) {
      return;
    }

    bake_result_reported_ = true;
    const navigation::NavMeshBuildResult& result = nav.last_build_result;
    if (!nav.built) {
      spdlog::error("Navmesh example bake failed: {} - {}",
                    navigation::navStatusName(result.status),
                    result.message);
      return;
    }

    spdlog::info("Navmesh example baked {} polygons from {} triangles; left-click the floor to move the agent",
                 result.polygon_count,
                 result.triangle_count);
  }

  void spawnCameraRig() {
    const math::Vec3 camera_position{start_.x + camera_follow_offset_.x,
                                     start_.y + camera_follow_offset_.y,
                                     start_.z + camera_follow_offset_.z};
    camera_entity_ = runtime::spawnCamera(*world,
                                          "Camera",
                                          camera_position,
                                          math::fromYawPitch(0.0f, camera_pitch_),
                                          components::CameraComponent{
                                              .near_clip = 0.05f,
                                              .far_clip = 180.0f,
                                              .is_primary = true,
                                          });
  }

  void spawnLighting() {
    runtime::spawnDirectionalLight(*world,
                                   "Sun Light",
                                   {0.0f, 50.0f, 0.0f},
                                   math::fromYawPitch(0.5f, -0.9f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                       .intensity = 0.8f,
                                       .casts_shadows = true,
                                       .shadow_extent = 60.0f,
                                   });

    runtime::spawnPointLight(*world,
                             "Point Light Warm",
                             {-9.0f, 4.0f, 1.0f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {1.0f, 0.65f, 0.35f, 1.0f},
                                 .intensity = 28.0f,
                                 .range = 24.0f,
                                 .casts_shadows = true,
                             });
    runtime::spawnPointLight(*world,
                             "Point Light Cool",
                             {8.0f, 3.5f, -6.0f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.35f, 0.6f, 1.0f, 1.0f},
                                 .intensity = 24.0f,
                                 .range = 22.0f,
                                 .casts_shadows = true,
                             });
    runtime::spawnPointLight(*world,
                             "Point Light Fill",
                             {0.0f, 6.0f, 12.0f},
                             components::LightComponent{
                                 .type = components::LightComponent::Type::Point,
                                 .color = {0.55f, 1.0f, 0.7f, 1.0f},
                                 .intensity = 16.0f,
                                 .range = 26.0f,
                             });

    runtime::spawnEnvironment(*world,
                              "Environment",
                              resolveExampleAssetPath("golden_gate_hills_4k.hdr").string(),
                              0.4f,
                              true);
  }

  bool screenToFloor(double mouse_x, double mouse_y, math::Vec3& out_point) const {
    if (graphics == nullptr || !world->isAlive(camera_entity_)) {
      return false;
    }

    int width = 0;
    int height = 0;
    graphics->getFramebufferSize(width, height);

    const auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const auto& camera = world->get<components::CameraComponent>(camera_entity_);
    renderer::ScreenRay ray;
    if (!renderer::screenPointToWorldRay(mouse_x,
                                         mouse_y,
                                         width,
                                         height,
                                         camera_transform.getPosition(),
                                         camera_transform.getRotation(),
                                         camera.fov_y_degrees,
                                         ray)) {
      return false;
    }

    if (std::abs(ray.direction.y) < 0.0001f) {
      return false;
    }

    const float t = (kFloorY - ray.origin.y) / ray.direction.y;
    if (t < 0.0f) {
      return false;
    }

    out_point = math::add(ray.origin, math::scale(ray.direction, t));
    return true;
  }

  void updateCamera() {
    if (!world->isAlive(camera_entity_) || !world->isAlive(player_entity_)) {
      return;
    }

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const auto& player_transform = world->get<components::TransformComponent>(player_entity_);
    const math::Vec3 player_pos =
        player_transform.getInterpolatedPosition(renderInterpolationAlpha());
    camera_transform.setPosition({player_pos.x + camera_follow_offset_.x,
                                  player_pos.y + camera_follow_offset_.y,
                                  player_pos.z + camera_follow_offset_.z});
    camera_transform.setRotation(math::fromYawPitch(0.0f, camera_pitch_));
  }

  void handleMoveClick() {
    if (input == nullptr || !input->actionPressed("move_to") ||
        !world->isAlive(nav_mesh_entity_) ||
        !world->has<components::NavMeshComponent>(nav_mesh_entity_) ||
        !world->isAlive(player_entity_) ||
        !world->has<components::NavMeshAgentComponent>(player_entity_)) {
      return;
    }

    const auto& nav = world->get<components::NavMeshComponent>(nav_mesh_entity_);
    if (!nav.built || !nav.nav_mesh.isValid()) {
      return;
    }

    double mouse_x = 0.0;
    double mouse_y = 0.0;
    if (!input->mousePosition(mouse_x, mouse_y)) {
      return;
    }

    math::Vec3 floor_hit;
    if (!screenToFloor(mouse_x, mouse_y, floor_hit)) {
      return;
    }

    if (!navigation::NavigationSystem::requestMoveTo(*world, player_entity_, floor_hit)) {
      return;
    }

    if (world->isAlive(target_marker_entity_)) {
      world->get<components::TransformComponent>(target_marker_entity_).setPosition(
          toMarkerVisualPoint(floor_hit));
      auto& marker_mesh = world->get<components::MeshComponent>(target_marker_entity_);
      marker_mesh.visible = true;
      marker_mesh.shadow_visible = true;
    }
  }

  void drawNavigationDebug() {
    if (graphics == nullptr) {
      return;
    }
    if (auto* nav_system = navigationSystem()) {
      nav_system->debugDraw(*world, *graphics, false);
    }
  }

  navigation::NavigationDiagnostics nav_diag_;
  math::Vec3 start_{0.0f, 0.0f, 0.0f};
  math::Vec3 camera_follow_offset_{0.0f, 10.0f, 18.0f};
  float camera_pitch_ = -0.5f;
  ecs::Entity world_entity_{};
  ecs::Entity nav_mesh_entity_{};
  ecs::Entity player_entity_{};
  ecs::Entity target_marker_entity_{};
  ecs::Entity camera_entity_{};
  bool bake_result_reported_ = false;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::NavMeshSceneExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Navmesh Example";
  config.window.width = 1280;
  config.window.height = 720;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.shadow_bias = 0.0006f;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
