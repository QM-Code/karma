#include <chrono>
#include <cstdlib>
#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/assets.h"
#include "karma/foliage.h"
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

template <typename Component, typename Verify>
void requireSerializerRoundTrip(std::string_view type_name,
                                Component component,
                                Verify verify) {
  karma::prefabs::ensureBuiltinComponentSerializers();
  const auto* serializer =
      karma::prefabs::componentSerializerRegistry().find(type_name);
  KARMA_REQUIRE(serializer != nullptr);
  karma::world::World authored;
  const karma::world::Entity authored_entity = authored.createEntity();
  authored.add(authored_entity, std::move(component));
  const Json payload = serializer->serialize(authored, authored_entity);
  karma::world::World loaded;
  const karma::world::Entity loaded_entity = loaded.createEntity();
  KARMA_REQUIRE(serializer->deserialize(loaded, loaded_entity, payload));
  KARMA_REQUIRE(loaded.has<Component>(loaded_entity));
  verify(loaded.get<Component>(loaded_entity), payload);
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
  "version": 2,
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
  world.add(root, karma::components::RenderTagsComponent{
                      .tags = {"selected", "outline"},
                  });

  const std::filesystem::path path = dir / "single.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  const Json saved = readJson(path);
  KARMA_REQUIRE(saved["version"] == 2);
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
  KARMA_REQUIRE(saved["nodes"][0]["components"]["RenderTagsComponent"]["tags"].is_array());
  KARMA_REQUIRE(saved["nodes"][0]["components"]["RenderTagsComponent"]["tags"][0] == "selected");
  KARMA_REQUIRE(saved["nodes"][0]["components"]["RenderTagsComponent"]["tags"][1] == "outline");

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

  const auto& render_tags =
      loaded_world.get<karma::components::RenderTagsComponent>(instance->root);
  KARMA_REQUIRE(render_tags.tags.size() == 2);
  KARMA_REQUIRE(render_tags.tags[0] == "selected");
  KARMA_REQUIRE(render_tags.tags[1] == "outline");
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

void testPhysicsAuthoringComponentsPrefabRoundTrip(
    const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  scene.createNode(root);
  world.setName(root, "PhysicsAuthoring");
  world.add(root, karma::components::TransformComponent{});
  world.add(root,
            karma::components::ColliderComponent::box(
                karma::components::BoxColliderShape{
                    .center = {0.0f, 1.0f, 0.0f},
                    .half_extents = {0.5f, 1.0f, 0.5f},
                }));

  karma::components::RigidbodyComponent body{};
  body.motion_type = karma::components::RigidbodyMotionType::Kinematic;
  body.motion_quality = karma::components::RigidbodyMotionQuality::LinearCast;
  body.allowed_dofs = karma::components::RigidbodyDofPlane2D;
  body.mass = 7.5f;
  body.velocity = {1.0f, 2.0f, 3.0f};
  body.angular_velocity = {4.0f, 5.0f, 6.0f};
  body.is_kinematic = true;
  body.use_gravity = false;
  body.is_trigger = true;
  body.gravity_factor = 0.35f;
  body.linear_damping = 0.15f;
  body.angular_damping = 0.25f;
  body.max_linear_velocity = 125.0f;
  body.max_angular_velocity = 32.0f;
  body.inertia_multiplier = 1.75f;
  body.velocity_solver_steps = 9u;
  body.position_solver_steps = 4u;
  body.allow_sleeping = false;
  body.allow_dynamic_or_kinematic = true;
  body.collide_kinematic_vs_non_dynamic = true;
  body.use_manifold_reduction = false;
  body.apply_gyroscopic_force = true;
  body.enhanced_internal_edge_removal = true;
  world.add(root, body);
  world.add(root,
            karma::components::PhysicsMaterialComponent{
                .friction = 0.7f,
                .restitution = 0.4f,
            });
  world.add(root,
            karma::components::PhysicsCollisionFilterComponent{
                .layers = 0x12u,
                .collides_with = 0xA5A5A5A5u,
            });

  const std::filesystem::path path = dir / "physics_authoring.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));

  const Json saved = readJson(path);
  const Json& components = saved["nodes"][0]["components"];
  const Json& body_json = components["RigidbodyComponent"];
  KARMA_REQUIRE(body_json["motion_type"] == "kinematic");
  KARMA_REQUIRE(body_json["motion_quality"] == "linear_cast");
  KARMA_REQUIRE(body_json["allowed_dofs"] ==
                karma::components::RigidbodyDofPlane2D);
  KARMA_REQUIRE(body_json["gravity_factor"] == body.gravity_factor);
  KARMA_REQUIRE(body_json["velocity_solver_steps"] ==
                body.velocity_solver_steps);
  KARMA_REQUIRE(body_json["enhanced_internal_edge_removal"] == true);
  KARMA_REQUIRE(components["PhysicsMaterialComponent"]["friction"] == 0.7f);
  KARMA_REQUIRE(
      components["PhysicsCollisionFilterComponent"]["collides_with"] ==
      0xA5A5A5A5u);

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance =
      karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  const auto& loaded =
      loaded_world.get<karma::components::RigidbodyComponent>(instance->root);
  KARMA_REQUIRE(loaded.motion_type == body.motion_type);
  KARMA_REQUIRE(loaded.motion_quality == body.motion_quality);
  KARMA_REQUIRE(loaded.allowed_dofs == body.allowed_dofs);
  KARMA_REQUIRE(nearly(loaded.mass, body.mass));
  KARMA_REQUIRE(nearlyVec3(loaded.velocity, body.velocity));
  KARMA_REQUIRE(nearlyVec3(loaded.angular_velocity, body.angular_velocity));
  KARMA_REQUIRE(loaded.is_kinematic == body.is_kinematic);
  KARMA_REQUIRE(loaded.use_gravity == body.use_gravity);
  KARMA_REQUIRE(loaded.is_trigger == body.is_trigger);
  KARMA_REQUIRE(nearly(loaded.gravity_factor, body.gravity_factor));
  KARMA_REQUIRE(nearly(loaded.linear_damping, body.linear_damping));
  KARMA_REQUIRE(nearly(loaded.angular_damping, body.angular_damping));
  KARMA_REQUIRE(nearly(loaded.max_linear_velocity, body.max_linear_velocity));
  KARMA_REQUIRE(nearly(loaded.max_angular_velocity, body.max_angular_velocity));
  KARMA_REQUIRE(nearly(loaded.inertia_multiplier, body.inertia_multiplier));
  KARMA_REQUIRE(loaded.velocity_solver_steps == body.velocity_solver_steps);
  KARMA_REQUIRE(loaded.position_solver_steps == body.position_solver_steps);
  KARMA_REQUIRE(loaded.allow_sleeping == body.allow_sleeping);
  KARMA_REQUIRE(loaded.allow_dynamic_or_kinematic ==
                body.allow_dynamic_or_kinematic);
  KARMA_REQUIRE(loaded.collide_kinematic_vs_non_dynamic ==
                body.collide_kinematic_vs_non_dynamic);
  KARMA_REQUIRE(loaded.use_manifold_reduction == body.use_manifold_reduction);
  KARMA_REQUIRE(loaded.apply_gyroscopic_force == body.apply_gyroscopic_force);
  KARMA_REQUIRE(loaded.enhanced_internal_edge_removal ==
                body.enhanced_internal_edge_removal);

  const auto& material =
      loaded_world.get<karma::components::PhysicsMaterialComponent>(
          instance->root);
  KARMA_REQUIRE(nearly(material.friction, 0.7f));
  KARMA_REQUIRE(nearly(material.restitution, 0.4f));
  const auto& filter =
      loaded_world.get<karma::components::PhysicsCollisionFilterComponent>(
          instance->root);
  KARMA_REQUIRE(filter.layers == 0x12u);
  KARMA_REQUIRE(filter.collides_with == 0xA5A5A5A5u);
}

void testLegacyRigidbodyPayloadKeepsAdvancedDefaults(
    const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "legacy_rigidbody.json";
  writeText(path,
            R"({
  "version": 2,
  "root": 0,
  "nodes": [{
    "id": 0,
    "name": "Legacy Body",
    "parent": null,
    "components": {
      "TransformComponent": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "ColliderComponent": {
        "type": "box",
        "is_trigger": false,
        "debug_draw": false,
        "shape": {"center": [0, 0, 0], "half_extents": [0.5, 0.5, 0.5]}
      },
      "RigidbodyComponent": {
        "mass": 3.0,
        "velocity": [1, 0, 0],
        "angular_velocity": [0, 2, 0],
        "is_kinematic": false,
        "use_gravity": true,
        "is_trigger": false
      }
    }
  }]
})");

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path);
  KARMA_REQUIRE(instance.has_value());
  const auto& body =
      world.get<karma::components::RigidbodyComponent>(instance->root);
  const karma::components::RigidbodyComponent defaults{};
  KARMA_REQUIRE(nearly(body.mass, 3.0f));
  KARMA_REQUIRE(nearlyVec3(body.velocity, {1.0f, 0.0f, 0.0f}));
  KARMA_REQUIRE(body.motion_type == defaults.motion_type);
  KARMA_REQUIRE(body.motion_quality == defaults.motion_quality);
  KARMA_REQUIRE(body.allowed_dofs == defaults.allowed_dofs);
  KARMA_REQUIRE(nearly(body.gravity_factor, defaults.gravity_factor));
  KARMA_REQUIRE(nearly(body.linear_damping, defaults.linear_damping));
  KARMA_REQUIRE(nearly(body.angular_damping, defaults.angular_damping));
  KARMA_REQUIRE(nearly(body.max_linear_velocity,
                       defaults.max_linear_velocity));
  KARMA_REQUIRE(nearly(body.max_angular_velocity,
                       defaults.max_angular_velocity));
  KARMA_REQUIRE(nearly(body.inertia_multiplier, defaults.inertia_multiplier));
  KARMA_REQUIRE(body.velocity_solver_steps == defaults.velocity_solver_steps);
  KARMA_REQUIRE(body.position_solver_steps == defaults.position_solver_steps);
  KARMA_REQUIRE(body.allow_sleeping == defaults.allow_sleeping);
  KARMA_REQUIRE(body.use_manifold_reduction == defaults.use_manifold_reduction);
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

void testPersistentComponentRegistryCoverage() {
  karma::prefabs::ensureBuiltinComponentSerializers();
  const auto& registry = karma::prefabs::componentSerializerRegistry();
  const std::vector<std::string_view> required{
      "TagComponent",
      "TransformComponent",
      "StaticComponent",
      "AudioListenerComponent",
      "AudioSourceComponent",
      "CameraComponent",
      "EnvironmentComponent",
      "MeshComponent",
      "InstancedMeshComponent",
      "FoliageComponent",
      "AnimatorComponent",
      "RootMotionComponent",
      "DeformableMeshComponent",
      "LightComponent",
      "LightPulseComponent",
      "VisibilityComponent",
      "RenderTagsComponent",
      "TerrainComponent",
      "ColliderComponent",
      "RigidbodyComponent",
      "PhysicsMaterialComponent",
      "PhysicsCollisionFilterComponent",
      "CharacterControllerComponent",
      "CollisionListenerComponent",
      "ContactListenerComponent",
      "GroundContactComponent",
      "PhysicsConstraintComponent",
      "PhysicsSoftBodyComponent",
      "PhysicsVehicleComponent",
#if defined(KARMA_ENABLE_NAVIGATION)
      "NavMeshSurfaceComponent",
      "NavOffMeshLinkComponent",
      "NavConvexVolumeComponent",
      "NavMeshComponent",
      "NavCrowdComponent",
      "NavCrowdAgentComponent",
      "NavMeshAgentComponent",
      "NavTileCacheComponent",
      "NavTileCacheObstacleComponent",
#endif
      "NetworkIdentityComponent",
      "NetworkAuthorityComponent",
      "NetworkReplicatedComponent",
      "ScriptComponent",
      "ParticleEffectComponent",
      "ParticleEffectOverrideComponent",
      "ParticleEmitterComponent",
      "ParticleBeamComponent",
      "VolumetricComponent",
  };
#if defined(KARMA_ENABLE_NAVIGATION)
  KARMA_REQUIRE(required.size() == 47u);
#else
  KARMA_REQUIRE(required.size() == 38u);
#endif
  KARMA_REQUIRE(registry.serializers().size() == required.size());
  for (std::string_view name : required) {
    const auto* serializer = registry.find(name);
    KARMA_REQUIRE(serializer != nullptr);
    KARMA_REQUIRE(serializer->serialize);
    KARMA_REQUIRE(serializer->deserialize);
  }
  const std::array<std::string_view, 4> runtime_only{
      "AnimationEventBufferComponent",
      "CollisionEventsComponent",
      "ContactEventsComponent",
      "PhysicsBodyForcesComponent",
  };
  for (std::string_view name : runtime_only) {
    KARMA_REQUIRE(registry.find(name) == nullptr);
  }
  KARMA_REQUIRE(
      registry.find("PhysicsConstraintComponent")->serialize_with_context);
  KARMA_REQUIRE(
      registry.find("PhysicsConstraintComponent")->deserialize_with_context);

  const auto* static_serializer = registry.find("StaticComponent");
  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  KARMA_REQUIRE(!static_serializer->deserialize(
      world,
      entity,
      Json{{"enabled", true},
           {"include_descendants", true},
           {"flags", 0x80000000u}}));
  KARMA_REQUIRE(!world.has<karma::components::StaticComponent>(entity));
}

void testPersistentAuthoringSubsetRoundTrips() {
  karma::components::AudioSourceComponent audio{};
  audio.clip_key = "audio/wind";
  audio.gain = 0.75f;
  audio.looping = true;
  requireSerializerRoundTrip(
      "AudioSourceComponent",
      std::move(audio),
      [](const auto& loaded, const Json&) {
        KARMA_REQUIRE(loaded.clip_key == "audio/wind");
        KARMA_REQUIRE(nearly(loaded.gain, 0.75f));
        KARMA_REQUIRE(loaded.looping);
      });

  karma::components::CameraComponent camera{};
  camera.perspective = false;
  camera.ortho_left = -4.0f;
  camera.ortho_right = 4.0f;
  camera.ortho_bottom = -3.0f;
  camera.ortho_top = 3.0f;
  camera.frame_graph_key = "editor/camera";
  camera.shader_override_fragment_path = "shaders/camera.frag";
  camera.anti_aliasing = karma::rendering::AntiAliasingSettings::ssaa(1.5f);
  requireSerializerRoundTrip(
      "CameraComponent",
      std::move(camera),
      [](const auto& loaded, const Json&) {
        KARMA_REQUIRE(!loaded.perspective);
        KARMA_REQUIRE(loaded.frame_graph_key == "editor/camera");
        KARMA_REQUIRE(loaded.shader_override_fragment_path ==
                      "shaders/camera.frag");
        KARMA_REQUIRE(loaded.anti_aliasing.mode ==
                      karma::rendering::AntiAliasingMode::SSAA);
      });

  karma::components::LightComponent light{};
  light.type = karma::components::LightComponent::Type::Directional;
  light.bake_mode = karma::components::LightComponent::BakeMode::Mixed;
  light.intensity = 2.5f;
  light.mixed_bake_mask_bit = 7u;
  requireSerializerRoundTrip(
      "LightComponent",
      light,
      [](const auto& loaded, const Json& payload) {
        KARMA_REQUIRE(loaded.bake_mode ==
                      karma::components::LightComponent::BakeMode::Mixed);
        KARMA_REQUIRE(payload["bake_mode"] == "mixed");
        KARMA_REQUIRE(!payload.contains("mixed_bake_mask_bit"));
        KARMA_REQUIRE(loaded.mixed_bake_mask_bit == UINT32_MAX);
      });

  karma::components::DeformableMeshComponent deformable{};
  deformable.base_morph_weights = {0.1f, 0.9f};
  deformable.morph_weights = {0.25f, 0.75f};
  deformable.path = karma::components::DeformationPath::CpuReference;
  requireSerializerRoundTrip(
      "DeformableMeshComponent",
      std::move(deformable),
      [](const auto& loaded, const Json& payload) {
        KARMA_REQUIRE(loaded.morph_weights.size() == 2u);
        KARMA_REQUIRE(loaded.path ==
                      karma::components::DeformationPath::CpuReference);
        KARMA_REQUIRE(payload.find("joint_palette") == payload.end());
        KARMA_REQUIRE(payload.find("deformation") == payload.end());
      });

#if defined(KARMA_ENABLE_NAVIGATION)
  karma::components::NavMeshComponent nav_mesh{};
  nav_mesh.source_mask = 0x42u;
  nav_mesh.build_config.build_mode = karma::navigation::NavMeshBuildMode::Tiled;
  nav_mesh.build_config.tile_size = 48;
  requireSerializerRoundTrip(
      "NavMeshComponent",
      std::move(nav_mesh),
      [](const auto& loaded, const Json& payload) {
        KARMA_REQUIRE(loaded.source_mask == 0x42u);
        KARMA_REQUIRE(loaded.build_config.build_mode ==
                      karma::navigation::NavMeshBuildMode::Tiled);
        KARMA_REQUIRE(loaded.build_config.tile_size == 48);
        KARMA_REQUIRE(payload.find("built") == payload.end());
        KARMA_REQUIRE(payload.find("build_version") == payload.end());
      });
#endif

  karma::components::NetworkAuthorityComponent authority{};
  authority.mode = karma::components::AuthorityMode::Owner;
  authority.owner_peer = 17u;
  requireSerializerRoundTrip(
      "NetworkAuthorityComponent",
      authority,
      [](const auto& loaded, const Json&) {
        KARMA_REQUIRE(loaded.mode == karma::components::AuthorityMode::Owner);
        KARMA_REQUIRE(loaded.owner_peer == 17u);
      });

  karma::components::PhysicsSoftBodyComponent soft_body{};
  soft_body.preset = karma::components::PhysicsSoftBodyPresetKind::Custom;
  soft_body.vertices.push_back(karma::components::PhysicsSoftBodyVertex{
      .position = {1.0f, 2.0f, 3.0f},
      .inverse_mass = 0.5f,
  });
  soft_body.pinned_vertices.push_back(0u);
  soft_body.pressure = 0.4f;
  requireSerializerRoundTrip(
      "PhysicsSoftBodyComponent",
      std::move(soft_body),
      [](const auto& loaded, const Json& payload) {
        KARMA_REQUIRE(loaded.vertices.size() == 1u);
        KARMA_REQUIRE(loaded.pinned_vertices == std::vector<uint32_t>{0u});
        KARMA_REQUIRE(nearly(loaded.pressure, 0.4f));
        KARMA_REQUIRE(payload.find("recreate") == payload.end());
      });

  karma::components::PhysicsVehicleComponent vehicle{};
  vehicle.controller =
      karma::components::PhysicsVehicleControllerKind::Motorcycle;
  vehicle.wheels.push_back(karma::components::PhysicsVehicleWheel{});
  vehicle.differentials.push_back(
      karma::components::PhysicsVehicleDifferential{
          .left_wheel = 0,
          .right_wheel = -1,
      });
  vehicle.motorcycle.max_lean_angle = 0.6f;
  requireSerializerRoundTrip(
      "PhysicsVehicleComponent",
      std::move(vehicle),
      [](const auto& loaded, const Json& payload) {
        KARMA_REQUIRE(loaded.controller ==
                      karma::components::PhysicsVehicleControllerKind::Motorcycle);
        KARMA_REQUIRE(loaded.wheels.size() == 1u);
        KARMA_REQUIRE(loaded.differentials.size() == 1u);
        KARMA_REQUIRE(nearly(loaded.motorcycle.max_lean_angle, 0.6f));
        KARMA_REQUIRE(payload.find("input") == payload.end());
      });
}

void testContextualPrefabEntityReferences(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "contextual_refs/prefab.json";
  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::Entity root = world.createEntity();
  const karma::world::Entity body_a = world.createEntity();
  const karma::world::Entity body_b = world.createEntity();
  world.setName(root, "Constraint");
  world.setName(body_a, "Body A");
  world.setName(body_b, "Body B");
  world.add(root, karma::components::TransformComponent{});
  world.add(body_a, karma::components::TransformComponent{});
  world.add(body_b, karma::components::TransformComponent{});
  world.add(root,
            karma::components::StaticComponent{
                .enabled = true,
                .include_descendants = false,
                .flags = karma::components::StaticComponentRender |
                         karma::components::StaticComponentCollision,
            });
  world.add(root,
            karma::components::PhysicsConstraintComponent{
                .body_a = body_a,
                .body_b = body_b,
                .kind = karma::components::PhysicsConstraintKind::Hinge,
                .max_friction_torque = 4.0f,
            });
  const karma::world::NodeId root_node = scene.createNode(root);
  const karma::world::NodeId body_a_node = scene.createNode(body_a);
  const karma::world::NodeId body_b_node = scene.createNode(body_b);
  KARMA_REQUIRE(scene.reparent(body_a_node, root_node));
  KARMA_REQUIRE(scene.reparent(body_b_node, root_node));

  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  Json json = readJson(path);
  const Json& constraint =
      json["nodes"][0]["components"]["PhysicsConstraintComponent"];
  KARMA_REQUIRE(constraint["body_a"] ==
                Json({{"scope", "prefab"}, {"node", 1u}}));
  KARMA_REQUIRE(constraint["body_b"] ==
                Json({{"scope", "prefab"}, {"node", 2u}}));

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance =
      karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  const auto& loaded_constraint =
      loaded_world.get<karma::components::PhysicsConstraintComponent>(
          instance->root);
  KARMA_REQUIRE(loaded_constraint.body_a == instance->find("Body A"));
  KARMA_REQUIRE(loaded_constraint.body_b == instance->find("Body B"));
  KARMA_REQUIRE(loaded_world.has<karma::components::StaticComponent>(
      instance->root));

  json["nodes"][0]["components"]["PhysicsConstraintComponent"]["body_a"] =
      Json{{"node_id", 1u}};
  const std::filesystem::path alias_path =
      dir / "contextual_refs/alias.prefab.json";
  writeText(alias_path, json.dump(2));
  karma::world::World alias_world;
  karma::world::Scene alias_scene;
  const auto alias_instance = karma::prefabs::instantiatePrefab(
      alias_world, alias_scene, alias_path);
  KARMA_REQUIRE(alias_instance.has_value());
  KARMA_REQUIRE(
      alias_world.get<karma::components::PhysicsConstraintComponent>(
          alias_instance->root).body_a == alias_instance->find("Body A"));

  json["nodes"][0]["components"]["PhysicsConstraintComponent"]["body_a"] =
      Json{{"scope", "prefab"}, {"node", 99u}};
  const std::filesystem::path invalid_path =
      dir / "contextual_refs/invalid.prefab.json";
  writeText(invalid_path, json.dump(2));
  KARMA_REQUIRE(!karma::prefabs::loadPrefabDocument(invalid_path).success());
}

void testUnknownComponentFails(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "unknown.json";
  writeText(path,
            R"({
  "version": 2,
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

void testBuiltinRegistryRepairPreservesOverrides() {
  auto& registry = karma::prefabs::componentSerializerRegistry();
  registry.clear();

  bool custom_transform_queried = false;
  KARMA_REQUIRE(registry.registerSerializer(karma::prefabs::ComponentSerializer{
      .type_name = "TransformComponent",
      .has =
          [&](const karma::world::World&, karma::world::Entity) {
            custom_transform_queried = true;
            return false;
          },
      .serialize =
          [](const karma::world::World&, karma::world::Entity) {
            return Json::object();
          },
      .deserialize =
          [](karma::world::World&, karma::world::Entity, const Json&) {
            return true;
          },
  }));

  karma::prefabs::ensureBuiltinComponentSerializers();
  KARMA_REQUIRE(registry.find("MeshComponent") != nullptr);
  const auto* transform = registry.find("TransformComponent");
  KARMA_REQUIRE(transform != nullptr);
  karma::world::World world;
  transform->has(world, {});
  KARMA_REQUIRE(custom_transform_queried);

  registry.clear();
  karma::prefabs::ensureBuiltinComponentSerializers();
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
  "version": 2,
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

  const std::filesystem::path disconnected = dir / "disconnected.json";
  writeText(disconnected,
            R"({
  "version": 2,
  "root": 0,
  "nodes": [
    { "id": 0, "name": "Root", "parent": null, "components": {} },
    { "id": 1, "name": "Orphan", "parent": null, "components": {} }
  ]
})");
  karma::world::World disconnected_world;
  karma::world::Scene disconnected_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(disconnected_world,
                                                   disconnected_scene,
                                                   disconnected)
                     .has_value());
  KARMA_REQUIRE(disconnected_world.entities().empty());

  const std::filesystem::path overflow = dir / "overflow.json";
  writeText(overflow,
            R"({
  "version": 18446744073709551615,
  "root": 0,
  "nodes": []
})");
  karma::world::World overflow_world;
  karma::world::Scene overflow_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(overflow_world, overflow_scene, overflow)
                     .has_value());
  KARMA_REQUIRE(overflow_world.entities().empty());

  const std::filesystem::path non_finite = dir / "non_finite.json";
  writeText(non_finite,
            R"({
  "version": 2,
  "root": 0,
  "nodes": [{
    "id": 0,
    "name": "BrokenFloat",
    "parent": null,
    "components": {
      "LightComponent": { "intensity": 1e100 }
    }
  }]
})");
  karma::world::World non_finite_world;
  karma::world::Scene non_finite_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(non_finite_world,
                                                   non_finite_scene,
                                                   non_finite)
                     .has_value());
  KARMA_REQUIRE(non_finite_world.entities().empty());
}

Json variableFeaturePrefabJson() {
  return Json{
      {"version", 2},
      {"root", 0},
      {"variables",
       Json{
           {"height", Json{{"type", "float"}, {"default", 2.0}}},
           {"radius", Json{{"type", "float"}, {"default", 0.5}}},
           {"intensity", Json{{"type", "float"}, {"default", 1.5}}},
           {"segments", Json{{"type", "int"}, {"default", 3}}},
           {"enabled", Json{{"type", "bool"}, {"default", true}}},
           {"texture", Json{{"type", "string"}, {"default", "default/texture"}}},
           {"offset",
            Json{{"type", "vec3"}, {"default", Json::array({0.25, 0.5, 0.75})}}},
           {"color",
            Json{{"type", "color"}, {"default", Json::array({0.4, 0.8, 1.0, 1.0})}}},
       }},
      {"nodes",
       Json::array({Json{
           {"id", 0},
           {"name", "VariableRoot"},
           {"parent", nullptr},
           {"components",
            Json{
                {"TransformComponent",
                 Json{
                     {"position", Json{{"$var", "offset"}}},
                     {"rotation", Json::array({0, 0, 0, 1})},
                     {"scale",
                      Json::array({Json{{"$var", "radius"}},
                                   Json{{"$expr", "height * 0.5 + 0.25"}},
                                   Json{{"$var", "radius"}}})},
                 }},
                {"LightComponent",
                 Json{
                     {"type", "point"},
                     {"color", Json{{"$var", "color"}}},
                     {"intensity", Json{{"$expr", "height * intensity + 1"}}},
                     {"range", Json{{"$expr", "segments * 2"}}},
                 }},
                {"ParticleEmitterComponent",
                 Json{
                     {"enabled", Json{{"$var", "enabled"}}},
                     {"playing", true},
                     {"texture_key", Json{{"$var", "texture"}}},
                 }},
            }},
       }})},
  };
}

void testPublicPrefabDocumentLoading(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "catalog/variables.prefab.json";
  std::filesystem::create_directories(path.parent_path());
  writeText(path, variableFeaturePrefabJson().dump(2));

  const karma::prefabs::PrefabLoadResult loaded =
      karma::prefabs::loadPrefabDocument(path);
  KARMA_REQUIRE(loaded.success());
  KARMA_REQUIRE(loaded.source_path == path);
  KARMA_REQUIRE(loaded.document->variables.contains("height"));
  KARMA_REQUIRE(loaded.document->variables["height"]["type"] == "float");
  KARMA_REQUIRE(loaded.document->nodes.size() == 1u);
  KARMA_REQUIRE(loaded.document->nodes[0].name == "VariableRoot");
  KARMA_REQUIRE(loaded.document->nodes[0]
                    .components["TransformComponent"]["position"]
                    .contains("$var"));

  const std::filesystem::path directory_prefab = dir / "catalog/directory";
  std::filesystem::create_directories(directory_prefab);
  writeText(directory_prefab / "prefab.json", simplePrefabJson());
  const karma::prefabs::PrefabLoadResult directory_loaded =
      karma::prefabs::loadPrefabDocument(directory_prefab);
  KARMA_REQUIRE(directory_loaded.success());
  KARMA_REQUIRE(directory_loaded.source_path == directory_prefab / "prefab.json");

  Json invalid_variables = variableFeaturePrefabJson();
  invalid_variables["variables"]["height"].erase("default");
  const std::filesystem::path invalid_path = dir / "catalog/invalid.prefab.json";
  writeText(invalid_path, invalid_variables.dump(2));
  const karma::prefabs::PrefabLoadResult invalid =
      karma::prefabs::loadPrefabDocument(invalid_path);
  KARMA_REQUIRE(!invalid.success());
  KARMA_REQUIRE(!invalid.document.has_value());
  KARMA_REQUIRE(!invalid.diagnostics.empty());

  const std::filesystem::path malformed_path = dir / "catalog/malformed.prefab.json";
  writeText(malformed_path, "{ malformed");
  const karma::prefabs::PrefabLoadResult malformed =
      karma::prefabs::loadPrefabDocument(malformed_path);
  KARMA_REQUIRE(!malformed.success());
  KARMA_REQUIRE(!malformed.diagnostics.empty());

  Json unknown_component = variableFeaturePrefabJson();
  unknown_component["nodes"][0]["components"]["UnknownGameplayComponent"] =
      Json::object();
  const std::filesystem::path unknown_path =
      dir / "catalog/unknown-component.prefab.json";
  writeText(unknown_path, unknown_component.dump(2));
  const karma::prefabs::PrefabLoadResult unknown =
      karma::prefabs::loadPrefabDocument(unknown_path);
  KARMA_REQUIRE(!unknown.success());
  KARMA_REQUIRE(!unknown.diagnostics.empty());

  Json invalid_component = variableFeaturePrefabJson();
  invalid_component["nodes"][0]["components"]["TransformComponent"]
                   ["scale"] = Json::array({1.0f, "large", 1.0f});
  const std::filesystem::path invalid_component_path =
      dir / "catalog/invalid-component.prefab.json";
  writeText(invalid_component_path, invalid_component.dump(2));
  const karma::prefabs::PrefabLoadResult invalid_component_result =
      karma::prefabs::loadPrefabDocument(invalid_component_path);
  KARMA_REQUIRE(!invalid_component_result.success());
  KARMA_REQUIRE(!invalid_component_result.diagnostics.empty());
}

void testPrefabVariablesResolveDefaultsAndOverrides(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "variables.prefab.json";
  writeText(path, variableFeaturePrefabJson().dump(2));

  {
    karma::world::World world;
    karma::world::Scene scene;
    const auto instance = karma::prefabs::instantiatePrefab(world, scene, path);
    KARMA_REQUIRE(instance.has_value());

    const auto& transform =
        world.get<karma::components::TransformComponent>(instance->root);
    KARMA_REQUIRE(nearlyVec3(transform.localPosition(), {0.25f, 0.5f, 0.75f}));
    KARMA_REQUIRE(nearlyVec3(transform.localScale(), {0.5f, 1.25f, 0.5f}));

    const auto& light = world.get<karma::components::LightComponent>(instance->root);
    KARMA_REQUIRE(nearly(light.color.g, 0.8f));
    KARMA_REQUIRE(nearly(light.intensity, 4.0f));
    KARMA_REQUIRE(nearly(light.range, 6.0f));

    const auto& emitter =
        world.get<karma::components::ParticleEmitterComponent>(instance->root);
    KARMA_REQUIRE(emitter.enabled);
    KARMA_REQUIRE(emitter.texture_key == "default/texture");
  }

  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.variables["height"] = 4.0;
  desc.variables["radius"] = 1.25;
  desc.variables["intensity"] = 2.0;
  desc.variables["segments"] = 5;
  desc.variables["enabled"] = false;
  desc.variables["texture"] = "override/texture";
  desc.variables["offset"] = Json::array({1.0, 2.0, 3.0});
  desc.variables["color"] = Json::array({0.1, 0.2, 0.3, 0.4});

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path, desc);
  KARMA_REQUIRE(instance.has_value());

  const auto& transform = world.get<karma::components::TransformComponent>(instance->root);
  KARMA_REQUIRE(nearlyVec3(transform.localPosition(), {1.0f, 2.0f, 3.0f}));
  KARMA_REQUIRE(nearlyVec3(transform.localScale(), {1.25f, 2.25f, 1.25f}));

  const auto& light = world.get<karma::components::LightComponent>(instance->root);
  KARMA_REQUIRE(nearly(light.color.r, 0.1f));
  KARMA_REQUIRE(nearly(light.color.a, 0.4f));
  KARMA_REQUIRE(nearly(light.intensity, 9.0f));
  KARMA_REQUIRE(nearly(light.range, 10.0f));

  const auto& emitter =
      world.get<karma::components::ParticleEmitterComponent>(instance->root);
  KARMA_REQUIRE(!emitter.enabled);
  KARMA_REQUIRE(emitter.texture_key == "override/texture");
}

void testPrefabVariableFailures(const std::filesystem::path& dir) {
  auto variables = [] {
    return Json{
        {"height", Json{{"type", "float"}, {"default", 2.0}}},
        {"color",
         Json{{"type", "color"}, {"default", Json::array({1.0, 0.5, 0.25, 1.0})}}},
    };
  };

  auto prefabWithPosition = [&](Json position) {
    return Json{
        {"version", 2},
        {"root", 0},
        {"variables", variables()},
        {"nodes",
         Json::array({Json{
             {"id", 0},
             {"name", "BrokenVariableRoot"},
             {"parent", nullptr},
             {"components",
              Json{{"TransformComponent",
                    Json{
                        {"position", std::move(position)},
                        {"rotation", Json::array({0, 0, 0, 1})},
                        {"scale", Json::array({1, 1, 1})},
                    }}}},
         }})},
    };
  };

  const std::filesystem::path unknown_override = dir / "variable_unknown_override.json";
  writeText(unknown_override,
            prefabWithPosition(Json::array({0, Json{{"$expr", "height"}}, 0})).dump(2));
  karma::prefabs::PrefabInstantiateDesc unknown_override_desc{};
  unknown_override_desc.variables["missing"] = 1.0;
  karma::world::World unknown_override_world;
  karma::world::Scene unknown_override_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(unknown_override_world,
                                                   unknown_override_scene,
                                                   unknown_override,
                                                   unknown_override_desc)
                      .has_value());
  KARMA_REQUIRE(unknown_override_world.entities().empty());

  const std::filesystem::path unknown_reference = dir / "variable_unknown_reference.json";
  writeText(unknown_reference,
            prefabWithPosition(Json::array({0, Json{{"$expr", "missing + 1"}}, 0}))
                .dump(2));
  karma::world::World unknown_reference_world;
  karma::world::Scene unknown_reference_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(unknown_reference_world,
                                                   unknown_reference_scene,
                                                   unknown_reference)
                      .has_value());
  KARMA_REQUIRE(unknown_reference_world.entities().empty());

  const std::filesystem::path invalid_expression = dir / "variable_invalid_expr.json";
  writeText(invalid_expression,
            prefabWithPosition(Json::array({0, Json{{"$expr", "height *"}}, 0}))
                .dump(2));
  karma::world::World invalid_expression_world;
  karma::world::Scene invalid_expression_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(invalid_expression_world,
                                                   invalid_expression_scene,
                                                   invalid_expression)
                      .has_value());
  KARMA_REQUIRE(invalid_expression_world.entities().empty());

  const std::filesystem::path type_mismatch = dir / "variable_type_mismatch.json";
  writeText(type_mismatch,
            prefabWithPosition(Json::array({0, Json{{"$expr", "height"}}, 0})).dump(2));
  karma::prefabs::PrefabInstantiateDesc type_mismatch_desc{};
  type_mismatch_desc.variables["height"] = "tall";
  karma::world::World type_mismatch_world;
  karma::world::Scene type_mismatch_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(type_mismatch_world,
                                                   type_mismatch_scene,
                                                   type_mismatch,
                                                   type_mismatch_desc)
                      .has_value());
  KARMA_REQUIRE(type_mismatch_world.entities().empty());

  const std::filesystem::path division_by_zero = dir / "variable_division_by_zero.json";
  writeText(division_by_zero,
            prefabWithPosition(Json::array({0, Json{{"$expr", "height / 0"}}, 0}))
                .dump(2));
  karma::world::World division_by_zero_world;
  karma::world::Scene division_by_zero_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(division_by_zero_world,
                                                   division_by_zero_scene,
                                                   division_by_zero)
                      .has_value());
  KARMA_REQUIRE(division_by_zero_world.entities().empty());

  const std::filesystem::path missing_default = dir / "variable_missing_default.json";
  Json missing_default_json = prefabWithPosition(Json::array({0, 0, 0}));
  missing_default_json["variables"]["height"].erase("default");
  writeText(missing_default, missing_default_json.dump(2));
  karma::world::World missing_default_world;
  karma::world::Scene missing_default_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(missing_default_world,
                                                   missing_default_scene,
                                                   missing_default)
                      .has_value());
  KARMA_REQUIRE(missing_default_world.entities().empty());

  const std::filesystem::path cleanup = dir / "variable_cleanup_after_component_fail.json";
  writeText(cleanup, prefabWithPosition(Json{{"$var", "color"}}).dump(2));
  karma::world::World cleanup_world;
  karma::world::Scene cleanup_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(cleanup_world, cleanup_scene, cleanup)
                     .has_value());
  KARMA_REQUIRE(cleanup_world.entities().empty());
}

std::string volumetricPrefabJson(const std::string& component_payload) {
  return R"({
  "version": 2,
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
  authored.radius = 0.35f;
  authored.capsule_half_length = 2.25f;
  authored.scale_with_transform = false;
  authored.visible = true;
  authored.overlay_depth = 0.16f;
  authored.surface_double_sided = true;
  authored.interior_material_key = "tests/volume/interior";
  authored.surface_material_key = "tests/volume/surface";
  world.add(root, authored);

  const std::filesystem::path path = dir / "volumetric_round_trip.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  const Json saved = readJson(path);
  const Json& components = saved["nodes"][0]["components"];
  KARMA_REQUIRE(components.contains("VolumetricComponent"));
  KARMA_REQUIRE(!components.contains("VolumeSphereComponent"));
  KARMA_REQUIRE(components["VolumetricComponent"]["shape"] == "capsule");
  KARMA_REQUIRE(components["VolumetricComponent"]["interior_material_key"] ==
                "tests/volume/interior");
  KARMA_REQUIRE(components["VolumetricComponent"]["surface_material_key"] ==
                "tests/volume/surface");
  KARMA_REQUIRE(components["VolumetricComponent"]["surface_double_sided"] == true);

  karma::assets::AssetRegistry assets;
  KARMA_REQUIRE(assets.registerMaterialAsset("tests/volume/interior",
                                             karma::rendering::MaterialDesc{}));
  KARMA_REQUIRE(assets.registerMaterialAsset("tests/volume/surface",
                                             karma::rendering::MaterialDesc{}));
  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.assets = &assets;
  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance =
      karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path, desc);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(loaded_world.has<karma::components::VolumetricComponent>(instance->root));
  const auto& loaded =
      loaded_world.get<karma::components::VolumetricComponent>(instance->root);
  KARMA_REQUIRE(loaded.shape == karma::components::VolumetricShape::Capsule);
  KARMA_REQUIRE(nearly(loaded.radius, 0.35f));
  KARMA_REQUIRE(nearly(loaded.capsule_half_length, 2.25f));
  KARMA_REQUIRE(nearly(loaded.overlay_depth, 0.16f));
  KARMA_REQUIRE(loaded.surface_double_sided);
  KARMA_REQUIRE(loaded.interior_material_key == "tests/volume/interior");
  KARMA_REQUIRE(loaded.surface_material_key == "tests/volume/surface");
}

void testVolumetricComponentValidation(const std::filesystem::path& dir) {
  const std::string valid = R"({
          "shape": "sphere",
          "radius": 2.0,
          "capsule_half_length": 1.0,
          "interior_material_key": "",
          "surface_material_key": ""
        })";
  const std::filesystem::path derived_path = dir / "volumetric_valid.json";
  writeText(derived_path, volumetricPrefabJson(valid));
  karma::world::World derived_world;
  karma::world::Scene derived_scene;
  const auto derived =
      karma::prefabs::instantiatePrefab(derived_world, derived_scene, derived_path);
  KARMA_REQUIRE(derived.has_value());
  const auto& volume =
      derived_world.get<karma::components::VolumetricComponent>(derived->root);
  KARMA_REQUIRE(volume.shape == karma::components::VolumetricShape::Sphere);
  KARMA_REQUIRE(!volume.surface_double_sided);
  KARMA_REQUIRE(volume.interior_material_key.empty());
  KARMA_REQUIRE(volume.surface_material_key.empty());

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
      {"legacy_color", R"({
          "shape": "sphere",
          "color": [0.18, 0.82, 1.0, 1.0],
          "radius": 1.0,
          "capsule_half_length": 1.0
        })"},
      {"legacy_center_opacity", R"({
          "shape": "sphere",
          "center_opacity": 0.62,
          "radius": 1.0,
          "capsule_half_length": 1.0
        })"},
      {"legacy_distortion_strength", R"({
          "shape": "sphere",
          "distortion_strength": 0.5,
          "radius": 1.0,
          "capsule_half_length": 1.0
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

  const std::vector<std::string> removed_fields{
      "emissive_color",
      "density",
      "scattering",
      "anisotropy",
      "absorption",
      "noise_strength",
  };
  for (const std::string& field : removed_fields) {
    const bool color_field = field.find("color") != std::string::npos;
    const std::string payload = std::string(R"({
          "shape": "sphere",
          "radius": 1.0,
          "capsule_half_length": 1.0,
          ")") + field + R"(": )" +
                                (color_field ? "[0.0, 0.0, 0.0, 1.0]" : "0.5") +
                                R"(
        })";
    const std::filesystem::path path = dir / ("legacy_" + field + ".json");
    writeText(path, volumetricPrefabJson(payload));
    karma::world::World invalid_world;
    karma::world::Scene invalid_scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(invalid_world, invalid_scene, path)
                       .has_value());
    KARMA_REQUIRE(invalid_world.entities().empty());
  }

  const std::string missing_material = R"({
          "shape": "sphere",
          "radius": 1.0,
          "capsule_half_length": 1.0,
          "interior_material_key": "tests/volume/missing",
          "surface_material_key": ""
        })";
  const std::filesystem::path missing_path = dir / "volumetric_missing_material.json";
  writeText(missing_path, volumetricPrefabJson(missing_material));
  karma::assets::AssetRegistry assets;
  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.assets = &assets;
  karma::world::World missing_world;
  karma::world::Scene missing_scene;
  KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(missing_world,
                                                   missing_scene,
                                                   missing_path,
                                                   desc)
                     .has_value());
  KARMA_REQUIRE(missing_world.entities().empty());

  KARMA_REQUIRE(assets.registerMaterialAsset("tests/volume/missing",
                                             karma::rendering::MaterialDesc{}));
  karma::world::World resolved_world;
  karma::world::Scene resolved_scene;
  const auto resolved =
      karma::prefabs::instantiatePrefab(resolved_world, resolved_scene, missing_path, desc);
  KARMA_REQUIRE(resolved.has_value());
  const auto& resolved_volume =
      resolved_world.get<karma::components::VolumetricComponent>(resolved->root);
  KARMA_REQUIRE(resolved_volume.interior_material_key == "tests/volume/missing");
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
  authored.source_revision = 42u;
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
  KARMA_REQUIRE(terrain_json["source_revision"] == 42u);
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
  KARMA_REQUIRE(loaded.tile_directory ==
                (dir / "terrain_tiles").lexically_normal());
  KARMA_REQUIRE(loaded.height_pattern == "dem_{x}_{z}.r32");
  KARMA_REQUIRE(loaded.color_pattern == "ortho_{x}_{z}.png");
  KARMA_REQUIRE(loaded.control_pattern == "splat_{x}_{y}.png");
  KARMA_REQUIRE(loaded.height_format == karma::components::TerrainHeightFormat::R32Float);
  KARMA_REQUIRE(loaded.raw_width == 513u);
  KARMA_REQUIRE(loaded.raw_height == 513u);
  KARMA_REQUIRE(loaded.flip_y);
  KARMA_REQUIRE(loaded.tile_index_base == 1);
  KARMA_REQUIRE(loaded.source_revision == 42u);
  KARMA_REQUIRE(loaded.material_layers.size() == 1u);
  KARMA_REQUIRE(loaded.material_layers[0].name == "grass");
  KARMA_REQUIRE(loaded.material_layers[0].material_key == "terrain/grass");
  KARMA_REQUIRE(loaded.material_layers[0].albedo_image ==
                (dir / "terrain/materials/grass_albedo.png").lexically_normal());
  KARMA_REQUIRE(loaded.material_layers[0].normal_image ==
                (dir / "terrain/materials/grass_normal.png").lexically_normal());
  KARMA_REQUIRE(loaded.material_layers[0].roughness_image ==
                (dir / "terrain/materials/grass_roughness.png").lexically_normal());
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
  karma::components::FoliageComponent foliage{};
  foliage.sidecar_path = "foliage/trees.kfoliage";
  foliage.mesh_asset_key = "trees/mesh";
  world.add(root, foliage);
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
  KARMA_REQUIRE(loaded.height_image ==
                (dir / "terrain/fixed_height.png").lexically_normal());
  KARMA_REQUIRE(loaded.heatmap_image ==
                (dir / "terrain/fixed_heatmap.png").lexically_normal());
  KARMA_REQUIRE(loaded.color_image ==
                (dir / "terrain/fixed_color.png").lexically_normal());
  KARMA_REQUIRE(loaded.control_image ==
                (dir / "terrain/fixed_splat.png").lexically_normal());
  KARMA_REQUIRE(loaded_world.get<karma::components::FoliageComponent>(
                    instance->root).sidecar_path ==
                (dir / "foliage/trees.kfoliage").lexically_normal());
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

  world.get<karma::components::TerrainComponent>(root).height_image =
      "/machine-specific/terrain.r32";
  KARMA_REQUIRE(!karma::prefabs::savePrefab(
      world, scene, root, dir / "unsafe-terrain-prefab.json"));
  KARMA_REQUIRE(
      !std::filesystem::exists(dir / "unsafe-terrain-prefab.json"));
}

void testInstantiatedFileBackedComponentsCanBeResaved(
    const std::filesystem::path& dir) {
  const std::filesystem::path prefab_dir = dir / "resaved_file_components";
  const std::filesystem::path source_path = prefab_dir / "source.json";
  const std::filesystem::path resaved_path = prefab_dir / "resaved.json";

  karma::world::World authored_world;
  karma::world::Scene authored_scene;
  const karma::world::Entity root = authored_world.createEntity();
  authored_scene.createNode(root);
  authored_world.setName(root, "FileBackedTerrain");

  karma::components::TerrainComponent terrain{};
  terrain.source = karma::components::TerrainSourceType::SingleImage;
  terrain.height_image = "terrain/height.r32";
  terrain.heatmap_image = "terrain/heat.png";
  terrain.color_image = "terrain/color.png";
  terrain.control_image = "terrain/control.png";
  terrain.height_format = karma::components::TerrainHeightFormat::R32Float;
  terrain.raw_width = 513u;
  terrain.raw_height = 513u;
  terrain.tile_resolution = 513u;
  terrain.material_layers.push_back(karma::components::TerrainMaterialLayer{
      .name = "ground",
      .albedo_image = "terrain/materials/albedo.png",
      .normal_image = "terrain/materials/normal.png",
      .roughness_image = "terrain/materials/roughness.png",
  });
  terrain.data_maps.push_back(karma::components::TerrainDataMapBinding{
      .name = "flow",
      .kind = karma::components::TerrainDataMapKind::Flow,
      .image = "terrain/data/flow.png",
  });
  authored_world.add(root, terrain);

  karma::components::FoliageComponent foliage{};
  foliage.sidecar_path = "foliage/trees.kfoliage";
  foliage.mesh_asset_key = "trees/mesh";
  authored_world.add(root, foliage);

  KARMA_REQUIRE(karma::prefabs::savePrefab(
      authored_world, authored_scene, root, source_path));

  karma::world::World instance_world;
  karma::world::Scene instance_scene;
  const auto instance = karma::prefabs::instantiatePrefab(
      instance_world, instance_scene, source_path);
  KARMA_REQUIRE(instance.has_value());

  auto& instance_terrain =
      instance_world.get<karma::components::TerrainComponent>(instance->root);
  auto& instance_foliage =
      instance_world.get<karma::components::FoliageComponent>(instance->root);
  KARMA_REQUIRE(instance_terrain.height_image.is_absolute());
  KARMA_REQUIRE(instance_terrain.material_layers[0].albedo_image.is_absolute());
  KARMA_REQUIRE(instance_terrain.data_maps[0].image.is_absolute());
  KARMA_REQUIRE(instance_foliage.sidecar_path.is_absolute());
  instance_terrain.height_scale = 321.0f;
  instance_foliage.view_distance = 512.0f;

  KARMA_REQUIRE(karma::prefabs::savePrefab(
      instance_world, instance_scene, instance->root, resaved_path));
  const Json resaved = readJson(resaved_path);
  const Json& terrain_json =
      resaved["nodes"][0]["components"]["TerrainComponent"];
  KARMA_REQUIRE(terrain_json["height_image"] == "terrain/height.r32");
  KARMA_REQUIRE(terrain_json["heatmap_image"] == "terrain/heat.png");
  KARMA_REQUIRE(terrain_json["color_image"] == "terrain/color.png");
  KARMA_REQUIRE(terrain_json["control_image"] == "terrain/control.png");
  KARMA_REQUIRE(terrain_json["material_layers"][0]["albedo_image"] ==
                "terrain/materials/albedo.png");
  KARMA_REQUIRE(terrain_json["material_layers"][0]["normal_image"] ==
                "terrain/materials/normal.png");
  KARMA_REQUIRE(terrain_json["material_layers"][0]["roughness_image"] ==
                "terrain/materials/roughness.png");
  KARMA_REQUIRE(terrain_json["data_maps"][0]["image"] ==
                "terrain/data/flow.png");
  KARMA_REQUIRE(resaved["nodes"][0]["components"]["FoliageComponent"]
                       ["sidecar_path"] == "foliage/trees.kfoliage");

  const auto loaded = karma::prefabs::loadPrefabDocument(resaved_path);
  KARMA_REQUIRE(loaded.success());
  karma::world::World reloaded_world;
  karma::world::Scene reloaded_scene;
  const auto reloaded = karma::prefabs::instantiatePrefab(
      reloaded_world, reloaded_scene, resaved_path);
  KARMA_REQUIRE(reloaded.has_value());
  const auto& reloaded_terrain =
      reloaded_world.get<karma::components::TerrainComponent>(reloaded->root);
  const auto& reloaded_foliage =
      reloaded_world.get<karma::components::FoliageComponent>(reloaded->root);
  KARMA_REQUIRE(reloaded_terrain.height_image ==
                (prefab_dir / "terrain/height.r32").lexically_normal());
  KARMA_REQUIRE(nearly(reloaded_terrain.height_scale, 321.0f));
  KARMA_REQUIRE(reloaded_foliage.sidecar_path ==
                (prefab_dir / "foliage/trees.kfoliage").lexically_normal());
  KARMA_REQUIRE(nearly(reloaded_foliage.view_distance, 512.0f));
}

void testTerrainComponentRejectsUnsafeDimensions() {
  karma::prefabs::ensureBuiltinComponentSerializers();
  const auto* serializer =
      karma::prefabs::componentSerializerRegistry().find("TerrainComponent");
  KARMA_REQUIRE(serializer != nullptr);

  karma::world::World authored_world;
  const karma::world::Entity authored_entity = authored_world.createEntity();
  authored_world.add(authored_entity,
                     karma::components::TerrainComponent{
                         .source = karma::components::TerrainSourceType::SingleImage,
                         .height_image = "height.r32",
                         .height_format =
                             karma::components::TerrainHeightFormat::R32Float,
                         .raw_width = 513u,
                         .raw_height = 513u,
                         .terrain_size = 512.0f,
                         .tile_resolution = 513u,
                     });
  const Json valid = serializer->serialize(authored_world, authored_entity);

  const auto rejected = [&](Json payload) {
    karma::world::World world;
    const karma::world::Entity entity = world.createEntity();
    KARMA_REQUIRE(!serializer->deserialize(world, entity, payload));
    KARMA_REQUIRE(!world.has<karma::components::TerrainComponent>(entity));
  };

  Json excessive_tile = valid;
  excessive_tile["tile_resolution"] =
      karma::rendering::kMaxTerrainTileResolution + 1u;
  rejected(std::move(excessive_tile));

  Json excessive_raw_width = valid;
  excessive_raw_width["raw_width"] = std::numeric_limits<uint32_t>::max();
  rejected(std::move(excessive_raw_width));

  Json excessive_raw_height = valid;
  excessive_raw_height["raw_height"] = std::numeric_limits<uint32_t>::max();
  rejected(std::move(excessive_raw_height));

  Json absolute_height = valid;
  absolute_height["height_image"] = "/outside/height.r32";
  rejected(std::move(absolute_height));

  Json parent_traversal = valid;
  parent_traversal["height_image"] = "../../outside/height.r32";
  rejected(std::move(parent_traversal));

  Json escaped_pattern = valid;
  escaped_pattern["height_pattern"] = "../tiles/{x}_{y}.r32";
  rejected(std::move(escaped_pattern));
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
  "version": 2,
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
  const karma::world::Entity external = world.createEntity();
  world.add(external, karma::components::TransformComponent{});
  const karma::world::NodeId external_node = scene.createNode(external);
  KARMA_REQUIRE(scene.reparent(external_node, scene.findNode(child)));
  KARMA_REQUIRE(world.isAlive(root));
  KARMA_REQUIRE(world.isAlive(child));
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, root));
  KARMA_REQUIRE(!world.isAlive(root));
  KARMA_REQUIRE(!world.isAlive(child));
  KARMA_REQUIRE(scene.findNode(root) == karma::world::Node::kInvalidId);
  KARMA_REQUIRE(scene.findNode(child) == karma::world::Node::kInvalidId);
  KARMA_REQUIRE(world.isAlive(external));
  KARMA_REQUIRE(scene.isAlive(scene.findNode(external)));
  KARMA_REQUIRE(scene.get(scene.findNode(external)).parent ==
                karma::world::Node::kInvalidId);

  const auto dead_root_instance = karma::prefabs::instantiatePrefab(world, scene, path);
  KARMA_REQUIRE(dead_root_instance.has_value());
  const karma::world::Entity dead_root = dead_root_instance->root;
  const karma::world::Entity owned_child = dead_root_instance->find("Child");
  const karma::world::NodeId dead_root_node = scene.findNode(dead_root);
  world.destroyEntity(dead_root);
  KARMA_REQUIRE(!world.isAlive(dead_root));
  KARMA_REQUIRE(scene.isAlive(dead_root_node));
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, dead_root));
  KARMA_REQUIRE(!world.isAlive(owned_child));
  KARMA_REQUIRE(!scene.isAlive(dead_root_node));
}

void testWorldIdentityAndCrossWorldPrefabOwnership(const std::filesystem::path& dir) {
  const std::filesystem::path path = dir / "cross_world.json";
  writeText(path, simplePrefabJson());

  karma::world::World first_world;
  karma::world::World second_world;
  karma::world::Scene first_scene;
  karma::world::Scene second_scene;
  KARMA_REQUIRE(first_world.instanceId() != second_world.instanceId());

  const auto first = karma::prefabs::instantiatePrefab(first_world, first_scene, path);
  const auto second = karma::prefabs::instantiatePrefab(second_world, second_scene, path);
  KARMA_REQUIRE(first.has_value());
  KARMA_REQUIRE(second.has_value());
  KARMA_REQUIRE(first->root == second->root);

  KARMA_REQUIRE(karma::prefabs::destroyPrefab(first_world, first_scene, first->root));
  KARMA_REQUIRE(!first_world.isAlive(first->root));
  KARMA_REQUIRE(second_world.isAlive(second->root));

  const uint64_t transferred_id = second_world.instanceId();
  karma::world::World moved_world(std::move(second_world));
  KARMA_REQUIRE(moved_world.instanceId() == transferred_id);
  KARMA_REQUIRE(second_world.instanceId() != transferred_id);
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(moved_world, second_scene, second->root));

  alignas(karma::world::World)
      std::array<std::byte, sizeof(karma::world::World)> storage{};
  auto* reused = std::construct_at(
      reinterpret_cast<karma::world::World*>(storage.data()));
  const uint64_t first_address_id = reused->instanceId();
  std::destroy_at(reused);
  reused = std::construct_at(
      reinterpret_cast<karma::world::World*>(storage.data()));
  KARMA_REQUIRE(reused->instanceId() != first_address_id);
  std::destroy_at(reused);
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
    const std::filesystem::path prefab_dir = dir / "package_shared_with_scene";
    std::filesystem::create_directories(prefab_dir / "particles");
    writeText(prefab_dir / "prefab.json", simplePrefabJson());
    writeText(prefab_dir / "particles/test.kpeffect", validParticleEffectJson().dump(2));
    writeText(prefab_dir / "assets.package.json",
              R"({
  "version": 1,
  "assets": [
    { "type": "particle_effect", "key": "shared/effect", "path": "particles/test.kpeffect" }
  ]
})");

    karma::assets::AssetRegistry assets;
    std::string diagnostic;
    const auto scene_package =
        assets.sharedPackageStore().acquirePackage(prefab_dir, &diagnostic);
    KARMA_REQUIRE(scene_package.has_value());
    KARMA_REQUIRE(diagnostic.empty());
    karma::prefabs::bindPrefabAssetRegistry(&assets);

    karma::world::World world;
    karma::world::Scene scene;
    const auto instance = karma::prefabs::instantiatePrefab(world, scene, prefab_dir);
    KARMA_REQUIRE(instance.has_value());
    KARMA_REQUIRE(assets.findParticleEffect("shared/effect") != nullptr);
    KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
    KARMA_REQUIRE(assets.findParticleEffect("shared/effect") != nullptr);

    KARMA_REQUIRE(
        assets.sharedPackageStore().releasePackage(*scene_package));
    KARMA_REQUIRE(assets.findParticleEffect("shared/effect") == nullptr);
    karma::prefabs::clearPrefabAssetPackages();
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

void testParticleSystemBeamStatsWithNullDevice() {
  karma::assets::AssetRegistry assets;
  karma::visual::particles::ParticleEffectDesc effect{};
  effect.emitters.push_back(karma::visual::particles::ParticleEmitterDesc{});
  auto& emitter = effect.emitters.front();
  emitter.texture_key = "generated/test/spark";
  emitter.emitter.enabled = true;
  emitter.emitter.playing = true;
  emitter.emitter.max_particles = 16u;
  emitter.emitter.burst_count = 4u;
  assets.registerParticleEffect("generated/test/sparks", effect);

  karma::world::World world;
  const karma::world::Entity effect_entity = world.createEntity();
  world.add(effect_entity, karma::components::TransformComponent{});
  world.add(effect_entity,
            karma::components::ParticleEffectComponent{
                .effect_key = "generated/test/sparks",
            });

  const karma::world::Entity beam_entity = world.createEntity();
  world.add(beam_entity, karma::components::TransformComponent{});
  world.add(beam_entity,
            karma::components::ParticleBeamComponent{
                .texture_key = "generated/test/beam",
                .local_path_points = {
                    {0.0f, 0.0f, 0.0f},
                    {1.0f, 0.25f, 0.0f},
                    {2.0f, 0.0f, 0.0f},
                },
                .start_width = 0.25f,
                .end_width = 0.1f,
            });

  karma::visual::particles::ParticleSystem system(nullptr, &assets);
  system.update(world, 0.016f, 1.0f);

  KARMA_REQUIRE(world.has<karma::components::ParticleEmitterComponent>(effect_entity));
  const auto& stats = system.lastStats();
  KARMA_REQUIRE(stats.effect_binding_updates == 1u);
  KARMA_REQUIRE(stats.simulated_emitters == 1u);
  KARMA_REQUIRE(stats.visible_emitters == 1u);
  KARMA_REQUIRE(stats.submitted_emitters == 0u);
  KARMA_REQUIRE(stats.submitted_beams == 1u);
  KARMA_REQUIRE(stats.beam_segments == 2u);
}

void testParticlePrefabVariablesResizeCoupledComponents(const std::filesystem::path& dir) {
  const Json prefab = Json{
      {"version", 2},
      {"root", 0},
      {"variables",
       Json{
           {"length", Json{{"type", "float"}, {"default", 5.0}}},
           {"width", Json{{"type", "float"}, {"default", 0.4}}},
           {"intensity", Json{{"type", "float"}, {"default", 6.0}}},
           {"color",
            Json{{"type", "color"}, {"default", Json::array({0.2, 0.7, 1.0, 0.9})}}},
       }},
      {"nodes",
       Json::array({Json{
           {"id", 0},
           {"name", "VariableBeam"},
           {"parent", nullptr},
           {"components",
            Json{
                {"TransformComponent",
                 Json{
                     {"position", Json::array({0, 0, 0})},
                     {"rotation", Json::array({0, 0, 0, 1})},
                     {"scale",
                      Json::array({Json{{"$var", "width"}},
                                   1.0,
                                   Json{{"$expr", "length"}}})},
                 }},
                {"ParticleBeamComponent",
                 Json{
                     {"texture_key", "generated/test/beam"},
                     {"local_path_points",
                      Json::array({Json::array({0, 0, 0}),
                                   Json::array({Json{{"$var", "length"}}, 0, 0})})},
                     {"start_width", Json{{"$var", "width"}}},
                     {"end_width", Json{{"$expr", "width * 0.5"}}},
                     {"start_color", Json{{"$var", "color"}}},
                     {"end_color", Json{{"$var", "color"}}},
                 }},
                {"ParticleEffectOverrideComponent",
                 Json{
                     {"emission_scale", Json{{"$expr", "width + 0.25"}}},
                     {"size_scale", Json{{"$var", "width"}}},
                     {"radius_scale", Json{{"$expr", "width * 2"}}},
                     {"source_path_points",
                      Json::array({Json::array({0, 0, 0}),
                                   Json::array(
                                       {Json{{"$expr", "length * 0.5"}}, 0, 0})})},
                     {"source_radius_max", Json{{"$var", "width"}}},
                     {"start_color", Json{{"$var", "color"}}},
                 }},
                {"LightComponent",
                 Json{
                     {"type", "point"},
                     {"color", Json{{"$var", "color"}}},
                     {"intensity", Json{{"$var", "intensity"}}},
                 }},
            }},
       }})},
  };

  const std::filesystem::path path = dir / "particle_variable_prefab.json";
  writeText(path, prefab.dump(2));

  karma::prefabs::PrefabInstantiateDesc desc{};
  desc.variables["length"] = 9.0;
  desc.variables["width"] = 0.75;
  desc.variables["intensity"] = 12.0;
  desc.variables["color"] = Json::array({1.0, 0.25, 0.1, 0.8});

  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, path, desc);
  KARMA_REQUIRE(instance.has_value());

  const auto& transform = world.get<karma::components::TransformComponent>(instance->root);
  KARMA_REQUIRE(nearlyVec3(transform.localScale(), {0.75f, 1.0f, 9.0f}));

  const auto& beam = world.get<karma::components::ParticleBeamComponent>(instance->root);
  KARMA_REQUIRE(beam.local_path_points.size() == 2u);
  KARMA_REQUIRE(nearly(beam.local_path_points[1].x, 9.0f));
  KARMA_REQUIRE(nearly(beam.start_width, 0.75f));
  KARMA_REQUIRE(nearly(beam.end_width, 0.375f));
  KARMA_REQUIRE(nearly(beam.start_color.g, 0.25f));
  KARMA_REQUIRE(nearly(beam.end_color.a, 0.8f));

  const auto& effect_override =
      world.get<karma::components::ParticleEffectOverrideComponent>(instance->root);
  KARMA_REQUIRE(nearly(effect_override.emission_scale, 1.0f));
  KARMA_REQUIRE(nearly(effect_override.size_scale, 0.75f));
  KARMA_REQUIRE(nearly(effect_override.radius_scale, 1.5f));
  KARMA_REQUIRE(effect_override.source_radius_max.has_value());
  KARMA_REQUIRE(nearly(*effect_override.source_radius_max, 0.75f));
  KARMA_REQUIRE(effect_override.source_path_points.has_value());
  KARMA_REQUIRE(effect_override.source_path_points->size() == 2u);
  KARMA_REQUIRE(nearly((*effect_override.source_path_points)[1].x, 4.5f));
  KARMA_REQUIRE(effect_override.start_color.has_value());
  KARMA_REQUIRE(nearly(effect_override.start_color->r, 1.0f));

  const auto& light = world.get<karma::components::LightComponent>(instance->root);
  KARMA_REQUIRE(nearly(light.intensity, 12.0f));
  KARMA_REQUIRE(nearly(light.color.b, 0.1f));
}

void testParticleBeamComponentPrefabRoundTrip(const std::filesystem::path& dir) {
  karma::world::World world;
  karma::world::Scene scene;
  const auto root = world.createEntity();
  world.setName(root, "beam");
  world.add(root, karma::components::TransformComponent{});
  auto node = scene.createNode(root);
  (void)node;

  karma::components::ParticleBeamComponent beam{};
  beam.enabled = true;
  beam.visible = true;
  beam.layer = 3u;
  beam.depth_test = false;
  beam.blend_mode = karma::components::ParticleBlendMode::Alpha;
  beam.texture_key = "generated/test/beam";
  beam.local_path_points = {
      {0.0f, 0.0f, 0.0f},
      {1.0f, 0.5f, 0.0f},
      {2.0f, 0.0f, 0.0f},
  };
  beam.start_width = 0.35f;
  beam.end_width = 0.12f;
  beam.start_color = {0.2f, 0.8f, 1.0f, 0.9f};
  beam.end_color = {0.7f, 0.2f, 1.0f, 0.2f};
  beam.edge_softness = 0.16f;
  beam.uv_repeat = 2.5f;
  beam.uv_scroll_speed = -1.25f;
  beam.time_scale = 0.75f;
  beam.restart_count = 4u;
  world.add(root, beam);

  const std::filesystem::path path = dir / "particle_beam.prefab.json";
  KARMA_REQUIRE(karma::prefabs::savePrefab(world, scene, root, path));
  const Json saved = readJson(path);
  const Json& serialized = saved["nodes"][0]["components"]["ParticleBeamComponent"];
  KARMA_REQUIRE(serialized["blend_mode"] == "alpha");
  KARMA_REQUIRE(serialized["local_path_points"].size() == 3u);
  KARMA_REQUIRE(serialized["restart_count"] == 4u);

  karma::world::World loaded_world;
  karma::world::Scene loaded_scene;
  const auto instance = karma::prefabs::instantiatePrefab(loaded_world, loaded_scene, path);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(loaded_world.has<karma::components::ParticleBeamComponent>(instance->root));
  const auto& loaded =
      loaded_world.get<karma::components::ParticleBeamComponent>(instance->root);
  KARMA_REQUIRE(loaded.layer == 3u);
  KARMA_REQUIRE(!loaded.depth_test);
  KARMA_REQUIRE(loaded.blend_mode == karma::components::ParticleBlendMode::Alpha);
  KARMA_REQUIRE(loaded.texture_key == "generated/test/beam");
  KARMA_REQUIRE(loaded.local_path_points.size() == 3u);
  KARMA_REQUIRE(nearly(loaded.start_width, 0.35f));
  KARMA_REQUIRE(nearly(loaded.end_width, 0.12f));
  KARMA_REQUIRE(nearly(loaded.edge_softness, 0.16f));
  KARMA_REQUIRE(nearly(loaded.uv_repeat, 2.5f));
  KARMA_REQUIRE(nearly(loaded.uv_scroll_speed, -1.25f));
  KARMA_REQUIRE(nearly(loaded.time_scale, 0.75f));
  KARMA_REQUIRE(loaded.restart_count == 4u);
}

void testParticleBeamComponentValidation(const std::filesystem::path& dir) {
  auto prefabWithBeamPayload = [](const std::string& payload) {
    return R"({
  "version": 2,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "beam",
      "parent": null,
      "components": {
        "TagComponent": { "name": "beam" },
        "TransformComponent": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        },
        "ParticleBeamComponent": )" + payload + R"(
      }
    }
  ]
})";
  };

  const std::string valid = R"({
  "enabled": true,
  "visible": true,
  "layer": 0,
  "depth_test": true,
  "blend_mode": "additive",
  "texture_key": "generated/test/beam",
  "local_path_points": [[0, 0, 0], [1, 0, 0]],
  "start_width": 0.2,
  "end_width": 0.1,
  "start_color": [1, 1, 1, 1],
  "end_color": [1, 1, 1, 0],
  "edge_softness": 0.1,
  "uv_repeat": 1.0,
  "uv_scroll_speed": 0.0,
  "time_scale": 1.0,
  "restart_count": 0
})";
  const std::filesystem::path valid_path = dir / "valid_particle_beam.prefab.json";
  writeText(valid_path, prefabWithBeamPayload(valid));
  karma::world::World valid_world;
  karma::world::Scene valid_scene;
  KARMA_REQUIRE(karma::prefabs::instantiatePrefab(valid_world, valid_scene, valid_path)
                    .has_value());

  const std::string invalid_payloads[] = {
      R"({"local_path_points": [[0, 0, 0]], "start_width": 0.2, "end_width": 0.1})",
      R"({"local_path_points": [[0, 0, 0], [1, 0, 0]], "start_width": 0.0, "end_width": 0.1})",
      R"({"local_path_points": [[0, 0, 0], [1, 0, 0]], "start_width": 0.2, "end_width": -0.1})",
      R"({"local_path_points": [[0, 0, 0], [1, 0, 0]], "start_width": 0.2, "end_width": 0.1, "blend_mode": "distortion"})",
  };
  for (std::size_t i = 0; i < std::size(invalid_payloads); ++i) {
    const std::filesystem::path path =
        dir / ("invalid_particle_beam_" + std::to_string(i) + ".prefab.json");
    writeText(path, prefabWithBeamPayload(invalid_payloads[i]));
    karma::world::World invalid_world;
    karma::world::Scene invalid_scene;
    KARMA_REQUIRE(!karma::prefabs::instantiatePrefab(invalid_world, invalid_scene, path)
                       .has_value());
  }
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
  authored.emitter.burst_count = 10u;
  authored.emitter.spawn_rate = 20.0f;
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
  effect_override.emission_scale = 0.5f;
  effect_override.size_scale = 2.0f;
  effect_override.radius_scale = 2.0f;
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
  KARMA_REQUIRE(applied.max_particles == 16u);
  KARMA_REQUIRE(applied.burst_count == 5u);
  KARMA_REQUIRE(nearly(applied.spawn_rate, 10.0f));
  KARMA_REQUIRE(nearly(applied.start_size_min, 0.4f));
  KARMA_REQUIRE(nearly(applied.start_size_max, 0.8f));
  KARMA_REQUIRE(nearly(applied.start_color.a, 0.4f));
  KARMA_REQUIRE(applied.source_shape == karma::components::ParticleSourceShape::Line);
  KARMA_REQUIRE(applied.source_path_points.size() == 2u);
  KARMA_REQUIRE(nearly(applied.source_path_points[1].x, 4.0f));
  KARMA_REQUIRE(nearly(applied.source_jitter_radius, 0.5f));

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
  effect.emitters[0].emitter.burst_count = 12u;
  effect.emitters[0].emitter.spawn_rate = 40.0f;
  assets.registerParticleEffect("spark", effect);
  system.update(world, 0.016f, 1.0f);
  const auto& reapplied = world.get<karma::components::ParticleEmitterComponent>(entity);
  KARMA_REQUIRE(reapplied.layer == 7u);
  KARMA_REQUIRE(reapplied.max_particles == 32u);
  KARMA_REQUIRE(reapplied.burst_count == 6u);
  KARMA_REQUIRE(nearly(reapplied.spawn_rate, 20.0f));
  KARMA_REQUIRE(!reapplied.enabled);
  KARMA_REQUIRE(!reapplied.playing);
  KARMA_REQUIRE(nearly(reapplied.start_delay, 0.75f));
}

void testParticleAuthoringHelpersRejectInvalidState() {
  karma::world::World world;
  const karma::world::Entity unbound = world.createEntity();
  KARMA_REQUIRE(!karma::visual::particles::setEffectOverrides(
      world, unbound, karma::components::ParticleEffectOverrideComponent{}));

  const karma::world::Entity invalid_effect =
      karma::visual::particles::createEffectEntity(
          world, karma::visual::particles::ParticleEffectEntityDesc{});
  KARMA_REQUIRE(!invalid_effect.isValid());

  const karma::world::Entity effect = karma::visual::particles::createEffectEntity(
      world,
      karma::visual::particles::ParticleEffectEntityDesc{
          .effect_key = "effects/test",
          .effect_override = karma::components::ParticleEffectOverrideComponent{},
      });
  KARMA_REQUIRE(effect.isValid());
  KARMA_REQUIRE(world.has<karma::components::ParticleEffectOverrideComponent>(effect));
  KARMA_REQUIRE(karma::visual::particles::bindEffect(
      world,
      effect,
      karma::visual::particles::ParticleEffectBindingDesc{
          .effect_key = "effects/rebound",
      }));
  KARMA_REQUIRE(!world.has<karma::components::ParticleEffectOverrideComponent>(effect));
  KARMA_REQUIRE(!karma::visual::particles::setEffectSourcePath(
      world, effect, {{0.0f, 0.0f, 0.0f}}));
  KARMA_REQUIRE(!karma::visual::particles::setEffectSourceBoxExtents(
      world, effect, {-1.0f, 1.0f, 1.0f}));

  const karma::world::Entity invalid_beam = karma::visual::particles::createBeamEntity(
      world, karma::visual::particles::ParticleBeamEntityDesc{});
  KARMA_REQUIRE(!invalid_beam.isValid());

  const karma::world::Entity beam = karma::visual::particles::createBeamEntity(
      world,
      karma::visual::particles::ParticleBeamEntityDesc{
          .local_path_points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
      });
  KARMA_REQUIRE(beam.isValid());
  KARMA_REQUIRE(!karma::visual::particles::setBeamPath(
      world, beam, {{0.0f, 0.0f, 0.0f}}));
  KARMA_REQUIRE(
      world.get<karma::components::ParticleBeamComponent>(beam).local_path_points.size() == 2u);
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

  KARMA_REQUIRE(karma::visual::restartLightPulse(world, entity));
  KARMA_REQUIRE(world.get<karma::components::LightPulseComponent>(entity).active);
  KARMA_REQUIRE(nearly(world.get<karma::components::LightPulseComponent>(entity).elapsed, 0.0f));
  KARMA_REQUIRE(visibility.visible);
  system.update(world, 0.0f);
  KARMA_REQUIRE(nearly(light.intensity, 10.0f));
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

void testGeneratedParticleExamplePrefabsDirectLoad() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());

  struct Example {
    const char* path;
    const char* asset_namespace;
    std::size_t expected_beams;
    std::size_t expected_effects;
    bool validate_detect_magic = false;
  };
  const std::array<Example, 13> examples{{
      {"arcane_barrage", "prefabs/arcane_barrage", 12u, 5u},
      {"blade_barrier", "prefabs/blade_barrier", 8u, 6u},
      {"breathe_fire", "prefabs/breathe_fire", 120u, 5u},
      {"chromatic_ray", "prefabs/chromatic_ray", 10u, 4u},
      {"daze", "prefabs/daze", 7u, 5u},
      {"detect_magic", "prefabs/detect_magic", 0u, 4u, true},
      {"fire_ray", "prefabs/fire_ray", 1u, 3u},
      {"fireball/projectile", "prefabs/fireball/projectile", 0u, 6u},
      {"fireball/explosion", "prefabs/fireball/explosion", 0u, 11u},
      {"heal", "prefabs/heal", 7u, 5u},
      {"haste", "prefabs/haste", 14u, 4u},
      {"impact_burst", "prefabs/impact_burst", 0u, 3u},
      {"magic_missile", "prefabs/magic_missile", 1u, 2u},
  }};

  for (const Example& example : examples) {
    const std::filesystem::path prefab_dir =
        repo_root / "examples/assets/prefabs" / example.path;
    const std::string asset_namespace = example.asset_namespace;
    const Json prefab = readJson(prefab_dir / "prefab.json");
    KARMA_REQUIRE(prefab.is_object());
    KARMA_REQUIRE(prefab["version"] == 2);
    if (example.validate_detect_magic) {
      KARMA_REQUIRE(prefab["variables"].is_object());
      KARMA_REQUIRE(prefab["variables"]["radius"]["type"] == "float");
      KARMA_REQUIRE(nearly(prefab["variables"]["radius"]["default"].get<float>(),
                           30.0f * 0.3048f));
    }
    const Json manifest = readJson(prefab_dir / "assets.package.json");
    KARMA_REQUIRE(manifest.is_object());
    KARMA_REQUIRE(manifest["assets"].is_array());
    for (const Json& asset : manifest["assets"]) {
      KARMA_REQUIRE(asset.is_object());
      KARMA_REQUIRE(asset["key"].is_string());
      KARMA_REQUIRE(asset["path"].is_string());
      const std::string key = asset["key"].get<std::string>();
      KARMA_REQUIRE(key.rfind(asset_namespace + "/", 0u) == 0u);
      KARMA_REQUIRE(karma::assets::AssetRegistry::isValidAssetKey(key));
      KARMA_REQUIRE(std::filesystem::exists(prefab_dir /
                                            asset["path"].get<std::string>()));
    }

    karma::assets::AssetRegistry assets;
    karma::prefabs::bindPrefabAssetRegistry(&assets);

    karma::world::World world;
    karma::world::Scene scene;
    karma::prefabs::PrefabInstantiateDesc desc{};
    desc.assets = &assets;
    if (example.validate_detect_magic) {
      desc.variables["radius"] = 4.25f;
    }
    const auto instance =
        karma::prefabs::instantiatePrefab(world, scene, prefab_dir, desc);
    KARMA_REQUIRE(instance.has_value());
    KARMA_REQUIRE(instance->valid());

    std::size_t beam_count = 0u;
    std::size_t effect_count = 0u;
    for (const karma::world::Entity entity : instance->entities) {
      if (world.has<karma::components::ParticleBeamComponent>(entity)) {
        ++beam_count;
        const auto& beam = world.get<karma::components::ParticleBeamComponent>(entity);
        KARMA_REQUIRE(!beam.texture_key.empty());
        KARMA_REQUIRE(beam.texture_key.rfind(asset_namespace + "/", 0u) == 0u);
        KARMA_REQUIRE(beam.local_path_points.size() >= 2u);
        KARMA_REQUIRE(beam.start_width > 0.0f);
        KARMA_REQUIRE(beam.end_width > 0.0f);
        KARMA_REQUIRE(assets.findTextureAsset(beam.texture_key) != nullptr);
      }
      if (world.has<karma::components::ParticleEffectComponent>(entity)) {
        ++effect_count;
        const auto& effect = world.get<karma::components::ParticleEffectComponent>(entity);
        KARMA_REQUIRE(effect.effect_key.rfind(asset_namespace + "/", 0u) == 0u);
        const auto* effect_asset = assets.findParticleEffect(effect.effect_key);
        KARMA_REQUIRE(effect_asset != nullptr);
        KARMA_REQUIRE(!effect_asset->emitters.empty());
        for (const auto& emitter : effect_asset->emitters) {
          KARMA_REQUIRE(emitter.texture_key.rfind(asset_namespace + "/", 0u) == 0u);
          KARMA_REQUIRE(assets.findTextureAsset(emitter.texture_key) != nullptr);
        }
      }
    }
    KARMA_REQUIRE(beam_count == example.expected_beams);
    KARMA_REQUIRE(effect_count == example.expected_effects);

    if (example.validate_detect_magic) {
      const karma::world::Entity volume = instance->find("shimmer_volume");
      KARMA_REQUIRE(volume.isValid());
      KARMA_REQUIRE(world.has<karma::components::VolumetricComponent>(volume));
      const auto& volumetric =
          world.get<karma::components::VolumetricComponent>(volume);
      KARMA_REQUIRE(volumetric.shape == karma::components::VolumetricShape::Sphere);
      KARMA_REQUIRE(nearly(volumetric.radius, 4.25f));
      KARMA_REQUIRE(!volumetric.scale_with_transform);
      KARMA_REQUIRE(volumetric.surface_double_sided);
      KARMA_REQUIRE(volumetric.interior_material_key ==
                    "prefabs/detect_magic/detect_magic_interior_volume");
      KARMA_REQUIRE(volumetric.surface_material_key ==
                    "prefabs/detect_magic/detect_magic_surface_volume");
      KARMA_REQUIRE(assets.findMaterialAsset(volumetric.interior_material_key) != nullptr);
      KARMA_REQUIRE(assets.findMaterialAsset(volumetric.surface_material_key) != nullptr);
      KARMA_REQUIRE(assets.findTextureAsset("prefabs/detect_magic/pixie_dust_atlas") !=
                    nullptr);

      const karma::world::Entity pixie = instance->find("pixie_dust");
      KARMA_REQUIRE(pixie.isValid());
      KARMA_REQUIRE(world.has<karma::components::ParticleEffectOverrideComponent>(
          pixie));
      const auto& pixie_override =
          world.get<karma::components::ParticleEffectOverrideComponent>(pixie);
      KARMA_REQUIRE(pixie_override.source_outer_radius.has_value());
      KARMA_REQUIRE(nearly(*pixie_override.source_outer_radius, 4.25f));
    }

    KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
    karma::prefabs::clearPrefabAssetPackages();
  }
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
  frame.submitted_beams = 2u;
  frame.beam_segments = 5u;
  frame.beam_draw_calls = 1u;
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
  KARMA_REQUIRE(line.find("submitted_beams=2.0") != std::string::npos);
  KARMA_REQUIRE(line.find("beam_segments=5.0") != std::string::npos);
  KARMA_REQUIRE(line.find("beam_draw_calls=1.0") != std::string::npos);
  KARMA_REQUIRE(line.find("simulation_ms=0.500") != std::string::npos);
  KARMA_REQUIRE(line.find("scene_color_copy=true") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_sort_overflow=true") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_global_sort_active=false") != std::string::npos);
  KARMA_REQUIRE(line.find("gpu_grouped_sort_fallback=true") != std::string::npos);
}

}  // namespace

int main() {
  const std::filesystem::path dir = makeTempDir();
  testBuiltinRegistryRepairPreservesOverrides();
  testPersistentComponentRegistryCoverage();
  testPersistentAuthoringSubsetRoundTrips();
  testSaveLoadSingleEntity(dir);
  testInstancedMeshLodPrefabRoundTrip(dir);
  testColliderComponentPrefabRoundTrips(dir);
  testPhysicsAuthoringComponentsPrefabRoundTrip(dir);
  testLegacyRigidbodyPayloadKeepsAdvancedDefaults(dir);
  testHierarchyRoundTrip(dir);
  testContextualPrefabEntityReferences(dir);
  testUnknownComponentFails(dir);
  testMalformedAndInvalidPayloads(dir);
  testPublicPrefabDocumentLoading(dir);
  testPrefabVariablesResolveDefaultsAndOverrides(dir);
  testPrefabVariableFailures(dir);
  testVolumetricComponentPrefabRoundTrip(dir);
  testVolumetricComponentValidation(dir);
  testTerrainComponentPrefabRoundTrip(dir);
  testSingleImageTerrainComponentPrefabRoundTrip(dir);
  testInstantiatedFileBackedComponentsCanBeResaved(dir);
  testTerrainComponentRejectsUnsafeDimensions();
  testMigratedPrefabAssetsDoNotUseLegacyComponentNames();
  testDestroyPrefab(dir);
  testWorldIdentityAndCrossWorldPrefabOwnership(dir);
  testMissingAssetPackageKeepsPrefabLoad(dir);
  testAssetPackageParsingSuccessAndFailure(dir);
  testAssetPackageMissingRegistryAndResourceFailure(dir);
  testParticleEffectParserV3();
  testParticleEffectParserV3SourceShapesAndMultiEmitter();
  testParticleSystemRendererOwnedState();
  testParticleSystemBeamStatsWithNullDevice();
  testParticlePrefabVariablesResizeCoupledComponents(dir);
  testParticleBeamComponentPrefabRoundTrip(dir);
  testParticleBeamComponentValidation(dir);
  testParticleSystemEffectLifecycleReapply();
  testParticleAuthoringHelpersRejectInvalidState();
  testParticleStatsReportFormatting();
  testLightPulseSystem();
  testExplosionPrefabDirectLoad();
  testEnergyOrbPrefabDirectLoad();
  testPathEnergyBeamPrefabDirectLoad();
  testGeneratedParticleExamplePrefabsDirectLoad();
  std::filesystem::remove_all(dir);
  return 0;
}
