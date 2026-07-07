#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "karma/assets.h"
#include "karma/assets.h"
#include "karma/assets.h"
#include "karma/app.h"
#include <glm/common.hpp>
#include <glm/mat4x4.hpp>

#include "karma/assets.h"
#include "karma/platform.h"
#include "karma/rendering.h"
#include "karma/components.h"
#include "karma/world.h"
#include "karma/world.h"

namespace {

bool diagnosticsContain(const karma::rendering::FrameGraphValidationResult& result,
                        std::string_view needle) {
  return std::any_of(result.diagnostics.begin(),
                     result.diagnostics.end(),
                     [&](const std::string& diagnostic) {
    return diagnostic.find(needle) != std::string::npos;
  });
}

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
  karma::rendering::GraphicsDevice device(window);

  karma::rendering::TerrainDesc desc{};
  const karma::rendering::TerrainId terrain = device.createTerrain(desc);
  assert(terrain == karma::rendering::kInvalidTerrain);

  karma::rendering::TerrainTileData tile{};
  tile.coord = {.x = 1, .z = -2};
  tile.resolution = 2u;
  tile.heights = {0.0f, 1.0f, 0.5f, 0.25f};
  tile.color_width = 1u;
  tile.color_height = 1u;
  tile.color_rgba8 = {255u, 255u, 255u, 255u};
  assert(tile.valid());
  device.uploadTerrainTile(terrain, tile);
  device.submitTerrain(karma::rendering::TerrainDrawItem{
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

  device.beginFrame(karma::rendering::FrameInfo{.width = 128, .height = 64, .delta_time = 0.016f});
  device.endFrame();
  const auto reset_stats = device.getTerrainStats();
  assert(reset_stats.submitted_tiles == 0u);
}

bool nearly(float a, float b) {
  return std::abs(a - b) < 0.0001f;
}

void testEngineConfigFramePacingDefaultAndOptOut() {
  karma::app::EngineConfig config{};
  assert(nearly(config.frame_pacing_fps, 60.0f));

  config.frame_pacing_fps = 0.0f;
  assert(nearly(config.frame_pacing_fps, 0.0f));
}

karma::world::MeshData makeTriangleMesh() {
  karma::world::MeshData mesh;
  mesh.vertices = {{0.0f, 0.0f, 0.0f},
                   {1.0f, 0.0f, 0.0f},
                   {0.0f, 0.0f, 1.0f}};
  mesh.indices = {0u, 1u, 2u};
  return mesh;
}

void testPrimitiveMeshAndDiffuseMaterialHelpers() {
  const karma::world::MeshData cube = karma::world::createCubeMesh(2.0f, "material/default");
  assert(cube.vertices.size() == 24u);
  assert(cube.normals.size() == cube.vertices.size());
  assert(cube.uvs.size() == cube.vertices.size());
  assert(cube.tangents.size() == cube.vertices.size());
  assert(cube.indices.size() == 36u);
  assert(cube.material_slots.size() == 1u);
  assert(cube.material_slots.front().default_material_key == "material/default");
  assert(cube.submeshes.size() == 1u);
  assert(cube.submeshes.front().index_count == cube.indices.size());

  glm::vec3 min_bounds = cube.vertices.front();
  glm::vec3 max_bounds = cube.vertices.front();
  for (const glm::vec3& vertex : cube.vertices) {
    min_bounds = glm::min(min_bounds, vertex);
    max_bounds = glm::max(max_bounds, vertex);
  }
  assert(nearly(min_bounds.x, -1.0f));
  assert(nearly(max_bounds.x, 1.0f));
  assert(nearly(min_bounds.y, -1.0f));
  assert(nearly(max_bounds.y, 1.0f));
  assert(nearly(min_bounds.z, -1.0f));
  assert(nearly(max_bounds.z, 1.0f));

  const karma::world::MeshData sphere = karma::world::createSphereMesh(
      karma::world::SphereMeshDesc{
          .radius = 1.0f,
          .segments = 8u,
          .rings = 4u,
          .material_key = "material/sphere",
      });
  assert(sphere.vertices.size() == 45u);
  assert(sphere.indices.size() == 192u);
  assert(sphere.material_slots.front().default_material_key == "material/sphere");
  assert(nearly(sphere.vertices.front().y, 1.0f));

  const karma::world::MeshData capsule = karma::world::createCapsuleMesh(
      karma::world::CapsuleMeshDesc{
          .radius = 0.5f,
          .cylinder_height = 1.0f,
          .segments = 8u,
          .hemisphere_rings = 4u,
          .material_key = "material/capsule",
      });
  assert(capsule.vertices.size() == 90u);
  assert(capsule.indices.size() == 432u);
  min_bounds = capsule.vertices.front();
  max_bounds = capsule.vertices.front();
  for (const glm::vec3& vertex : capsule.vertices) {
    min_bounds = glm::min(min_bounds, vertex);
    max_bounds = glm::max(max_bounds, vertex);
  }
  assert(nearly(max_bounds.y, 1.0f));
  assert(nearly(min_bounds.y, -1.0f));

  const karma::rendering::MaterialDesc blue =
      karma::rendering::createDiffuseMaterial({0.1f, 0.2f, 0.8f, 1.0f}, 0.65f);
  assert(nearly(blue.base_color.b, 0.8f));
  assert(nearly(blue.roughness, 0.65f));
  assert(nearly(blue.metallic, 0.0f));
  assert(!blue.transparent);
  assert(blue.alpha_mode == karma::rendering::MaterialDesc::AlphaMode::Opaque);

  karma::rendering::DiffuseMaterialDesc transparent_desc{};
  transparent_desc.base_color = {1.0f, 0.2f, 0.1f, 0.45f};
  transparent_desc.double_sided = true;
  const karma::rendering::MaterialAssetDesc transparent =
      karma::rendering::createDiffuseMaterialAsset("material/glass", transparent_desc);
  assert(transparent.material_key == "material/glass");
  assert(transparent.surface.transparent);
  assert(!transparent.surface.depth_write);
  assert(transparent.surface.double_sided);
  assert(transparent.surface.alpha_mode == karma::rendering::MaterialDesc::AlphaMode::Blend);
}

void testAssetRegistryRegisterResolveUnregister() {
  karma::assets::AssetRegistry assets;

  const uint64_t start_version = assets.version();
  assets.registerMeshAsset("mesh/tri", makeTriangleMesh());
  assert(assets.version() > start_version);
  assert(assets.findMeshAsset("mesh/tri") != nullptr);
  assert(assets.findMeshAsset("missing") == nullptr);

  karma::assets::TextureAsset texture{};
  texture.desc.width = 1;
  texture.desc.height = 1;
  texture.bytes = {255u, 255u, 255u, 255u};
  assets.registerTextureAsset("texture/white", std::move(texture));
  assert(assets.findTextureAsset("texture/white") != nullptr);

  karma::rendering::MaterialDesc material{};
  material.base_color = {0.1f, 0.2f, 0.3f, 1.0f};
  assets.registerMaterialAsset("material/base", material);
  karma::rendering::MaterialVariantDesc variant{};
  variant.base_material_key = "material/base";
  variant.params["roughness"] = 0.4f;
  assets.registerMaterialVariant("material/variant", variant);
  const auto resolved_material = assets.resolveMaterial("material/variant");
  assert(resolved_material.has_value());
  assert(nearly(resolved_material->surface.base_color.b, 0.3f));
  assert(resolved_material->params.contains("roughness"));

  karma::rendering::ShaderPassAssetDesc shader_pass{};
  shader_pass.pipeline.name = "fullscreen";
  shader_pass.pipeline.vertex_shader_path = "shaders/fullscreen_vs.hlsl";
  shader_pass.pipeline.fragment_shader_path = "shaders/composite_ps.hlsl";
  assert(assets.registerShaderPass("passes/composite", shader_pass));
  assert(assets.findShaderPass("passes/composite") != nullptr);

  karma::rendering::FrameGraphDesc graph =
      karma::rendering::defaultFrameGraphDesc();
  graph.frame_graph_key = "graphs/cinematic";
  karma::rendering::FrameGraphPassDesc composite{};
  composite.name = "composite";
  composite.kind = karma::rendering::FrameGraphPassKind::Shader;
  composite.shader_pass_key = "passes/composite";
  composite.inputs["source"] = std::string(karma::rendering::kFrameGraphCameraColor);
  composite.outputs["target"] = std::string(karma::rendering::kFrameGraphCameraColor);
  graph.passes.push_back(composite);
  assert(karma::rendering::validateFrameGraphDesc(graph).valid());
  assert(assets.registerFrameGraph("graphs/cinematic", graph));
  assert(assets.findFrameGraph("graphs/cinematic") != nullptr);
  assert(assets.resolveFrameGraph("graphs/cinematic").frame_graph_key == "graphs/cinematic");

  karma::visual::particles::ParticleEffectAsset effect{};
  effect.emitters.push_back(karma::visual::particles::ParticleEmitterDesc{});
  effect.emitters.front().texture_key = "texture/white";
  assets.registerParticleEffect("particle/spark", std::move(effect));
  assert(assets.findParticleEffect("particle/spark") != nullptr);
  assert(assets.findParticleEffect("particle/spark")->primaryEmitter() != nullptr);
  const uint64_t before_texture_version = assets.textureVersion();
  karma::assets::TextureAsset particle_texture{};
  particle_texture.desc.width = 1;
  particle_texture.desc.height = 1;
  particle_texture.bytes = {255u, 255u, 255u, 255u};
  assert(assets.registerTextureAsset("particle/texture_asset", std::move(particle_texture)));
  assert(assets.textureVersion() > before_texture_version);
  assert(assets.findTextureAsset("particle/texture_asset") != nullptr);
  assert(assets.unregisterTextureAsset("particle/texture_asset"));
  assert(assets.findTextureAsset("particle/texture_asset") == nullptr);

  assets.registerAudioClip("audio/wind", karma::assets::AudioClipAsset{.path = "wind.ogg"});
  assert(assets.findAudioClip("audio/wind") != nullptr);

  assets.registerEnvironmentMap("env/studio",
                                karma::assets::EnvironmentMapAsset{.path = "studio.hdr"});
  assert(assets.findEnvironmentMap("env/studio") != nullptr);

  karma::world::AnimationClip clip{};
  clip.name = "idle";
  clip.duration_seconds = 1.25f;
  assets.registerAnimationClip("clip/idle", clip);
  assert(assets.findAnimationClip("clip/idle") != nullptr);
  assert(nearly(assets.findAnimationClip("clip/idle")->duration_seconds, 1.25f));

  karma::world::Skeleton skeleton{};
  skeleton.name = "humanoid";
  skeleton.joints.push_back(karma::world::Joint{.name = "root"});
  assets.registerSkeleton("skeleton/humanoid", skeleton);
  assert(assets.findSkeleton("skeleton/humanoid") != nullptr);

  karma::world::Skin skin{};
  skin.name = "skin";
  skin.joint_node_indices = {0u};
  assets.registerSkin("skin/body", skin);
  assert(assets.findSkin("skin/body") != nullptr);

  karma::assets::GltfSceneAsset scene{};
  scene.scene_key = "scene/root";
  scene.mesh_asset_keys.push_back("mesh/tri");
  assets.registerGltfSceneAsset("scene/import", scene);
  assert(assets.findGltfSceneAsset("scene/import") != nullptr);

  assert(assets.unregisterMeshAsset("mesh/tri"));
  assert(assets.findMeshAsset("mesh/tri") == nullptr);
  assert(!assets.unregisterMeshAsset("mesh/tri"));
  assert(assets.unregisterTextureAsset("texture/white"));
  assets.unregisterMaterial("material/variant");
  assert(assets.unregisterShaderPass("passes/composite"));
  assert(assets.unregisterFrameGraph("graphs/cinematic"));
  assert(assets.unregisterParticleEffect("particle/spark"));
  assert(assets.unregisterAudioClip("audio/wind"));
  assert(assets.unregisterEnvironmentMap("env/studio"));
  assert(assets.unregisterAnimationClip("clip/idle"));
  assert(assets.unregisterSkeleton("skeleton/humanoid"));
  assert(assets.unregisterSkin("skin/body"));
  assert(assets.unregisterGltfSceneAsset("scene/import"));
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
}

std::size_t countJsonFiles(const std::filesystem::path& dir) {
  std::size_t count = 0u;
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec) {
    return 0u;
  }
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
      ++count;
    }
  }
  return count;
}

void writeSolidRowsTga(const std::filesystem::path& path) {
  constexpr uint16_t kWidth = 4u;
  constexpr uint16_t kHeight = 8u;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  const uint8_t header[18] = {
      0u, 0u, 2u,
      0u, 0u, 0u, 0u, 0u,
      0u, 0u,
      0u, 0u,
      static_cast<uint8_t>(kWidth & 0xffu),
      static_cast<uint8_t>((kWidth >> 8u) & 0xffu),
      static_cast<uint8_t>(kHeight & 0xffu),
      static_cast<uint8_t>((kHeight >> 8u) & 0xffu),
      32u,
      0x28u,
  };
  stream.write(reinterpret_cast<const char*>(header), sizeof(header));
  for (uint16_t y = 0u; y < kHeight; ++y) {
    const bool top_half = y < kHeight / 2u;
    for (uint16_t x = 0u; x < kWidth; ++x) {
      (void)x;
      const uint8_t bgra[4] = {
          0u,
          static_cast<uint8_t>(top_half ? 0u : 255u),
          static_cast<uint8_t>(top_half ? 255u : 0u),
          255u,
      };
      stream.write(reinterpret_cast<const char*>(bgra), sizeof(bgra));
    }
  }
}

bool rgbaNear(const std::vector<uint8_t>& bytes,
              int width,
              int x,
              int y,
              uint8_t r,
              uint8_t g,
              uint8_t b) {
  const std::size_t index =
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
       static_cast<std::size_t>(x)) * 4u;
  if (index + 3u >= bytes.size()) {
    return false;
  }
  const auto near = [](uint8_t actual, uint8_t expected) {
    return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= 8;
  };
  return near(bytes[index + 0u], r) &&
         near(bytes[index + 1u], g) &&
         near(bytes[index + 2u], b) &&
         bytes[index + 3u] >= 240u;
}

void setEnvVar(const char* name, const char* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

void unsetEnvVar(const char* name) {
#if defined(_WIN32)
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

std::filesystem::path makeTempDir(std::string_view name) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      (std::string(name) + "_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

std::filesystem::path findRepoRoot() {
  std::vector<std::filesystem::path> starts{std::filesystem::current_path()};
  const std::filesystem::path source_path = std::filesystem::path(__FILE__);
  if (source_path.is_absolute()) {
    starts.push_back(source_path.parent_path());
  }

  for (std::filesystem::path start : starts) {
    for (std::filesystem::path cursor = start; !cursor.empty(); cursor = cursor.parent_path()) {
      if (std::filesystem::exists(cursor / "examples/assets/prefabs/explosion/assets.package.json")) {
        return cursor;
      }
      if (cursor == cursor.parent_path()) {
        break;
      }
    }
  }
  return {};
}

void testAssetRegistryMaterialInheritance() {
  karma::assets::AssetRegistry assets;

  karma::rendering::MaterialDesc base{};
  base.base_color = {0.25f, 0.5f, 0.75f, 1.0f};
  base.roughness = 0.8f;
  base.metallic = 0.1f;
  assets.registerMaterialAsset("paint", base);

  karma::rendering::MaterialVariantDesc variant{};
  variant.base_material_key = "paint";
  variant.params["roughness"] = 0.25f;
  assets.registerMaterialVariant("paint/local", variant);

  const auto resolved = assets.resolveMaterial("paint/local");
  assert(resolved.has_value());
  assert(nearly(resolved->surface.base_color.r, 0.25f));
  assert(nearly(resolved->surface.base_color.g, 0.5f));
  assert(nearly(resolved->surface.base_color.b, 0.75f));
  assert(nearly(resolved->surface.metallic, 0.1f));
  assert(nearly(resolved->surface.roughness, 0.25f));

  base.base_color = {1.0f, 0.0f, 0.0f, 1.0f};
  assets.registerMaterialAsset("paint", base);
  const auto after_shared_edit = assets.resolveMaterial("paint/local");
  assert(after_shared_edit.has_value());
  assert(nearly(after_shared_edit->surface.base_color.r, 1.0f));
  assert(nearly(after_shared_edit->surface.roughness, 0.25f));
}

void testMaterialFileLoading() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "karma_material_loader_tests";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "materials");
  writeText(dir / "shaders" / "custom_vs.hlsl",
            "void VSMain() {}\n");
  writeText(dir / "shaders" / "custom_ps.hlsl",
            "void PSMain() {}\n");

  const std::filesystem::path standard = dir / "materials" / "standard.mat";
  writeText(standard,
            R"({
              "version": 2,
              "pipeline": { "name": "standard" },
              "surface": {
                "base_color": [0.1, 0.2, 0.3, 1.0],
                "roughness": 0.4,
                "metallic": 0.2
              },
              "textures": { "base_color": "textures/albedo" }
            })");
  std::string diagnostic;
  auto standard_desc = karma::assets::loadMaterialAssetDesc(standard, &diagnostic);
  assert(standard_desc.has_value());
  assert(standard_desc->pipeline.name == "standard");
  assert(nearly(standard_desc->surface.base_color.g, 0.2f));
  assert(nearly(standard_desc->surface.roughness, 0.4f));
  assert(standard_desc->textures["base_color"] == "textures/albedo");

  const std::filesystem::path custom = dir / "materials" / "custom.mat";
  writeText(custom,
            R"({
              "version": 2,
              "pipeline": {
                "name": "custom",
                "vertex": "../shaders/custom_vs.hlsl",
                "fragment": "../shaders/custom_ps.hlsl",
                "vertex_entry": "VSMain",
                "fragment_entry": "PSMain",
                "defines": ["USE_FOG"]
              },
              "params": {
                "wave_tint_strength": 0.75,
                "material_params0": [0.1, 0.2, 0.3, 0.4]
              }
            })");
  auto custom_desc = karma::assets::loadMaterialAssetDesc(custom, &diagnostic);
  assert(custom_desc.has_value());
  assert(custom_desc->pipeline.name == "custom");
  assert(custom_desc->pipeline.vertex_shader_path ==
         (dir / "shaders" / "custom_vs.hlsl").lexically_normal());
  assert(custom_desc->pipeline.vertex_entry_point == "VSMain");
  assert(custom_desc->pipeline.defines.size() == 1);
  const auto custom_params = custom_desc->params.find("material_params0");
  assert(custom_params != custom_desc->params.end());
  const auto* custom_param_value =
      std::get_if<karma::rendering::Color>(&custom_params->second);
  assert(custom_param_value != nullptr);
  assert(nearly(custom_param_value->b, 0.3f));

  const std::filesystem::path variant = dir / "materials" / "variant.mat";
  writeText(variant,
            R"({
              "version": 2,
              "kind": "variant",
              "base": "paint",
              "surface": { "roughness": 0.15 },
              "render_state": { "transparent": true }
            })");
  auto variant_desc = karma::assets::loadMaterialVariantDesc(variant, &diagnostic);
  assert(variant_desc.has_value());
  assert(variant_desc->base_material_key == "paint");
  assert(variant_desc->params.contains("roughness"));
  assert(variant_desc->params.contains("transparent"));

  const std::filesystem::path invalid = dir / "materials" / "invalid.mat";
  writeText(invalid, R"({"version": 2, "pipeline": { "name": "custom" }})");
  auto invalid_desc = karma::assets::loadMaterialAssetDesc(invalid, &diagnostic);
  assert(!invalid_desc.has_value());
  assert(!diagnostic.empty());

  const std::filesystem::path missing_shader = dir / "materials" / "missing_shader.mat";
  writeText(missing_shader,
            R"({
              "version": 2,
              "pipeline": {
                "name": "custom",
                "vertex": "../shaders/missing_vs.hlsl",
                "fragment": "../shaders/custom_ps.hlsl"
              }
            })");
  auto missing_shader_desc =
      karma::assets::loadMaterialAssetDesc(missing_shader, &diagnostic);
  assert(!missing_shader_desc.has_value());
  assert(diagnostic.find("missing") != std::string::npos ||
         diagnostic.find("unreadable") != std::string::npos);

  std::filesystem::remove_all(dir);
}

void testAssetKeyValidationAndPackages() {
  assert(karma::assets::AssetRegistry::isValidAssetKey("examples/mesh/world"));
  assert(karma::assets::AssetRegistry::isValidAssetKey("default"));
  assert(!karma::assets::AssetRegistry::isValidAssetKey(""));
  assert(!karma::assets::AssetRegistry::isValidAssetKey("/tmp/world"));
  assert(!karma::assets::AssetRegistry::isValidAssetKey("../world"));
  assert(!karma::assets::AssetRegistry::isValidAssetKey("textures\\world"));
  assert(!karma::assets::AssetRegistry::isValidAssetKey("examples/assets/world.glb"));
  assert(!karma::assets::AssetRegistry::assetKeyValidationError("examples/assets/world.glb").empty());

  const std::filesystem::path repo_root = findRepoRoot();
  assert(!repo_root.empty());
  const std::filesystem::path dir = makeTempDir("karma_asset_package_tests");
  std::filesystem::create_directories(dir / "textures");
  std::filesystem::create_directories(dir / "particles");
  std::filesystem::copy_file(repo_root / "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                             dir / "textures/spark_atlas.png",
                             std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(repo_root / "examples/assets/prefabs/explosion/particles/explosion_flash.kpeffect",
                             dir / "particles/explosion_flash.kpeffect",
                             std::filesystem::copy_options::overwrite_existing);

  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                {
                  "type": "texture_rgba8",
                  "key": "package/spark_atlas",
                  "path": "textures/spark_atlas.png",
                  "generate_mips": false
                },
                {
                  "type": "particle_effect",
                  "key": "package/flash",
                  "path": "particles/explosion_flash.kpeffect"
                },
                {
                  "type": "environment_map",
                  "key": "package/env",
                  "path": "../environment.hdr"
                }
              ]
            })");

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(assets, dir, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());
  const auto* texture = assets.findTextureAsset("package/spark_atlas");
  assert(texture != nullptr);
  assert(texture->desc.width == 256);
  assert(texture->desc.height == 64);
  assert(!texture->desc.generate_mips);
  assert(!texture->bytes.empty());
#if defined(KARMA_ENABLE_KTX2)
  assert(texture->payload_format ==
         karma::assets::TextureAsset::PayloadFormat::KTX2_BASIS_UASTC);
  assert(!texture->fallback_rgba8.empty());
  assert(texture->fallback_rgba8.size() == 256u * 64u * 4u);
  assert(texture->subresources.empty());
#else
  assert(texture->payload_format == karma::assets::TextureAsset::PayloadFormat::RGBA8);
  assert(texture->fallback_rgba8.empty());
  assert(texture->subresources.size() == 1u);
#endif
  assert(assets.findParticleEffect("package/flash") != nullptr);
  assert(assets.findEnvironmentMap("package/env") != nullptr);
  assert(karma::assets::unloadAssetPackage(assets, *package));
  assert(assets.findTextureAsset("package/spark_atlas") == nullptr);
  assert(assets.findParticleEffect("package/flash") == nullptr);
  assert(assets.findEnvironmentMap("package/env") == nullptr);

  writeText(dir / "rollback.package.json",
            R"({
              "version": 1,
              "assets": [
                { "type": "environment_map", "key": "package/rollback_env", "path": "environment.hdr" },
                { "type": "particle_effect", "key": "package/missing_effect", "path": "particles/missing.kpeffect" }
              ]
            })");
  diagnostic.clear();
  auto rollback_package =
      karma::assets::importAssetPackage(assets, dir / "rollback.package.json", &diagnostic);
  assert(!rollback_package.has_value());
  assert(!diagnostic.empty());
  assert(assets.findEnvironmentMap("package/rollback_env") == nullptr);
  assert(assets.findParticleEffect("package/missing_effect") == nullptr);

  assert(assets.registerEnvironmentMap(
      "package/dupe_env",
      karma::assets::EnvironmentMapAsset{.path = dir / "environment.hdr"}));
  writeText(dir / "duplicate.package.json",
            R"({
              "version": 1,
              "assets": [
                { "type": "environment_map", "key": "package/dupe_env", "path": "environment.hdr" }
              ]
            })");
  diagnostic.clear();
  auto duplicate_package =
      karma::assets::importAssetPackage(assets, dir / "duplicate.package.json", &diagnostic);
  assert(!duplicate_package.has_value());
  assert(!diagnostic.empty());
  assert(assets.findEnvironmentMap("package/dupe_env") != nullptr);

  std::filesystem::remove_all(dir);
}

void testFrameGraphValidationAndRegistryFallback() {
  karma::assets::AssetRegistry assets;
  const auto& fallback = assets.resolveFrameGraph("missing");
  assert(fallback.frame_graph_key == karma::rendering::kDefaultFrameGraphKey);

  const karma::rendering::FrameGraphDesc& default_graph =
      karma::rendering::defaultFrameGraphDesc();
  assert(karma::rendering::validateFrameGraphDesc(default_graph).valid());
  assert(default_graph.passes.size() == 9u);
  assert(default_graph.passes[0].builtin_pass == "clear");
  assert(default_graph.passes[1].builtin_pass == "skybox");
  assert(default_graph.passes[2].builtin_pass == "shadows");
  assert(default_graph.passes[3].builtin_pass == "opaque");
  assert(default_graph.passes[4].builtin_pass == "terrain");
  assert(default_graph.passes[5].builtin_pass == "transparent");
  assert(default_graph.passes[6].builtin_pass == "particles");
  assert(default_graph.passes[7].builtin_pass == "lines");
  assert(default_graph.passes[8].builtin_pass == "present");

  karma::rendering::PostProcessSettings post_process{};
  post_process.bloom_enabled = true;
  karma::rendering::FrameGraphDesc post_graph =
      karma::rendering::frameGraphFromPostProcessSettings(post_process, "graphs/post");
  const auto post_it = std::find_if(
      post_graph.passes.begin(),
      post_graph.passes.end(),
      [](const karma::rendering::FrameGraphPassDesc& pass) {
    return pass.builtin_pass == "post_process";
  });
  const auto lines_it = std::find_if(
      post_graph.passes.begin(),
      post_graph.passes.end(),
      [](const karma::rendering::FrameGraphPassDesc& pass) {
    return pass.builtin_pass == "lines";
  });
  assert(post_it != post_graph.passes.end());
  assert(lines_it != post_graph.passes.end());
  assert(post_it < lines_it);
  assert(karma::rendering::validateFrameGraphDesc(post_graph).valid());

  karma::rendering::FrameGraphDesc unknown_builtin = default_graph;
  unknown_builtin.passes.front().builtin_pass = "not_a_builtin";
  karma::rendering::FrameGraphValidationResult unknown_builtin_result =
      karma::rendering::validateFrameGraphDesc(unknown_builtin);
  assert(!unknown_builtin_result.valid());
  assert(diagnosticsContain(unknown_builtin_result, "unknown builtin"));

  karma::rendering::FrameGraphDesc invalid_depth_slot = default_graph;
  invalid_depth_slot.passes.front().outputs["depth"] =
      std::string(karma::rendering::kFrameGraphCameraColor);
  karma::rendering::FrameGraphValidationResult invalid_depth_result =
      karma::rendering::validateFrameGraphDesc(invalid_depth_slot);
  assert(!invalid_depth_result.valid());
  assert(diagnosticsContain(invalid_depth_result, "expects depth resource"));

  karma::rendering::FrameGraphDesc invalid_output = default_graph;
  invalid_output.output_resource =
      std::string(karma::rendering::kFrameGraphCameraDepth);
  karma::rendering::FrameGraphValidationResult invalid_output_result =
      karma::rendering::validateFrameGraphDesc(invalid_output);
  assert(!invalid_output_result.valid());
  assert(diagnosticsContain(invalid_output_result, "output_resource must be a color"));

  karma::rendering::FrameGraphDesc disabled_invalid_pass = default_graph;
  disabled_invalid_pass.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "disabled-invalid-shader",
      .kind = karma::rendering::FrameGraphPassKind::Shader,
      .enabled = false,
  });
  assert(karma::rendering::validateFrameGraphDesc(disabled_invalid_pass).valid());

  karma::rendering::FrameGraphDesc shader_graph{};
  shader_graph.frame_graph_key = "graphs/shader";
  shader_graph.output_resource =
      std::string(karma::rendering::kFrameGraphCameraColor);
  shader_graph.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "fullscreen",
      .kind = karma::rendering::FrameGraphPassKind::Shader,
      .shader_pass_key = "passes/fullscreen",
      .outputs = {{"target", std::string(karma::rendering::kFrameGraphCameraColor)}},
  });
  karma::rendering::FrameGraphValidationOptions shader_options{};
  shader_options.require_shader_pass_keys = true;
  shader_options.shader_pass_keys = {"passes/other"};
  karma::rendering::FrameGraphValidationResult missing_shader_result =
      karma::rendering::validateFrameGraphDesc(shader_graph, shader_options);
  assert(!missing_shader_result.valid());
  assert(diagnosticsContain(missing_shader_result, "references missing shader pass"));
  shader_options.shader_pass_keys = {"passes/fullscreen"};
  assert(karma::rendering::validateFrameGraphDesc(shader_graph, shader_options).valid());

  karma::rendering::FrameGraphDesc mask_graph{};
  mask_graph.frame_graph_key = "graphs/mask";
  mask_graph.output_resource = std::string(karma::rendering::kFrameGraphCameraColor);
  mask_graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "selection_mask",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
      .format = karma::rendering::TextureFormat::R8,
  });
  mask_graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "selection_depth",
      .kind = karma::rendering::FrameGraphResourceKind::DepthTexture,
  });
  mask_graph.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "selected_mask",
      .kind = karma::rendering::FrameGraphPassKind::SceneMask,
      .render_tags = {"selected"},
      .outputs = {{"target", "selection_mask"}, {"depth", "selection_depth"}},
      .clear = true,
      .clear_depth = true,
  });
  assert(karma::rendering::validateFrameGraphDesc(mask_graph).valid());

  karma::rendering::FrameGraphDesc missing_mask_tags = mask_graph;
  missing_mask_tags.passes.front().render_tags.clear();
  karma::rendering::FrameGraphValidationResult missing_mask_tags_result =
      karma::rendering::validateFrameGraphDesc(missing_mask_tags);
  assert(!missing_mask_tags_result.valid());
  assert(diagnosticsContain(missing_mask_tags_result, "requires at least one render tag"));

  karma::rendering::FrameGraphDesc empty_mask_tag = mask_graph;
  empty_mask_tag.passes.front().render_tags = {""};
  karma::rendering::FrameGraphValidationResult empty_mask_tag_result =
      karma::rendering::validateFrameGraphDesc(empty_mask_tag);
  assert(!empty_mask_tag_result.valid());
  assert(diagnosticsContain(empty_mask_tag_result, "must not be empty"));

  karma::rendering::FrameGraphDesc duplicate_mask_tag = mask_graph;
  duplicate_mask_tag.passes.front().render_tags = {"selected", "selected"};
  karma::rendering::FrameGraphValidationResult duplicate_mask_tag_result =
      karma::rendering::validateFrameGraphDesc(duplicate_mask_tag);
  assert(!duplicate_mask_tag_result.valid());
  assert(diagnosticsContain(duplicate_mask_tag_result, "duplicate render tag"));

  karma::rendering::FrameGraphDesc mask_clear_depth_without_output = mask_graph;
  mask_clear_depth_without_output.passes.front().outputs.erase("depth");
  karma::rendering::FrameGraphValidationResult mask_clear_depth_without_output_result =
      karma::rendering::validateFrameGraphDesc(mask_clear_depth_without_output);
  assert(!mask_clear_depth_without_output_result.valid());
  assert(diagnosticsContain(mask_clear_depth_without_output_result,
                            "clear_depth requires depth output"));

  karma::rendering::FrameGraphDesc graph{};
  graph.frame_graph_key = "graphs/test";
  graph.output_resource = "post";
  graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "post",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
  });
  graph.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "scene",
      .kind = karma::rendering::FrameGraphPassKind::Scene,
      .outputs = {{"color", "post"}},
  });
  assert(karma::rendering::validateFrameGraphDesc(graph).valid());
  assert(assets.registerFrameGraph("graphs/test", graph));
  assert(assets.resolveFrameGraph("graphs/test").output_resource == "post");
  assert(assets.unregisterFrameGraph("graphs/test"));
  assert(assets.resolveFrameGraph("graphs/test").frame_graph_key ==
         karma::rendering::kDefaultFrameGraphKey);

  karma::rendering::FrameGraphDesc duplicate_resource = graph;
  duplicate_resource.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "post",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
  });
  assert(!karma::rendering::validateFrameGraphDesc(duplicate_resource).valid());

  karma::rendering::FrameGraphDesc missing_resource = graph;
  missing_resource.passes.front().inputs["missing"] = "does_not_exist";
  assert(!karma::rendering::validateFrameGraphDesc(missing_resource).valid());

  karma::rendering::FrameGraphDesc cyclic{};
  cyclic.frame_graph_key = "graphs/cycle";
  cyclic.output_resource = "a";
  cyclic.resources.push_back(karma::rendering::FrameGraphResourceDesc{.name = "a"});
  cyclic.resources.push_back(karma::rendering::FrameGraphResourceDesc{.name = "b"});
  cyclic.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "a_writer",
      .kind = karma::rendering::FrameGraphPassKind::Copy,
      .inputs = {{"source", "b"}},
      .outputs = {{"target", "a"}},
  });
  cyclic.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "b_writer",
      .kind = karma::rendering::FrameGraphPassKind::Copy,
      .inputs = {{"source", "a"}},
      .outputs = {{"target", "b"}},
  });
  assert(!karma::rendering::validateFrameGraphDesc(cyclic).valid());
}

void testFrameGraphAssetPackageLoadCacheAndUnload() {
  const std::filesystem::path dir = makeTempDir("karma_frame_graph_package_tests");
  const std::filesystem::path cache_dir = dir / "cache";
  std::filesystem::create_directories(dir / "shaders");
  std::filesystem::create_directories(dir / "passes");
  std::filesystem::create_directories(dir / "graphs");

  writeText(dir / "shaders/fullscreen_vs.hlsl", "void main() {}\n");
  writeText(dir / "shaders/composite_ps.hlsl", "void main() {}\n");
  writeText(dir / "passes/composite.kshaderpass",
            R"({
              "version": 1,
              "pipeline": {
                "name": "fullscreen",
                "vertex": "../shaders/fullscreen_vs.hlsl",
                "fragment": "../shaders/composite_ps.hlsl",
                "vertex_entry": "main",
                "fragment_entry": "main",
                "defines": ["TEST_SHADER_PASS"]
              },
              "params": { "tone_exposure": 1.25, "enabled": true },
              "textures": {},
              "render_state": { "depth_test": false, "depth_write": false, "blend": false }
            })");
  writeText(dir / "graphs/composite.kframegraph",
            R"({
              "version": 1,
              "resources": [
                { "name": "post_ping", "kind": "color_texture", "scale": [1.0, 1.0], "format": "rgba8" },
                { "name": "selection_mask", "kind": "color_texture", "scale": [1.0, 1.0], "format": "r8" },
                { "name": "selection_depth", "kind": "depth_texture", "scale": [1.0, 1.0] }
              ],
              "passes": [
                { "name": "scene", "kind": "scene",
                  "outputs": { "color": "camera_color", "depth": "camera_depth" } },
                { "name": "selected_mask", "kind": "scene_mask", "render_tags": ["selected"],
                  "outputs": { "target": "selection_mask", "depth": "selection_depth" },
                  "clear": true, "clear_depth": true },
                { "name": "composite", "kind": "shader", "shader_pass": "passes/composite",
                  "inputs": { "source": "camera_color", "depth": "camera_depth" },
                  "outputs": { "target": "camera_color" },
                  "params": { "tone_mapping_enabled": true } }
              ],
              "output_resource": "camera_color"
            })");
  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                { "type": "shader_pass", "key": "passes/composite", "path": "passes/composite.kshaderpass" },
                { "type": "render_graph", "key": "graphs/composite", "path": "graphs/composite.kframegraph" }
              ]
            })");

  karma::assets::AssetPackageOptions options{};
  options.cache.root = cache_dir;
  options.cache.enabled = true;
  options.cache.flush = false;

  karma::assets::AssetRegistry cold_assets;
  std::string diagnostic;
  auto cold_package =
      karma::assets::importAssetPackage(cold_assets, dir, options, &diagnostic);
  assert(cold_package.has_value());
  assert(diagnostic.empty());
  assert(cold_assets.findShaderPass("passes/composite") != nullptr);
  assert(cold_assets.findFrameGraph("graphs/composite") != nullptr);
  assert(countJsonFiles(cache_dir / "packages") == 1u);
  for (const auto& asset : cold_package->assets) {
    if (asset.type == "shader_pass" || asset.type == "render_graph") {
      assert(asset.cache_blob_key.empty());
    }
  }
  for (const auto& manifest_entry :
       std::filesystem::directory_iterator(cache_dir / "packages")) {
    std::ifstream stream(manifest_entry.path());
    std::string contents((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
    assert(contents.find("shader_pass_assets") == std::string::npos);
  }

  karma::assets::AssetRegistry warm_assets;
  diagnostic.clear();
  auto warm_package =
      karma::assets::importAssetPackage(warm_assets, dir, options, &diagnostic);
  assert(warm_package.has_value());
  assert(diagnostic.empty());
  assert(warm_assets.findShaderPass("passes/composite") != nullptr);
  assert(warm_assets.findFrameGraph("graphs/composite") != nullptr);
  assert(countJsonFiles(cache_dir / "packages") == 1u);
  assert(karma::assets::unloadAssetPackage(warm_assets, *warm_package));
  assert(warm_assets.findShaderPass("passes/composite") == nullptr);
  assert(warm_assets.findFrameGraph("graphs/composite") == nullptr);

  writeText(dir / "shaders/composite_ps.hlsl", "void main() { float x = 1.0; }\n");
  karma::assets::AssetRegistry changed_shader_assets;
  diagnostic.clear();
  auto changed_shader_package =
      karma::assets::importAssetPackage(changed_shader_assets, dir, options, &diagnostic);
  assert(changed_shader_package.has_value());
  assert(diagnostic.empty());
  assert(countJsonFiles(cache_dir / "packages") == 2u);

  writeText(dir / "bad.package.json",
            R"({
              "version": 1,
              "assets": [
                { "type": "shader_pass", "key": "passes/bad", "path": "passes/missing_shader.kshaderpass" }
              ]
            })");
  writeText(dir / "passes/missing_shader.kshaderpass",
            R"({
              "version": 1,
              "pipeline": {
                "name": "fullscreen",
                "vertex": "../shaders/missing_vs.hlsl",
                "fragment": "../shaders/composite_ps.hlsl"
              }
            })");
  karma::assets::AssetRegistry bad_assets;
  diagnostic.clear();
  auto bad_package =
      karma::assets::importAssetPackage(bad_assets, dir / "bad.package.json", options, &diagnostic);
  assert(!bad_package.has_value());
  assert(!diagnostic.empty());

  std::filesystem::remove_all(dir);
}

void testAssetCacheV2AndPackageWarmRestore() {
  const std::filesystem::path repo_root = findRepoRoot();
  assert(!repo_root.empty());
  const std::filesystem::path dir = makeTempDir("karma_asset_cache_v2_tests");
  const std::filesystem::path cache_dir = dir / "cache";

  setEnvVar("KARMA_ASSET_CACHE_DIR", cache_dir.string().c_str());
  setEnvVar("KARMA_ASSET_CACHE_FLUSH", "1");
  unsetEnvVar("KARMA_ASSET_CACHE");
  karma::assets::AssetCacheConfig env_config =
      karma::assets::AssetCacheConfig::fromEnvironment();
  assert(env_config.root == cache_dir);
  assert(env_config.enabled);
  assert(env_config.flush);
  karma::assets::AssetCache env_cache(env_config);
  assert(std::filesystem::exists(cache_dir / "index.json"));

  karma::assets::TextureAsset cached_texture{};
  cached_texture.desc.width = 1;
  cached_texture.desc.height = 1;
  cached_texture.desc.format = karma::rendering::TextureFormat::RGBA8;
  cached_texture.bytes = {1u, 2u, 3u, 4u};
  cached_texture.subresources.push_back(karma::rendering::TextureUploadSubresource{
      .mip_level = 0u,
      .array_layer = 0u,
      .width = 1,
      .height = 1,
      .offset = 0u,
      .size = 4u,
      .row_stride = 4u,
  });
  assert(env_cache.writeTexture("texture_blob", cached_texture));
  auto read_texture = env_cache.readTexture("texture_blob");
  assert(read_texture.has_value());
  assert(read_texture->bytes == cached_texture.bytes);

  karma::world::MeshData cached_mesh{};
  cached_mesh.vertices = {glm::vec3{1.0f, 2.0f, 3.0f}, glm::vec3{4.0f, 5.0f, 6.0f}};
  cached_mesh.normals = {glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f}};
  cached_mesh.uvs = {glm::vec2{0.25f, 0.5f}, glm::vec2{0.75f, 1.0f}};
  cached_mesh.uvs1 = {glm::vec2{0.1f, 0.2f}, glm::vec2{0.3f, 0.4f}};
  cached_mesh.tangents = {glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
                          glm::vec4{0.0f, 1.0f, 0.0f, -1.0f}};
  cached_mesh.joint_indices = {glm::uvec4{1u, 2u, 3u, 4u}, glm::uvec4{5u, 6u, 7u, 8u}};
  cached_mesh.joint_weights = {glm::vec4{0.1f, 0.2f, 0.3f, 0.4f},
                               glm::vec4{0.4f, 0.3f, 0.2f, 0.1f}};
  cached_mesh.indices = {0u, 1u, 0u};
  cached_mesh.morph_targets.push_back(karma::world::MeshData::MorphTarget{
      .position_deltas = {glm::vec3{0.1f, 0.0f, 0.0f}},
      .normal_deltas = {glm::vec3{0.0f, 0.1f, 0.0f}},
      .tangent_deltas = {glm::vec3{0.0f, 0.0f, 0.1f}},
  });
  cached_mesh.submeshes.push_back(karma::world::MeshSubmesh{
      .index_offset = 0u,
      .index_count = 3u,
      .material_slot = 1u,
  });
  cached_mesh.material_slots.push_back(karma::world::MeshMaterialSlot{
      .name = "slot_a",
      .default_material_key = "material/a",
  });
  assert(env_cache.writeMesh("mesh_blob", cached_mesh));
  auto read_mesh = env_cache.readMesh("mesh_blob");
  assert(read_mesh.has_value());
  assert(read_mesh->vertices.size() == 2u);
  assert(nearly(read_mesh->vertices[1].z, 6.0f));
  assert(read_mesh->indices == cached_mesh.indices);
  assert(read_mesh->joint_indices[1].w == 8u);
  assert(read_mesh->morph_targets.size() == 1u);
  assert(nearly(read_mesh->morph_targets[0].tangent_deltas[0].z, 0.1f));
  assert(read_mesh->submeshes[0].material_slot == 1u);
  assert(read_mesh->material_slots[0].default_material_key == "material/a");

  auto imported_material = std::make_shared<karma::rendering::ImportedMaterialData>();
  imported_material->texcoord_row0[0] = glm::vec4(1.0f, 0.0f, 0.25f, 0.0f);
  imported_material->texcoord_row1[0] = glm::vec4(0.0f, -1.0f, 0.75f, 0.0f);
  karma::rendering::ImportedMaterialTexture import_only_texture{};
  import_only_texture.source_key = "embedded/0";
  import_only_texture.source_bytes = {1u, 2u, 3u, 4u};
  imported_material->textures.push_back(std::move(import_only_texture));
  karma::rendering::MaterialAssetDesc cached_material{};
  cached_material.material_key = "material/cached";
  cached_material.textures["base_color"] = "texture/cached";
  cached_material.material_asset_path = "models/source.glb";
  cached_material.material_asset_index = 3u;
  cached_material.imported_material = imported_material;
  assert(env_cache.writeMaterialAsset("material_blob", cached_material));
  auto read_material = env_cache.readMaterialAsset("material_blob");
  assert(read_material.has_value());
  assert(read_material->textures.at("base_color") == "texture/cached");
  assert(read_material->material_asset_path == std::filesystem::path("models/source.glb"));
  assert(read_material->material_asset_index == 3u);
  assert(read_material->imported_material != nullptr);
  assert(read_material->imported_material->textures.empty());
  assert(nearly(read_material->imported_material->texcoord_row0[0].z, 0.25f));
  assert(nearly(read_material->imported_material->texcoord_row1[0].y, -1.0f));
  assert(nearly(read_material->imported_material->texcoord_row1[0].z, 0.75f));

  std::filesystem::create_directories(cache_dir / "blobs");
  {
    std::ofstream legacy(cache_dir / "blobs" / "legacy.kasset", std::ios::binary);
    legacy << "KASSET01";
  }
  assert(!env_cache.readTexture("legacy").has_value());

  setEnvVar("KARMA_ASSET_CACHE", "0");
  karma::assets::AssetCacheConfig disabled =
      karma::assets::AssetCacheConfig::fromEnvironment();
  assert(!disabled.enabled);
  unsetEnvVar("KARMA_ASSET_CACHE");
  unsetEnvVar("KARMA_ASSET_CACHE_FLUSH");

  std::filesystem::create_directories(dir / "textures");
  std::filesystem::create_directories(dir / "particles");
  std::filesystem::copy_file(repo_root / "examples/assets/prefabs/explosion/textures/spark_atlas.png",
                             dir / "textures/spark_atlas.png",
                             std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(repo_root / "examples/assets/prefabs/explosion/particles/explosion_flash.kpeffect",
                             dir / "particles/explosion_flash.kpeffect",
                             std::filesystem::copy_options::overwrite_existing);
  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                {
                  "type": "texture_rgba8",
                  "key": "cache/package/spark_atlas",
                  "path": "textures/spark_atlas.png",
                  "generate_mips": false
                },
                {
                  "type": "particle_effect",
                  "key": "cache/package/flash",
                  "path": "particles/explosion_flash.kpeffect"
                },
                {
                  "type": "environment_map",
                  "key": "cache/package/env",
                  "path": "../environment.hdr"
                }
              ]
            })");

  karma::assets::AssetPackageOptions options{};
  options.cache.root = cache_dir;
  options.cache.enabled = true;
  options.cache.flush = false;

  karma::assets::AssetRegistry cold_assets;
  std::string diagnostic;
  auto cold_package =
      karma::assets::importAssetPackage(cold_assets, dir, options, &diagnostic);
  assert(cold_package.has_value());
  assert(diagnostic.empty());
  assert(cold_assets.findTextureAsset("cache/package/spark_atlas") != nullptr);
  assert(cold_assets.findParticleEffect("cache/package/flash") != nullptr);
  assert(cold_assets.findEnvironmentMap("cache/package/env") != nullptr);
  const karma::assets::TextureAsset* cold_texture =
      cold_assets.findTextureAsset("cache/package/spark_atlas");
  assert(cold_texture != nullptr);
  auto rgba_upload = karma::assets::prepareTextureUpload(*cold_texture, {});
  assert(rgba_upload.has_value());
  assert(rgba_upload->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(!rgba_upload->upload.bytes.empty());
#if defined(KARMA_ENABLE_KTX2)
  auto default_compressed_upload = karma::assets::prepareTextureUpload(
      *cold_texture,
      karma::assets::TextureRuntimeCapabilities{.bc7_unorm = true, .bc7_srgb = true});
  assert(default_compressed_upload.has_value());
  assert(default_compressed_upload->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(default_compressed_upload->upload.bytes == cold_texture->fallback_rgba8);

  setEnvVar("KARMA_TEXTURE_BC7", "1");
  auto bc7_upload = karma::assets::prepareTextureUpload(
      *cold_texture,
      karma::assets::TextureRuntimeCapabilities{.bc7_unorm = true, .bc7_srgb = true});
  assert(bc7_upload.has_value());
  assert(bc7_upload->desc.format == karma::rendering::TextureFormat::BC7_RGBA_UNORM);
  assert(!bc7_upload->upload.subresources.empty());
  assert(bc7_upload->upload.subresources.front().row_stride > 0u);
  unsetEnvVar("KARMA_TEXTURE_BC7");

  setEnvVar("KARMA_TEXTURE_BC7", "0");
  auto bc7_disabled_upload = karma::assets::prepareTextureUpload(
      *cold_texture,
      karma::assets::TextureRuntimeCapabilities{.bc7_unorm = true, .bc7_srgb = true});
  assert(bc7_disabled_upload.has_value());
  assert(bc7_disabled_upload->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(bc7_disabled_upload->upload.bytes == cold_texture->fallback_rgba8);
  unsetEnvVar("KARMA_TEXTURE_BC7");
#endif

  std::string texture_blob_key;
  for (const auto& asset : cold_package->assets) {
    if (asset.type == "texture" || asset.type == "texture_rgba8") {
      texture_blob_key = asset.cache_blob_key;
    }
    if (asset.type == "environment_map") {
      assert(asset.cache_blob_key.empty());
    }
  }
  assert(!texture_blob_key.empty());
  assert(std::filesystem::exists(cache_dir / "blobs" / (texture_blob_key + ".kasset")));

  karma::assets::AssetRegistry warm_assets;
  diagnostic.clear();
  auto warm_package =
      karma::assets::importAssetPackage(warm_assets, dir, options, &diagnostic);
  assert(warm_package.has_value());
  assert(diagnostic.empty());
  assert(warm_assets.findTextureAsset("cache/package/spark_atlas") != nullptr);
  assert(warm_assets.findParticleEffect("cache/package/flash") != nullptr);
  assert(warm_assets.findEnvironmentMap("cache/package/env") != nullptr);

  std::filesystem::remove(cache_dir / "blobs" / (texture_blob_key + ".kasset"));
  karma::assets::AssetRegistry rebuild_assets;
  diagnostic.clear();
  auto rebuilt_package =
      karma::assets::importAssetPackage(rebuild_assets, dir, options, &diagnostic);
  assert(rebuilt_package.has_value());
  assert(diagnostic.empty());
  assert(rebuild_assets.findTextureAsset("cache/package/spark_atlas") != nullptr);
  assert(std::filesystem::exists(cache_dir / "blobs" / (texture_blob_key + ".kasset")));

  karma::assets::AssetPackageOptions disabled_options = options;
  disabled_options.cache.enabled = false;
  karma::assets::AssetRegistry disabled_assets;
  diagnostic.clear();
  auto disabled_package =
      karma::assets::importAssetPackage(disabled_assets, dir, disabled_options, &diagnostic);
  assert(disabled_package.has_value());
  assert(diagnostic.empty());
  assert(disabled_assets.findTextureAsset("cache/package/spark_atlas") != nullptr);

  unsetEnvVar("KARMA_ASSET_CACHE_DIR");
  unsetEnvVar("KARMA_ASSET_CACHE");
  unsetEnvVar("KARMA_ASSET_CACHE_FLUSH");
  std::filesystem::remove_all(dir);
}

void testTexturePreparedUploadPreservesRowOrder() {
  const std::filesystem::path dir = makeTempDir("karma_texture_orientation_tests");
  const std::filesystem::path cache_dir = dir / "cache";
  setEnvVar("KARMA_ASSET_CACHE_DIR", cache_dir.string().c_str());
  unsetEnvVar("KARMA_ASSET_CACHE");
  unsetEnvVar("KARMA_ASSET_CACHE_FLUSH");

  const std::filesystem::path source = dir / "textures" / "rows.tga";
  writeSolidRowsTga(source);
  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                {
                  "type": "texture_rgba8",
                  "key": "tests/textures/rows",
                  "path": "textures/rows.tga",
                  "generate_mips": false
                }
              ]
            })");

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(assets, dir, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());
  const karma::assets::TextureAsset* texture =
      assets.findTextureAsset("tests/textures/rows");
  assert(texture != nullptr);
  assert(texture->desc.width == 4);
  assert(texture->desc.height == 8);

  auto prepared = karma::assets::prepareTextureUpload(*texture, {});
  assert(prepared.has_value());
  assert(prepared->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(prepared->upload.subresources.size() == 1u);
  assert(rgbaNear(prepared->upload.bytes, prepared->desc.width, 0, 0,
                  255u, 0u, 0u));
  assert(rgbaNear(prepared->upload.bytes, prepared->desc.width, 0,
                  prepared->desc.height - 1, 0u, 255u, 0u));

  unsetEnvVar("KARMA_ASSET_CACHE_DIR");
  unsetEnvVar("KARMA_ASSET_CACHE");
  unsetEnvVar("KARMA_ASSET_CACHE_FLUSH");
  std::filesystem::remove_all(dir);
}

void testImportedMaterialTextureMatchesRendererOrigin() {
  const std::filesystem::path dir = makeTempDir("karma_imported_material_texture_tests");
  const std::filesystem::path source = dir / "textures" / "rows.tga";
  writeSolidRowsTga(source);

  auto imported_material = std::make_shared<karma::rendering::ImportedMaterialData>();
  karma::rendering::ImportedMaterialTexture imported_texture{};
  imported_texture.semantic =
      karma::rendering::ImportedMaterialTextureSemantic::BaseColor;
  imported_texture.source_key = source.string();
  imported_texture.raw_name = source.filename().string();
  imported_texture.resolved_path = source;
  imported_texture.label = "rows";
  imported_texture.embedded = false;
  imported_texture.compressed = true;
  imported_texture.srgb = true;
  imported_material->textures.push_back(std::move(imported_texture));

  karma::rendering::MaterialAssetDesc material{};
  material.imported_material = imported_material;

  karma::assets::AssetRegistry assets;
  const std::vector<std::string> texture_keys =
      assets.registerImportedMaterialTextures("tests/material/rows", material);
  assert(texture_keys.size() == 1u);
  assert(material.textures.at("base_color") == texture_keys.front());

  const karma::assets::TextureAsset* texture =
      assets.findTextureAsset(texture_keys.front());
  assert(texture != nullptr);
  auto prepared = karma::assets::prepareTextureUpload(*texture, {});
  assert(prepared.has_value());
  assert(prepared->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(prepared->desc.width == 4);
  assert(prepared->desc.height == 8);
  assert(rgbaNear(prepared->upload.bytes, prepared->desc.width, 0, 0,
                  0u, 255u, 0u));
  assert(rgbaNear(prepared->upload.bytes, prepared->desc.width, 0,
                  prepared->desc.height - 1, 255u, 0u, 0u));

  std::filesystem::remove_all(dir);
}

void testAssetPackageAsyncCommitAndStore() {
  const std::filesystem::path dir = makeTempDir("karma_asset_package_async_tests");
  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                { "type": "environment_map", "key": "package/async_env", "path": "env.hdr" }
              ]
            })");

  karma::assets::AssetRegistry assets;
  karma::assets::AssetPackageJob job = karma::assets::loadAssetPackageAsync(dir);
  assert(assets.findEnvironmentMap("package/async_env") == nullptr);
  job.wait();
  assert(job.success());
  assert(job.handle() != nullptr);
  karma::assets::AssetPackageHandle committed{};
  assert(karma::assets::commitAssetPackageJob(assets, job, &committed));
  assert(committed.valid());
  assert(assets.findEnvironmentMap("package/async_env") != nullptr);
  assert(karma::assets::unloadAssetPackage(assets, committed));
  assert(assets.findEnvironmentMap("package/async_env") == nullptr);

  writeText(dir / "invalid.package.json", R"({"version": 1, "assets": "bad"})");
  karma::assets::AssetPackageJob failed =
      karma::assets::loadAssetPackageAsync(dir / "invalid.package.json");
  failed.wait();
  assert(!failed.success());
  assert(!karma::assets::commitAssetPackageJob(assets, failed));
  assert(assets.findEnvironmentMap("package/async_env") == nullptr);

  karma::assets::AssetPackageStore store(assets);
  std::string diagnostic;
  auto first = store.acquirePackage(dir, &diagnostic);
  assert(first.has_value());
  assert(diagnostic.empty());
  auto second = store.acquirePackage(dir, &diagnostic);
  assert(second.has_value());
  assert(assets.findEnvironmentMap("package/async_env") != nullptr);
  assert(store.releasePackage(*first));
  assert(assets.findEnvironmentMap("package/async_env") != nullptr);
  assert(store.releasePackage(*second));
  assert(assets.findEnvironmentMap("package/async_env") == nullptr);

  std::filesystem::remove_all(dir);
}

void testGltfSceneInstantiationRegistersLogicalMeshKeys() {
  const std::filesystem::path repo_root = findRepoRoot();
  assert(!repo_root.empty());
  const std::filesystem::path world_path = repo_root / "examples/assets/world.glb";
  assert(std::filesystem::exists(world_path));

  const std::filesystem::path package_dir = makeTempDir("karma_gltf_scene_package_tests");
  writeText(package_dir / "assets.package.json",
            std::string(R"({
              "version": 1,
              "assets": [
                {
                  "type": "gltf_scene",
                  "key": "tests/gltf/world",
                  "path": ")") + world_path.generic_string() +
                R"(",
                  "import_meshes": true,
                  "import_lights": false
                }
              ]
            })");

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(assets, package_dir, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());
  const karma::assets::GltfSceneAsset* scene_asset =
      assets.findGltfSceneAsset("tests/gltf/world");
  assert(scene_asset != nullptr);
  assert(scene_asset->valid());

  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::GltfSceneImportResult imported =
      karma::world::instantiateGltfSceneAsset(
          world,
          scene,
          assets,
          *scene_asset,
          karma::world::GltfSceneInstantiateOptions{
              .create_synthetic_root = false,
              .autoplay_animations = false,
          });
  assert(imported.valid());

  bool saw_mesh = false;
  for (const karma::world::Entity entity : imported.entities) {
    if (!world.isAlive(entity) || !world.has<karma::components::MeshComponent>(entity)) {
      continue;
    }
    saw_mesh = true;
    const auto& mesh = world.get<karma::components::MeshComponent>(entity);
    assert(karma::assets::AssetRegistry::isValidAssetKey(mesh.mesh_asset_key));
    assert(mesh.mesh_asset_key.rfind("tests/gltf/world/meshes/", 0) == 0);
    assert(mesh.mesh_asset_key.find(".glb") == std::string::npos);
    assert(mesh.mesh_asset_key.find("#node=") == std::string::npos);
    const auto* mesh_asset = assets.findMeshAsset(mesh.mesh_asset_key);
    assert(mesh_asset != nullptr);
    for (const auto& slot : mesh_asset->material_slots) {
      if (!slot.default_material_key.empty()) {
        assert(karma::assets::AssetRegistry::isValidAssetKey(slot.default_material_key));
        assert(assets.findMaterialAsset(slot.default_material_key) != nullptr);
      }
    }
  }
  assert(saw_mesh);

  karma::assets::AssetRegistry cached_assets;
  auto cached_package =
      karma::assets::importAssetPackage(cached_assets, package_dir, &diagnostic);
  assert(cached_package.has_value());
  assert(diagnostic.empty());
  const karma::assets::GltfSceneAsset* cached_scene_ptr =
      cached_assets.findGltfSceneAsset("tests/gltf/world");
  assert(cached_scene_ptr != nullptr);
  const karma::assets::GltfSceneAsset& cached_scene = *cached_scene_ptr;
  assert(cached_scene.valid());
  assert(cached_scene.nodes.size() == scene_asset->nodes.size());
  assert(!cached_scene.mesh_asset_keys.empty());
  assert(!cached_scene.material_keys.empty());

  bool saw_cached_default_material = false;
  for (const std::string& mesh_key : cached_scene.mesh_asset_keys) {
    const auto* mesh = cached_assets.findMeshAsset(mesh_key);
    assert(mesh != nullptr);
    for (const auto& slot : mesh->material_slots) {
      if (!slot.default_material_key.empty()) {
        saw_cached_default_material = true;
        assert(cached_assets.findMaterialAsset(slot.default_material_key) != nullptr);
      }
    }
  }
  assert(saw_cached_default_material);

  const std::filesystem::path cache_dir = makeTempDir("karma_gltf_scene_cache_tests");
  karma::assets::AssetCacheConfig cache_config{};
  cache_config.root = cache_dir;
  cache_config.enabled = true;
  cache_config.flush = true;
  karma::assets::AssetCache cache(cache_config);
  assert(cache.writeGltfScene("cached_scene_blob", cached_scene));
  auto restored_scene = cache.readGltfScene("cached_scene_blob");
  assert(restored_scene.has_value());
  assert(restored_scene->valid());
  assert(restored_scene->nodes.size() == cached_scene.nodes.size());
  assert(restored_scene->mesh_asset_keys == cached_scene.mesh_asset_keys);
  std::filesystem::remove_all(cache_dir);

  karma::world::World cached_world;
  karma::world::Scene cached_scene_graph;
  const karma::world::GltfSceneImportResult cached_imported =
      karma::world::instantiateGltfSceneAsset(
          cached_world,
          cached_scene_graph,
          cached_assets,
          *restored_scene,
          karma::world::GltfSceneInstantiateOptions{
              .create_synthetic_root = true,
              .autoplay_animations = false,
          });
  assert(cached_imported.valid());

  bool saw_cached_mesh = false;
  for (const karma::world::Entity entity : cached_imported.entities) {
    if (!cached_world.isAlive(entity) ||
        !cached_world.has<karma::components::MeshComponent>(entity)) {
      continue;
    }
    saw_cached_mesh = true;
    const auto& mesh = cached_world.get<karma::components::MeshComponent>(entity);
    assert(mesh.mesh_asset_key.rfind("tests/gltf/world/meshes/", 0) == 0);
    assert(cached_assets.findMeshAsset(mesh.mesh_asset_key) != nullptr);
  }
  assert(saw_cached_mesh);
  std::filesystem::remove_all(package_dir);
}

void testDeformationHeadlessNoopApi() {
  DummyWindow window;
  karma::rendering::GraphicsDevice device(window);

  karma::rendering::DeformationDesc desc{};
  desc.skinning_enabled = true;
  desc.morphing_enabled = true;
  desc.joint_palette = {glm::mat4(1.0f), glm::mat4(2.0f)};
  desc.morph_weights = {0.25f, 0.75f};

  const karma::rendering::DeformationId deformation = device.createDeformation(desc);
  assert(deformation == karma::rendering::kInvalidDeformation);
  device.updateDeformation(deformation, desc);
  device.destroyDeformation(deformation);

  const auto stats = device.getDeformationStats();
  assert(stats.resource_count == 0u);
  assert(stats.joint_matrix_count == 0u);
  assert(stats.morph_weight_count == 0u);
}

}  // namespace

int main() {
  testEngineConfigFramePacingDefaultAndOptOut();
  testPrimitiveMeshAndDiffuseMaterialHelpers();
  testAssetRegistryMaterialInheritance();
  testMaterialFileLoading();
  testAssetKeyValidationAndPackages();
  testFrameGraphValidationAndRegistryFallback();
  testFrameGraphAssetPackageLoadCacheAndUnload();
  testAssetCacheV2AndPackageWarmRestore();
  testTexturePreparedUploadPreservesRowOrder();
  testImportedMaterialTextureMatchesRendererOrigin();
  testAssetPackageAsyncCommitAndStore();
  testGltfSceneInstantiationRegistersLogicalMeshKeys();
  testAssetRegistryRegisterResolveUnregister();

  testTerrainHeadlessNoopApi();
  testDeformationHeadlessNoopApi();

  return 0;
}
