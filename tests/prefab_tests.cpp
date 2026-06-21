#include <chrono>
#include <cstdlib>
#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/assets.h"
#include "karma/prefabs.h"
#include "karma/visual.h"
#include "karma/visual.h"
#include "karma/visual.h"
#include "karma/rendering.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"
#include "karma/world.h"

namespace {

using Json = nlohmann::json;

#define KARMA_REQUIRE(expression)                                      \
  do {                                                                \
    if (!(expression)) {                                               \
      std::cerr << "Requirement failed: " << #expression << " at "   \
                << __FILE__ << ":" << __LINE__ << '\n';              \
      std::abort();                                                    \
    }                                                                 \
  } while (false)

std::filesystem::path makeTempDir() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_prefab_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream stream(path);
  stream << text;
}

Json readJson(const std::filesystem::path& path) {
  std::ifstream stream(path);
  Json json;
  stream >> json;
  return json;
}

bool nearly(float a, float b) {
  const float diff = a > b ? a - b : b - a;
  return diff < 0.0001f;
}

bool nearlyVec3(const karma::math::Vec3& a, const karma::math::Vec3& b) {
  return nearly(a.x, b.x) && nearly(a.y, b.y) && nearly(a.z, b.z);
}

void requireVec3VectorEquals(const std::vector<karma::math::Vec3>& actual,
                             const std::vector<karma::math::Vec3>& expected) {
  KARMA_REQUIRE(actual.size() == expected.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    KARMA_REQUIRE(nearlyVec3(actual[i], expected[i]));
  }
}

template <typename Shape>
const Shape& requireShape(const karma::components::ColliderComponent& component) {
  const Shape* shape = std::get_if<Shape>(&component.shape);
  KARMA_REQUIRE(shape != nullptr);
  return *shape;
}

void requireColliderEquals(const karma::components::ColliderComponent& actual,
                           const karma::components::ColliderComponent& expected) {
  KARMA_REQUIRE(actual.type == expected.type);
  KARMA_REQUIRE(actual.is_trigger == expected.is_trigger);
  KARMA_REQUIRE(actual.debug_draw == expected.debug_draw);
  KARMA_REQUIRE(karma::components::colliderTypeMatchesShape(actual));

  using Type = karma::components::ColliderShapeType;
  switch (expected.type) {
    case Type::Box: {
      const auto& actual_shape = requireShape<karma::components::BoxColliderShape>(actual);
      const auto& expected_shape = requireShape<karma::components::BoxColliderShape>(expected);
      KARMA_REQUIRE(nearlyVec3(actual_shape.center, expected_shape.center));
      KARMA_REQUIRE(nearlyVec3(actual_shape.half_extents, expected_shape.half_extents));
      break;
    }
    case Type::Sphere: {
      const auto& actual_shape = requireShape<karma::components::SphereColliderShape>(actual);
      const auto& expected_shape = requireShape<karma::components::SphereColliderShape>(expected);
      KARMA_REQUIRE(nearlyVec3(actual_shape.center, expected_shape.center));
      KARMA_REQUIRE(nearly(actual_shape.radius, expected_shape.radius));
      break;
    }
    case Type::Capsule: {
      const auto& actual_shape = requireShape<karma::components::CapsuleColliderShape>(actual);
      const auto& expected_shape =
          requireShape<karma::components::CapsuleColliderShape>(expected);
      KARMA_REQUIRE(nearlyVec3(actual_shape.center, expected_shape.center));
      KARMA_REQUIRE(nearly(actual_shape.radius, expected_shape.radius));
      KARMA_REQUIRE(nearly(actual_shape.height, expected_shape.height));
      break;
    }
    case Type::Cylinder: {
      const auto& actual_shape = requireShape<karma::components::CylinderColliderShape>(actual);
      const auto& expected_shape =
          requireShape<karma::components::CylinderColliderShape>(expected);
      KARMA_REQUIRE(nearlyVec3(actual_shape.center, expected_shape.center));
      KARMA_REQUIRE(nearly(actual_shape.radius, expected_shape.radius));
      KARMA_REQUIRE(nearly(actual_shape.height, expected_shape.height));
      KARMA_REQUIRE(nearly(actual_shape.convex_radius, expected_shape.convex_radius));
      break;
    }
    case Type::TaperedCapsule: {
      const auto& actual_shape =
          requireShape<karma::components::TaperedCapsuleColliderShape>(actual);
      const auto& expected_shape =
          requireShape<karma::components::TaperedCapsuleColliderShape>(expected);
      KARMA_REQUIRE(nearlyVec3(actual_shape.center, expected_shape.center));
      KARMA_REQUIRE(nearly(actual_shape.top_radius, expected_shape.top_radius));
      KARMA_REQUIRE(nearly(actual_shape.bottom_radius, expected_shape.bottom_radius));
      KARMA_REQUIRE(nearly(actual_shape.height, expected_shape.height));
      break;
    }
    case Type::ConvexHull: {
      const auto& actual_shape = requireShape<karma::components::ConvexHullColliderShape>(actual);
      const auto& expected_shape =
          requireShape<karma::components::ConvexHullColliderShape>(expected);
      KARMA_REQUIRE(nearlyVec3(actual_shape.center, expected_shape.center));
      requireVec3VectorEquals(actual_shape.points, expected_shape.points);
      KARMA_REQUIRE(nearly(actual_shape.convex_radius, expected_shape.convex_radius));
      break;
    }
    case Type::Triangle: {
      const auto& actual_shape = requireShape<karma::components::TriangleColliderShape>(actual);
      const auto& expected_shape =
          requireShape<karma::components::TriangleColliderShape>(expected);
      for (size_t i = 0; i < actual_shape.points.size(); ++i) {
        KARMA_REQUIRE(nearlyVec3(actual_shape.points[i], expected_shape.points[i]));
      }
      KARMA_REQUIRE(nearly(actual_shape.convex_radius, expected_shape.convex_radius));
      break;
    }
    case Type::HeightField: {
      const auto& actual_shape = requireShape<karma::components::HeightFieldColliderShape>(actual);
      const auto& expected_shape =
          requireShape<karma::components::HeightFieldColliderShape>(expected);
      KARMA_REQUIRE(actual_shape.samples.size() == expected_shape.samples.size());
      for (size_t i = 0; i < actual_shape.samples.size(); ++i) {
        KARMA_REQUIRE(nearly(actual_shape.samples[i], expected_shape.samples[i]));
      }
      KARMA_REQUIRE(actual_shape.sample_count == expected_shape.sample_count);
      KARMA_REQUIRE(nearlyVec3(actual_shape.offset, expected_shape.offset));
      KARMA_REQUIRE(nearlyVec3(actual_shape.scale, expected_shape.scale));
      KARMA_REQUIRE(actual_shape.block_size == expected_shape.block_size);
      KARMA_REQUIRE(actual_shape.bits_per_sample == expected_shape.bits_per_sample);
      break;
    }
    case Type::Mesh: {
      const auto& actual_shape = requireShape<karma::components::MeshColliderShape>(actual);
      const auto& expected_shape = requireShape<karma::components::MeshColliderShape>(expected);
      KARMA_REQUIRE(actual_shape.mesh_asset_key == expected_shape.mesh_asset_key);
      requireVec3VectorEquals(actual_shape.vertices, expected_shape.vertices);
      KARMA_REQUIRE(actual_shape.indices == expected_shape.indices);
      break;
    }
  }
}

Json validParticleEffectJson() {
  return Json{
      {"version", 3},
      {"emitters",
       Json::array({Json{
           {"texture", "test/texture"},
           {"playback",
            Json{{"enabled", true},
                 {"playing", true},
                 {"loop", true},
                 {"emit_burst_on_start", true},
                 {"local_space", false},
                 {"time_scale", 1.0f},
                 {"start_delay", 0.0f},
                 {"duration", 0.0f}}},
           {"render",
            Json{{"layer", 0},
                 {"depth_test", true},
                 {"blend_mode", "additive"},
                 {"alignment", "billboard"},
                 {"shading_mode", "standard"},
                 {"use_soft_mask", true},
                 {"soft_particle_distance", 0.25f},
                 {"distortion_strength", 0.0f},
                 {"fresnel_power", 4.0f},
                 {"fresnel_strength", 1.0f},
                 {"refraction_strength", 0.0f},
                 {"interior_glow", 0.0f}}},
           {"atlas",
            Json{{"columns", 1},
                 {"rows", 1},
                 {"frame_count", 1},
                 {"frame_width", 0},
                 {"frame_height", 0},
                 {"border_x", 0},
                 {"border_y", 0},
                 {"spacing_x", 0},
                 {"spacing_y", 0},
                 {"animation_fps", 0.0f},
                 {"animate_over_lifetime", false},
                 {"random_start_frame", false}}},
           {"emission",
            Json{{"max_particles", 8}, {"burst_count", 4}, {"seed", 7}, {"spawn_rate", 0.0f}}},
           {"lifetime", Json{{"min", 1.0f}, {"max", 1.0f}}},
           {"size",
            Json{{"start_min", 0.1f},
                 {"start_max", 0.1f},
                 {"end_min", 0.0f},
                 {"end_max", 0.0f},
                 {"curve_exponent", 1.0f}}},
           {"rotation",
            Json{{"initial_min", 0.0f},
                 {"initial_max", 0.0f},
                 {"angular_velocity_min", 0.0f},
                 {"angular_velocity_max", 0.0f}}},
           {"source",
            Json{{"shape", "box"},
                 {"box_extents", Json::array({0.0f, 0.0f, 0.0f})},
                 {"radius_min", 0.0f},
                 {"radius_max", 0.0f},
                 {"radial_speed_min", 0.0f},
                 {"radial_speed_max", 0.0f}}},
           {"motion",
            Json{{"velocity_min", Json::array({0.0f, 0.0f, 0.0f})},
                 {"velocity_max", Json::array({0.0f, 0.0f, 0.0f})},
                 {"acceleration", Json::array({0.0f, 0.0f, 0.0f})},
                 {"drag", 0.0f}}},
           {"collision",
            Json{{"ground", false},
                 {"ground_height", 0.0f},
                 {"bounce_damping", 0.35f},
                 {"friction", 0.25f},
                 {"rest_speed_threshold", 0.35f}}},
           {"color",
            Json{{"start", Json::array({1.0f, 1.0f, 1.0f, 1.0f})},
                 {"end", Json::array({1.0f, 1.0f, 1.0f, 0.0f})},
                 {"alpha_curve_exponent", 1.0f}}},
       }})},
  };
}

std::string simplePrefabJson() {
  return R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Root",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    }
  ]
})";
}

std::filesystem::path findRepoRoot() {
  std::vector<std::filesystem::path> starts{std::filesystem::current_path()};
  std::filesystem::path source_path = std::filesystem::path(__FILE__);
  if (source_path.is_absolute()) {
    starts.push_back(source_path.parent_path());
  }

  for (std::filesystem::path start : starts) {
    for (std::filesystem::path cursor = start; !cursor.empty(); cursor = cursor.parent_path()) {
      if (std::filesystem::exists(cursor / "examples/assets/prefabs/explosion/prefab.json")) {
        return cursor;
      }
      if (cursor == cursor.parent_path()) {
        break;
      }
    }
  }
  return {};
}

void testSaveLoadSingleEntity(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "Crate");
  world.add(root, karma::components::TransformComponent{
                      {1.0f, 2.0f, 3.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f},
                      {2.0f, 2.0f, 2.0f},
                  });
  world.add(root, karma::components::MeshComponent{
                      .mesh_asset_key = "assets/crate",
                      .materials = {karma::components::MeshMaterialAssignment{
                          .slot = 0,
                          .material_key = "crate",
                      }},
                      .visible = true,
                      .shadow_visible = false,
                  });
  world.add(root, karma::components::LightComponent{
                      .type = karma::components::LightComponent::Type::Point,
                      .color = {0.5f, 0.6f, 0.7f, 1.0f},
                      .intensity = 4.0f,
                      .range = 12.0f,
                  });

  const std::filesystem::path path = dir / "single.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  const Json saved = readJson(path);
  KARMA_REQUIRE(saved["nodes"][0]["components"]["MeshComponent"]["mesh_asset_key"] == "assets/crate");
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("mesh_key"));
  KARMA_REQUIRE(saved["nodes"][0]["components"]["MeshComponent"]["materials"].is_array());
  KARMA_REQUIRE(saved["nodes"][0]["components"]["MeshComponent"]["materials"][0]["slot"] == 0);
  KARMA_REQUIRE(saved["nodes"][0]["components"]["MeshComponent"]["materials"][0]["material_key"] == "crate");
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("material_key"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("texture_key"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("mesh_id"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("material_id"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("owns_mesh_id"));
  KARMA_REQUIRE(!saved["nodes"][0]["components"]["MeshComponent"].contains("owns_material_id"));

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->valid());
  KARMA_REQUIRE(instance->root_scene_node != karma::world::Node::kInvalidId);
  KARMA_REQUIRE(loaded_world.has<karma::components::TagComponent>(instance->root));
  KARMA_REQUIRE(loaded_world.get<karma::components::TagComponent>(instance->root).name == "Crate");

  const auto& transform =
      loaded_world.get<karma::components::TransformComponent>(instance->root);
  KARMA_REQUIRE(nearly(transform.getPosition().x, 1.0f));
  KARMA_REQUIRE(nearly(transform.getPosition().y, 2.0f));
  KARMA_REQUIRE(nearly(transform.getPosition().z, 3.0f));

  const auto& mesh = loaded_world.get<karma::components::MeshComponent>(instance->root);
  KARMA_REQUIRE(mesh.mesh_asset_key == "assets/crate");
  KARMA_REQUIRE(mesh.materials.size() == 1);
  KARMA_REQUIRE(mesh.materials[0].slot == 0);
  KARMA_REQUIRE(mesh.materials[0].material_key == "crate");
  KARMA_REQUIRE(!mesh.shadow_visible);

  const auto& light = loaded_world.get<karma::components::LightComponent>(instance->root);
  KARMA_REQUIRE(light.type == karma::components::LightComponent::Type::Point);
  KARMA_REQUIRE(nearly(light.color.r, 0.5f));
  KARMA_REQUIRE(nearly(light.intensity, 4.0f));
  KARMA_REQUIRE(nearly(light.range, 12.0f));
}

void testInstancedMeshLodPrefabRoundTrip(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "Grass Chunk");
  world.add(root, karma::components::InstancedMeshComponent{
                      .mesh_asset_key = "assets/grass_cluster",
                      .materials = {karma::components::MeshMaterialAssignment{
                          .slot = 0,
                          .material_key = "grass_near",
                      }},
                      .lods = {karma::components::InstancedMeshLodLevel{
                          .start_distance = 28.0f,
                          .mesh_asset_key = "assets/grass_billboard",
                          .materials = {karma::components::MeshMaterialAssignment{
                              .slot = 0,
                              .material_key = "grass_far",
                          }},
                          .render_mode = karma::rendering::InstanceLodRenderMode::UprightBillboard,
                          .shadow_visible = false,
                      }},
                      .gpu_layout = karma::rendering::InstanceGpuLayout::PositionYawScaleParams,
                      .planar_instances = {karma::components::PlanarMeshInstance{
                          .position = {1.0f, 0.0f, 2.0f},
                          .yaw_radians = 0.5f,
                          .scale = {1.0f, 2.0f, 1.0f},
                      }},
                      .instance_revision = 3u,
                      .dynamic = false,
                      .visible = true,
                      .shadow_visible = false,
                  });

  const std::filesystem::path path = dir / "instanced_lod.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  const Json saved = readJson(path);
  const Json& serialized = saved["nodes"][0]["components"]["InstancedMeshComponent"];
  KARMA_REQUIRE(serialized["lods"].is_array());
  KARMA_REQUIRE(serialized["lods"].size() == 1);
  KARMA_REQUIRE(serialized["lods"][0]["start_distance"] == 28.0f);
  KARMA_REQUIRE(serialized["lods"][0]["mesh_asset_key"] == "assets/grass_billboard");
  KARMA_REQUIRE(serialized["lods"][0]["render_mode"] == "upright_billboard");
  KARMA_REQUIRE(serialized["lods"][0]["materials"][0]["material_key"] == "grass_far");

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  const auto& loaded =
      loaded_world.get<karma::components::InstancedMeshComponent>(instance->root);
  KARMA_REQUIRE(loaded.lods.size() == 1);
  KARMA_REQUIRE(nearly(loaded.lods[0].start_distance, 28.0f));
  KARMA_REQUIRE(loaded.lods[0].mesh_asset_key == "assets/grass_billboard");
  KARMA_REQUIRE(loaded.lods[0].materials.size() == 1);
  KARMA_REQUIRE(loaded.lods[0].materials[0].material_key == "grass_far");
  KARMA_REQUIRE(loaded.lods[0].render_mode ==
                karma::rendering::InstanceLodRenderMode::UprightBillboard);
  KARMA_REQUIRE(!loaded.lods[0].shadow_visible);

  Json legacy = saved;
  legacy["nodes"][0]["components"]["InstancedMeshComponent"].erase("lods");
  const std::filesystem::path legacy_path = dir / "instanced_lod_legacy.json";
  writeText(legacy_path, legacy.dump(2));
  karma::world::World legacy_world;
  karma::world::Scene legacy_scene;
  const auto legacy_instance =
      karma::prefabs::instantiatePrefab(legacy_world, legacy_scene, legacy_path);
  KARMA_REQUIRE(legacy_instance.has_value());
  const auto& legacy_component =
      legacy_world.get<karma::components::InstancedMeshComponent>(legacy_instance->root);
  KARMA_REQUIRE(legacy_component.lods.empty());
}

void testColliderComponentPrefabRoundTrips(const std::filesystem::path& dir) {
  struct ColliderCase {
    std::string name;
    karma::components::ColliderComponent component;
  };

  std::vector<ColliderCase> cases;
  cases.push_back({"box",
                   karma::components::ColliderComponent::box(
                       karma::components::BoxColliderShape{
                           .center = {0.1f, 0.2f, 0.3f},
                           .half_extents = {1.0f, 2.0f, 3.0f},
                       },
                       true,
                       false)});
  cases.push_back({"sphere",
                   karma::components::ColliderComponent::sphere(
                       karma::components::SphereColliderShape{
                           .center = {0.4f, 0.5f, 0.6f},
                           .radius = 1.25f,
                       },
                       false,
                       true)});
  cases.push_back({"capsule",
                   karma::components::ColliderComponent::capsule(
                       karma::components::CapsuleColliderShape{
                           .center = {0.7f, 0.8f, 0.9f},
                           .radius = 0.35f,
                           .height = 2.4f,
                       },
                       true,
                       true)});
  cases.push_back({"cylinder",
                   karma::components::ColliderComponent::cylinder(
                       karma::components::CylinderColliderShape{
                           .center = {1.0f, 1.1f, 1.2f},
                           .radius = 0.75f,
                           .height = 3.5f,
                           .convex_radius = 0.05f,
                       })});
  cases.push_back({"tapered_capsule",
                   karma::components::ColliderComponent::taperedCapsule(
                       karma::components::TaperedCapsuleColliderShape{
                           .center = {1.3f, 1.4f, 1.5f},
                           .top_radius = 0.2f,
                           .bottom_radius = 0.45f,
                           .height = 2.2f,
                       })});
  cases.push_back({"convex_hull",
                   karma::components::ColliderComponent::convexHull(
                       karma::components::ConvexHullColliderShape{
                           .center = {1.6f, 1.7f, 1.8f},
                           .points = {{0.0f, 0.0f, 0.0f},
                                      {1.0f, 0.0f, 0.0f},
                                      {0.0f, 1.0f, 0.0f},
                                      {0.0f, 0.0f, 1.0f}},
                           .convex_radius = 0.02f,
                       })});
  karma::components::TriangleColliderShape triangle{};
  triangle.points = {karma::math::Vec3{0.0f, 0.0f, 0.0f},
                     karma::math::Vec3{1.0f, 0.0f, 0.0f},
                     karma::math::Vec3{0.0f, 1.0f, 0.0f}};
  triangle.convex_radius = 0.01f;
  cases.push_back({"triangle", karma::components::ColliderComponent::triangle(triangle)});
  cases.push_back({"height_field",
                   karma::components::ColliderComponent::heightField(
                       karma::components::HeightFieldColliderShape{
                           .samples = {0.0f, 0.25f, 0.5f, 0.75f},
                           .sample_count = 2,
                           .offset = {-1.0f, 0.0f, -1.0f},
                           .scale = {2.0f, 0.5f, 2.0f},
                           .block_size = 4,
                           .bits_per_sample = 16,
                       })});
  cases.push_back({"mesh",
                   karma::components::ColliderComponent::mesh(
                       karma::components::MeshColliderShape{
                           .mesh_asset_key = "assets/collision_mesh",
                           .vertices = {{0.0f, 0.0f, 0.0f},
                                        {1.0f, 0.0f, 0.0f},
                                        {0.0f, 1.0f, 0.0f}},
                           .indices = {0, 1, 2},
                       })});

  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  const auto root_node = scene.createNode(root);
  world.setName(root, "ColliderRoot");
  world.add(root, karma::components::TransformComponent{});

  for (const ColliderCase& collider_case : cases) {
    const karma::world::Entity entity = world.createEntity();
    const auto node = scene.createNode(entity);
    scene.reparent(node, root_node);
    world.setName(entity, collider_case.name);
    world.add(entity, karma::components::TransformComponent{});
    world.add(entity, collider_case.component);
  }

  const std::filesystem::path path = dir / "colliders.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  const Json saved = readJson(path);
  size_t serialized_colliders = 0;
  for (const Json& node : saved["nodes"]) {
    const Json& components = node["components"];
    KARMA_REQUIRE(!components.contains("BoxColliderComponent"));
    KARMA_REQUIRE(!components.contains("SphereColliderComponent"));
    KARMA_REQUIRE(!components.contains("CapsuleColliderComponent"));
    KARMA_REQUIRE(!components.contains("MeshColliderComponent"));
    if (components.contains("ColliderComponent")) {
      ++serialized_colliders;
      KARMA_REQUIRE(components["ColliderComponent"].contains("type"));
      KARMA_REQUIRE(components["ColliderComponent"].contains("is_trigger"));
      KARMA_REQUIRE(components["ColliderComponent"].contains("debug_draw"));
      KARMA_REQUIRE(components["ColliderComponent"].contains("shape"));
    }
  }
  KARMA_REQUIRE(serialized_colliders == cases.size());

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  for (const ColliderCase& collider_case : cases) {
    const karma::world::Entity entity = instance->find(collider_case.name);
    KARMA_REQUIRE(entity.isValid());
    KARMA_REQUIRE(loaded_world.has<karma::components::ColliderComponent>(entity));
    requireColliderEquals(loaded_world.get<karma::components::ColliderComponent>(entity),
                          collider_case.component);
  }
}

void testHierarchyRoundTrip(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  const karma::world::Entity child = world.createEntity();
  const auto root_node = scene.createNode(root);
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  world.setName(root, "Root");
  world.add(root, karma::components::TransformComponent{});
  world.setName(child, "Child");
  world.add(child,
            karma::components::TransformComponent{
                {2.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f},
                {1.0f, 1.0f, 1.0f},
            });

  const std::filesystem::path path = dir / "hierarchy.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.root_transform.setPosition({10.0f, 0.0f, 0.0f});
  const auto instance =
      karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path, desc);
  KARMA_REQUIRE(instance.has_value());

  const karma::world::Entity loaded_child = instance->find("Child");
  KARMA_REQUIRE(loaded_child.isValid());
  const auto loaded_root_node = loaded_scene.findNode(instance->root);
  const auto loaded_child_node = loaded_scene.findNode(loaded_child);
  KARMA_REQUIRE(loaded_scene.isAlive(loaded_root_node));
  KARMA_REQUIRE(loaded_scene.isAlive(loaded_child_node));
  KARMA_REQUIRE(loaded_scene.get(loaded_child_node).parent == loaded_root_node);

  const auto& child_transform =
      loaded_world.get<karma::components::TransformComponent>(loaded_child);
  KARMA_REQUIRE(nearly(child_transform.localPosition().x, 2.0f));
  KARMA_REQUIRE(nearly(child_transform.getPosition().x, 12.0f));
}

void testUnknownComponentFails(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "unknown.json";
  writeText(path,
            R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "UnknownCarrier",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
        "OptionalFeature": { "enabled": true }
      }
    }
  ]
})");

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path);
  KARMA_REQUIRE(!instance.has_value());
}

void testMalformedAndInvalidPayloads(const std::filesystem::path& dir) {
  const std::filesystem::path malformed = dir / "malformed.json";
  writeText(malformed, "{ invalid json");
  karma::world::World world_a;
  karma::world::Scene scene_a;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world_a, scene_a, malformed).has_value());
  KARMA_REQUIRE(world_a.entities().empty());

  const std::filesystem::path invalid = dir / "invalid_component.json";
  writeText(invalid,
            R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Broken",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    }
  ]
})");
  karma::world::World world_b;
  karma::world::Scene scene_b;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world_b, scene_b, invalid).has_value());
  KARMA_REQUIRE(world_b.entities().empty());
}

std::string volumetricPrefabJson(const std::string& component_payload) {
  return R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Volume",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
        "VolumetricComponent": )" + component_payload + R"(
      }
    }
  ]
})";
}

void testVolumetricComponentPrefabRoundTrip(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "Volume");
  world.add(root, karma::components::TransformComponent{});
  karma::components::VolumetricComponent authored{};
  authored.shape = karma::components::VolumetricShape::Capsule;
  authored.color = {0.24f, 0.56f, 1.0f, 1.0f};
  authored.emissive_color = {0.8f, 1.5f, 3.0f, 1.0f};
  authored.density = 1.75f;
  authored.center_opacity = 0.7f;
  authored.scattering = 1.2f;
  authored.anisotropy = 0.25f;
  authored.absorption = 0.04f;
  authored.distortion_strength = 0.2f;
  authored.noise_strength = 0.45f;
  authored.radius = 0.35f;
  authored.capsule_half_length = 2.25f;
  authored.scale_with_transform = false;
  authored.visible = true;
  authored.overlay_depth = 0.16f;
  world.add(root, authored);

  const std::filesystem::path path = dir / "volumetric_round_trip.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  const Json saved = readJson(path);
  const Json& components = saved["nodes"][0]["components"];
  KARMA_REQUIRE(components.contains("VolumetricComponent"));
  KARMA_REQUIRE(!components.contains("VolumeSphereComponent"));
  KARMA_REQUIRE(components["VolumetricComponent"]["shape"] == "capsule");

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(loaded_world.has<karma::components::VolumetricComponent>(instance->root));
  const auto& loaded =
      loaded_world.get<karma::components::VolumetricComponent>(instance->root);
  KARMA_REQUIRE(loaded.shape == karma::components::VolumetricShape::Capsule);
  KARMA_REQUIRE(nearly(loaded.radius, 0.35f));
  KARMA_REQUIRE(nearly(loaded.capsule_half_length, 2.25f));
  KARMA_REQUIRE(nearly(loaded.density, 1.75f));
  KARMA_REQUIRE(nearly(loaded.scattering, 1.2f));
  KARMA_REQUIRE(nearly(loaded.anisotropy, 0.25f));
  KARMA_REQUIRE(nearly(loaded.absorption, 0.04f));
}

void testVolumetricComponentValidation(const std::filesystem::path& dir) {
  const std::string valid = R"({
          "shape": "sphere",
          "color": [0.18, 0.82, 1.0, 1.0],
          "emissive_color": [0.0, 0.0, 0.0, 1.0],
          "center_opacity": 0.62,
          "radius": 2.0,
          "capsule_half_length": 1.0
        })";
  const std::filesystem::path derived_path = dir / "volumetric_derived_density.json";
  writeText(derived_path, volumetricPrefabJson(valid));
  karma::world::World derived_world;
  karma::world::Scene derived_scene;
  const auto derived =
      karma::prefabs::instantiatePrefab(derived_world, derived_scene, derived_path);
  KARMA_REQUIRE(derived.has_value());
  const auto& volume =
      derived_world.get<karma::components::VolumetricComponent>(derived->root);
  KARMA_REQUIRE(volume.shape == karma::components::VolumetricShape::Sphere);
  KARMA_REQUIRE(volume.density > 0.0f);

  const std::vector<std::pair<std::string, std::string>> invalid_cases{
      {"invalid_shape", R"({
          "shape": "tube",
          "radius": 1.0,
          "capsule_half_length": 1.0
        })"},
      {"negative_radius", R"({
          "shape": "sphere",
          "radius": -1.0,
          "capsule_half_length": 1.0
        })"},
      {"negative_length", R"({
          "shape": "capsule",
          "radius": 1.0,
          "capsule_half_length": -0.1
        })"},
  };
  for (const auto& [name, payload] : invalid_cases) {
    const std::filesystem::path path = dir / (name + ".json");
    writeText(path, volumetricPrefabJson(payload));
    karma::world::World invalid_world;
    karma::world::Scene invalid_scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(invalid_world, invalid_scene, path)
                       .has_value());
    KARMA_REQUIRE(invalid_world.entities().empty());
  }
}

void testTerrainComponentPrefabRoundTrip(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "Terrain");
  world.add(root, karma::components::TransformComponent{});

  karma::components::TerrainComponent authored{};
  authored.source = karma::components::TerrainSourceType::ImageTileDirectory;
  authored.tile_directory = "terrain_tiles";
  authored.height_pattern = "dem_{x}_{z}.r32";
  authored.color_pattern = "ortho_{x}_{z}.png";
  authored.control_pattern = "splat_{x}_{y}.png";
  authored.height_format = karma::components::TerrainHeightFormat::R32Float;
  authored.raw_width = 513u;
  authored.raw_height = 513u;
  authored.flip_y = true;
  authored.tile_index_base = 1;
  authored.material_layers.push_back(karma::components::TerrainMaterialLayer{
      .name = "grass",
      .material_key = "terrain/grass",
      .albedo_image = "terrain/materials/grass_albedo.png",
      .normal_image = "terrain/materials/grass_normal.png",
      .roughness_image = "terrain/materials/grass_roughness.png",
      .uv_scale = 24.0f,
  });
  authored.data_maps.push_back(karma::components::TerrainDataMapBinding{
      .name = "flow",
      .kind = karma::components::TerrainDataMapKind::Flow,
      .pattern = "flow_{x}_{y}.png",
      .channel = 0u,
  });
  authored.tile_size = 750.0f;
  authored.tile_resolution = 129u;
  authored.origin_tile_x = -3;
  authored.origin_tile_z = 7;
  authored.height_scale = 220.0f;
  authored.height_offset = -12.0f;
  authored.view_distance = 2500.0f;
  authored.base_patch_size = 8u;
  authored.tessellation_factor = 24.0f;
  authored.target_tessellated_edge_size = 12.5f;
  authored.layer = 2u;
  authored.visible = false;
  authored.cpu_fallback_enabled = false;
  world.add(root, authored);

  const std::filesystem::path path = dir / "terrain.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  const Json saved = readJson(path);
  const Json& terrain_json = saved["nodes"][0]["components"]["TerrainComponent"];
  KARMA_REQUIRE(terrain_json["source"] == "image_tile_directory");
  KARMA_REQUIRE(terrain_json["height_pattern"] == "dem_{x}_{z}.r32");
  KARMA_REQUIRE(terrain_json["height_format"] == "r32_float");
  KARMA_REQUIRE(terrain_json["material_layers"].size() == 1u);
  KARMA_REQUIRE(terrain_json["material_layers"][0]["material_key"] == "terrain/grass");
  KARMA_REQUIRE(terrain_json["data_maps"].size() == 1u);
  KARMA_REQUIRE(terrain_json["layer"] == 2u);

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(loaded_world.has<karma::components::TerrainComponent>(instance->root));
  const auto& loaded =
      loaded_world.get<karma::components::TerrainComponent>(instance->root);
  KARMA_REQUIRE(loaded.source == karma::components::TerrainSourceType::ImageTileDirectory);
  KARMA_REQUIRE(loaded.tile_directory == std::filesystem::path("terrain_tiles"));
  KARMA_REQUIRE(loaded.height_pattern == "dem_{x}_{z}.r32");
  KARMA_REQUIRE(loaded.color_pattern == "ortho_{x}_{z}.png");
  KARMA_REQUIRE(loaded.control_pattern == "splat_{x}_{y}.png");
  KARMA_REQUIRE(loaded.height_format == karma::components::TerrainHeightFormat::R32Float);
  KARMA_REQUIRE(loaded.raw_width == 513u);
  KARMA_REQUIRE(loaded.raw_height == 513u);
  KARMA_REQUIRE(loaded.flip_y);
  KARMA_REQUIRE(loaded.tile_index_base == 1);
  KARMA_REQUIRE(loaded.material_layers.size() == 1u);
  KARMA_REQUIRE(loaded.material_layers[0].name == "grass");
  KARMA_REQUIRE(loaded.material_layers[0].material_key == "terrain/grass");
  KARMA_REQUIRE(loaded.material_layers[0].albedo_image ==
                std::filesystem::path("terrain/materials/grass_albedo.png"));
  KARMA_REQUIRE(nearly(loaded.material_layers[0].uv_scale, 24.0f));
  KARMA_REQUIRE(loaded.data_maps.size() == 1u);
  KARMA_REQUIRE(loaded.data_maps[0].kind ==
                karma::components::TerrainDataMapKind::Flow);
  KARMA_REQUIRE(loaded.data_maps[0].pattern == "flow_{x}_{y}.png");
  KARMA_REQUIRE(nearly(loaded.tile_size, 750.0f));
  KARMA_REQUIRE(loaded.tile_resolution == 129u);
  KARMA_REQUIRE(loaded.origin_tile_x == -3);
  KARMA_REQUIRE(loaded.origin_tile_z == 7);
  KARMA_REQUIRE(nearly(loaded.height_scale, 220.0f));
  KARMA_REQUIRE(nearly(loaded.height_offset, -12.0f));
  KARMA_REQUIRE(nearly(loaded.view_distance, 2500.0f));
  KARMA_REQUIRE(loaded.base_patch_size == 8u);
  KARMA_REQUIRE(nearly(loaded.tessellation_factor, 24.0f));
  KARMA_REQUIRE(nearly(loaded.target_tessellated_edge_size, 12.5f));
  KARMA_REQUIRE(loaded.layer == 2u);
  KARMA_REQUIRE(!loaded.visible);
  KARMA_REQUIRE(!loaded.cpu_fallback_enabled);
}

void testSingleImageTerrainComponentPrefabRoundTrip(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "FixedTerrain");
  world.add(root, karma::components::TransformComponent{});

  karma::components::TerrainComponent authored{};
  authored.source = karma::components::TerrainSourceType::SingleImage;
  authored.height_image = "terrain/fixed_height.png";
  authored.heatmap_image = "terrain/fixed_heatmap.png";
  authored.color_image = "terrain/fixed_color.png";
  authored.control_image = "terrain/fixed_splat.png";
  authored.terrain_size = 2048.0f;
  authored.tile_resolution = 513u;
  authored.origin_tile_x = 5;
  authored.origin_tile_z = -8;
  authored.height_scale = 90.0f;
  authored.height_offset = 3.5f;
  authored.layer = 3u;
  world.add(root, authored);
  world.add(root, karma::components::ColliderComponent{
                      .is_trigger = true,
                      .debug_draw = true,
                  });

  const std::filesystem::path path = dir / "terrain_single_image.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  const Json saved = readJson(path);
  const Json& terrain_json = saved["nodes"][0]["components"]["TerrainComponent"];
  KARMA_REQUIRE(terrain_json["source"] == "single_image");
  KARMA_REQUIRE(terrain_json["height_image"] == "terrain/fixed_height.png");
  KARMA_REQUIRE(terrain_json["heatmap_image"] == "terrain/fixed_heatmap.png");
  KARMA_REQUIRE(terrain_json["color_image"] == "terrain/fixed_color.png");
  KARMA_REQUIRE(terrain_json["control_image"] == "terrain/fixed_splat.png");
  KARMA_REQUIRE(nearly(terrain_json["terrain_size"].get<float>(), 2048.0f));
  const Json& collider_json = saved["nodes"][0]["components"]["ColliderComponent"];
  KARMA_REQUIRE(collider_json["is_trigger"] == true);
  KARMA_REQUIRE(collider_json["debug_draw"] == true);

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(loaded_world.has<karma::components::TerrainComponent>(instance->root));
  const auto& loaded =
      loaded_world.get<karma::components::TerrainComponent>(instance->root);
  KARMA_REQUIRE(loaded.source == karma::components::TerrainSourceType::SingleImage);
  KARMA_REQUIRE(loaded.height_image == std::filesystem::path("terrain/fixed_height.png"));
  KARMA_REQUIRE(loaded.heatmap_image == std::filesystem::path("terrain/fixed_heatmap.png"));
  KARMA_REQUIRE(loaded.color_image == std::filesystem::path("terrain/fixed_color.png"));
  KARMA_REQUIRE(loaded.control_image == std::filesystem::path("terrain/fixed_splat.png"));
  KARMA_REQUIRE(nearly(loaded.terrain_size, 2048.0f));
  KARMA_REQUIRE(loaded.tile_resolution == 513u);
  KARMA_REQUIRE(loaded.origin_tile_x == 5);
  KARMA_REQUIRE(loaded.origin_tile_z == -8);
  KARMA_REQUIRE(nearly(loaded.height_scale, 90.0f));
  KARMA_REQUIRE(nearly(loaded.height_offset, 3.5f));
  KARMA_REQUIRE(loaded.layer == 3u);
  KARMA_REQUIRE(loaded_world.has<karma::components::ColliderComponent>(instance->root));
  const auto& collider =
      loaded_world.get<karma::components::ColliderComponent>(instance->root);
  KARMA_REQUIRE(collider.is_trigger);
  KARMA_REQUIRE(collider.debug_draw);
}

void testMigratedPrefabAssetsDoNotUseLegacyComponentNames() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path prefab_root = repo_root / "examples/assets/prefabs";
  const std::array<std::string, 7> legacy_names{
      "LocalTransformComponent",
      "BoxColliderComponent",
      "SphereColliderComponent",
      "CapsuleColliderComponent",
      "MeshColliderComponent",
      "CharacterControllerComponent",
      "VolumeSphereComponent",
  };
  for (const auto& entry : std::filesystem::recursive_directory_iterator(prefab_root)) {
    if (!entry.is_regular_file() || entry.path().filename() != "prefab.json") {
      continue;
    }
    std::ifstream stream(entry.path());
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    for (const std::string& legacy_name : legacy_names) {
      KARMA_REQUIRE(text.find(legacy_name) == std::string::npos);
    }
  }
}

void testDestroyPrefab(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "destroy.json";
  writeText(path,
            R"({
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Root",
      "parent": null,
      "components": {
        "TransformComponent": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    },
    {
      "id": 1,
      "name": "Child",
      "parent": 0,
      "components": {
        "TransformComponent": { "position": [1, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
      }
    }
  ]
})");

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path);
  KARMA_REQUIRE(instance.has_value());
  const karma::world::Entity root = instance->root;
  const karma::world::Entity child = instance->find("Child");
  KARMA_REQUIRE(world.isAlive(root));
  KARMA_REQUIRE(world.isAlive(child));
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, root));
  KARMA_REQUIRE(!world.isAlive(root));
  KARMA_REQUIRE(!world.isAlive(child));
  KARMA_REQUIRE(scene.findNode(root) == karma::world::Node::kInvalidId);
  KARMA_REQUIRE(scene.findNode(child) == karma::world::Node::kInvalidId);
}

void testMissingAssetPackageKeepsPrefabLoad(const std::filesystem::path& dir) {
  const std::filesystem::path prefab_dir = dir / "missing_package";
  std::filesystem::create_directories(prefab_dir);
  writeText(prefab_dir / "prefab.json", simplePrefabJson());

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(world.isAlive(instance->root));
}

void testAssetPackageParsingSuccessAndFailure(const std::filesystem::path& dir) {
  {
    const std::filesystem::path prefab_dir = dir / "package_success";
    std::filesystem::create_directories(prefab_dir / "particles");
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "particles/test.kpeffect", validParticleEffectJson().dump(2));
    writeText(prefab_dir / "assets.package.json",
              R"({
  "version": 1,
  "assets": [
    { "type": "particle_effect", "key": "test/effect", "path": "particles/test.kpeffect" }
  ]
})");

    karma::assets::AssetRegistry assets;
    karma::prefabs::bindPrefabAssetRegistry(&assets);
    karma::world::World world;
    karma::world::Scene scene;
    const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
    KARMA_REQUIRE(instance.has_value());
    KARMA_REQUIRE(assets.findParticleEffect("test/effect") != nullptr);
    karma::prefabs::clearPrefabAssetPackages();
    KARMA_REQUIRE(assets.findParticleEffect("test/effect") == nullptr);
  }

  {
    const std::filesystem::path prefab_dir = dir / "package_failure";
    std::filesystem::create_directories(prefab_dir);
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "assets.package.json",
              R"({ "version": 1, "assets": { "key": "bad" } })");

    karma::world::World world;
    karma::world::Scene scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    KARMA_REQUIRE(world.entities().empty());
  }
}

void testAssetPackageMissingRegistryAndResourceFailure(const std::filesystem::path& dir) {
  {
    const std::filesystem::path prefab_dir = dir / "missing_registry";
    std::filesystem::create_directories(prefab_dir);
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "assets.package.json",
              R"({
  "version": 1,
  "assets": [
    { "type": "texture_rgba8", "key": "missing/texture", "path": "textures/missing.png" }
  ]
})");

    karma::prefabs::clearPrefabAssetPackages();
    karma::world::World world;
    karma::world::Scene scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    KARMA_REQUIRE(world.entities().empty());
  }

  {
    const std::filesystem::path prefab_dir = dir / "bad_effect";
    std::filesystem::create_directories(prefab_dir);
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "assets.package.json",
              R"({
  "version": 1,
  "assets": [
    { "type": "particle_effect", "key": "bad/effect", "path": "particles/missing.kpeffect" }
  ]
})");

    karma::assets::AssetRegistry assets;
    karma::prefabs::bindPrefabAssetRegistry(&assets);
    karma::world::World world;
    karma::world::Scene scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(world, scene, prefab_dir).has_value());
    KARMA_REQUIRE(world.entities().empty());
    karma::prefabs::clearPrefabAssetPackages();
  }
}

void testParticleEffectParserV3() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path valid = dir / "valid.kpeffect";
  writeText(valid, validParticleEffectJson().dump(2));

  karma::visual::particles::ParticleEffectAsset effect{};
  KARMA_REQUIRE(karma::visual::particles::loadParticleEffectAsset(valid, effect));
  const auto* primary = effect.primaryEmitter();
  KARMA_REQUIRE(primary != nullptr);
  KARMA_REQUIRE(primary->texture_key == "test/texture");
  KARMA_REQUIRE(primary->emitter.max_particles == 8u);
  KARMA_REQUIRE(primary->emitter.source_shape ==
                karma::components::ParticleSourceShape::Box);

  Json unknown = validParticleEffectJson();
  unknown["emitters"][0]["render"]["unknown"] = 1;
  const std::filesystem::path unknown_path = dir / "unknown.kpeffect";
  writeText(unknown_path, unknown.dump(2));
  karma::visual::particles::ParticleEffectAsset invalid_effect{};
  KARMA_REQUIRE(!karma::visual::particles::loadParticleEffectAsset(unknown_path, invalid_effect));

  Json invalid_enum = validParticleEffectJson();
  invalid_enum["emitters"][0]["render"]["blend_mode"] = "multiply";
  const std::filesystem::path invalid_enum_path = dir / "invalid_enum.kpeffect";
  writeText(invalid_enum_path, invalid_enum.dump(2));
  KARMA_REQUIRE(!karma::visual::particles::loadParticleEffectAsset(invalid_enum_path, invalid_effect));

  Json missing_block = validParticleEffectJson();
  missing_block["emitters"][0].erase("motion");
  const std::filesystem::path missing_block_path = dir / "missing_block.kpeffect";
  writeText(missing_block_path, missing_block.dump(2));
  KARMA_REQUIRE(!karma::visual::particles::loadParticleEffectAsset(missing_block_path, invalid_effect));

  Json missing_source = validParticleEffectJson();
  missing_source["emitters"][0].erase("source");
  const std::filesystem::path missing_source_path = dir / "missing_source.kpeffect";
  writeText(missing_source_path, missing_source.dump(2));
  KARMA_REQUIRE(!karma::visual::particles::loadParticleEffectAsset(missing_source_path, invalid_effect));

  std::filesystem::remove_all(dir);
}

void testParticleEffectParserV3SourceShapesAndMultiEmitter() {
  const std::filesystem::path dir = makeTempDir();
  const std::vector<std::string> shapes{
      "box",
      "sphere",
      "sphere_surface",
      "disc",
      "ring",
      "cylinder",
      "capsule",
      "cone",
      "line",
      "path",
      "trail_path",
      "mesh_surface",
  };

  for (const std::string& shape : shapes) {
    Json json = validParticleEffectJson();
    Json& source = json["emitters"][0]["source"];
    source["shape"] = shape;
    source["dimensions"] = Json::array({2.0f, 0.5f, 0.25f});
    source["inner_radius"] = 0.25f;
    source["outer_radius"] = 0.75f;
    source["height"] = 1.5f;
    source["angle"] = 6.2831853f;
    source["points"] = Json::array({
        Json::array({0.0f, 0.0f, 0.0f}),
        Json::array({1.0f, 0.0f, 0.0f}),
        Json::array({1.0f, 1.0f, 0.0f}),
    });
    source["closed_loop"] = shape == "trail_path";
    source["sampling"] = shape == "line" ? "vertices" : "sequential";
    source["jitter_radius"] = 0.05f;
    source["mesh_asset_key"] = "test/mesh";
    source["distribution"] = shape == "ring" ? "edge" : "uniform";

    const std::filesystem::path path = dir / (shape + ".kpeffect");
    writeText(path, json.dump(2));
    karma::visual::particles::ParticleEffectAsset effect{};
    KARMA_REQUIRE(karma::visual::particles::loadParticleEffectAsset(path, effect));
    const auto* primary = effect.primaryEmitter();
    KARMA_REQUIRE(primary != nullptr);
    KARMA_REQUIRE(!primary->emitter.source_path_points.empty());
  }

  Json multi = validParticleEffectJson();
  Json second = multi["emitters"][0];
  second["texture"] = "test/second_texture";
  second["emission"]["max_particles"] = 23;
  second["source"]["shape"] = "path";
  second["source"]["points"] = Json::array({
      Json::array({0.0f, 0.0f, 0.0f}),
      Json::array({0.0f, 1.0f, 0.0f}),
  });
  multi["emitters"].push_back(second);
  const std::filesystem::path multi_path = dir / "multi.kpeffect";
  writeText(multi_path, multi.dump(2));
  karma::visual::particles::ParticleEffectAsset effect{};
  KARMA_REQUIRE(karma::visual::particles::loadParticleEffectAsset(multi_path, effect));
  KARMA_REQUIRE(effect.emitters.size() == 2u);
  KARMA_REQUIRE(effect.emitters[1].texture_key == "test/second_texture");
  KARMA_REQUIRE(effect.emitters[1].emitter.max_particles == 23u);

  std::filesystem::remove_all(dir);
}

void testParticleSystemRendererOwnedState() {
  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, karma::components::TransformComponent{});
  karma::components::ParticleEmitterComponent emitter{};
  emitter.enabled = true;
  emitter.playing = true;
  emitter.loop = false;
  emitter.emit_burst_on_start = true;
  emitter.max_particles = 8;
  emitter.burst_count = 4;
  emitter.start_delay = 0.1f;
  emitter.particle_lifetime_min = 1.0f;
  emitter.particle_lifetime_max = 1.0f;
  world.add(entity, emitter);

  karma::visual::particles::ParticleSystem system(nullptr, nullptr);
  system.update(world, 0.05f, 1.0f);
  KARMA_REQUIRE(system.liveParticleCount(entity) == 0u);
}

void testParticleSystemEffectLifecycleReapply() {
  karma::assets::AssetRegistry assets;
  karma::visual::particles::ParticleEffectDesc effect{};
  effect.emitters.push_back(karma::visual::particles::ParticleEmitterDesc{});
  auto& authored = effect.emitters[0];
  authored.emitter.enabled = true;
  authored.emitter.playing = true;
  authored.emitter.layer = 2u;
  authored.texture_key = "spark/base_texture";
  authored.emitter.max_particles = 32u;
  authored.emitter.start_delay = 0.1f;
  authored.emitter.start_size_min = 0.2f;
  authored.emitter.start_size_max = 0.4f;
  authored.emitter.start_color = {1.0f, 0.5f, 0.25f, 0.8f};
  assets.registerParticleEffect("spark", effect);

  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, karma::components::TransformComponent{});
  world.add(entity, karma::components::VisibilityComponent{.visible = false});
  world.add(entity, karma::components::ParticleEffectComponent{
                        .effect_key = "spark",
                        .preserve_enabled = true,
                        .preserve_playing = true,
                        .preserve_start_delay = true,
                    });
  karma::components::ParticleEmitterComponent existing{};
  existing.enabled = false;
  existing.playing = false;
  existing.start_delay = 0.75f;
  existing.max_particles = 1u;
  world.add(entity, existing);
  karma::components::ParticleEffectOverrideComponent effect_override{};
  effect_override.size_scale = 2.0f;
  effect_override.alpha_scale = 0.5f;
  effect_override.texture_key = "spark/override_texture";
  effect_override.source_shape = karma::components::ParticleSourceShape::Line;
  effect_override.source_path_points = std::vector<karma::math::Vec3>{
      {0.0f, 0.0f, 0.0f},
      {2.0f, 0.0f, 0.0f},
  };
  effect_override.source_jitter_radius = 0.25f;
  world.add(entity, effect_override);

  karma::visual::particles::ParticleSystem system(nullptr, &assets);
  system.update(world, 0.016f, 1.0f);
  const auto& applied = world.get<karma::components::ParticleEmitterComponent>(entity);
  KARMA_REQUIRE(!applied.enabled);
  KARMA_REQUIRE(!applied.playing);
  KARMA_REQUIRE(nearly(applied.start_delay, 0.75f));
  KARMA_REQUIRE(applied.layer == 2u);
  KARMA_REQUIRE(applied.texture_key == "spark/override_texture");
  KARMA_REQUIRE(applied.max_particles == 32u);
  KARMA_REQUIRE(nearly(applied.start_size_min, 0.4f));
  KARMA_REQUIRE(nearly(applied.start_size_max, 0.8f));
  KARMA_REQUIRE(nearly(applied.start_color.a, 0.4f));
  KARMA_REQUIRE(applied.source_shape == karma::components::ParticleSourceShape::Line);
  KARMA_REQUIRE(applied.source_path_points.size() == 2u);
  KARMA_REQUIRE(nearly(applied.source_jitter_radius, 0.25f));

  auto& override_component =
      world.get<karma::components::ParticleEffectOverrideComponent>(entity);
  override_component.texture_key = "spark/updated_texture";
  system.update(world, 0.016f, 1.0f);
  KARMA_REQUIRE(world.get<karma::components::ParticleEmitterComponent>(entity).texture_key ==
                "spark/updated_texture");

  auto& effect_component =
      world.get<karma::components::ParticleEffectComponent>(entity);
  effect_component.restart_count += 1u;
  system.update(world, 0.016f, 1.0f);
  KARMA_REQUIRE(effect_component.applied_restart_count == effect_component.restart_count);

  effect.emitters[0].emitter.layer = 7u;
  effect.emitters[0].emitter.max_particles = 64u;
  assets.registerParticleEffect("spark", effect);
  system.update(world, 0.016f, 1.0f);
  const auto& reapplied = world.get<karma::components::ParticleEmitterComponent>(entity);
  KARMA_REQUIRE(reapplied.layer == 7u);
  KARMA_REQUIRE(reapplied.max_particles == 64u);
  KARMA_REQUIRE(!reapplied.enabled);
  KARMA_REQUIRE(!reapplied.playing);
  KARMA_REQUIRE(nearly(reapplied.start_delay, 0.75f));
}

void testLightPulseSystem() {
  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, karma::components::LightComponent{
                        .type = karma::components::LightComponent::Type::Point,
                        .intensity = 0.0f,
                        .range = 0.1f,
                    });
  world.add(entity, karma::components::VisibilityComponent{.visible = false});
  world.add(entity, karma::components::LightPulseComponent{
                        .duration = 1.0f,
                        .peak_intensity = 10.0f,
                        .peak_range = 5.0f,
                        .off_intensity = 0.0f,
                        .off_range = 0.1f,
                    });

  karma::visual::LightPulseSystem system;
  system.update(world, 0.0f);
  auto& light = world.get<karma::components::LightComponent>(entity);
  auto& visibility = world.get<karma::components::VisibilityComponent>(entity);
  KARMA_REQUIRE(nearly(light.intensity, 10.0f));
  KARMA_REQUIRE(nearly(light.range, 5.0f));
  KARMA_REQUIRE(visibility.visible);

  system.update(world, 0.5f);
  KARMA_REQUIRE(light.intensity > 0.0f && light.intensity < 10.0f);
  KARMA_REQUIRE(light.range > 0.1f && light.range < 5.0f);
  KARMA_REQUIRE(visibility.visible);

  system.update(world, 0.6f);
  const auto& pulse = world.get<karma::components::LightPulseComponent>(entity);
  KARMA_REQUIRE(!pulse.active);
  KARMA_REQUIRE(nearly(light.intensity, 0.0f));
  KARMA_REQUIRE(nearly(light.range, 0.1f));
  KARMA_REQUIRE(!visibility.visible);
}

void testExplosionPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path prefab_dir = repo_root / "examples/assets/prefabs/explosion";

  karma::assets::AssetRegistry assets;
  karma::prefabs::bindPrefabAssetRegistry(&assets);

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->find("flash").isValid());
  KARMA_REQUIRE(instance->find("smoke").isValid());
  const karma::world::Entity glow = instance->find("glow");
  KARMA_REQUIRE(glow.isValid());
  KARMA_REQUIRE(world.has<karma::components::LightPulseComponent>(glow));
  const karma::world::Entity smoke = instance->find("smoke");
  KARMA_REQUIRE(world.has<karma::components::ParticleEmitterComponent>(smoke));
  const auto& smoke_emitter = world.get<karma::components::ParticleEmitterComponent>(smoke);
  KARMA_REQUIRE(nearly(smoke_emitter.start_delay, 0.24f));
  KARMA_REQUIRE(assets.findParticleEffect("prefabs/explosion/flash") != nullptr);
  KARMA_REQUIRE(assets.findParticleEffect("prefabs/explosion/smoke_flipbook") != nullptr);

  const char* texture_keys[] = {
      "prefabs/explosion/spark_atlas",
      "prefabs/explosion/glow_atlas",
      "prefabs/explosion/smoke_atlas",
      "prefabs/explosion/heat_atlas",
      "prefabs/explosion/dust_ring_atlas",
      "prefabs/explosion/shock_ring_atlas",
      "prefabs/explosion/scorch_atlas",
      "prefabs/explosion/debris_atlas",
      "prefabs/explosion/explosion00_flipbook",
      "prefabs/explosion/explosion01_smoke_flipbook",
  };
  auto countTextureSize = [&assets, &texture_keys](int width, int height) {
    std::uint32_t count = 0u;
    for (const char* key : texture_keys) {
      const auto* texture = assets.findTextureAsset(key);
      KARMA_REQUIRE(texture != nullptr);
      KARMA_REQUIRE(!texture->bytes.empty());
      if (texture->desc.width == width && texture->desc.height == height) {
        count += 1u;
      }
    }
    return count;
  };
  KARMA_REQUIRE(countTextureSize(256, 64) == 8u);
  KARMA_REQUIRE(countTextureSize(2024, 2024) == 2u);

  const auto* core_flipbook = assets.findParticleEffect("prefabs/explosion/core_flipbook");
  KARMA_REQUIRE(core_flipbook != nullptr);
  const auto* core_primary = core_flipbook->primaryEmitter();
  KARMA_REQUIRE(core_primary != nullptr);
  KARMA_REQUIRE(core_primary->texture_key == "prefabs/explosion/explosion00_flipbook");
  KARMA_REQUIRE(core_primary->emitter.atlas_frame_count == 25u);
  KARMA_REQUIRE(core_primary->emitter.atlas_frame_width == 400u);
  KARMA_REQUIRE(core_primary->emitter.atlas_frame_height == 400u);
  KARMA_REQUIRE(core_primary->emitter.atlas_border_x == 4u);
  KARMA_REQUIRE(core_primary->emitter.atlas_border_y == 4u);
  KARMA_REQUIRE(core_primary->emitter.atlas_spacing_x == 4u);
  KARMA_REQUIRE(core_primary->emitter.atlas_spacing_y == 4u);
  KARMA_REQUIRE(core_primary->emitter.blend_mode ==
                karma::components::ParticleBlendMode::Additive);

  const auto* smoke_flipbook = assets.findParticleEffect("prefabs/explosion/smoke_flipbook");
  KARMA_REQUIRE(smoke_flipbook != nullptr);
  const auto* smoke_primary = smoke_flipbook->primaryEmitter();
  KARMA_REQUIRE(smoke_primary != nullptr);
  KARMA_REQUIRE(smoke_primary->texture_key == "prefabs/explosion/explosion01_smoke_flipbook");
  KARMA_REQUIRE(smoke_primary->emitter.atlas_frame_count == 25u);
  KARMA_REQUIRE(smoke_primary->emitter.atlas_frame_width == 400u);
  KARMA_REQUIRE(smoke_primary->emitter.atlas_frame_height == 400u);
  KARMA_REQUIRE(smoke_primary->emitter.atlas_border_x == 4u);
  KARMA_REQUIRE(smoke_primary->emitter.atlas_border_y == 4u);
  KARMA_REQUIRE(smoke_primary->emitter.atlas_spacing_x == 4u);
  KARMA_REQUIRE(smoke_primary->emitter.atlas_spacing_y == 4u);
  KARMA_REQUIRE(smoke_primary->emitter.blend_mode ==
                karma::components::ParticleBlendMode::Alpha);

  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
  for (const char* key : texture_keys) {
    KARMA_REQUIRE(assets.findTextureAsset(key) == nullptr);
  }
  KARMA_REQUIRE(assets.findParticleEffect("prefabs/explosion/flash") == nullptr);
  karma::prefabs::clearPrefabAssetPackages();
}

void testEnergyOrbPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path prefab_dir = repo_root / "examples/assets/prefabs/energy_orb";

  karma::assets::AssetRegistry assets;
  karma::prefabs::bindPrefabAssetRegistry(&assets);

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->find("shell").isValid());
  KARMA_REQUIRE(instance->find("core").isValid());
  KARMA_REQUIRE(instance->find("arcs").isValid());
  KARMA_REQUIRE(instance->find("halo").isValid());
  KARMA_REQUIRE(instance->find("distortion").isValid());
  const karma::world::Entity glow = instance->find("glow");
  KARMA_REQUIRE(glow.isValid());
  KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));

  const karma::world::Entity shell = instance->find("shell");
  KARMA_REQUIRE(world.has<karma::components::MeshComponent>(shell));
  KARMA_REQUIRE(world.get<karma::components::MeshComponent>(shell).mesh_asset_key ==
         "prefabs/energy_orb/orb_shell");
  KARMA_REQUIRE(assets.findMeshAsset("prefabs/energy_orb/orb_shell") != nullptr);
  KARMA_REQUIRE(world.has<karma::components::TransformComponent>(shell));
  const auto& shell_transform = world.get<karma::components::TransformComponent>(shell);
  KARMA_REQUIRE(nearly(shell_transform.localScale().x, 0.28875f));
  KARMA_REQUIRE(nearly(shell_transform.localScale().y, 0.28875f));
  KARMA_REQUIRE(nearly(shell_transform.localScale().z, 0.28875f));

  const karma::world::Entity core = instance->find("core");
  KARMA_REQUIRE(world.has<karma::components::ParticleEffectComponent>(core));
  KARMA_REQUIRE(world.has<karma::components::ParticleEmitterComponent>(core));
  KARMA_REQUIRE(world.get<karma::components::ParticleEffectComponent>(core).effect_key ==
         "energy_orb_core");
  KARMA_REQUIRE(world.get<karma::components::ParticleEmitterComponent>(core).playing);
  KARMA_REQUIRE(assets.findParticleEffect("energy_orb_core") != nullptr);
  KARMA_REQUIRE(assets.findParticleEffect("energy_orb_arcs") != nullptr);
  KARMA_REQUIRE(assets.findParticleEffect("energy_orb_halo") != nullptr);
  KARMA_REQUIRE(assets.findParticleEffect("energy_orb_distortion") != nullptr);
  const char* texture_keys[] = {
      "orb_core_atlas",
      "orb_arc_atlas",
      "orb_halo_atlas",
      "orb_distortion_atlas",
  };
  for (const char* key : texture_keys) {
    const auto* texture = assets.findTextureAsset(key);
    KARMA_REQUIRE(texture != nullptr);
    KARMA_REQUIRE(texture->desc.width == 768);
    KARMA_REQUIRE(texture->desc.height == 128);
    KARMA_REQUIRE(!texture->bytes.empty());
  }

  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
  for (const char* key : texture_keys) {
    KARMA_REQUIRE(assets.findTextureAsset(key) == nullptr);
  }
  KARMA_REQUIRE(assets.findMeshAsset("prefabs/energy_orb/orb_shell") == nullptr);
  KARMA_REQUIRE(assets.findParticleEffect("energy_orb_core") == nullptr);
  karma::prefabs::clearPrefabAssetPackages();
}

void testPathEnergyBeamPrefabDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path prefab_dir =
      repo_root / "examples/assets/prefabs/beam_impostor";

  karma::assets::AssetRegistry assets;
  karma::prefabs::bindPrefabAssetRegistry(&assets);

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->find("path_warm_glow").isValid());
  KARMA_REQUIRE(instance->find("path_hot_core").isValid());
  KARMA_REQUIRE(instance->find("path_electric_core").isValid());
  KARMA_REQUIRE(instance->find("path_heat_distortion").isValid());
  KARMA_REQUIRE(instance->find("path_endpoint_glow").isValid());
  KARMA_REQUIRE(instance->find("path_light_0").isValid());

  const karma::world::Entity core = instance->find("path_hot_core");
  KARMA_REQUIRE(world.has<karma::components::ParticleEffectComponent>(core));
  KARMA_REQUIRE(world.has<karma::components::ParticleEffectOverrideComponent>(core));
  KARMA_REQUIRE(world.get<karma::components::ParticleEffectComponent>(core).effect_key ==
                "path_energy/hot_core");
  const auto& core_override =
      world.get<karma::components::ParticleEffectOverrideComponent>(core);
  KARMA_REQUIRE(core_override.source_shape.has_value());
  KARMA_REQUIRE(*core_override.source_shape == karma::components::ParticleSourceShape::Path);
  KARMA_REQUIRE(core_override.source_path_points.has_value());
  KARMA_REQUIRE(core_override.source_path_points->size() == 6u);

  const auto* hot_core = assets.findParticleEffect("path_energy/hot_core");
  KARMA_REQUIRE(hot_core != nullptr);
  const auto* hot_core_emitter = hot_core->primaryEmitter();
  KARMA_REQUIRE(hot_core_emitter != nullptr);
  KARMA_REQUIRE(hot_core_emitter->emitter.source_shape ==
                karma::components::ParticleSourceShape::Path);
  KARMA_REQUIRE(hot_core_emitter->emitter.max_particles == 520u);
  KARMA_REQUIRE(hot_core_emitter->texture_key == "path_energy/glow_atlas");

  const auto* distortion = assets.findParticleEffect("path_energy/heat_distortion");
  KARMA_REQUIRE(distortion != nullptr);
  const auto* distortion_emitter = distortion->primaryEmitter();
  KARMA_REQUIRE(distortion_emitter != nullptr);
  KARMA_REQUIRE(distortion_emitter->emitter.blend_mode ==
                karma::components::ParticleBlendMode::Distortion);
  const char* texture_keys[] = {
      "path_energy/glow_atlas",
      "path_energy/spark_atlas",
      "path_energy/heat_atlas",
  };
  for (const char* key : texture_keys) {
    const auto* texture = assets.findTextureAsset(key);
    KARMA_REQUIRE(texture != nullptr);
    KARMA_REQUIRE(texture->desc.width == 256);
    KARMA_REQUIRE(texture->desc.height == 64);
    KARMA_REQUIRE(!texture->bytes.empty());
  }

  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
  for (const char* key : texture_keys) {
    KARMA_REQUIRE(assets.findTextureAsset(key) == nullptr);
  }
  KARMA_REQUIRE(assets.findParticleEffect("path_energy/hot_core") == nullptr);
  karma::prefabs::clearPrefabAssetPackages();
}

void testParticleStatsReportFormatting() {
  karma::rendering::ParticlePassStats totals{};
  karma::rendering::ParticlePassStats frame{};
  frame.submitted_emitters = 3u;
  frame.gpu_particle_capacity = 128u;
  frame.gpu_alive_particles = 42u;
  frame.gpu_dead_particles = 86u;
  frame.gpu_compute_dispatches = 2u;
  frame.gpu_indirect_draws = 4u;
  frame.gpu_indirect_dispatches = 1u;
  frame.gpu_sort_key_count = 12u;
  frame.gpu_sort_passes = 1u;
  frame.gpu_stats_readback_age = 1u;
  frame.gpu_allocator_live_emitters = 5u;
  frame.gpu_allocator_free_ranges = 2u;
  frame.gpu_allocator_active_capacity = 96u;
  frame.gpu_allocator_high_water_capacity = 160u;
  frame.gpu_allocator_retired_emitters = 1u;
  frame.gpu_allocator_reused_slots = 3u;
  frame.gpu_allocator_allocation_failures = 1u;
  frame.gpu_culled_emitters = 2u;
  frame.gpu_culled_particles = 9u;
  frame.gpu_culling_dispatches = 2u;
  frame.cpu_fallback_particles = 7u;
  frame.simulation_ms = 0.5f;
  frame.scene_color_copy = true;
  frame.gpu_sort_overflow = true;
  frame.gpu_grouped_sort_fallback = true;
  karma::rendering::accumulateParticleStats(totals, frame);
  karma::rendering::accumulateParticleStats(totals, frame);

  const std::string line = karma::rendering::formatParticleStatsReport(
      karma::rendering::ParticleStatsReport{
          .totals = totals,
          .frame_count = 2u,
          .elapsed_seconds = 1.0,
      });
  KARMA_REQUIRE(line.find("submitted_emitters=3.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_particle_capacity=128.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_alive_particles=42.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_dead_particles=86.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_compute_dispatches=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_indirect_draws=4.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_indirect_dispatches=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_key_count=12.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_passes=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_stats_readback_age=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_live_emitters=5.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_free_ranges=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_active_capacity=96.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_high_water_capacity=160.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_retired_emitters=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_reused_slots=3.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_allocator_allocation_failures=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_culled_emitters=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_culled_particles=9.0") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_culling_dispatches=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("cpu_fallback_particles=7.0") != std::string::npos);
  KARMA_REQUIRE(line.find("simulation_ms=0.500") != std::string::npos);
  KARMA_REQUIRE(line.find("scene_color_copy=true") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_overflow=true") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_global_sort_active=false") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_grouped_sort_fallback=true") != std::string::npos);
}

}  // namespace

int main() {
  const std::filesystem::path dir = makeTempDir();
  testSaveLoadSingleEntity(dir);
  testInstancedMeshLodPrefabRoundTrip(dir);
  testColliderComponentPrefabRoundTrips(dir);
  testHierarchyRoundTrip(dir);
  testUnknownComponentFails(dir);
  testMalformedAndInvalidPayloads(dir);
  testVolumetricComponentPrefabRoundTrip(dir);
  testVolumetricComponentValidation(dir);
  testTerrainComponentPrefabRoundTrip(dir);
  testSingleImageTerrainComponentPrefabRoundTrip(dir);
  testMigratedPrefabAssetsDoNotUseLegacyComponentNames();
  testDestroyPrefab(dir);
  testMissingAssetPackageKeepsPrefabLoad(dir);
  testAssetPackageParsingSuccessAndFailure(dir);
  testAssetPackageMissingRegistryAndResourceFailure(dir);
  testParticleEffectParserV3();
  testParticleEffectParserV3SourceShapesAndMultiEmitter();
  testParticleSystemRendererOwnedState();
  testParticleSystemEffectLifecycleReapply();
  testParticleStatsReportFormatting();
  testLightPulseSystem();
  testExplosionPrefabDirectLoad();
  testEnergyOrbPrefabDirectLoad();
  testPathEnergyBeamPrefabDirectLoad();
  std::filesystem::remove_all(dir);
  return 0;
}
