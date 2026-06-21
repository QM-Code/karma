#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "karma/karma.h"
#include "karma/math.h"
#include "karma/physics.h"

namespace karma::demo::physics_examples {

constexpr float kPi = 3.14159265358979323846f;

struct CameraRig {
  world::Entity entity{};
  float yaw = 0.0f;
  float pitch = 0.0f;
  float target_yaw = 0.0f;
  float target_pitch = 0.0f;
  float move_speed = 14.0f;
};

math::Vec3 vadd(const math::Vec3& a, const math::Vec3& b);
math::Vec3 vsub(const math::Vec3& a, const math::Vec3& b);
math::Vec3 vscale(const math::Vec3& v, float s);
math::Vec3 componentMul(const math::Vec3& a, const math::Vec3& b);
math::Vec3 rotated(const math::Quat& q, const math::Vec3& v);
math::Quat axisAngle(const math::Vec3& axis, float radians);

void bindFlyCameraControls(app::InputSystem& input);
void addDefaultLighting(world::World& world, assets::AssetRegistry* assets);
void createFlyCamera(world::World& world,
                     CameraRig& rig,
                     const math::Vec3& position,
                     float yaw,
                     float pitch);
void updateFlyCamera(world::World& world, app::InputSystem& input, CameraRig& rig, float dt);

void destroyEntities(world::World& world, std::vector<world::Entity>& entities);
void setTransform(world::World& world,
                  world::Entity entity,
                  const math::Vec3& position,
                  const math::Quat& rotation = {},
                  const math::Vec3& scale = {1.0f, 1.0f, 1.0f});
world::Entity addStaticBox(world::World& world,
                         const math::Vec3& position,
                         const math::Vec3& half_extents,
                         uint32_t layers = 1u,
                         uint32_t collides_with = 0xFFFFFFFFu);

physics::PhysicsShapeDesc makeBoxShape(const math::Vec3& half_extents);
physics::PhysicsShapeDesc makeSphereShape(float radius);
physics::PhysicsShapeDesc makeCapsuleShape(float radius, float height);
physics::PhysicsShapeDesc makeCylinderShape(float radius, float height, float convex_radius = 0.02f);
physics::PhysicsShapeDesc makeTaperedCapsuleShape(float top_radius,
                                                  float bottom_radius,
                                                  float height);
physics::PhysicsShapeDesc makeConvexHullShape(float radius);
physics::PhysicsShapeDesc makeTriangleShape(float size);
physics::PhysicsShapeDesc makeMeshWedgeShape(float radius);
physics::PhysicsShapeDesc makeHeightFieldShape(uint32_t sample_count,
                                               float spacing,
                                               float height_scale);
physics::PhysicsShapeDesc makeCompoundShape();

void drawReference(rendering::GraphicsDevice& graphics, float radius = 20.0f);
void drawWireBox(rendering::GraphicsDevice& graphics,
                 const math::Vec3& center,
                 const math::Quat& rotation,
                 const math::Vec3& half_extents,
                 const math::Color& color,
                 bool depth_test = true,
                 float thickness = 1.0f);
void drawWireSphere(rendering::GraphicsDevice& graphics,
                    const math::Vec3& center,
                    const math::Quat& rotation,
                    float radius,
                    const math::Color& color,
                    bool depth_test = true,
                    float thickness = 1.0f);
void drawWireCapsule(rendering::GraphicsDevice& graphics,
                     const math::Vec3& center,
                     const math::Quat& rotation,
                     float radius,
                     float height,
                     const math::Color& color,
                     bool depth_test = true,
                     float thickness = 1.0f);
void drawWireCylinder(rendering::GraphicsDevice& graphics,
                      const math::Vec3& center,
                      const math::Quat& rotation,
                      float radius,
                      float height,
                      const math::Color& color,
                      bool depth_test = true,
                      float thickness = 1.0f);
void drawWireTaperedCapsule(rendering::GraphicsDevice& graphics,
                            const math::Vec3& center,
                            const math::Quat& rotation,
                            float top_radius,
                            float bottom_radius,
                            float height,
                            const math::Color& color,
                            bool depth_test = true,
                            float thickness = 1.0f);
void drawShapeDesc(rendering::GraphicsDevice& graphics,
                   const physics::PhysicsShapeDesc& shape,
                   const math::Vec3& position,
                   const math::Quat& rotation,
                   const math::Color& color,
                   bool depth_test = true,
                   float thickness = 1.0f);
void drawEntityColliders(rendering::GraphicsDevice& graphics,
                         const world::World& world,
                         world::Entity entity,
                         const math::Color& color,
                         bool depth_test = true,
                         float thickness = 1.0f);

std::string handleLabel(std::uintptr_t handle);

}  // namespace karma::demo::physics_examples
