#include "karma/navigation.h"

#include "karma/rendering.h"

namespace karma::navigation {

void NavQuery::debugDrawPath(rendering::GraphicsDevice& graphics,
                             const NavPath& path,
                             const math::Color& color,
                             bool depth_test) {
  if (path.points.size() < 2u) {
    return;
  }
  for (size_t index = 1u; index < path.points.size(); ++index) {
    graphics.drawLine(path.points[index - 1u],
                      path.points[index],
                      color,
                      depth_test,
                      2.0f);
  }
}

}  // namespace karma::navigation
