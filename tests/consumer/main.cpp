#include <karma/headless.h>

#include <string>

int main() {
  static_assert(karma::core::VersionMajor == 0);

  karma::components::TransformComponent transform({1.0f, 2.0f, 3.0f});
  transform.setPosition({4.0f, 5.0f, 6.0f});

  const karma::math::Vec3 position = transform.getInterpolatedPosition(1.0f);
  if (position.x != 4.0f || position.y != 5.0f || position.z != 6.0f) {
    return 1;
  }

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  const auto package =
      karma::assets::importAssetPackage(assets,
                                         "karma_consumer_smoke_missing",
                                         &diagnostic);
  return package.has_value() ? 2 : 0;
}
