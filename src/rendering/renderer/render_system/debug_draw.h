#pragma once

#include "karma/rendering/renderer/device.h"
#include "karma/world/components/transform.h"

namespace karma::renderer::render_system {

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

}  // namespace karma::renderer::render_system
