#include <karma/karma.h>

#include <utility>

#if defined(KARMA_TEST_EXPECT_NATIVE_UI) && !defined(KARMA_ENABLE_NATIVE_UI)
#error "The native graphical profile must expose KARMA_ENABLE_NATIVE_UI"
#endif

int main() {
  static_assert(karma::core::VersionMinor == 7);

#if defined(KARMA_TEST_EXPECT_NATIVE_UI)
  // Construct the retained system so this smoke verifies the native static
  // archive and its link-only implementation dependencies, not only headers
  // and profile compile definitions.
  karma::assets::AssetRegistry assets;
  karma::ui::UiSystemConfig ui_config;
  ui_config.enabled = false;
  karma::ui::System ui(assets, nullptr, std::move(ui_config));
#endif

  karma::components::TransformComponent transform({1.0f, 2.0f, 3.0f});
  transform.setPosition({4.0f, 5.0f, 6.0f});

  const karma::math::Vec3 position = transform.getInterpolatedPosition(1.0f);
  return (position.x == 4.0f && position.y == 5.0f && position.z == 6.0f) ? 0 : 1;
}
