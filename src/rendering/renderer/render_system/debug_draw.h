#pragma once

#include "karma/rendering.h"
#include "karma/components.h"

namespace karma::rendering::render_system {

struct CapsuleWireDimensions {
  float radius = 0.0f;
  float cylinder_half_length = 0.0f;
};

float scaledSphereWireRadius(float radius, const math::Vec3& scale);
CapsuleWireDimensions scaledCapsuleWireDimensions(float radius,
                                                  float total_height,
                                                  const math::Vec3& scale);

void drawBoxWire(GraphicsDevice& device,
                 const components::TransformComponent& transform,
                 const math::Vec3& center,
                 const math::Vec3& half_extents,
                 const math::Color& color,
                 float interpolation_alpha);
void drawSphereWire(GraphicsDevice& device,
                    const components::TransformComponent& transform,
                    const math::Vec3& center,
                    float radius,
                    const math::Color& color,
                    float interpolation_alpha);
void drawCapsuleWire(GraphicsDevice& device,
                     const components::TransformComponent& transform,
                     const math::Vec3& center,
                     float radius,
                     float height,
                     const math::Color& color,
                     float interpolation_alpha);

}  // namespace karma::rendering::render_system
