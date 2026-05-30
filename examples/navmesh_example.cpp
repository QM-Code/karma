#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr float kFloorY = 0.0f;
constexpr float kTargetMarkerY = 0.45f;
constexpr float kPi = 3.14159265358979323846f;

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

void appendVertex(renderer::MeshData& mesh,
                  const glm::vec3& position,
                  const glm::vec3& normal,
                  const glm::vec2& uv = {}) {
  mesh.vertices.push_back(position);
  mesh.normals.push_back(normal);
  mesh.uvs.push_back(uv);
  mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
}

void appendQuad(renderer::MeshData& mesh,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                const glm::vec3& normal) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  appendVertex(mesh, a, normal, {0.0f, 0.0f});
  appendVertex(mesh, b, normal, {1.0f, 0.0f});
  appendVertex(mesh, c, normal, {1.0f, 1.0f});
  appendVertex(mesh, d, normal, {0.0f, 1.0f});
  mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

renderer::MeshData buildBoxMesh(const glm::vec3& half_extents) {
  const glm::vec3 min = -half_extents;
  const glm::vec3 max = half_extents;
  renderer::MeshData mesh;

  appendQuad(mesh, {min.x, max.y, min.z}, {max.x, max.y, min.z},
             {max.x, max.y, max.z}, {min.x, max.y, max.z}, {0.0f, 1.0f, 0.0f});
  appendQuad(mesh, {min.x, min.y, max.z}, {max.x, min.y, max.z},
             {max.x, min.y, min.z}, {min.x, min.y, min.z}, {0.0f, -1.0f, 0.0f});
  appendQuad(mesh, {min.x, min.y, min.z}, {max.x, min.y, min.z},
             {max.x, max.y, min.z}, {min.x, max.y, min.z}, {0.0f, 0.0f, -1.0f});
  appendQuad(mesh, {max.x, min.y, max.z}, {min.x, min.y, max.z},
             {min.x, max.y, max.z}, {max.x, max.y, max.z}, {0.0f, 0.0f, 1.0f});
  appendQuad(mesh, {min.x, min.y, max.z}, {min.x, min.y, min.z},
             {min.x, max.y, min.z}, {min.x, max.y, max.z}, {-1.0f, 0.0f, 0.0f});
  appendQuad(mesh, {max.x, min.y, min.z}, {max.x, min.y, max.z},
             {max.x, max.y, max.z}, {max.x, max.y, min.z}, {1.0f, 0.0f, 0.0f});

  return mesh;
}

renderer::MaterialId createMaterial(renderer::GraphicsDevice* graphics,
                                    const math::Color& color,
                                    bool unlit = false) {
  if (graphics == nullptr) {
    return renderer::kInvalidMaterial;
  }
  renderer::MaterialDesc material;
  material.base_color = color;
  material.roughness = 0.85f;
  material.metallic = 0.0f;
  material.unlit = unlit;
  if (unlit) {
    material.emissive_color = color;
  }
  return graphics->createMaterial(material);
}

math::Vec3 add(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 scale(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

math::Vec3 toMarkerVisualPoint(const math::Vec3& nav_point) {
  return {nav_point.x, kTargetMarkerY, nav_point.z};
}

}  // namespace

class NavMeshSceneExample final : public app::GameInterface {
 public:
  void onStart() override {
    nav_diag_enabled_ = envFlagEnabled(std::getenv("KARMA_NAVMESH_DIAG"));
    if (nav_diag_enabled_) {
      spdlog::info("KARMA_NAVMESH_DIAG enabled; logging nav request diagnostics");
    }
    input->bindMouse("move_to", platform::MouseButton::Left, input::Trigger::Pressed);
    buildSceneMeshes();
    createNavigation();
    spawnCamera();
    spawnLights();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    const auto frame_start = Clock::now();
    path_time_ += dt;
    auto section_start = Clock::now();
    handleMoveClick();
    auto section_end = Clock::now();
    const double click_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    navigation_system_.update(*world, dt);
    section_end = Clock::now();
    const double navigation_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    updateCamera();
    section_end = Clock::now();
    const double camera_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    drawNavigationDebug();
    section_end = Clock::now();
    const double debug_draw_ms = elapsedMs(section_start, section_end);
    const double on_update_ms = elapsedMs(frame_start, section_end);

    logNavigationDiagnostics(dt,
                             click_ms,
                             navigation_ms,
                             camera_ms,
                             debug_draw_ms,
                             on_update_ms);
  }

  void onShutdown() override {}

 private:
  void buildSceneMeshes() {
    marker_mesh_ = buildBoxMesh({0.18f, 0.18f, 0.18f});

    const std::string world_mesh_key = resolveExampleAssetPath("world.glb").string();
    const std::string tank_mesh_key = resolveExampleAssetPath("tank_final.glb").string();
    const renderer::MeshId target_marker_mesh_id =
        graphics != nullptr ? graphics->createMesh(marker_mesh_) : renderer::kInvalidMesh;
    const renderer::MaterialId target_material =
        createMaterial(graphics, {0.98f, 0.72f, 0.1f, 1.0f}, true);

    world_entity_ = spawnMeshAsset("World", world_mesh_key, {});
    world->add(world_entity_, components::NavMeshSurfaceComponent{
                                .area = navigation::kNavAreaDefault,
                                .mesh_key = world_mesh_key,
                            });

    player_entity_ = spawnMeshAsset("Click Move Tank", tank_mesh_key, start_);
    target_marker_entity_ =
        spawnMesh("Move Target", target_marker_mesh_id, target_material, toMarkerVisualPoint(start_), false);
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

    navigation_system_.update(*world, 0.0f);
    const auto& nav = world->get<components::NavMeshComponent>(nav_mesh_entity_);
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

  ecs::Entity spawnMesh(const std::string& name,
                        renderer::MeshId mesh,
                        renderer::MaterialId material,
                        const math::Vec3& position,
                        bool visible = true) {
    const ecs::Entity entity = world->createEntity();
    world->setName(entity, name);
    components::TransformComponent transform;
    transform.setPosition(position);
    world->add(entity, transform);
    world->add(entity, components::MeshComponent{
                          .mesh_id = mesh,
                          .material_id = material,
                          .owns_mesh_id = mesh != renderer::kInvalidMesh,
                          .owns_material_id = material != renderer::kInvalidMaterial,
                          .visible = visible,
                          .shadow_visible = visible,
                      });
    return entity;
  }

  ecs::Entity spawnMeshAsset(const std::string& name,
                             const std::string& mesh_key,
                             const math::Vec3& position) {
    const ecs::Entity entity = world->createEntity();
    world->setName(entity, name);
    components::TransformComponent transform;
    transform.setPosition(position);
    world->add(entity, transform);
    world->add(entity, components::MeshComponent{
                          .mesh_key = mesh_key,
                      });
    return entity;
  }

  void spawnCamera() {
    const ecs::Entity camera = world->createEntity();
    camera_entity_ = camera;
    world->setName(camera, "Camera");
    components::TransformComponent transform;
    transform.setPosition({start_.x + camera_follow_offset_.x,
                           start_.y + camera_follow_offset_.y,
                           start_.z + camera_follow_offset_.z});
    transform.setRotation(math::fromYawPitch(0.0f, camera_pitch_));
    world->add(camera, transform);
    world->add(camera, components::CameraComponent{
                          .near_clip = 0.05f,
                          .far_clip = 180.0f,
                          .is_primary = true,
                      });
    world->add(camera, components::AudioListenerComponent{});
  }

  void spawnLights() {
    const ecs::Entity sun = world->createEntity();
    world->setName(sun, "Sun Light");
    components::TransformComponent sun_transform;
    sun_transform.setPosition({0.0f, 50.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.5f, -0.9f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 1.0f, 1.0f, 1.0f},
                        .intensity = 0.8f,
                        .casts_shadows = true,
                        .shadow_extent = 60.0f,
                    });

    const ecs::Entity point_warm = world->createEntity();
    world->setName(point_warm, "Point Light Warm");
    components::TransformComponent point_warm_xform;
    point_warm_xform.setPosition({-9.0f, 4.0f, 1.0f});
    world->add(point_warm, point_warm_xform);
    world->add(point_warm, components::LightComponent{
                         .type = components::LightComponent::Type::Point,
                         .color = {1.0f, 0.65f, 0.35f, 1.0f},
                         .intensity = 28.0f,
                         .range = 24.0f,
                         .casts_shadows = true,
                     });

    const ecs::Entity point_cool = world->createEntity();
    world->setName(point_cool, "Point Light Cool");
    components::TransformComponent point_cool_xform;
    point_cool_xform.setPosition({8.0f, 3.5f, -6.0f});
    world->add(point_cool, point_cool_xform);
    world->add(point_cool, components::LightComponent{
                              .type = components::LightComponent::Type::Point,
                              .color = {0.35f, 0.6f, 1.0f, 1.0f},
                              .intensity = 24.0f,
                              .range = 22.0f,
                              .casts_shadows = true,
                          });

    const ecs::Entity point_fill = world->createEntity();
    world->setName(point_fill, "Point Light Fill");
    components::TransformComponent point_fill_xform;
    point_fill_xform.setPosition({0.0f, 6.0f, 12.0f});
    world->add(point_fill, point_fill_xform);
    world->add(point_fill, components::LightComponent{
                              .type = components::LightComponent::Type::Point,
                              .color = {0.55f, 1.0f, 0.7f, 1.0f},
                              .intensity = 16.0f,
                              .range = 26.0f,
                          });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                .environment_map =
                                    resolveExampleAssetPath("golden_gate_hills_4k.hdr").string(),
                                .intensity = 0.4f,
                                .draw_skybox = true,
                            });
  }

  bool screenToFloor(double mouse_x, double mouse_y, math::Vec3& out_point) const {
    if (graphics == nullptr || !world->isAlive(camera_entity_)) {
      return false;
    }

    int width = 0;
    int height = 0;
    graphics->getFramebufferSize(width, height);
    if (width <= 0 || height <= 0) {
      return false;
    }

    const auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const auto& camera = world->get<components::CameraComponent>(camera_entity_);
    const float ndc_x = static_cast<float>((mouse_x / static_cast<double>(width)) * 2.0 - 1.0);
    const float ndc_y = static_cast<float>(1.0 - (mouse_y / static_cast<double>(height)) * 2.0);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float tan_half_fov = std::tan(camera.fov_y_degrees * 0.5f * kPi / 180.0f);
    const math::Vec3 camera_ray =
        math::normalize({ndc_x * aspect * tan_half_fov, ndc_y * tan_half_fov, -1.0f});
    const math::Vec3 ray_dir = math::normalize(math::rotateVec(camera_transform.getRotation(), camera_ray));
    const math::Vec3 ray_origin = camera_transform.getPosition();
    if (std::abs(ray_dir.y) < 0.0001f) {
      return false;
    }

    const float t = (kFloorY - ray_origin.y) / ray_dir.y;
    if (t < 0.0f) {
      return false;
    }

    out_point = add(ray_origin, scale(ray_dir, t));
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

    navigation::NavigationSystem::requestMoveTo(*world, player_entity_, floor_hit);

    if (world->isAlive(target_marker_entity_)) {
      world->get<components::TransformComponent>(target_marker_entity_).setPosition(
          toMarkerVisualPoint(floor_hit));
      auto& marker_mesh = world->get<components::MeshComponent>(target_marker_entity_);
      marker_mesh.visible = true;
      marker_mesh.shadow_visible = true;
    }
  }

  navigation::NavPath currentDebugPath() const {
    navigation::NavPath debug_path;
    if (!world->isAlive(player_entity_) ||
        !world->has<components::NavMeshAgentComponent>(player_entity_) ||
        !world->has<components::TransformComponent>(player_entity_)) {
      return debug_path;
    }

    const auto& agent = world->get<components::NavMeshAgentComponent>(player_entity_);
    if (agent.path.empty() || agent.next_waypoint >= agent.path.size()) {
      return debug_path;
    }

    const auto& transform = world->get<components::TransformComponent>(player_entity_);
    debug_path.status = navigation::NavStatus::Success;
    const math::Vec3 position = transform.getPosition();
    debug_path.points.push_back({position.x, position.y - agent.height_offset, position.z});
    for (size_t i = agent.next_waypoint; i < agent.path.size(); ++i) {
      debug_path.points.push_back(agent.path[i]);
    }
    return debug_path;
  }

  void drawNavigationDebug() {
    if (graphics == nullptr) {
      return;
    }

    navigation_system_.debugDraw(*world, *graphics, false);
    const navigation::NavPath debug_path = currentDebugPath();

    const float pulse = 0.25f + 0.15f * std::sin(path_time_ * 3.0f);
    for (const math::Vec3& point : debug_path.points) {
      const math::Vec3 top = add(point, {0.0f, 0.35f + pulse, 0.0f});
      graphics->drawLine(point, top, {1.0f, 0.82f, 0.08f, 1.0f}, false, 2.0f);
    }
  }

  void logNavigationDiagnostics(float dt,
                                double click_ms,
                                double navigation_ms,
                                double camera_ms,
                                double debug_draw_ms,
                                double on_update_ms) {
    if (!nav_diag_enabled_) {
      return;
    }

    const navigation::NavigationSystemStats& stats = navigation_system_.stats();
    if (stats.submitted_requests == logged_submitted_requests_ &&
        stats.completed_requests == logged_completed_requests_ &&
        stats.failed_requests == logged_failed_requests_ &&
        stats.stale_results == logged_stale_results_) {
      return;
    }

    logged_submitted_requests_ = stats.submitted_requests;
    logged_completed_requests_ = stats.completed_requests;
    logged_failed_requests_ = stats.failed_requests;
    logged_stale_results_ = stats.stale_results;

    spdlog::info(
        "Nav diag: main update={:.3f}ms rebuild={:.3f} submit={:.3f} move={:.3f} apply={:.3f}; "
        "worker queue={:.3f}ms solve={:.3f}ms cache_rebuilt={}; requests submitted={} completed={} "
        "failed={} stale={} pending={} last={} status={} points={}; "
        "example frame_dt={:.3f}ms on_update={:.3f} click={:.3f} nav_call={:.3f} camera={:.3f} debug_draw={:.3f}",
        stats.last_update_ms,
        stats.last_rebuild_ms,
        stats.last_submit_ms,
        stats.last_move_ms,
        stats.last_apply_ms,
        stats.last_worker_queue_wait_ms,
        stats.last_worker_solve_ms,
        stats.last_worker_cache_rebuilt,
        stats.submitted_requests,
        stats.completed_requests,
        stats.failed_requests,
        stats.stale_results,
        stats.pending_requests,
        stats.last_request_id,
        navigation::navStatusName(stats.last_path_status),
        stats.last_path_point_count,
        dt * 1000.0f,
        on_update_ms,
        click_ms,
        navigation_ms,
        camera_ms,
        debug_draw_ms);
  }

  renderer::MeshData marker_mesh_;
  navigation::NavigationSystem navigation_system_;
  math::Vec3 start_{0.0f, 0.0f, 0.0f};
  math::Vec3 camera_follow_offset_{0.0f, 10.0f, 18.0f};
  float camera_pitch_ = -0.5f;
  ecs::Entity world_entity_{};
  ecs::Entity nav_mesh_entity_{};
  ecs::Entity player_entity_{};
  ecs::Entity target_marker_entity_{};
  ecs::Entity camera_entity_{};
  bool nav_diag_enabled_ = false;
  float path_time_ = 0.0f;
  uint64_t logged_submitted_requests_ = 0;
  uint64_t logged_completed_requests_ = 0;
  uint64_t logged_failed_requests_ = 0;
  uint64_t logged_stale_results_ = 0;
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
