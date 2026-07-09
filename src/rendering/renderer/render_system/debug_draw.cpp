#include "debug_draw.h"

#include "extractors.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace karma::rendering::render_system {
namespace {

void drawCircle(GraphicsDevice& device,
                const glm::vec3& center,
                const glm::vec3& axis_x,
                const glm::vec3& axis_y,
                float radius,
                const math::Color& color,
                int segments = 24) {
  constexpr float kPi = 3.14159265358979323846f;
  const float step = static_cast<float>(2.0f * kPi) / static_cast<float>(segments);
  glm::vec3 prev = center + radius * axis_x;
  for (int i = 1; i <= segments; ++i) {
    const float angle = step * static_cast<float>(i);
    const glm::vec3 next = center + radius * (std::cos(angle) * axis_x + std::sin(angle) * axis_y);
    device.drawLine({prev.x, prev.y, prev.z}, {next.x, next.y, next.z}, color, true, 1.0f);
    prev = next;
  }
}

void drawArc(GraphicsDevice& device,
             const glm::vec3& center,
             const glm::vec3& axis_x,
             const glm::vec3& axis_y,
             float radius,
             float start_angle,
             float end_angle,
             const math::Color& color,
             int segments = 12) {
  const float step = (end_angle - start_angle) / static_cast<float>(segments);
  glm::vec3 prev = center + radius *
      (std::cos(start_angle) * axis_x + std::sin(start_angle) * axis_y);
  for (int i = 1; i <= segments; ++i) {
    const float angle = start_angle + step * static_cast<float>(i);
    const glm::vec3 next = center + radius *
        (std::cos(angle) * axis_x + std::sin(angle) * axis_y);
    device.drawLine({prev.x, prev.y, prev.z}, {next.x, next.y, next.z}, color, true, 1.0f);
    prev = next;
  }
}

float finiteAbs(float value) {
  return std::isfinite(value) ? std::abs(value) : 0.0f;
}

float finiteProduct(float a, float b) {
  const float product = a * b;
  return std::isfinite(product) ? product : 0.0f;
}

}  // namespace

float scaledSphereWireRadius(float radius, const math::Vec3& scale) {
  const float max_scale = std::max({finiteAbs(scale.x), finiteAbs(scale.y),
                                    finiteAbs(scale.z)});
  return finiteProduct(finiteAbs(radius), max_scale);
}

CapsuleWireDimensions scaledCapsuleWireDimensions(float radius,
                                                  float total_height,
                                                  const math::Vec3& scale) {
  CapsuleWireDimensions dimensions{};
  dimensions.radius = finiteProduct(
      finiteAbs(radius), std::max(finiteAbs(scale.x), finiteAbs(scale.z)));
  const float total_half_height =
      finiteProduct(finiteAbs(total_height), finiteAbs(scale.y)) * 0.5f;
  dimensions.cylinder_half_length =
      std::max(total_half_height - dimensions.radius, 0.0f);
  return dimensions;
}

void drawBoxWire(GraphicsDevice& device,
                 const components::TransformComponent& transform,
                 const math::Vec3& center,
                 const math::Vec3& half_extents,
                 const math::Color& color,
                 float interpolation_alpha) {
  const math::Vec3 c = center;
  const math::Vec3 h = half_extents;
  const math::Vec3 corners[8] = {
      {c.x - h.x, c.y - h.y, c.z - h.z},
      {c.x + h.x, c.y - h.y, c.z - h.z},
      {c.x + h.x, c.y + h.y, c.z - h.z},
      {c.x - h.x, c.y + h.y, c.z - h.z},
      {c.x - h.x, c.y - h.y, c.z + h.z},
      {c.x + h.x, c.y - h.y, c.z + h.z},
      {c.x + h.x, c.y + h.y, c.z + h.z},
      {c.x - h.x, c.y + h.y, c.z + h.z},
  };

  const int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  glm::vec3 world_corners[8];
  for (int i = 0; i < 8; ++i) {
    world_corners[i] = transformPoint(transform, corners[i], interpolation_alpha);
  }

  for (const auto& edge : edges) {
    const glm::vec3 a = world_corners[edge[0]];
    const glm::vec3 b = world_corners[edge[1]];
    device.drawLine({a.x, a.y, a.z}, {b.x, b.y, b.z}, color, true, 1.0f);
  }
}

void drawSphereWire(GraphicsDevice& device,
                    const components::TransformComponent& transform,
                    const math::Vec3& center,
                    float radius,
                    const math::Color& color,
                    float interpolation_alpha) {
  const glm::vec3 world_center = transformPoint(transform, center, interpolation_alpha);
  const float r = scaledSphereWireRadius(radius, transform.getScale());
  if (r <= 0.0f) {
    return;
  }
  drawCircle(device, world_center, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, r, color);
  drawCircle(device, world_center, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, r, color);
  drawCircle(device, world_center, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, r, color);
}

void drawCapsuleWire(GraphicsDevice& device,
                     const components::TransformComponent& transform,
                     const math::Vec3& center,
                     float radius,
                     float height,
                     const math::Color& color,
                     float interpolation_alpha) {
  constexpr float kPi = 3.14159265358979323846f;
  const CapsuleWireDimensions dimensions =
      scaledCapsuleWireDimensions(radius, height, transform.getScale());
  const float r = dimensions.radius;
  const float half_length = dimensions.cylinder_half_length;
  if (r <= 0.0f) {
    return;
  }
  const glm::quat rot = ::karma::rendering::render_system::toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::mat3 basis = glm::mat3_cast(rot);
  const glm::vec3 up = basis * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = basis * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 forward = basis * glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 world_center = transformPoint(transform, center, interpolation_alpha);
  const glm::vec3 top = world_center + up * half_length;
  const glm::vec3 bottom = world_center - up * half_length;

  if (half_length <= 1.0e-6f) {
    drawCircle(device, world_center, right, forward, r, color);
    drawCircle(device, world_center, right, up, r, color);
    drawCircle(device, world_center, forward, up, r, color);
    return;
  }

  drawCircle(device, top, right, forward, r, color);
  drawCircle(device, bottom, right, forward, r, color);

  drawArc(device, top, right, up, r, 0.0f, kPi, color);
  drawArc(device, bottom, right, up, r, kPi, 2.0f * kPi, color);
  drawArc(device, top, forward, up, r, 0.0f, kPi, color);
  drawArc(device, bottom, forward, up, r, kPi, 2.0f * kPi, color);

  const glm::vec3 offsets[4] = {right * r, -right * r, forward * r, -forward * r};
  for (const auto& offset : offsets) {
    const glm::vec3 a = top + offset;
    const glm::vec3 b = bottom + offset;
    device.drawLine({a.x, a.y, a.z}, {b.x, b.y, b.z}, color, true, 1.0f);
  }
}

}  // namespace karma::rendering::render_system
