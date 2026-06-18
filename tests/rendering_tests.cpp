#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "karma/content/assets/asset_package.h"
#include "karma/content/assets/asset_registry.h"
#include "karma/content/materials/material_loader.h"
#include <glm/mat4x4.hpp>

#include "karma/content/importers/gltf_scene_import.h"
#include "karma/platform/window/window.h"
#include "karma/rendering/renderer/device.h"
#include "karma/world/components/mesh.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

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

karma::geometry::MeshData makeTriangleMesh() {
  karma::geometry::MeshData mesh;
  mesh.vertices = {{0.0f, 0.0f, 0.0f},
                   {1.0f, 0.0f, 0.0f},
                   {0.0f, 0.0f, 1.0f}};
  mesh.indices = {0u, 1u, 2u};
  return mesh;
}

void testAssetRegistryRegisterResolveUnregister() {
  karma::content::AssetRegistry assets;

  const uint64_t start_version = assets.version();
  assets.registerMeshAsset("mesh/tri", makeTriangleMesh());
  assert(assets.version() > start_version);
  assert(assets.findMeshAsset("mesh/tri") != nullptr);
  assert(assets.findMeshAsset("missing") == nullptr);

  karma::content::TextureAsset texture{};
  texture.desc.width = 1;
  texture.desc.height = 1;
  texture.bytes = {255u, 255u, 255u, 255u};
  assets.registerTextureAsset("texture/white", std::move(texture));
  assert(assets.findTextureAsset("texture/white") != nullptr);

  karma::renderer::MaterialDesc material{};
  material.base_color = {0.1f, 0.2f, 0.3f, 1.0f};
  assets.registerMaterialAsset("material/base", material);
  karma::renderer::MaterialVariantDesc variant{};
  variant.base_material_key = "material/base";
  variant.params["roughness"] = 0.4f;
  assets.registerMaterialVariant("material/variant", variant);
  const auto resolved_material = assets.resolveMaterial("material/variant");
  assert(resolved_material.has_value());
  assert(nearly(resolved_material->surface.base_color.b, 0.3f));
  assert(resolved_material->params.contains("roughness"));

  karma::renderer::PostProcessSettings profile{};
  profile.bloom_enabled = true;
  assets.registerPostProcessProfile("post/cinematic", profile);
  assert(assets.findPostProcessProfile("post/cinematic") != nullptr);
  assert(assets.resolvePostProcessProfile("post/cinematic").bloom_enabled);

  karma::particles::ParticleEffectAsset effect{};
  effect.emitters.push_back(karma::particles::ParticleEmitterDesc{});
  effect.emitters.front().texture_key = "texture/white";
  assets.registerParticleEffect("particle/spark", std::move(effect));
  assert(assets.findParticleEffect("particle/spark") != nullptr);
  assert(assets.findParticleEffect("particle/spark")->primaryEmitter() != nullptr);
  const uint64_t before_texture_version = assets.textureVersion();
  karma::content::TextureAsset particle_texture{};
  particle_texture.desc.width = 1;
  particle_texture.desc.height = 1;
  particle_texture.bytes = {255u, 255u, 255u, 255u};
  assert(assets.registerTextureAsset("particle/texture_asset", std::move(particle_texture)));
  assert(assets.textureVersion() > before_texture_version);
  assert(assets.findTextureAsset("particle/texture_asset") != nullptr);
  assert(assets.unregisterTextureAsset("particle/texture_asset"));
  assert(assets.findTextureAsset("particle/texture_asset") == nullptr);

  assets.registerAudioClip("audio/wind", karma::content::AudioClipAsset{.path = "wind.ogg"});
  assert(assets.findAudioClip("audio/wind") != nullptr);

  assets.registerEnvironmentMap("env/studio",
                                karma::content::EnvironmentMapAsset{.path = "studio.hdr"});
  assert(assets.findEnvironmentMap("env/studio") != nullptr);

  karma::animation::AnimationClip clip{};
  clip.name = "idle";
  clip.duration_seconds = 1.25f;
  assets.registerAnimationClip("clip/idle", clip);
  assert(assets.findAnimationClip("clip/idle") != nullptr);
  assert(nearly(assets.findAnimationClip("clip/idle")->duration_seconds, 1.25f));

  karma::animation::Skeleton skeleton{};
  skeleton.name = "humanoid";
  skeleton.joints.push_back(karma::animation::Joint{.name = "root"});
  assets.registerSkeleton("skeleton/humanoid", skeleton);
  assert(assets.findSkeleton("skeleton/humanoid") != nullptr);

  karma::animation::Skin skin{};
  skin.name = "skin";
  skin.joint_node_indices = {0u};
  assets.registerSkin("skin/body", skin);
  assert(assets.findSkin("skin/body") != nullptr);

  karma::content::GltfSceneAsset scene{};
  scene.scene_key = "scene/root";
  scene.mesh_asset_keys.push_back("mesh/tri");
  assets.registerGltfSceneAsset("scene/import", scene);
  assert(assets.findGltfSceneAsset("scene/import") != nullptr);

  assert(assets.unregisterMeshAsset("mesh/tri"));
  assert(assets.findMeshAsset("mesh/tri") == nullptr);
  assert(!assets.unregisterMeshAsset("mesh/tri"));
  assert(assets.unregisterTextureAsset("texture/white"));
  assets.unregisterMaterial("material/variant");
  assets.unregisterPostProcessProfile("post/cinematic");
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
  karma::content::AssetRegistry assets;

  karma::renderer::MaterialDesc base{};
  base.base_color = {0.25f, 0.5f, 0.75f, 1.0f};
  base.roughness = 0.8f;
  base.metallic = 0.1f;
  assets.registerMaterialAsset("paint", base);

  karma::renderer::MaterialVariantDesc variant{};
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
  auto standard_desc = karma::content::loadMaterialAssetDesc(standard, &diagnostic);
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
              "params": { "wave_tint_strength": 0.75 }
            })");
  auto custom_desc = karma::content::loadMaterialAssetDesc(custom, &diagnostic);
  assert(custom_desc.has_value());
  assert(custom_desc->pipeline.name == "custom");
  assert(custom_desc->pipeline.vertex_shader_path ==
         (dir / "shaders" / "custom_vs.hlsl").lexically_normal());
  assert(custom_desc->pipeline.vertex_entry_point == "VSMain");
  assert(custom_desc->pipeline.defines.size() == 1);

  const std::filesystem::path variant = dir / "materials" / "variant.mat";
  writeText(variant,
            R"({
              "version": 2,
              "kind": "variant",
              "base": "paint",
              "surface": { "roughness": 0.15 },
              "render_state": { "transparent": true }
            })");
  auto variant_desc = karma::content::loadMaterialVariantDesc(variant, &diagnostic);
  assert(variant_desc.has_value());
  assert(variant_desc->base_material_key == "paint");
  assert(variant_desc->params.contains("roughness"));
  assert(variant_desc->params.contains("transparent"));

  const std::filesystem::path invalid = dir / "materials" / "invalid.mat";
  writeText(invalid, R"({"version": 2, "pipeline": { "name": "custom" }})");
  auto invalid_desc = karma::content::loadMaterialAssetDesc(invalid, &diagnostic);
  assert(!invalid_desc.has_value());
  assert(!diagnostic.empty());

  std::filesystem::remove_all(dir);
}

void testAssetKeyValidationAndPackages() {
  assert(karma::content::AssetRegistry::isValidAssetKey("examples/mesh/world"));
  assert(karma::content::AssetRegistry::isValidAssetKey("default"));
  assert(!karma::content::AssetRegistry::isValidAssetKey(""));
  assert(!karma::content::AssetRegistry::isValidAssetKey("/tmp/world"));
  assert(!karma::content::AssetRegistry::isValidAssetKey("../world"));
  assert(!karma::content::AssetRegistry::isValidAssetKey("textures\\world"));
  assert(!karma::content::AssetRegistry::isValidAssetKey("examples/assets/world.glb"));
  assert(!karma::content::AssetRegistry::assetKeyValidationError("examples/assets/world.glb").empty());

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

  karma::content::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::content::importAssetPackage(assets, dir, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());
  const auto* texture = assets.findTextureAsset("package/spark_atlas");
  assert(texture != nullptr);
  assert(texture->desc.width == 256);
  assert(texture->desc.height == 64);
  assert(!texture->desc.generate_mips);
  assert(!texture->bytes.empty());
  assert(assets.findParticleEffect("package/flash") != nullptr);
  assert(assets.findEnvironmentMap("package/env") != nullptr);
  assert(karma::content::unloadAssetPackage(assets, *package));
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
      karma::content::importAssetPackage(assets, dir / "rollback.package.json", &diagnostic);
  assert(!rollback_package.has_value());
  assert(!diagnostic.empty());
  assert(assets.findEnvironmentMap("package/rollback_env") == nullptr);
  assert(assets.findParticleEffect("package/missing_effect") == nullptr);

  assert(assets.registerEnvironmentMap(
      "package/dupe_env",
      karma::content::EnvironmentMapAsset{.path = dir / "environment.hdr"}));
  writeText(dir / "duplicate.package.json",
            R"({
              "version": 1,
              "assets": [
                { "type": "environment_map", "key": "package/dupe_env", "path": "environment.hdr" }
              ]
            })");
  diagnostic.clear();
  auto duplicate_package =
      karma::content::importAssetPackage(assets, dir / "duplicate.package.json", &diagnostic);
  assert(!duplicate_package.has_value());
  assert(!diagnostic.empty());
  assert(assets.findEnvironmentMap("package/dupe_env") != nullptr);

  std::filesystem::remove_all(dir);
}

void testGltfSceneInstantiationRegistersLogicalMeshKeys() {
  const std::filesystem::path repo_root = findRepoRoot();
  assert(!repo_root.empty());
  const std::filesystem::path world_path = repo_root / "examples/assets/world.glb";
  assert(std::filesystem::exists(world_path));

  const karma::scene::GltfScenePrefab prefab =
      karma::scene::loadGltfScenePrefab(world_path,
                                        karma::scene::GltfSceneLoadOptions{
                                            .import_meshes = true,
                                            .import_lights = false,
                                        });
  assert(prefab.valid());

  karma::content::AssetRegistry assets;
  karma::ecs::World world;
  karma::scene::Scene scene;
  const karma::scene::GltfSceneImportResult imported =
      karma::scene::instantiateGltfScenePrefab(
          world,
          scene,
          assets,
          prefab,
          karma::scene::GltfSceneInstantiateOptions{
              .create_synthetic_root = false,
              .autoplay_animations = false,
              .asset_key_prefix = "tests/gltf/world",
          });
  assert(imported.valid());

  bool saw_mesh = false;
  for (const karma::ecs::Entity entity : imported.entities) {
    if (!world.isAlive(entity) || !world.has<karma::components::MeshComponent>(entity)) {
      continue;
    }
    saw_mesh = true;
    const auto& mesh = world.get<karma::components::MeshComponent>(entity);
    assert(karma::content::AssetRegistry::isValidAssetKey(mesh.mesh_asset_key));
    assert(mesh.mesh_asset_key.rfind("tests/gltf/world/", 0) == 0);
    assert(mesh.mesh_asset_key.find(".glb") == std::string::npos);
    assert(mesh.mesh_asset_key.find("#node=") == std::string::npos);
    const auto* mesh_asset = assets.findMeshAsset(mesh.mesh_asset_key);
    assert(mesh_asset != nullptr);
    for (const auto& slot : mesh_asset->material_slots) {
      if (!slot.default_material_key.empty()) {
        assert(karma::content::AssetRegistry::isValidAssetKey(slot.default_material_key));
        assert(assets.findMaterialAsset(slot.default_material_key) != nullptr);
      }
    }
  }
  assert(saw_mesh);
}

void testDeformationHeadlessNoopApi() {
  DummyWindow window;
  karma::renderer::GraphicsDevice device(window);

  karma::renderer::DeformationDesc desc{};
  desc.skinning_enabled = true;
  desc.morphing_enabled = true;
  desc.joint_palette = {glm::mat4(1.0f), glm::mat4(2.0f)};
  desc.morph_weights = {0.25f, 0.75f};

  const karma::renderer::DeformationId deformation = device.createDeformation(desc);
  assert(deformation == karma::renderer::kInvalidDeformation);
  device.updateDeformation(deformation, desc);
  device.destroyDeformation(deformation);

  const auto stats = device.getDeformationStats();
  assert(stats.resource_count == 0u);
  assert(stats.joint_matrix_count == 0u);
  assert(stats.morph_weight_count == 0u);
}

}  // namespace

int main() {
  testAssetRegistryMaterialInheritance();
  testMaterialFileLoading();
  testAssetKeyValidationAndPackages();
  testGltfSceneInstantiationRegistersLogicalMeshKeys();
  testAssetRegistryRegisterResolveUnregister();

  karma::content::AssetRegistry assets;

  constexpr const char* kDefaultPostProfile = "default";
  assert(!assets.resolvePostProcessProfile(kDefaultPostProfile).bloom_enabled);
  assert(!assets.resolvePostProcessProfile("missing").bloom_enabled);

  karma::renderer::PostProcessSettings default_profile{};
  default_profile.bloom_enabled = true;
  default_profile.bloom_intensity = 0.6f;
  assets.registerPostProcessProfile(kDefaultPostProfile, default_profile);
  assert(assets.resolvePostProcessProfile(kDefaultPostProfile).bloom_enabled);
  assert(assets.resolvePostProcessProfile("missing").bloom_enabled);
  assert(assets.resolvePostProcessProfile("missing").bloom_intensity == 0.6f);

  karma::renderer::PostProcessSettings named_profile{};
  named_profile.tone_mapping_enabled = true;
  named_profile.tone_exposure = 1.25f;
  assets.registerPostProcessProfile("cinematic", named_profile);
  assert(assets.resolvePostProcessProfile("cinematic").tone_mapping_enabled);
  assert(assets.resolvePostProcessProfile("cinematic").tone_exposure == 1.25f);
  assert(!assets.resolvePostProcessProfile("cinematic").bloom_enabled);

  karma::renderer::PostProcessSettings replacement_default{};
  replacement_default.depth_of_field_enabled = true;
  assets.registerPostProcessProfile(kDefaultPostProfile, replacement_default);
  assert(assets.resolvePostProcessProfile(kDefaultPostProfile).depth_of_field_enabled);
  assert(assets.resolvePostProcessProfile("missing").depth_of_field_enabled);

  assets.unregisterPostProcessProfile("cinematic");
  assert(assets.resolvePostProcessProfile("cinematic").depth_of_field_enabled);

  testTerrainHeadlessNoopApi();
  testDeformationHeadlessNoopApi();

  return 0;
}
