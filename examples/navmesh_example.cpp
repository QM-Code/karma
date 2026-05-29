#include "karma/karma.h"

#include <cmath>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr float kFloorY = 0.0f;

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

void appendFloorRect(renderer::MeshData& mesh, float x0, float z0, float x1, float z1) {
  appendQuad(mesh,
             {x0, kFloorY, z0},
             {x1, kFloorY, z0},
             {x1, kFloorY, z1},
             {x0, kFloorY, z1},
             {0.0f, 1.0f, 0.0f});
}

renderer::MeshData buildWalkableFloorMesh() {
  renderer::MeshData mesh;
  appendFloorRect(mesh, -6.0f, -5.0f, -1.2f, 5.0f);
  appendFloorRect(mesh, 1.2f, -5.0f, 6.0f, 5.0f);
  appendFloorRect(mesh, -1.2f, -5.0f, 1.2f, -1.2f);
  appendFloorRect(mesh, -1.2f, 1.2f, 1.2f, 5.0f);
  return mesh;
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

}  // namespace

class NavMeshSceneExample final : public app::GameInterface {
 public:
  void onStart() override {
    buildSceneMeshes();
    bakeNavMesh();
    spawnCamera();
    spawnLights();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    path_time_ += dt;
    drawNavigationDebug();
  }

  void onShutdown() override {}

 private:
  void buildSceneMeshes() {
    floor_mesh_ = buildWalkableFloorMesh();
    obstacle_mesh_ = buildBoxMesh({1.0f, 0.9f, 1.0f});
    marker_mesh_ = buildBoxMesh({0.18f, 0.18f, 0.18f});

    const renderer::MeshId floor_mesh_id =
        graphics != nullptr ? graphics->createMesh(floor_mesh_) : renderer::kInvalidMesh;
    const renderer::MeshId obstacle_mesh_id =
        graphics != nullptr ? graphics->createMesh(obstacle_mesh_) : renderer::kInvalidMesh;
    const renderer::MeshId start_marker_mesh_id =
        graphics != nullptr ? graphics->createMesh(marker_mesh_) : renderer::kInvalidMesh;
    const renderer::MeshId goal_marker_mesh_id =
        graphics != nullptr ? graphics->createMesh(marker_mesh_) : renderer::kInvalidMesh;

    const renderer::MaterialId floor_material =
        createMaterial(graphics, {0.34f, 0.39f, 0.36f, 1.0f});
    const renderer::MaterialId obstacle_material =
        createMaterial(graphics, {0.42f, 0.32f, 0.48f, 1.0f});
    const renderer::MaterialId start_material =
        createMaterial(graphics, {0.05f, 0.82f, 0.45f, 1.0f}, true);
    const renderer::MaterialId goal_material =
        createMaterial(graphics, {0.98f, 0.72f, 0.1f, 1.0f}, true);

    spawnMesh("Walkable Floor", floor_mesh_id, floor_material, {});
    spawnMesh("Center Blocker", obstacle_mesh_id, obstacle_material, {0.0f, 0.9f, 0.0f});
    spawnMesh("Start Marker", start_marker_mesh_id, start_material, add(start_, {0.0f, 0.28f, 0.0f}));
    spawnMesh("Goal Marker", goal_marker_mesh_id, goal_material, add(goal_, {0.0f, 0.28f, 0.0f}));
  }

  void bakeNavMesh() {
    navigation::NavMeshInputGeometry geometry;
    navigation::appendGeometry(geometry, floor_mesh_);

    navigation::NavMeshBuildConfig config;
    config.cell_size = 0.2f;
    config.cell_height = 0.1f;
    config.agent_height = 1.8f;
    config.agent_radius = 0.25f;
    config.agent_max_climb = 0.25f;
    config.region_min_size = 4.0f;
    config.region_merge_size = 8.0f;

    navigation::NavMeshBuildResult result;
    if (!nav_mesh_.build(geometry, config, &result)) {
      spdlog::error("Navmesh example bake failed: {} - {}",
                    navigation::navStatusName(result.status),
                    result.message);
      return;
    }

    navigation::NavQuery query(nav_mesh_);
    path_ = query.findPath(start_, goal_, {1.5f, 2.0f, 1.5f});
    spdlog::info("Navmesh example baked {} polygons from {} triangles; path {} with {} points",
                 result.polygon_count,
                 result.triangle_count,
                 navigation::navStatusName(path_.status),
                 path_.points.size());
  }

  void spawnMesh(const std::string& name,
                 renderer::MeshId mesh,
                 renderer::MaterialId material,
                 const math::Vec3& position) {
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
                          .visible = true,
                          .shadow_visible = true,
                      });
  }

  void spawnCamera() {
    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Camera");
    components::TransformComponent transform;
    transform.setPosition({0.0f, 8.5f, 12.5f});
    transform.setRotation(math::fromYawPitch(0.0f, -0.62f));
    world->add(camera, transform);
    world->add(camera, components::CameraComponent{
                          .near_clip = 0.05f,
                          .far_clip = 80.0f,
                          .is_primary = true,
                      });
  }

  void spawnLights() {
    const ecs::Entity sun = world->createEntity();
    world->setName(sun, "Sun Light");
    components::TransformComponent sun_transform;
    sun_transform.setRotation(math::fromYawPitch(0.7f, -0.9f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 0.96f, 0.88f, 1.0f},
                        .intensity = 1.1f,
                        .casts_shadows = true,
                        .shadow_extent = 18.0f,
                    });

    const ecs::Entity fill = world->createEntity();
    world->setName(fill, "Fill Light");
    components::TransformComponent fill_transform;
    fill_transform.setPosition({0.0f, 5.0f, 4.0f});
    world->add(fill, fill_transform);
    world->add(fill, components::LightComponent{
                         .type = components::LightComponent::Type::Point,
                         .color = {0.55f, 0.75f, 1.0f, 1.0f},
                         .intensity = 10.0f,
                         .range = 18.0f,
                     });
  }

  void drawNavigationDebug() {
    if (graphics == nullptr || !nav_mesh_.isValid()) {
      return;
    }

    nav_mesh_.debugDraw(*graphics, {0.05f, 0.95f, 0.48f, 1.0f}, false);
    navigation::NavQuery::debugDrawPath(*graphics, path_, {1.0f, 0.82f, 0.08f, 1.0f}, false);

    const float pulse = 0.25f + 0.15f * std::sin(path_time_ * 3.0f);
    for (const math::Vec3& point : path_.points) {
      const math::Vec3 top = add(point, {0.0f, 0.35f + pulse, 0.0f});
      graphics->drawLine(point, top, {1.0f, 0.82f, 0.08f, 1.0f}, false, 2.0f);
    }
  }

  renderer::MeshData floor_mesh_;
  renderer::MeshData obstacle_mesh_;
  renderer::MeshData marker_mesh_;
  navigation::NavMesh nav_mesh_;
  navigation::NavPath path_;
  math::Vec3 start_{-4.6f, 0.1f, 0.0f};
  math::Vec3 goal_{4.6f, 0.1f, 0.0f};
  float path_time_ = 0.0f;
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
  config.shadow_map_size = 1024;
  config.shadow_pcf_radius = 1;
  config.lighting_exposure = 1.05f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
