#include <karma/karma.h>
#include <karma/content/geometry/mesh_loader.h>

int main() {
  karma::components::TransformComponent transform({1.0f, 2.0f, 3.0f});
  transform.setPosition({4.0f, 5.0f, 6.0f});

  const karma::math::Vec3 position = transform.getInterpolatedPosition(1.0f);
  if (position.x != 4.0f || position.y != 5.0f || position.z != 6.0f) {
    return 1;
  }

  const auto meshes = karma::geometry::loadGLB("karma_consumer_smoke_missing.glb");
  return meshes.empty() ? 0 : 2;
}
