#include <karma/karma.h>

int main() {
  static_assert(karma::core::VersionMinor == 5);

  karma::components::TransformComponent transform({1.0f, 2.0f, 3.0f});
  transform.setPosition({4.0f, 5.0f, 6.0f});

  const karma::math::Vec3 position = transform.getInterpolatedPosition(1.0f);
  return (position.x == 4.0f && position.y == 5.0f && position.z == 6.0f) ? 0 : 1;
}
