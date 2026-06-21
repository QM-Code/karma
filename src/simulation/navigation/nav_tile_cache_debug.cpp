#include "karma/navigation.h"

#include <cmath>

#include "karma/rendering.h"

namespace karma::navigation {
namespace {

constexpr float kPi = 3.14159265358979323846f;

math::Vec3 addVec(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 rotateYaw(const math::Vec3& point, float yaw) {
  const float c = std::cos(yaw);
  const float s = std::sin(yaw);
  return {point.x * c - point.z * s, point.y, point.x * s + point.z * c};
}

void drawCircle(rendering::GraphicsDevice& graphics,
                const math::Vec3& center,
                float radius,
                const math::Color& color,
                bool depth_test) {
  constexpr int kSegments = 32;
  math::Vec3 prev{center.x + radius, center.y, center.z};
  for (int i = 1; i <= kSegments; ++i) {
    const float angle = (static_cast<float>(i) / static_cast<float>(kSegments)) * kPi * 2.0f;
    const math::Vec3 next{
        center.x + std::cos(angle) * radius,
        center.y,
        center.z + std::sin(angle) * radius,
    };
    graphics.drawLine(prev, next, color, depth_test, 1.0f);
    prev = next;
  }
}

void drawBox(rendering::GraphicsDevice& graphics,
             const math::Vec3& center,
             const math::Vec3& half_extents,
             float yaw,
             const math::Color& color,
             bool depth_test) {
  const math::Vec3 local[8] = {
      {-half_extents.x, -half_extents.y, -half_extents.z},
      { half_extents.x, -half_extents.y, -half_extents.z},
      { half_extents.x,  half_extents.y, -half_extents.z},
      {-half_extents.x,  half_extents.y, -half_extents.z},
      {-half_extents.x, -half_extents.y,  half_extents.z},
      { half_extents.x, -half_extents.y,  half_extents.z},
      { half_extents.x,  half_extents.y,  half_extents.z},
      {-half_extents.x,  half_extents.y,  half_extents.z},
  };
  math::Vec3 corners[8];
  for (int i = 0; i < 8; ++i) {
    corners[i] = addVec(center, rotateYaw(local[i], yaw));
  }

  const int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };
  for (const auto& edge : edges) {
    graphics.drawLine(corners[edge[0]], corners[edge[1]], color, depth_test, 1.0f);
  }
}

}  // namespace

void NavTileCache::debugDraw(rendering::GraphicsDevice& graphics,
                             const math::Color& color,
                             bool depth_test,
                             bool draw_tile_bounds) const {
  if (draw_tile_bounds) {
    const math::Color bounds_color{0.96f, 0.78f, 0.18f, 0.55f};
    for (const NavTileCacheTileInfo& tile : tiles()) {
      const math::Vec3 center{
          (tile.bounds_min.x + tile.bounds_max.x) * 0.5f,
          (tile.bounds_min.y + tile.bounds_max.y) * 0.5f,
          (tile.bounds_min.z + tile.bounds_max.z) * 0.5f,
      };
      const math::Vec3 half_extents{
          (tile.bounds_max.x - tile.bounds_min.x) * 0.5f,
          (tile.bounds_max.y - tile.bounds_min.y) * 0.5f,
          (tile.bounds_max.z - tile.bounds_min.z) * 0.5f,
      };
      drawBox(graphics, center, half_extents, 0.0f, bounds_color, depth_test);
    }
  }

  for (const NavTileCacheObstacleInfo& obstacle : obstacles()) {
    switch (obstacle.shape) {
      case NavTileCacheObstacleShape::Cylinder: {
        const math::Vec3 bottom = obstacle.position;
        const math::Vec3 top{obstacle.position.x,
                             obstacle.position.y + obstacle.height,
                             obstacle.position.z};
        drawCircle(graphics, bottom, obstacle.radius, color, depth_test);
        drawCircle(graphics, top, obstacle.radius, color, depth_test);
        graphics.drawLine({bottom.x + obstacle.radius, bottom.y, bottom.z},
                          {top.x + obstacle.radius, top.y, top.z},
                          color,
                          depth_test,
                          1.0f);
        graphics.drawLine({bottom.x - obstacle.radius, bottom.y, bottom.z},
                          {top.x - obstacle.radius, top.y, top.z},
                          color,
                          depth_test,
                          1.0f);
        graphics.drawLine({bottom.x, bottom.y, bottom.z + obstacle.radius},
                          {top.x, top.y, top.z + obstacle.radius},
                          color,
                          depth_test,
                          1.0f);
        graphics.drawLine({bottom.x, bottom.y, bottom.z - obstacle.radius},
                          {top.x, top.y, top.z - obstacle.radius},
                          color,
                          depth_test,
                          1.0f);
        break;
      }
      case NavTileCacheObstacleShape::Box:
        drawBox(graphics, obstacle.position, obstacle.half_extents, 0.0f, color, depth_test);
        break;
      case NavTileCacheObstacleShape::OrientedBox:
        drawBox(graphics,
                obstacle.position,
                obstacle.half_extents,
                obstacle.yaw_radians,
                color,
                depth_test);
        break;
    }
  }
}

}  // namespace karma::navigation
