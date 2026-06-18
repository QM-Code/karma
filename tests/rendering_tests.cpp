#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "karma/content/materials/material_loader.h"
#include "karma/platform/window/window.h"
#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/material_library.h"
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

bool nearly(float a, float b) {
  return std::abs(a - b) < 0.0001f;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
}

void testMaterialLibraryInheritance() {
  karma::renderer::MaterialLibrary library;

  karma::renderer::MaterialDesc base{};
  base.base_color = {0.25f, 0.5f, 0.75f, 1.0f};
  base.roughness = 0.8f;
  base.metallic = 0.1f;
  library.registerMaterialDesc("paint", base);

  karma::renderer::MaterialInstanceDesc instance{};
  instance.parent_material_key = "paint";
  instance.params["roughness"] = 0.25f;
  library.registerMaterialInstance("paint/local", instance);

  const auto resolved = library.resolve("paint/local");
  assert(resolved.has_value());
  assert(nearly(resolved->surface.base_color.r, 0.25f));
  assert(nearly(resolved->surface.base_color.g, 0.5f));
  assert(nearly(resolved->surface.base_color.b, 0.75f));
  assert(nearly(resolved->surface.metallic, 0.1f));
  assert(nearly(resolved->surface.roughness, 0.25f));

  base.base_color = {1.0f, 0.0f, 0.0f, 1.0f};
  library.registerMaterialDesc("paint", base);
  const auto after_shared_edit = library.resolve("paint/local");
  assert(after_shared_edit.has_value());
  assert(nearly(after_shared_edit->surface.base_color.r, 1.0f));
  assert(nearly(after_shared_edit->surface.roughness, 0.25f));
}

void testMaterialFileLoading() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "karma_material_loader_tests";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "materials");

  const std::filesystem::path standard = dir / "materials" / "standard.mat";
  writeText(standard,
            R"({
              "version": 1,
              "pipeline": { "type": "standard" },
              "surface": {
                "base_color": [0.1, 0.2, 0.3, 1.0],
                "roughness": 0.4,
                "metallic": 0.2
              },
              "textures": { "base_color": "../textures/albedo.png" }
            })");
  std::string diagnostic;
  auto standard_desc = karma::content::loadMaterialAssetDesc(standard, &diagnostic);
  assert(standard_desc.has_value());
  assert(standard_desc->pipeline.type ==
         karma::renderer::MaterialPipelineDesc::Type::Standard);
  assert(nearly(standard_desc->surface.base_color.g, 0.2f));
  assert(nearly(standard_desc->surface.roughness, 0.4f));
  assert(standard_desc->textures["base_color"] ==
         (dir / "textures" / "albedo.png").lexically_normal());

  const std::filesystem::path custom = dir / "materials" / "custom.mat";
  writeText(custom,
            R"({
              "version": 1,
              "pipeline": {
                "type": "custom",
                "vertex": "../shaders/custom_vs.hlsl",
                "fragment": "../shaders/custom_ps.hlsl",
                "vertex_entry": "VSMain",
                "fragment_entry": "PSMain",
                "defines": ["USE_FOG"]
              },
              "params": { "wave_tint_strength": 0.75 }
            })");
  auto custom_desc = karma::content::loadMaterialAssetDesc(custom, &diagnostic);
  assert(custom_desc.has_value());
  assert(custom_desc->pipeline.type == karma::renderer::MaterialPipelineDesc::Type::Custom);
  assert(custom_desc->pipeline.vertex_shader_path ==
         (dir / "shaders" / "custom_vs.hlsl").lexically_normal());
  assert(custom_desc->pipeline.vertex_entry_point == "VSMain");
  assert(custom_desc->pipeline.defines.size() == 1);

  const std::filesystem::path instance = dir / "materials" / "instance.mat";
  writeText(instance,
            R"({
              "version": 1,
              "kind": "instance",
              "parent": "paint",
              "surface": { "roughness": 0.15 },
              "render_state": { "transparent": true }
            })");
  auto instance_desc = karma::content::loadMaterialInstanceDesc(instance, &diagnostic);
  assert(instance_desc.has_value());
  assert(instance_desc->parent_material_key == "paint");
  assert(instance_desc->params.contains("roughness"));
  assert(instance_desc->params.contains("transparent"));

  const std::filesystem::path invalid = dir / "materials" / "invalid.mat";
  writeText(invalid, R"({"version": 1, "pipeline": { "type": "custom" }})");
  auto invalid_desc = karma::content::loadMaterialAssetDesc(invalid, &diagnostic);
  assert(!invalid_desc.has_value());
  assert(!diagnostic.empty());

  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  testMaterialLibraryInheritance();
  testMaterialFileLoading();

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
