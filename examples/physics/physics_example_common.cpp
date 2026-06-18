#include "physics_example_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <variant>

#include "demo_asset_paths.h"
#include "karma/core/math/glm.h"
#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"

namespace karma::demo::physics_examples {

namespace {

math::Vec3 transformPoint(const math::Vec3& center,
                          const math::Quat& rotation,
                          const math::Vec3& local) {
  return vadd(center, rotated(rotation, local));
}

void line(renderer::GraphicsDevice& graphics,
          const math::Vec3& a,
          const math::Vec3& b,
          const math::Color& color,
          bool depth_test,
          float thickness) {
  graphics.drawLine(a, b, color, depth_test, thickness);
}

void drawCircle(renderer::GraphicsDevice& graphics,
                const math::Vec3& center,
                const math::Quat& rotation,
                float radius,
                int plane,
                const math::Color& color,
                bool depth_test,
                float thickness) {
  constexpr int kSegments = 36;
  math::Vec3 previous{};
  for (int i = 0; i <= kSegments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSegments) * kPi * 2.0f;
    const float c = std::cos(t) * radius;
    const float s = std::sin(t) * radius;
    math::Vec3 local{};
    if (plane == 0) {
      local = {c, s, 0.0f};
    } else if (plane == 1) {
      local = {c, 0.0f, s};
    } else {
      local = {0.0f, c, s};
    }
    const math::Vec3 point = transformPoint(center, rotation, local);
    if (i > 0) {
      line(graphics, previous, point, color, depth_test, thickness);
    }
    previous = point;
  }
}

std::vector<math::Vec3> hullPoints(float radius) {
  return {
      {0.0f, radius, 0.0f},
      {radius * 0.95f, radius * 0.15f, 0.0f},
      {0.0f, radius * 0.15f, radius * 0.95f},
      {-radius * 0.95f, radius * 0.15f, 0.0f},
      {0.0f, radius * 0.15f, -radius * 0.95f},
      {radius * 0.62f, -radius * 0.75f, radius * 0.45f},
      {-radius * 0.62f, -radius * 0.75f, radius * 0.45f},
      {-radius * 0.62f, -radius * 0.75f, -radius * 0.45f},
      {radius * 0.62f, -radius * 0.75f, -radius * 0.45f},
  };
}

std::vector<math::Vec3> wedgeVertices(float radius) {
  return {
      {-radius, -radius * 0.35f, -radius},
      {radius, -radius * 0.35f, -radius},
      {radius, -radius * 0.35f, radius},
      {-radius, -radius * 0.35f, radius},
      {-radius, radius * 0.45f, radius},
      {radius, radius * 0.45f, radius},
  };
}

std::vector<uint32_t> wedgeIndices() {
  return {
      0, 1, 2, 0, 2, 3,
      3, 2, 5, 3, 5, 4,
      0, 3, 4, 0, 4, 1,
      1, 4, 5, 1, 5, 2,
      0, 2, 1, 0, 3, 2,
  };
}

void drawPointSet(renderer::GraphicsDevice& graphics,
                  const std::vector<glm::vec3>& points,
                  const math::Vec3& center,
                  const math::Quat& rotation,
                  const math::Color& color,
                  bool depth_test,
                  float thickness) {
  if (points.size() < 2) {
    return;
  }
  std::vector<math::Vec3> world_points;
  world_points.reserve(points.size());
  for (const glm::vec3& point : points) {
    world_points.push_back(transformPoint(center, rotation, math::fromGlm(point)));
  }
  float max_distance_sq = 0.0f;
  for (const math::Vec3& point : world_points) {
    max_distance_sq = std::max(max_distance_sq, math::lengthSquared(vsub(point, center)));
  }
  const float edge_limit = max_distance_sq * 1.55f;
  for (size_t i = 0; i < world_points.size(); ++i) {
    line(graphics, center, world_points[i], color, depth_test, thickness);
    for (size_t j = i + 1; j < world_points.size(); ++j) {
      if (math::lengthSquared(vsub(world_points[i], world_points[j])) <= edge_limit) {
        line(graphics, world_points[i], world_points[j], color, depth_test, thickness);
      }
    }
  }
}

void drawTriangle(renderer::GraphicsDevice& graphics,
                  const std::array<glm::vec3, 3>& triangle,
                  const math::Vec3& center,
                  const math::Quat& rotation,
                  const math::Color& color,
                  bool depth_test,
                  float thickness) {
  const math::Vec3 a = transformPoint(center, rotation, math::fromGlm(triangle[0]));
  const math::Vec3 b = transformPoint(center, rotation, math::fromGlm(triangle[1]));
  const math::Vec3 c = transformPoint(center, rotation, math::fromGlm(triangle[2]));
  line(graphics, a, b, color, depth_test, thickness);
  line(graphics, b, c, color, depth_test, thickness);
  line(graphics, c, a, color, depth_test, thickness);
}

}  // namespace

math::Vec3 vadd(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 vsub(const math::Vec3& a, const math::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

math::Vec3 vscale(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

math::Vec3 componentMul(const math::Vec3& a, const math::Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

math::Vec3 rotated(const math::Quat& q, const math::Vec3& v) {
  return math::rotateVec(q, v);
}

math::Quat axisAngle(const math::Vec3& axis, float radians) {
  const math::Vec3 n = math::normalize(axis);
  const float half = radians * 0.5f;
  const float s = std::sin(half);
  return {n.x * s, n.y * s, n.z * s, std::cos(half)};
}

void bindFlyCameraControls(input::InputSystem& input) {
  input.bindKey("cam_forward", platform::Key::W);
  input.bindKey("cam_backward", platform::Key::S);
  input.bindKey("cam_left", platform::Key::A);
  input.bindKey("cam_right", platform::Key::D);
  input.bindKey("cam_up", platform::Key::E);
  input.bindKey("cam_down", platform::Key::Q);
  input.bindMouse("cam_look", platform::MouseButton::Right);
}

void addDefaultLighting(ecs::World& world, content::AssetRegistry* assets) {
  auto light = world.createEntity();
  components::TransformComponent light_xform{};
  light_xform.setPosition({0.0f, 35.0f, 0.0f});
  light_xform.setRotation(math::fromYawPitch(0.65f, -0.9f));
  world.add(light, light_xform);
  components::LightComponent light_component{};
  light_component.type = components::LightComponent::Type::Directional;
  light_component.color = {1.0f, 0.97f, 0.9f, 1.0f};
  light_component.intensity = 1.15f;
  light_component.shadow_extent = 80.0f;
  world.add(light, light_component);

  auto environment = world.createEntity();
  components::EnvironmentComponent env{};
  env.environment_map_asset_key = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");
  env.intensity = 0.45f;
  env.draw_skybox = true;
  world.add(environment, env);
}

void createFlyCamera(ecs::World& world,
                     CameraRig& rig,
                     const math::Vec3& position,
                     float yaw,
                     float pitch) {
  auto camera = world.createEntity();
  components::TransformComponent transform{};
  transform.setPosition(position);
  transform.setRotation(math::fromYawPitch(yaw, pitch));
  world.add(camera, transform);
  components::CameraComponent camera_component{};
  camera_component.is_primary = true;
  camera_component.fov_y_degrees = 62.0f;
  camera_component.near_clip = 0.05f;
  camera_component.far_clip = 500.0f;
  world.add(camera, camera_component);
  world.add(camera, components::AudioListenerComponent{});

  rig.entity = camera;
  rig.yaw = yaw;
  rig.pitch = pitch;
  rig.target_yaw = yaw;
  rig.target_pitch = pitch;
}

void updateFlyCamera(ecs::World& world, input::InputSystem& input, CameraRig& rig, float dt) {
  if (!world.isAlive(rig.entity)) {
    return;
  }

  constexpr float kLookSensitivity = 0.00085f;
  constexpr float kSmoothing = 20.0f;
  if (input.actionDown("cam_look")) {
    rig.target_yaw -= input.mouseDeltaX() * kLookSensitivity;
    rig.target_pitch -= input.mouseDeltaY() * kLookSensitivity;
  }
  rig.target_pitch = std::clamp(rig.target_pitch, -1.52f, 1.52f);

  const float alpha = 1.0f - std::exp(-kSmoothing * dt);
  rig.yaw += (rig.target_yaw - rig.yaw) * alpha;
  rig.pitch += (rig.target_pitch - rig.pitch) * alpha;

  auto& transform = world.get<components::TransformComponent>(rig.entity);
  const math::Quat rotation = math::fromYawPitch(rig.yaw, rig.pitch);
  const math::Vec3 forward = math::normalize(rotated(rotation, {0.0f, 0.0f, -1.0f}));
  const math::Vec3 up{0.0f, 1.0f, 0.0f};
  const math::Vec3 right = math::normalize(math::cross(forward, up));

  float forward_input = 0.0f;
  float right_input = 0.0f;
  float up_input = 0.0f;
  if (input.actionDown("cam_forward")) forward_input += 1.0f;
  if (input.actionDown("cam_backward")) forward_input -= 1.0f;
  if (input.actionDown("cam_right")) right_input += 1.0f;
  if (input.actionDown("cam_left")) right_input -= 1.0f;
  if (input.actionDown("cam_up")) up_input += 1.0f;
  if (input.actionDown("cam_down")) up_input -= 1.0f;

  const math::Vec3 delta = vadd(vadd(vscale(forward, forward_input), vscale(right, right_input)),
                                vscale(up, up_input));
  math::Vec3 position = transform.getPosition();
  if (math::lengthSquared(delta) > 0.0001f) {
    position = vadd(position, vscale(math::normalize(delta), rig.move_speed * dt));
  }
  transform.setPosition(position);
  transform.setRotation(rotation);
}

void destroyEntities(ecs::World& world, std::vector<ecs::Entity>& entities) {
  for (ecs::Entity entity : entities) {
    if (world.isAlive(entity)) {
      world.destroyEntity(entity);
    }
  }
  entities.clear();
}

void setTransform(ecs::World& world,
                  ecs::Entity entity,
                  const math::Vec3& position,
                  const math::Quat& rotation,
                  const math::Vec3& scale) {
  components::TransformComponent transform{};
  transform.setPosition(position);
  transform.setRotation(rotation);
  transform.setScale(scale);
  world.add(entity, transform);
}

ecs::Entity addStaticBox(ecs::World& world,
                         const math::Vec3& position,
                         const math::Vec3& half_extents,
                         uint32_t layers,
                         uint32_t collides_with) {
  auto entity = world.createEntity();
  setTransform(world, entity, position);
  world.add(entity,
            components::ColliderComponent::box(
                components::BoxColliderShape{.half_extents = half_extents},
                false,
                true));
  components::PhysicsCollisionFilterComponent filter{};
  filter.layers = layers;
  filter.collides_with = collides_with;
  world.add(entity, filter);
  return entity;
}

physics::PhysicsShapeDesc makeBoxShape(const math::Vec3& half_extents) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::Box;
  shape.half_extents = math::toGlm(half_extents);
  return shape;
}

physics::PhysicsShapeDesc makeSphereShape(float radius) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::Sphere;
  shape.radius = radius;
  return shape;
}

physics::PhysicsShapeDesc makeCapsuleShape(float radius, float height) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::Capsule;
  shape.radius = radius;
  shape.height = height;
  return shape;
}

physics::PhysicsShapeDesc makeCylinderShape(float radius, float height, float convex_radius) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::Cylinder;
  shape.radius = radius;
  shape.height = height;
  shape.convex_radius = convex_radius;
  return shape;
}

physics::PhysicsShapeDesc makeTaperedCapsuleShape(float top_radius,
                                                  float bottom_radius,
                                                  float height) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::TaperedCapsule;
  shape.top_radius = top_radius;
  shape.bottom_radius = bottom_radius;
  shape.height = height;
  return shape;
}

physics::PhysicsShapeDesc makeConvexHullShape(float radius) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::ConvexHull;
  for (const math::Vec3& point : hullPoints(radius)) {
    shape.points.push_back(math::toGlm(point));
  }
  shape.convex_radius = 0.02f;
  return shape;
}

physics::PhysicsShapeDesc makeTriangleShape(float size) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::Triangle;
  shape.triangle = {
      glm::vec3{-size, 0.0f, -size},
      glm::vec3{size, 0.0f, -size},
      glm::vec3{0.0f, 0.0f, size},
  };
  shape.convex_radius = 0.02f;
  return shape;
}

physics::PhysicsShapeDesc makeMeshWedgeShape(float radius) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::Mesh;
  for (const math::Vec3& vertex : wedgeVertices(radius)) {
    shape.mesh_vertices.push_back(math::toGlm(vertex));
  }
  shape.mesh_indices = wedgeIndices();
  return shape;
}

physics::PhysicsShapeDesc makeHeightFieldShape(uint32_t sample_count,
                                               float spacing,
                                               float height_scale) {
  physics::PhysicsShapeDesc shape{};
  shape.type = physics::PhysicsShapeType::HeightField;
  shape.height_sample_count = sample_count;
  shape.height_scale = {spacing, height_scale, spacing};
  const float half = static_cast<float>(sample_count - 1u) * spacing * 0.5f;
  shape.height_offset = {-half, 0.0f, -half};
  shape.height_samples.reserve(sample_count * sample_count);
  for (uint32_t z = 0; z < sample_count; ++z) {
    for (uint32_t x = 0; x < sample_count; ++x) {
      const float xf = static_cast<float>(x) * 0.62f;
      const float zf = static_cast<float>(z) * 0.48f;
      shape.height_samples.push_back(std::sin(xf) * 0.45f + std::cos(zf) * 0.35f);
    }
  }
  return shape;
}

physics::PhysicsShapeDesc makeCompoundShape() {
  physics::PhysicsShapeDesc compound{};
  compound.type = physics::PhysicsShapeType::Compound;

  auto box = makeBoxShape({0.6f, 0.35f, 0.45f});
  box.center = {-0.35f, 0.0f, 0.0f};
  compound.children.push_back(box);

  auto sphere = makeSphereShape(0.42f);
  sphere.center = {0.45f, 0.2f, 0.0f};
  compound.children.push_back(sphere);

  auto capsule = makeCapsuleShape(0.22f, 1.25f);
  capsule.center = {0.1f, 0.0f, 0.45f};
  capsule.rotation = glm::angleAxis(kPi * 0.5f, glm::vec3{1.0f, 0.0f, 0.0f});
  compound.children.push_back(capsule);

  return compound;
}

void drawReference(renderer::GraphicsDevice& graphics, float radius) {
  const math::Color grid{0.23f, 0.26f, 0.29f, 0.58f};
  for (int i = -static_cast<int>(radius); i <= static_cast<int>(radius); ++i) {
    const float p = static_cast<float>(i);
    line(graphics, {-radius, 0.0f, p}, {radius, 0.0f, p}, grid, true, 1.0f);
    line(graphics, {p, 0.0f, -radius}, {p, 0.0f, radius}, grid, true, 1.0f);
  }
  line(graphics, {0.0f, 0.0f, 0.0f}, {2.5f, 0.0f, 0.0f},
       {1.0f, 0.15f, 0.12f, 1.0f}, false, 2.0f);
  line(graphics, {0.0f, 0.0f, 0.0f}, {0.0f, 2.5f, 0.0f},
       {0.18f, 0.9f, 0.25f, 1.0f}, false, 2.0f);
  line(graphics, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.5f},
       {0.2f, 0.45f, 1.0f, 1.0f}, false, 2.0f);
}

void drawWireBox(renderer::GraphicsDevice& graphics,
                 const math::Vec3& center,
                 const math::Quat& rotation,
                 const math::Vec3& half_extents,
                 const math::Color& color,
                 bool depth_test,
                 float thickness) {
  const std::array<math::Vec3, 8> corners = {
      transformPoint(center, rotation, {-half_extents.x, -half_extents.y, -half_extents.z}),
      transformPoint(center, rotation, {half_extents.x, -half_extents.y, -half_extents.z}),
      transformPoint(center, rotation, {half_extents.x, -half_extents.y, half_extents.z}),
      transformPoint(center, rotation, {-half_extents.x, -half_extents.y, half_extents.z}),
      transformPoint(center, rotation, {-half_extents.x, half_extents.y, -half_extents.z}),
      transformPoint(center, rotation, {half_extents.x, half_extents.y, -half_extents.z}),
      transformPoint(center, rotation, {half_extents.x, half_extents.y, half_extents.z}),
      transformPoint(center, rotation, {-half_extents.x, half_extents.y, half_extents.z}),
  };
  constexpr std::array<std::array<int, 2>, 12> edges = {{
      {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
      {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
      {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
  }};
  for (const auto& edge : edges) {
    line(graphics, corners[edge[0]], corners[edge[1]], color, depth_test, thickness);
  }
}

void drawWireSphere(renderer::GraphicsDevice& graphics,
                    const math::Vec3& center,
                    const math::Quat& rotation,
                    float radius,
                    const math::Color& color,
                    bool depth_test,
                    float thickness) {
  drawCircle(graphics, center, rotation, radius, 0, color, depth_test, thickness);
  drawCircle(graphics, center, rotation, radius, 1, color, depth_test, thickness);
  drawCircle(graphics, center, rotation, radius, 2, color, depth_test, thickness);
}

void drawWireCapsule(renderer::GraphicsDevice& graphics,
                     const math::Vec3& center,
                     const math::Quat& rotation,
                     float radius,
                     float height,
                     const math::Color& color,
                     bool depth_test,
                     float thickness) {
  const float half_cylinder = std::max(height * 0.5f - radius, 0.0f);
  const math::Vec3 top{0.0f, half_cylinder, 0.0f};
  const math::Vec3 bottom{0.0f, -half_cylinder, 0.0f};
  drawCircle(graphics, transformPoint(center, rotation, top), rotation, radius, 1, color, depth_test,
             thickness);
  drawCircle(graphics, transformPoint(center, rotation, bottom), rotation, radius, 1, color,
             depth_test, thickness);
  for (int i = 0; i < 4; ++i) {
    const float angle = static_cast<float>(i) * kPi * 0.5f;
    const math::Vec3 rim{std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
    line(graphics, transformPoint(center, rotation, vadd(top, rim)),
         transformPoint(center, rotation, vadd(bottom, rim)), color, depth_test, thickness);
  }
  drawWireSphere(graphics, transformPoint(center, rotation, top), rotation, radius, color,
                 depth_test, thickness * 0.75f);
  drawWireSphere(graphics, transformPoint(center, rotation, bottom), rotation, radius, color,
                 depth_test, thickness * 0.75f);
}

void drawWireCylinder(renderer::GraphicsDevice& graphics,
                      const math::Vec3& center,
                      const math::Quat& rotation,
                      float radius,
                      float height,
                      const math::Color& color,
                      bool depth_test,
                      float thickness) {
  const float half = height * 0.5f;
  const math::Vec3 top{0.0f, half, 0.0f};
  const math::Vec3 bottom{0.0f, -half, 0.0f};
  drawCircle(graphics, transformPoint(center, rotation, top), rotation, radius, 1, color, depth_test,
             thickness);
  drawCircle(graphics, transformPoint(center, rotation, bottom), rotation, radius, 1, color,
             depth_test, thickness);
  for (int i = 0; i < 8; ++i) {
    const float angle = static_cast<float>(i) / 8.0f * kPi * 2.0f;
    const math::Vec3 rim{std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
    line(graphics, transformPoint(center, rotation, vadd(top, rim)),
         transformPoint(center, rotation, vadd(bottom, rim)), color, depth_test, thickness);
  }
}

void drawWireTaperedCapsule(renderer::GraphicsDevice& graphics,
                            const math::Vec3& center,
                            const math::Quat& rotation,
                            float top_radius,
                            float bottom_radius,
                            float height,
                            const math::Color& color,
                            bool depth_test,
                            float thickness) {
  const float cap = std::max(top_radius, bottom_radius);
  const float half_cylinder = std::max(height * 0.5f - cap, 0.0f);
  const math::Vec3 top{0.0f, half_cylinder, 0.0f};
  const math::Vec3 bottom{0.0f, -half_cylinder, 0.0f};
  drawCircle(graphics, transformPoint(center, rotation, top), rotation, top_radius, 1, color,
             depth_test, thickness);
  drawCircle(graphics, transformPoint(center, rotation, bottom), rotation, bottom_radius, 1, color,
             depth_test, thickness);
  for (int i = 0; i < 8; ++i) {
    const float angle = static_cast<float>(i) / 8.0f * kPi * 2.0f;
    const math::Vec3 top_rim{std::cos(angle) * top_radius, 0.0f, std::sin(angle) * top_radius};
    const math::Vec3 bottom_rim{std::cos(angle) * bottom_radius, 0.0f,
                                std::sin(angle) * bottom_radius};
    line(graphics, transformPoint(center, rotation, vadd(top, top_rim)),
         transformPoint(center, rotation, vadd(bottom, bottom_rim)), color, depth_test, thickness);
  }
  drawWireSphere(graphics, transformPoint(center, rotation, top), rotation, top_radius, color,
                 depth_test, thickness * 0.75f);
  drawWireSphere(graphics, transformPoint(center, rotation, bottom), rotation, bottom_radius, color,
                 depth_test, thickness * 0.75f);
}

void drawShapeDesc(renderer::GraphicsDevice& graphics,
                   const physics::PhysicsShapeDesc& shape,
                   const math::Vec3& position,
                   const math::Quat& rotation,
                   const math::Color& color,
                   bool depth_test,
                   float thickness) {
  const math::Vec3 center = transformPoint(position, rotation, math::fromGlm(shape.center));
  const math::Quat shape_rotation = math::mul(rotation, math::fromGlm(shape.rotation));
  switch (shape.type) {
    case physics::PhysicsShapeType::Box:
      drawWireBox(graphics, center, shape_rotation, math::fromGlm(shape.half_extents), color,
                  depth_test, thickness);
      break;
    case physics::PhysicsShapeType::Sphere:
      drawWireSphere(graphics, center, shape_rotation, shape.radius, color, depth_test, thickness);
      break;
    case physics::PhysicsShapeType::Capsule:
      drawWireCapsule(graphics, center, shape_rotation, shape.radius, shape.height, color,
                      depth_test, thickness);
      break;
    case physics::PhysicsShapeType::Cylinder:
      drawWireCylinder(graphics, center, shape_rotation, shape.radius, shape.height, color,
                       depth_test, thickness);
      break;
    case physics::PhysicsShapeType::TaperedCapsule:
      drawWireTaperedCapsule(graphics, center, shape_rotation, shape.top_radius,
                             shape.bottom_radius, shape.height, color, depth_test, thickness);
      break;
    case physics::PhysicsShapeType::ConvexHull:
      drawPointSet(graphics, shape.points, center, shape_rotation, color, depth_test, thickness);
      break;
    case physics::PhysicsShapeType::Triangle:
      drawTriangle(graphics, shape.triangle, center, shape_rotation, color, depth_test, thickness);
      break;
    case physics::PhysicsShapeType::Mesh:
      for (size_t i = 0; i + 2 < shape.mesh_indices.size(); i += 3) {
        std::array<glm::vec3, 3> triangle = {
            shape.mesh_vertices[shape.mesh_indices[i]],
            shape.mesh_vertices[shape.mesh_indices[i + 1]],
            shape.mesh_vertices[shape.mesh_indices[i + 2]],
        };
        drawTriangle(graphics, triangle, center, shape_rotation, color, depth_test, thickness);
      }
      break;
    case physics::PhysicsShapeType::HeightField: {
      const uint32_t n = shape.height_sample_count;
      if (n < 2 || shape.height_samples.size() < static_cast<size_t>(n * n)) {
        break;
      }
      auto sample = [&](uint32_t x, uint32_t z) {
        const float y = shape.height_samples[z * n + x];
        const glm::vec3 local = shape.height_offset +
                                glm::vec3{static_cast<float>(x) * shape.height_scale.x,
                                          y * shape.height_scale.y,
                                          static_cast<float>(z) * shape.height_scale.z};
        return transformPoint(center, shape_rotation, math::fromGlm(local));
      };
      for (uint32_t z = 0; z < n; ++z) {
        for (uint32_t x = 0; x < n; ++x) {
          if (x + 1 < n) line(graphics, sample(x, z), sample(x + 1, z), color, depth_test,
                              thickness);
          if (z + 1 < n) line(graphics, sample(x, z), sample(x, z + 1), color, depth_test,
                              thickness);
        }
      }
      break;
    }
    case physics::PhysicsShapeType::Compound:
      for (const physics::PhysicsShapeDesc& child : shape.children) {
        drawShapeDesc(graphics, child, position, rotation, color, depth_test, thickness);
      }
      break;
  }
}

void drawEntityColliders(renderer::GraphicsDevice& graphics,
                         const ecs::World& world,
                         ecs::Entity entity,
                         const math::Color& color,
                         bool depth_test,
                         float thickness) {
  if (!world.isAlive(entity) || !world.has<components::TransformComponent>(entity)) {
    return;
  }
  const auto& transform = world.get<components::TransformComponent>(entity);
  const math::Vec3 position = transform.getPosition();
  const math::Quat rotation = transform.getRotation();
  const math::Vec3 scale = transform.getScale();

  if (!world.has<components::ColliderComponent>(entity)) {
    return;
  }
  const auto& collider = world.get<components::ColliderComponent>(entity);
  std::visit(
      [&](const auto& shape_data) {
        using Shape = std::decay_t<decltype(shape_data)>;
        if constexpr (std::is_same_v<Shape, components::BoxColliderShape>) {
          drawWireBox(graphics,
                      transformPoint(position, rotation, componentMul(shape_data.center, scale)),
                      rotation,
                      componentMul(shape_data.half_extents, scale),
                      color,
                      depth_test,
                      thickness);
        } else if constexpr (std::is_same_v<Shape, components::SphereColliderShape>) {
          const float radius = shape_data.radius * std::max({scale.x, scale.y, scale.z});
          drawWireSphere(graphics,
                         transformPoint(position, rotation, componentMul(shape_data.center, scale)),
                         rotation,
                         radius,
                         color,
                         depth_test,
                         thickness);
        } else if constexpr (std::is_same_v<Shape, components::CapsuleColliderShape>) {
          drawWireCapsule(graphics,
                          transformPoint(position, rotation, componentMul(shape_data.center, scale)),
                          rotation,
                          shape_data.radius * std::max(scale.x, scale.z),
                          shape_data.height * scale.y,
                          color,
                          depth_test,
                          thickness);
        } else if constexpr (std::is_same_v<Shape, components::CylinderColliderShape>) {
          drawWireCylinder(graphics,
                           transformPoint(position, rotation, componentMul(shape_data.center, scale)),
                           rotation,
                           shape_data.radius * std::max(scale.x, scale.z),
                           shape_data.height * scale.y,
                           color,
                           depth_test,
                           thickness);
        } else if constexpr (std::is_same_v<Shape, components::TaperedCapsuleColliderShape>) {
          drawWireTaperedCapsule(
              graphics,
              transformPoint(position, rotation, componentMul(shape_data.center, scale)),
              rotation,
              shape_data.top_radius * std::max(scale.x, scale.z),
              shape_data.bottom_radius * std::max(scale.x, scale.z),
              shape_data.height * scale.y,
              color,
              depth_test,
              thickness);
        } else if constexpr (std::is_same_v<Shape, components::ConvexHullColliderShape>) {
          std::vector<glm::vec3> points;
          points.reserve(shape_data.points.size());
          for (const math::Vec3& point : shape_data.points) {
            points.push_back(math::toGlm(componentMul(point, scale)));
          }
          drawPointSet(graphics,
                       points,
                       transformPoint(position, rotation, componentMul(shape_data.center, scale)),
                       rotation,
                       color,
                       depth_test,
                       thickness);
        } else if constexpr (std::is_same_v<Shape, components::TriangleColliderShape>) {
          std::array<glm::vec3, 3> triangle{};
          for (size_t i = 0; i < triangle.size(); ++i) {
            triangle[i] = math::toGlm(componentMul(shape_data.points[i], scale));
          }
          drawTriangle(graphics, triangle, position, rotation, color, depth_test, thickness);
        } else if constexpr (std::is_same_v<Shape, components::HeightFieldColliderShape>) {
          physics::PhysicsShapeDesc shape{};
          shape.type = physics::PhysicsShapeType::HeightField;
          shape.height_samples = shape_data.samples;
          shape.height_sample_count = shape_data.sample_count;
          shape.height_offset = math::toGlm(componentMul(shape_data.offset, scale));
          shape.height_scale = math::toGlm(componentMul(shape_data.scale, scale));
          shape.height_block_size = shape_data.block_size;
          shape.height_bits_per_sample = shape_data.bits_per_sample;
          drawShapeDesc(graphics, shape, position, rotation, color, depth_test, thickness);
        } else if constexpr (std::is_same_v<Shape, components::MeshColliderShape>) {
          physics::PhysicsShapeDesc shape{};
          shape.type = physics::PhysicsShapeType::Mesh;
          for (const math::Vec3& vertex : shape_data.vertices) {
            shape.mesh_vertices.push_back(math::toGlm(componentMul(vertex, scale)));
          }
          shape.mesh_indices = shape_data.indices;
          drawShapeDesc(graphics, shape, position, rotation, color, depth_test, thickness);
        }
      },
      collider.shape);
}

std::string handleLabel(std::uintptr_t handle) {
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "0x%zx", static_cast<size_t>(handle));
  return buffer;
}

}  // namespace karma::demo::physics_examples
