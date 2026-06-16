#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <string_view>
#include <vector>

#include "karma/platform/window/window.h"
#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/post_process_profile_library.h"

namespace {

class DummyWindow final : public karma::platform::Window {
 public:
  void pollEvents() override {}
  const std::vector<karma::platform::Event>& events() const override { return events_; }
  void clearEvents() override {}
  bool shouldClose() const override { return false; }
  void requestClose() override {}
  void swapBuffers() override {}
  void setVsync(bool) override {}
  void setFullscreen(bool) override {}
  bool isFullscreen() const override { return false; }
  void setIcon(const std::string&) override {}
  void getFramebufferSize(int& width, int& height) const override {
    width = 128;
    height = 64;
  }
  float getContentScale() const override { return 1.0f; }
  bool isKeyDown(karma::platform::Key) const override { return false; }
  bool isMouseDown(karma::platform::MouseButton) const override { return false; }
  void setCursorVisible(bool) override {}
  void setClipboardText(std::string_view) override {}
  std::string getClipboardText() const override { return {}; }
  void* nativeHandle() const override { return nullptr; }

 private:
  std::vector<karma::platform::Event> events_;
};

void testTerrainHeadlessNoopApi() {
  DummyWindow window;
  karma::renderer::GraphicsDevice device(window);

  karma::renderer::TerrainDesc desc{};
  const karma::renderer::TerrainId terrain = device.createTerrain(desc);
  assert(terrain == karma::renderer::kInvalidTerrain);

  karma::renderer::TerrainTileData tile{};
  tile.coord = {.x = 1, .z = -2};
  tile.resolution = 2u;
  tile.heights = {0.0f, 1.0f, 0.5f, 0.25f};
  tile.color_width = 1u;
  tile.color_height = 1u;
  tile.color_rgba8 = {255u, 255u, 255u, 255u};
  assert(tile.valid());
  device.uploadTerrainTile(terrain, tile);
  device.submitTerrain(karma::renderer::TerrainDrawItem{
      .instance = 7u,
      .terrain = terrain,
      .coord = tile.coord,
  });
  device.evictTerrainTile(terrain, tile.coord);
  device.destroyTerrain(terrain);

  const auto caps = device.getTerrainCapabilities();
  assert(!caps.supported);
  assert(!caps.hardware_tessellation);
  assert(caps.cpu_fallback);

  const auto stats = device.getTerrainStats();
  assert(stats.terrain_count == 0u);
  assert(stats.resident_tiles == 0u);
  assert(stats.submitted_tiles == 0u);
  assert(stats.drawn_tiles == 0u);

  device.beginFrame(karma::renderer::FrameInfo{.width = 128, .height = 64, .delta_time = 0.016f});
  device.endFrame();
  const auto reset_stats = device.getTerrainStats();
  assert(reset_stats.submitted_tiles == 0u);
}

}  // namespace

int main() {
  karma::renderer::PostProcessProfileLibrary profiles;

  assert(!profiles.resolve("").bloom_enabled);
  assert(!profiles.resolve("missing").bloom_enabled);

  karma::renderer::PostProcessSettings default_profile{};
  default_profile.bloom_enabled = true;
  default_profile.bloom_intensity = 0.6f;
  profiles.setDefaultProfile(default_profile);
  assert(profiles.resolve("").bloom_enabled);
  assert(profiles.resolve("missing").bloom_enabled);
  assert(profiles.resolve("missing").bloom_intensity == 0.6f);

  karma::renderer::PostProcessSettings named_profile{};
  named_profile.tone_mapping_enabled = true;
  named_profile.tone_exposure = 1.25f;
  profiles.registerProfile("cinematic", named_profile);
  assert(profiles.resolve("cinematic").tone_mapping_enabled);
  assert(profiles.resolve("cinematic").tone_exposure == 1.25f);
  assert(!profiles.resolve("cinematic").bloom_enabled);

  karma::renderer::PostProcessSettings replacement_default{};
  replacement_default.depth_of_field_enabled = true;
  profiles.registerProfile(std::string(karma::renderer::kDefaultPostProcessProfileKey),
                           replacement_default);
  assert(profiles.resolve("").depth_of_field_enabled);
  assert(profiles.resolve("missing").depth_of_field_enabled);

  profiles.unregisterProfile("cinematic");
  assert(profiles.resolve("cinematic").depth_of_field_enabled);

  testTerrainHeadlessNoopApi();

  return 0;
}
