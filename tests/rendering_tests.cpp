#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
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

#include "../src/rendering/renderer/render_system/extractors.h"
#include "../src/rendering/renderer/render_system/debug_draw.h"
#include "../src/content/importers/gltf_scene_import_internal.h"
#include "../src/private/rendering/ktx_cube_orientation.hpp"
#include "../src/private/rendering/point_shadow_policy.hpp"

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

void testRenderFrameOwnershipAndRenderTargetValidation() {
  karma::rendering::RenderTargetDesc target{};
  assert(target.valid());
  target.width = 320;
  assert(!target.valid());
  target.height = 180;
  assert(target.valid());
  target.width = 0;
  assert(!target.valid());
  target.width = -1;
  target.height = -1;
  assert(!target.valid());
  target = {};
  target.depth = false;
  assert(!target.valid());
  target.stencil = true;
  assert(!target.valid());

  DummyWindow window;
  karma::rendering::GraphicsDevice device(window);
  const karma::rendering::FrameInfo frame{
      .width = 128,
      .height = 64,
      .delta_time = 0.016f,
  };
  device.beginFrame(frame);
  bool nested_begin_rejected = false;
  try {
    device.beginFrame(frame);
  } catch (const std::logic_error&) {
    nested_begin_rejected = true;
  }
  assert(nested_begin_rejected);
  device.endFrame();
  device.beginFrame(frame);
  device.endFrame();
}

void testTerrainHeadlessNoopApi() {
  DummyWindow window;
  karma::rendering::GraphicsDevice device(window);
  assert(!device.isValid());

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

void testSkyboxShaderPreservesGeneratedCubemapOrientation() {
  const std::filesystem::path source_path =
      std::filesystem::path(__FILE__).parent_path() /
      "../src/rendering/renderer/backends/diligent/passes/environment.cpp";
  std::ifstream stream(source_path);
  assert(stream);
  const std::string source((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());

  const size_t skybox_begin = source.find("kSkyboxPS");
  const size_t skybox_end = source.find("kIrradiancePS", skybox_begin);
  assert(skybox_begin != std::string::npos);
  assert(skybox_end != std::string::npos);
  const std::string_view shader(source.data() + skybox_begin,
                                skybox_end - skybox_begin);
  assert(shader.find("normalize(input.local_pos)") != std::string_view::npos);
  assert(shader.find("-input.local_pos.y") == std::string_view::npos);
  assert(shader.find("g_Params.x") != std::string_view::npos);
  assert(shader.find("1.0 - exp(-color)") != std::string_view::npos);

  const size_t prefilter_begin = source.find("kPrefilterPS", skybox_end);
  assert(prefilter_begin != std::string::npos);
  const std::string_view irradiance_shader(source.data() + skybox_end,
                                           prefilter_begin - skybox_end);
  assert(irradiance_shader.find("Hammersley(i, SAMPLE_COUNT)") !=
         std::string_view::npos);
  assert(irradiance_shader.find("irradiance += g_EnvMap.Sample") !=
         std::string_view::npos);
  assert(irradiance_shader.find(".rgb * cos_theta") == std::string_view::npos);
  assert(irradiance_shader.find("irradiance /= SAMPLE_COUNT") !=
         std::string_view::npos);

  const size_t brdf_begin = source.find("kBrdfLutVS", prefilter_begin);
  assert(brdf_begin != std::string::npos);
  const std::string_view prefilter_shader(source.data() + prefilter_begin,
                                          brdf_begin - prefilter_begin);
  assert(prefilter_shader.find("DistributionGGX") != std::string_view::npos);
  assert(prefilter_shader.find("SampleLevel(g_Sampler, L, mip_level)") !=
         std::string_view::npos);
  assert(source.find("lighting_exposure_ * environment_intensity_") !=
         std::string::npos);
  assert(source.find("normalizeKtxCubemapWorldDirections") != std::string::npos);
}

void testKtxCubemapOrientationNormalization() {
  constexpr std::size_t kFaceCount = 6u;
  constexpr std::size_t kFaceBytes = 4u;
  constexpr std::size_t kSecondMipOffset = kFaceCount * kFaceBytes;
  std::vector<unsigned char> bytes(kSecondMipOffset + kFaceCount);
  for (std::size_t face = 0; face < kFaceCount; ++face) {
    for (std::size_t texel = 0; texel < kFaceBytes; ++texel) {
      bytes[face * kFaceBytes + texel] =
          static_cast<unsigned char>(face * 10u + texel);
    }
    bytes[kSecondMipOffset + face] = static_cast<unsigned char>(100u + face);
  }
  const std::vector<std::size_t> offsets{
      0u, 4u, 8u, 12u, 16u, 20u,
      24u, 25u, 26u, 27u, 28u, 29u,
  };
  const std::vector<std::uint32_t> widths{2u, 1u};
  const std::vector<std::uint32_t> heights{2u, 1u};

  assert(karma::rendering::detail::normalizeKtxCubemapWorldDirections(
      bytes, offsets, widths, heights, 1u));
  const std::vector<unsigned char> expected{
      2u,  3u,  0u,  1u,
      12u, 13u, 10u, 11u,
      32u, 33u, 30u, 31u,
      22u, 23u, 20u, 21u,
      42u, 43u, 40u, 41u,
      52u, 53u, 50u, 51u,
      100u, 101u, 103u, 102u, 104u, 105u,
  };
  assert(bytes == expected);

  std::vector<unsigned char> invalid(8u);
  const std::vector<std::size_t> overlapping_offsets(12u, 0u);
  assert(!karma::rendering::detail::normalizeKtxCubemapWorldDirections(
      invalid, overlapping_offsets, widths, heights, 1u));
}

void testAssimpEmbeddedTextureCanonicalization() {
  const std::vector<uint8_t> top_down_bgra{
      0u, 0u, 255u, 255u,      0u, 255u, 0u, 255u,
      255u, 0u, 0u, 255u,      255u, 255u, 255u, 128u,
  };
  std::vector<uint8_t> rgba;
  assert(karma::world::detail::canonicalizeAssimpEmbeddedTexture(
      top_down_bgra, 2u, 2u, rgba));
  const std::vector<uint8_t> expected{
      0u, 0u, 255u, 255u,      255u, 255u, 255u, 128u,
      255u, 0u, 0u, 255u,      0u, 255u, 0u, 255u,
  };
  assert(rgba == expected);

  rgba = {1u};
  assert(!karma::world::detail::canonicalizeAssimpEmbeddedTexture(
      std::span<const uint8_t>(top_down_bgra.data(), 4u), 2u, 2u, rgba));
  assert(rgba.empty());

  auto imported_material = std::make_shared<karma::rendering::ImportedMaterialData>();
  karma::rendering::ImportedMaterialTexture imported_texture{};
  imported_texture.semantic =
      karma::rendering::ImportedMaterialTextureSemantic::Emissive;
  imported_texture.source_key = "tests/raw-assimp-texture";
  imported_texture.raw_name = "raw-assimp-texture";
  imported_texture.label = "raw-assimp-texture";
  imported_texture.source_bytes = expected;
  imported_texture.width = 2u;
  imported_texture.height = 2u;
  imported_texture.embedded = true;
  imported_texture.compressed = false;
  imported_texture.srgb = true;
  imported_material->textures.push_back(std::move(imported_texture));

  karma::rendering::MaterialAssetDesc material{};
  material.imported_material = std::move(imported_material);
  karma::assets::AssetRegistry assets;
  const std::vector<std::string> keys =
      assets.registerImportedMaterialTextures("tests/raw-assimp-material", material);
  assert(keys.size() == 1u);
  const karma::assets::TextureAsset* texture = assets.findTextureAsset(keys.front());
  assert(texture != nullptr);
  const auto prepared = karma::assets::prepareTextureUpload(*texture, {});
  assert(prepared.has_value());
  assert(!prepared->upload.subresources.empty());
  const auto& base = prepared->upload.subresources.front();
  const std::size_t bottom_left = base.offset;
  const std::size_t top_left = base.offset + base.row_stride;
  assert(bottom_left + 3u < prepared->upload.bytes.size());
  assert(top_left + 3u < prepared->upload.bytes.size());
  assert(prepared->upload.bytes[bottom_left + 2u] == 255u);
  assert(prepared->upload.bytes[top_left + 0u] == 255u);
}

void testTextureUploadValidation() {
  std::size_t texture_size = 99u;
  assert(karma::rendering::tryTextureDataSize(4, 3, 4u, texture_size));
  assert(texture_size == 48u);
  assert(!karma::rendering::tryTextureDataSize(0, 3, 4u, texture_size));
  assert(texture_size == 0u);
  assert(!karma::rendering::tryTextureDataSize(
      2, 1, std::numeric_limits<std::size_t>::max(), texture_size));
  assert(texture_size == 0u);

  karma::rendering::TextureDesc desc{
      .width = 4,
      .height = 2,
      .format = karma::rendering::TextureFormat::RGBA8,
      .mip_levels = 2u,
  };
  assert(desc.valid());

  karma::rendering::TextureUploadData upload{};
  upload.format = desc.format;
  upload.bytes.resize(36u);
  upload.subresources.push_back(karma::rendering::TextureUploadSubresource{
      .mip_level = 0u,
      .width = 4,
      .height = 2,
      .offset = 0u,
      .size = 36u,
      .row_stride = 20u,
  });
  assert(karma::rendering::validateTextureUpload(desc, upload));

  upload.subresources.front().size = 35u;
  assert(!karma::rendering::validateTextureUpload(desc, upload));
  upload.subresources.front().size = 36u;
  upload.subresources.front().row_stride = 15u;
  assert(!karma::rendering::validateTextureUpload(desc, upload));
  upload.subresources.front().row_stride = 20u;
  upload.subresources.front().width = 3;
  assert(!karma::rendering::validateTextureUpload(desc, upload));
  upload.subresources.front().width = 4;
  upload.subresources.push_back(upload.subresources.front());
  assert(!karma::rendering::validateTextureUpload(desc, upload));

  karma::rendering::TextureDesc rgb_desc{
      .width = 2,
      .height = 2,
      .format = karma::rendering::TextureFormat::RGB8,
  };
  karma::rendering::TextureUploadData rgb_upload{};
  rgb_upload.format = rgb_desc.format;
  rgb_upload.bytes.resize(14u);
  rgb_upload.subresources.push_back(karma::rendering::TextureUploadSubresource{
      .width = 2,
      .height = 2,
      .size = 14u,
      .row_stride = 8u,
  });
  assert(karma::rendering::validateTextureUpload(rgb_desc, rgb_upload));
  assert(karma::rendering::textureUploadMinimumRowStride(rgb_desc.format, 2) == 6u);

  karma::rendering::TextureDesc r8_desc{
      .width = 3,
      .height = 2,
      .format = karma::rendering::TextureFormat::R8,
  };
  karma::rendering::TextureUploadData r8_upload{};
  r8_upload.format = r8_desc.format;
  r8_upload.bytes.resize(6u);
  r8_upload.subresources.push_back(karma::rendering::TextureUploadSubresource{
      .width = 3,
      .height = 2,
      .size = 6u,
  });
  assert(karma::rendering::validateTextureUpload(r8_desc, r8_upload));

  karma::rendering::TextureDesc rgba16f_desc{
      .width = 2,
      .height = 2,
      .format = karma::rendering::TextureFormat::RGBA16F,
  };
  karma::rendering::TextureUploadData rgba16f_upload{};
  rgba16f_upload.format = rgba16f_desc.format;
  rgba16f_upload.bytes.resize(32u);
  rgba16f_upload.subresources.push_back(
      karma::rendering::TextureUploadSubresource{
          .width = 2,
          .height = 2,
          .size = 32u,
      });
  assert(rgba16f_desc.valid());
  assert(karma::rendering::textureUploadMinimumRowStride(
             rgba16f_desc.format, 2) == 16u);
  assert(karma::rendering::validateTextureUpload(rgba16f_desc,
                                                  rgba16f_upload));
  rgba16f_desc.srgb = true;
  assert(!rgba16f_desc.valid());

  karma::rendering::TextureDesc bc7_desc{
      .width = 7,
      .height = 5,
      .format = karma::rendering::TextureFormat::BC7_RGBA_UNORM,
  };
  karma::rendering::TextureUploadData bc7_upload{};
  bc7_upload.format = bc7_desc.format;
  bc7_upload.bytes.resize(64u);
  bc7_upload.subresources.push_back(karma::rendering::TextureUploadSubresource{
      .width = 7,
      .height = 5,
      .size = 64u,
  });
  assert(karma::rendering::validateTextureUpload(bc7_desc, bc7_upload));
  assert(karma::rendering::textureUploadMinimumRowStride(bc7_desc.format, 7) == 32u);
  assert(karma::rendering::textureUploadRowCount(bc7_desc.format, 5) == 2u);
  bc7_desc.generate_mips = true;
  assert(!bc7_desc.valid());
  assert(!karma::rendering::validateTextureUpload(bc7_desc, bc7_upload));

  karma::rendering::TextureDesc generated_mip_desc{
      .width = 4,
      .height = 2,
      .format = karma::rendering::TextureFormat::RGBA8,
      .generate_mips = true,
  };
  karma::rendering::TextureUploadData generated_mip_upload{};
  generated_mip_upload.format = generated_mip_desc.format;
  generated_mip_upload.bytes.resize(8u);
  generated_mip_upload.subresources.push_back(
      karma::rendering::TextureUploadSubresource{
          .mip_level = 1u,
          .width = 2,
          .height = 1,
          .size = 8u,
      });
  assert(!karma::rendering::validateTextureUpload(generated_mip_desc,
                                                   generated_mip_upload));
  generated_mip_upload.bytes.resize(32u);
  generated_mip_upload.subresources.front() =
      karma::rendering::TextureUploadSubresource{
          .width = 4,
          .height = 2,
          .size = 32u,
      };
  assert(karma::rendering::validateTextureUpload(generated_mip_desc,
                                                  generated_mip_upload));
  generated_mip_upload.subresources.push_back(
      karma::rendering::TextureUploadSubresource{
          .mip_level = 1u,
          .width = 2,
          .height = 1,
          .offset = 0u,
          .size = 8u,
      });
  assert(!karma::rendering::validateTextureUpload(generated_mip_desc,
                                                   generated_mip_upload));

  auto invalid_format_desc = desc;
  invalid_format_desc.format = static_cast<karma::rendering::TextureFormat>(255);
  assert(!invalid_format_desc.valid());

  desc.mip_levels = 4u;
  assert(!desc.valid());
}

void testScreenPointToWorldRayValidation() {
  karma::rendering::ScreenRay ray{};
  assert(karma::rendering::screenPointToWorldRay(
      50.0, 25.0, 100, 50, {}, {0.0f, 0.0f, 0.0f, 2.0f}, 60.0f, ray));
  assert(nearly(ray.origin.x, 0.0f));
  assert(nearly(ray.direction.x, 0.0f));
  assert(nearly(ray.direction.y, 0.0f));
  assert(nearly(ray.direction.z, -1.0f));

  const karma::rendering::ScreenRay sentinel{
      .origin = {3.0f, 4.0f, 5.0f},
      .direction = {1.0f, 0.0f, 0.0f},
  };
  ray = sentinel;
  assert(!karma::rendering::screenPointToWorldRay(
      50.0, 25.0, 100, 50, {}, {}, 180.0f, ray));
  assert(nearly(ray.origin.x, sentinel.origin.x));
  assert(nearly(ray.direction.x, sentinel.direction.x));

  assert(!karma::rendering::screenPointToWorldRay(
      std::numeric_limits<double>::quiet_NaN(),
      25.0,
      100,
      50,
      {},
      {},
      60.0f,
      ray));
}

void testDebugWireScaleAndCapsuleDimensions() {
  assert(nearly(karma::rendering::render_system::scaledSphereWireRadius(
                    0.5f, {-2.0f, -3.0f, -1.0f}),
                1.5f));
  const auto capsule =
      karma::rendering::render_system::scaledCapsuleWireDimensions(
          0.5f, 4.0f, {-2.0f, -3.0f, -1.0f});
  assert(nearly(capsule.radius, 1.0f));
  assert(nearly(capsule.cylinder_half_length, 5.0f));

  const auto sphere_capsule =
      karma::rendering::render_system::scaledCapsuleWireDimensions(
          1.0f, 1.0f, {1.0f, 1.0f, 1.0f});
  assert(nearly(sphere_capsule.radius, 1.0f));
  assert(nearly(sphere_capsule.cylinder_half_length, 0.0f));

  const float maximum = std::numeric_limits<float>::max();
  assert(nearly(karma::rendering::render_system::scaledSphereWireRadius(
                    maximum, {2.0f, 1.0f, 1.0f}),
                0.0f));
  const auto overflow_capsule =
      karma::rendering::render_system::scaledCapsuleWireDimensions(
          maximum, maximum, {2.0f, 2.0f, 2.0f});
  assert(nearly(overflow_capsule.radius, 0.0f));
  assert(nearly(overflow_capsule.cylinder_half_length, 0.0f));
}

void testEngineConfigFramePacingDefaultAndOptOut() {
  karma::app::EngineConfig config{};
  assert(nearly(config.frame_pacing_fps, 60.0f));

  config.frame_pacing_fps = 0.0f;
  assert(nearly(config.frame_pacing_fps, 0.0f));
}

void testAntiAliasingSettingsDefaultsAndClamp() {
  const karma::rendering::AntiAliasingSettings defaults{};
  assert(defaults.mode == karma::rendering::AntiAliasingMode::None);
  assert(defaults.msaa_samples == 4u);
  assert(nearly(defaults.ssaa_scale, 2.0f));

  const auto none = karma::rendering::AntiAliasingSettings::none();
  assert(none.mode == karma::rendering::AntiAliasingMode::None);
  assert(none.msaa_samples == 1u);
  assert(nearly(none.ssaa_scale, 1.0f));

  const auto msaa = karma::rendering::AntiAliasingSettings::msaa(3u);
  assert(msaa.mode == karma::rendering::AntiAliasingMode::MSAA);
  assert(msaa.msaa_samples == 4u);
  assert(nearly(msaa.ssaa_scale, 1.0f));

  const auto ssaa = karma::rendering::AntiAliasingSettings::ssaa(8.0f);
  assert(ssaa.mode == karma::rendering::AntiAliasingMode::SSAA);
  assert(ssaa.msaa_samples == 1u);
  assert(nearly(ssaa.ssaa_scale, 4.0f));

  const auto disabled_ssaa = karma::rendering::AntiAliasingSettings::ssaa(0.75f);
  assert(disabled_ssaa.mode == karma::rendering::AntiAliasingMode::None);
  assert(disabled_ssaa.msaa_samples == 1u);
  assert(nearly(disabled_ssaa.ssaa_scale, 1.0f));

  auto raw_ssaa = karma::rendering::AntiAliasingSettings{};
  raw_ssaa.mode = karma::rendering::AntiAliasingMode::SSAA;
  raw_ssaa.ssaa_scale = std::numeric_limits<float>::quiet_NaN();
  raw_ssaa = karma::rendering::clampAntiAliasingSettings(raw_ssaa);
  assert(raw_ssaa.mode == karma::rendering::AntiAliasingMode::None);
  assert(nearly(raw_ssaa.ssaa_scale, 1.0f));

  auto raw_msaa = karma::rendering::AntiAliasingSettings{};
  raw_msaa.mode = karma::rendering::AntiAliasingMode::MSAA;
  raw_msaa.msaa_samples = 3u;
  raw_msaa.ssaa_scale = 3.0f;
  raw_msaa = karma::rendering::clampAntiAliasingSettings(raw_msaa);
  assert(raw_msaa.mode == msaa.mode);
  assert(raw_msaa.msaa_samples == msaa.msaa_samples);
  assert(nearly(raw_msaa.ssaa_scale, msaa.ssaa_scale));
}

void testRendererSettingsClampNonFiniteValues() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  karma::rendering::PostProcessSettings post_process{};
  post_process.bloom_threshold = nan;
  post_process.bloom_intensity = nan;
  post_process.bloom_radius = nan;
  post_process.tone_exposure = nan;
  post_process.tone_contrast = nan;
  post_process.tone_saturation = nan;
  post_process.ssao_radius = nan;
  post_process.ssao_intensity = nan;
  post_process.ssao_power = nan;
  post_process.ssr_intensity = nan;
  post_process.ssr_max_roughness = nan;
  post_process.ssr_thickness = nan;
  post_process.taa_feedback = nan;
  post_process.taa_sharpening = nan;
  post_process.dof_focus_depth = nan;
  post_process.dof_focus_range = nan;
  post_process.dof_intensity = nan;
  const auto clamped_post_process =
      karma::rendering::clampPostProcessSettings(post_process);
  assert(nearly(clamped_post_process.bloom_threshold, 1.0f));
  assert(nearly(clamped_post_process.tone_exposure, 1.0f));
  assert(nearly(clamped_post_process.ssao_radius, 1.5f));
  assert(nearly(clamped_post_process.ssr_thickness, 0.08f));
  assert(nearly(clamped_post_process.taa_feedback, 0.92f));
  assert(nearly(clamped_post_process.dof_focus_depth, 8.0f));

  const auto graph = karma::rendering::frameGraphFromPostProcessSettings(post_process);
  const auto post_pass = std::find_if(
      graph.passes.begin(), graph.passes.end(), [](const auto& pass) {
        return pass.builtin_pass == "post_process";
      });
  assert(post_pass != graph.passes.end());
  assert(nearly(std::get<float>(post_pass->params.at("bloom_threshold")), 1.0f));

  const auto shadow = karma::rendering::clampShadowSettings({
      .bias = nan,
      .map_size = std::numeric_limits<int>::max(),
      .pcf_radius = -5,
      .raster_depth_bias = std::numeric_limits<int>::min(),
      .raster_slope_bias = nan,
      .receiver_bias_scale = nan,
      .normal_bias_scale = nan,
  });
  assert(nearly(shadow.bias, 0.0006f));
  assert(shadow.map_size == 16384);
  assert(shadow.pcf_radius == 0);
  assert(shadow.raster_depth_bias == -65536);
  assert(std::isfinite(shadow.raster_slope_bias));
  assert(std::isfinite(shadow.receiver_bias_scale));
  assert(std::isfinite(shadow.normal_bias_scale));

  const auto point_shadow = karma::rendering::clampPointShadowSettings({
      .constant_bias = nan,
      .slope_bias_scale = nan,
      .normal_bias_scale = nan,
      .receiver_bias_scale = nan,
  });
  assert(nearly(point_shadow.constant_bias, 0.0012f));
  assert(nearly(point_shadow.slope_bias_scale, 2.0f));
  assert(nearly(point_shadow.normal_bias_scale, 1.5f));
  assert(nearly(point_shadow.receiver_bias_scale, 0.35f));

  const auto local_lighting = karma::rendering::clampLocalLightingSettings({
      .distance_damping = nan,
      .range_falloff_exponent = nan,
      .ao_affects_local_lights = true,
      .directional_shadow_lift_strength = nan,
  });
  assert(nearly(local_lighting.distance_damping, 0.02f));
  assert(nearly(local_lighting.range_falloff_exponent, 1.1f));
  assert(local_lighting.ao_affects_local_lights);
  assert(nearly(local_lighting.directional_shadow_lift_strength, 0.0f));
  assert(nearly(karma::rendering::clampLightingExposure(nan), 1.0f));
}

void testUIDrawDataValidation() {
  karma::rendering::UIDrawData draw_data{};
  draw_data.vertices = {
      {.x = 0.0f, .y = 0.0f},
      {.x = 1.0f, .y = 0.0f},
      {.x = 0.0f, .y = 1.0f},
  };
  draw_data.indices = {0u, 1u, 2u};
  draw_data.commands.push_back({.index_count = 3u});
  assert(karma::rendering::validateUIDrawData(draw_data));

  draw_data.vertices.front().x = std::numeric_limits<float>::quiet_NaN();
  assert(!karma::rendering::validateUIDrawData(draw_data));
  draw_data.vertices.front().x = 0.0f;
  draw_data.indices.back() = 3u;
  assert(!karma::rendering::validateUIDrawData(draw_data));
  draw_data.indices.back() = 2u;
  draw_data.commands.front().index_offset = 2u;
  assert(!karma::rendering::validateUIDrawData(draw_data));
  draw_data.commands.front().index_offset = 0u;
  draw_data.commands.front().scissor_enabled = true;
  draw_data.commands.front().scissor_w = -1;
  draw_data.commands.front().scissor_h = 10;
  assert(!karma::rendering::validateUIDrawData(draw_data));

  assert(!karma::rendering::validateUIDrawCounts(
      karma::rendering::kMaxUIVertices + 1u, 3u, 1u));
  assert(!karma::rendering::validateUIDrawCounts(
      3u, karma::rendering::kMaxUIIndices + 1u, 1u));
}

void testCameraDataCarriesAntiAliasingSettings() {
  karma::components::CameraComponent camera{};
  karma::components::TransformComponent transform{};
  camera.anti_aliasing = karma::rendering::AntiAliasingSettings::msaa(8u);

  const karma::rendering::CameraData data =
      karma::rendering::render_system::toCameraData(camera, transform, 1.0f);
  assert(data.anti_aliasing.mode == karma::rendering::AntiAliasingMode::MSAA);
  assert(data.anti_aliasing.msaa_samples == 8u);
  assert(nearly(data.anti_aliasing.ssaa_scale, 1.0f));

  const karma::components::CameraComponent default_camera{};
  const karma::rendering::CameraData default_data =
      karma::rendering::render_system::toCameraData(default_camera, transform, 1.0f);
  assert(default_data.anti_aliasing.mode == karma::rendering::AntiAliasingMode::None);
  assert(default_data.anti_aliasing.msaa_samples == 1u);
  assert(nearly(default_data.anti_aliasing.ssaa_scale, 1.0f));
}

void testCameraAndLightExtractionSanitizesRuntimeData() {
  karma::components::CameraComponent camera{};
  camera.fov_y_degrees = std::numeric_limits<float>::quiet_NaN();
  camera.near_clip = -10.0f;
  camera.far_clip = -20.0f;
  camera.ortho_left = 2.0f;
  camera.ortho_right = 2.0f;
  camera.shader_user_params["z_param"] = {
      std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f, 4.0f};
  camera.shader_user_params["a_param"] = {1.0f, 2.0f, 3.0f, 4.0f};

  karma::components::TransformComponent transform{};
  transform.setRotation({0.0f, 0.0f, 0.0f, 2.0f});
  const auto data = karma::rendering::render_system::toCameraData(camera, transform, 1.0f);
  assert(nearly(data.fov_y_degrees, 60.0f));
  assert(data.near_clip >= 0.0001f);
  assert(data.far_clip > data.near_clip);
  assert(!nearly(data.ortho_left, data.ortho_right));
  assert(nearly(glm::dot(data.rotation, data.rotation), 1.0f));
  assert(data.shader_user_param_count == 2u);
  assert(data.shader_user_params[0].key_hash ==
         karma::rendering::cameraShaderParamKeyHash("a_param"));
  assert(data.shader_user_params[1].key_hash ==
         karma::rendering::cameraShaderParamKeyHash("z_param"));
  assert(std::isfinite(data.shader_user_params[1].value.r));

  karma::components::CameraComponent parameter_heavy_camera{};
  const uint32_t authored_parameter_count =
      karma::rendering::kCameraShaderUserParamCapacity + 5u;
  for (uint32_t index = authored_parameter_count; index-- > 0u;) {
    std::string key = "param_00";
    key[6] = static_cast<char>('0' + (index / 10u));
    key[7] = static_cast<char>('0' + (index % 10u));
    parameter_heavy_camera.shader_user_params.emplace(
        std::move(key),
        karma::math::Color{static_cast<float>(index), 0.0f, 0.0f, 1.0f});
  }
  const auto parameter_heavy_data = karma::rendering::render_system::toCameraData(
      parameter_heavy_camera, transform, 1.0f);
  assert(parameter_heavy_data.shader_user_param_count ==
         karma::rendering::kCameraShaderUserParamCapacity);
  assert(parameter_heavy_data.shader_user_params.front().key_hash ==
         karma::rendering::cameraShaderParamKeyHash("param_00"));
  assert(parameter_heavy_data.shader_user_params.back().key_hash ==
         karma::rendering::cameraShaderParamKeyHash("param_31"));

  karma::components::LightComponent light{};
  light.intensity = std::numeric_limits<float>::quiet_NaN();
  light.range = -1.0f;
  light.inner_cone_degrees = std::numeric_limits<float>::infinity();
  const auto light_data =
      karma::rendering::render_system::toLightData(light, transform, 1.0f);
  assert(nearly(light_data.intensity, 0.0f));
  assert(nearly(light_data.range, 0.0f));
  assert(std::isfinite(light_data.inner_cone_cos));
  assert(std::isfinite(light_data.outer_cone_cos));
}

void testFrameGraphCopyAndSceneMaskContractsForAaCameras() {
  karma::components::CameraComponent camera{};
  karma::components::TransformComponent transform{};
  camera.frame_graph_key = "aa_graph";
  camera.anti_aliasing = karma::rendering::AntiAliasingSettings::ssaa(2.0f);

  const karma::rendering::CameraData data =
      karma::rendering::render_system::toCameraData(camera, transform, 1.0f);
  assert(data.anti_aliasing.mode == karma::rendering::AntiAliasingMode::SSAA);

  karma::rendering::FrameGraphDesc graph{};
  graph.frame_graph_key = "aa_graph";
  graph.output_resource = std::string(karma::rendering::kFrameGraphCameraColor);
  graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "half_res",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
      .size_mode = karma::rendering::FrameGraphResourceSizeMode::CameraRelative,
      .width_scale = 0.5f,
      .height_scale = 0.5f,
  });
  graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "mask_color",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
  });
  graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "mask_depth",
      .kind = karma::rendering::FrameGraphResourceKind::DepthTexture,
  });

  karma::rendering::FrameGraphPassDesc copy{};
  copy.name = "copy_to_half_res";
  copy.kind = karma::rendering::FrameGraphPassKind::Copy;
  copy.inputs["source"] = std::string(karma::rendering::kFrameGraphCameraColor);
  copy.outputs["target"] = "half_res";
  graph.passes.push_back(copy);

  karma::rendering::FrameGraphPassDesc mask{};
  mask.name = "aa_scene_mask";
  mask.kind = karma::rendering::FrameGraphPassKind::SceneMask;
  mask.render_tags = {"outline"};
  mask.outputs["target"] = "mask_color";
  mask.outputs["depth"] = "mask_depth";
  mask.clear_depth = true;
  graph.passes.push_back(mask);

  const karma::rendering::FrameGraphValidationResult result =
      karma::rendering::validateFrameGraphDesc(graph);
  assert(result.valid());
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

template <typename T>
void appendPod(std::vector<uint8_t>& bytes, const T& value) {
  const auto* raw = reinterpret_cast<const uint8_t*>(&value);
  bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

void alignBytes4(std::vector<uint8_t>& bytes) {
  while ((bytes.size() % 4u) != 0u) {
    bytes.push_back(0u);
  }
}

void appendFloatVector(std::vector<uint8_t>& bytes,
                       std::initializer_list<float> values) {
  for (float value : values) {
    appendPod(bytes, value);
  }
}

void appendU16Vector(std::vector<uint8_t>& bytes,
                     std::initializer_list<uint16_t> values) {
  for (uint16_t value : values) {
    appendPod(bytes, value);
  }
}

void writeBinary(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void writeAlphaBlendGltf(const std::filesystem::path& dir) {
  std::vector<uint8_t> buffer;
  const std::size_t position_offset = buffer.size();
  appendFloatVector(buffer,
                    {0.0f, 0.0f, 0.0f,
                     1.0f, 0.0f, 0.0f,
                     0.0f, 1.0f, 0.0f});
  alignBytes4(buffer);
  const std::size_t normal_offset = buffer.size();
  appendFloatVector(buffer,
                    {0.0f, 0.0f, 1.0f,
                     0.0f, 0.0f, 1.0f,
                     0.0f, 0.0f, 1.0f});
  alignBytes4(buffer);
  const std::size_t uv_offset = buffer.size();
  appendFloatVector(buffer,
                    {0.0f, 0.0f,
                     1.0f, 0.0f,
                     0.0f, 1.0f});
  alignBytes4(buffer);
  const std::size_t index_offset = buffer.size();
  appendU16Vector(buffer, {0u, 1u, 2u});
  alignBytes4(buffer);
  writeBinary(dir / "alpha.bin", buffer);

  const std::string alpha_png =
      "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAGUlEQVR4nAXBAQ0AAAjAINwMbvMLgrSjCw8zjQWBzP8qVQAAAABJRU5ErkJggg==";
  writeText(dir / "alpha.gltf",
            std::string(R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "name": "AlphaTriangle", "mesh": 0 }],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "indices": 3,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "alpha-leaf",
    "alphaMode": "BLEND",
    "doubleSided": true,
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
    }
  }],
  "textures": [{ "source": 0 }],
  "images": [{ "uri": "data:image/png;base64,)") + alpha_png + R"(" }],
  "buffers": [{ "uri": "alpha.bin", "byteLength": )" +
                std::to_string(buffer.size()) + R"( }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": )" + std::to_string(position_offset) + R"(, "byteLength": 36, "target": 34962 },
    { "buffer": 0, "byteOffset": )" + std::to_string(normal_offset) + R"(, "byteLength": 36, "target": 34962 },
    { "buffer": 0, "byteOffset": )" + std::to_string(uv_offset) + R"(, "byteLength": 24, "target": 34962 },
    { "buffer": 0, "byteOffset": )" + std::to_string(index_offset) + R"(, "byteLength": 6, "target": 34963 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR", "min": [0], "max": [2] }
  ]
})");
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

bool rgbaDominant(const std::vector<uint8_t>& bytes,
                  int width,
                  int x,
                  int y,
                  uint8_t channel) {
  const std::size_t index =
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
       static_cast<std::size_t>(x)) * 4u;
  if (index + 3u >= bytes.size() || channel >= 3u) {
    return false;
  }
  for (uint8_t i = 0u; i < 3u; ++i) {
    if (i != channel && bytes[index + channel] <= bytes[index + i] + 64u) {
      return false;
    }
  }
  return bytes[index + channel] >= 128u && bytes[index + 3u] >= 220u;
}

bool rgbaDominantSubresource(const std::vector<uint8_t>& bytes,
                             const karma::rendering::TextureUploadSubresource& subresource,
                             int x,
                             int y,
                             uint8_t channel) {
  const std::size_t index = subresource.offset +
                            static_cast<std::size_t>(y) * subresource.row_stride +
                            static_cast<std::size_t>(x) * 4u;
  if (index + 3u >= bytes.size() || channel >= 3u) {
    return false;
  }
  for (uint8_t i = 0u; i < 3u; ++i) {
    if (i != channel && bytes[index + channel] <= bytes[index + i] + 64u) {
      return false;
    }
  }
  return bytes[index + channel] >= 128u && bytes[index + 3u] >= 220u;
}

bool subresourceHasAlphaBelow(const std::vector<uint8_t>& bytes,
                              const karma::rendering::TextureUploadSubresource& subresource,
                              uint8_t threshold) {
  for (uint32_t y = 0u; y < subresource.height; ++y) {
    for (uint32_t x = 0u; x < subresource.width; ++x) {
      const std::size_t index =
          subresource.offset + static_cast<std::size_t>(y) * subresource.row_stride +
          static_cast<std::size_t>(x) * 4u + 3u;
      if (index < bytes.size() && bytes[index] < threshold) {
        return true;
      }
    }
  }
  return false;
}

bool subresourceHasAlphaAbove(const std::vector<uint8_t>& bytes,
                              const karma::rendering::TextureUploadSubresource& subresource,
                              uint8_t threshold) {
  for (uint32_t y = 0u; y < subresource.height; ++y) {
    for (uint32_t x = 0u; x < subresource.width; ++x) {
      const std::size_t index =
          subresource.offset + static_cast<std::size_t>(y) * subresource.row_stride +
          static_cast<std::size_t>(x) * 4u + 3u;
      if (index < bytes.size() && bytes[index] > threshold) {
        return true;
      }
    }
  }
  return false;
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

void testHdrSceneColorPipelineContract() {
  const std::filesystem::path root = findRepoRoot();
  assert(!root.empty());
  auto read_source = [&root](const std::filesystem::path& relative) {
    std::ifstream stream(root / relative);
    assert(stream);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  };

  const std::string backend_header = read_source(
      "src/rendering/renderer/backends/diligent/backend.hpp");
  assert(backend_header.find(
             "scene_color_format_ = Diligent::TEX_FORMAT_RGBA16_FLOAT") !=
         std::string::npos);

  const std::string backend_init = read_source(
      "src/rendering/renderer/backends/diligent/backend_init.cpp");
  assert(backend_init.find(
             "GetTextureFormatInfoExt(Diligent::TEX_FORMAT_RGBA16_FLOAT)") !=
         std::string::npos);
  assert(backend_init.find("graphics.RTVFormats[0] = sceneColorFormat();") !=
         std::string::npos);

  const std::vector<std::filesystem::path> scene_pass_sources = {
      "src/rendering/renderer/backends/diligent/passes/camera_override.cpp",
      "src/rendering/renderer/backends/diligent/passes/environment.cpp",
      "src/rendering/renderer/backends/diligent/passes/line.cpp",
      "src/rendering/renderer/backends/diligent/passes/beam.cpp",
      "src/rendering/renderer/backends/diligent/passes/particles.cpp",
      "src/rendering/renderer/backends/diligent/passes/terrain.cpp",
  };
  for (const auto& source_path : scene_pass_sources) {
    assert(read_source(source_path).find("sceneColorFormat()") != std::string::npos);
  }

  const std::string frame_source = read_source(
      "src/rendering/renderer/backends/diligent/passes/frame.cpp");
  const std::string target_source = read_source(
      "src/rendering/renderer/backends/diligent/resources/render_targets.cpp");
  assert(frame_source.find("color_desc.Format = sceneColorFormat();") !=
         std::string::npos);
  assert(target_source.find("color_desc.Format = sceneColorFormat();") !=
         std::string::npos);

  const std::string render_source = read_source(
      "src/rendering/renderer/backends/diligent/backend_render.cpp");
  const size_t native_present_copy = render_source.find("native_copy_supported");
  const size_t converted_present =
      render_source.find("runPresentBlit(present_source_srv");
  assert(native_present_copy != std::string::npos);
  assert(converted_present != std::string::npos);
  assert(native_present_copy < converted_present);

  const std::string graph_source = read_source(
      "src/rendering/renderer/backends/diligent/passes/frame_graph_shader.cpp");
  assert(graph_source.find("pipelineCacheKey(*asset, pass, target_format)") !=
         std::string::npos);
  assert(graph_source.find(
             "ensureFrameGraphShaderPassPipeline(*asset, pass, target_format") !=
         std::string::npos);
  assert(graph_source.find("asset.pipeline.vertex_entry_point") !=
         std::string::npos);
  assert(graph_source.find("asset.pipeline.fragment_entry_point") !=
         std::string::npos);
  assert(graph_source.find("asset.pipeline.defines") != std::string::npos);
  assert(graph_source.find("asset.blend_mode") != std::string::npos);
  assert(graph_source.find(
             "return Diligent::TEX_FORMAT_RGBA16_FLOAT;") !=
         std::string::npos);
  assert(graph_source.find("sortedStringPairs(pass.inputs)") !=
         std::string::npos);
  assert(graph_source.find("sortedStringPairs(asset.textures)") !=
         std::string::npos);

  const std::string temporal_shader = read_source(
      "src/rendering/renderer/backends/diligent/shaders/post_process/temporal_resolve_ps.hlsl");
  const std::string embedded_shaders = read_source(
      "src/rendering/renderer/backends/diligent/passes/post_process/shader_source.cpp");
  assert(temporal_shader.find("return float4(max(resolved, 0.0), source.a);") !=
         std::string::npos);
  assert(embedded_shaders.find("return float4(max(resolved, 0.0), source.a);") !=
         std::string::npos);

  const std::string ui_source = read_source(
      "src/rendering/renderer/backends/diligent/backend_ui.cpp");
  assert(ui_source.find("swap_chain_->GetDesc().ColorBufferFormat") !=
         std::string::npos);
}

void testPerRenderTargetTemporalHistoryContract() {
  const std::filesystem::path root = findRepoRoot();
  assert(!root.empty());
  auto read_source = [&root](const std::filesystem::path& relative) {
    std::ifstream stream(root / relative);
    assert(stream);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  };

  const std::string backend_header = read_source(
      "src/rendering/renderer/backends/diligent/backend.hpp");
  assert(backend_header.find(
             "unordered_map<rendering::RenderTargetId, PostProcessHistoryResources>") !=
         std::string::npos);
  assert(backend_header.find("post_process_history_valid_") == std::string::npos);
  assert(backend_header.find("post_process_history_index_") == std::string::npos);

  const std::string resource_source = read_source(
      "src/rendering/renderer/backends/diligent/passes/post_process/resources.cpp");
  assert(resource_source.find("kMaxPostProcessHistoryTargets") !=
         std::string::npos);
  assert(resource_source.find("targets_.find(it->first)") !=
         std::string::npos);
  assert(resource_source.find("PostProcessHistoryResources replacement") !=
         std::string::npos);
  assert(resource_source.find(
             "post_process_histories_.emplace(target, std::move(replacement))") !=
         std::string::npos);

  const std::string chain_source = read_source(
      "src/rendering/renderer/backends/diligent/passes/post_process/chain.cpp");
  assert(chain_source.find("post_process_histories_.find(target)") !=
         std::string::npos);
  assert(chain_source.find("ensurePostProcessHistoryResources(target") !=
         std::string::npos);
  assert(chain_source.find("temporalCameraChanged(history->camera, camera_)") !=
         std::string::npos);
  assert(chain_source.find("taa_requested && history != nullptr") !=
         std::string::npos);
  assert(chain_source.find("context_->CopyTexture") == std::string::npos);

  const std::string render_source = read_source(
      "src/rendering/renderer/backends/diligent/backend_render.cpp");
  const size_t apply_call = render_source.find("applyPostProcessChain(");
  assert(apply_call != std::string::npos);
  assert(render_source.find("target);", apply_call) != std::string::npos);

  const std::string state_source = read_source(
      "src/rendering/renderer/backends/diligent/passes/render_state.cpp");
  assert(state_source.find("post_process_history_valid_") == std::string::npos);
}

void testNormalMapMinificationFilteringContract() {
  const std::filesystem::path root = findRepoRoot();
  assert(!root.empty());
  std::ifstream stream(
      root / "src/rendering/renderer/backends/diligent/backend_init.cpp");
  assert(stream);
  const std::string source{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};

  const size_t helper = source.find("float4 SampleAntialiasedNormal(");
  const size_t helper_end = source.find("float Bayer4x4(", helper);
  assert(helper != std::string::npos);
  assert(helper_end != std::string::npos);
  const std::string_view body(source.data() + helper, helper_end - helper);
  assert(body.find("normal_texture.GetDimensions(width, height)") !=
         std::string_view::npos);
  assert(body.find("ddx(uv)") != std::string_view::npos);
  assert(body.find("ddy(uv)") != std::string_view::npos);
  assert(body.find("SampleBias(g_SamplerData, uv, mip_bias)") !=
         std::string_view::npos);
  assert(body.find("normal_confidence") != std::string_view::npos);
  assert(body.find("variance") != std::string_view::npos);

  assert(source.find("SampleAntialiasedNormal(g_NormalTex") !=
         std::string::npos);
  assert(source.find("SampleAntialiasedNormal(g_ClearcoatNormalTex") !=
         std::string::npos);
  assert(source.find("roughness * roughness + normal_sample.w") !=
         std::string::npos);
  assert(source.find("clearcoat_normal_sample.w") != std::string::npos);
}

void testRenderCopyUnbindContract() {
  const std::filesystem::path root = findRepoRoot();
  assert(!root.empty());
  auto read_source = [&root](const std::filesystem::path& relative) {
    std::ifstream stream(root / relative);
    assert(stream);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  };
  auto count_occurrences = [](std::string_view text, std::string_view needle) {
    size_t count = 0u;
    size_t offset = 0u;
    while ((offset = text.find(needle, offset)) != std::string_view::npos) {
      ++count;
      offset += needle.size();
    }
    return count;
  };

  const std::string backend_header = read_source(
      "src/rendering/renderer/backends/diligent/backend.hpp");
  assert(backend_header.find("void copyTextureAfterRender(") !=
         std::string::npos);

  const std::string common_source = read_source(
      "src/rendering/renderer/backends/diligent/backend_common.cpp");
  const size_t helper = common_source.find(
      "void DiligentBackend::copyTextureAfterRender(");
  const size_t unbind = common_source.find("context_->SetRenderTargets(0,", helper);
  const size_t copy = common_source.find("context_->CopyTexture(copy_attribs);", helper);
  assert(helper != std::string::npos);
  assert(unbind != std::string::npos);
  assert(copy != std::string::npos);
  assert(helper < unbind && unbind < copy);
  assert(count_occurrences(common_source, "CopyTexture(") == 1u);

  const std::string render_source = read_source(
      "src/rendering/renderer/backends/diligent/backend_render.cpp");
  assert(count_occurrences(
             render_source,
             "copyTextureAfterRender(particle_scene_texture,") == 2u);

  const std::string graph_source = read_source(
      "src/rendering/renderer/backends/diligent/passes/frame_graph_shader.cpp");
  assert(count_occurrences(graph_source, "copyTextureAfterRender(") == 2u);

  const std::filesystem::path backend_root =
      root / "src/rendering/renderer/backends/diligent";
  for (const auto& entry : std::filesystem::recursive_directory_iterator(backend_root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".cpp" ||
        entry.path().filename() == "backend_common.cpp") {
      continue;
    }
    std::ifstream stream(entry.path());
    assert(stream);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    assert(source.find("CopyTexture(") == std::string::npos);
  }
}

void testParticleShaderFallbackContracts() {
  const std::filesystem::path root = findRepoRoot();
  assert(!root.empty());
  auto read_source = [&root](const std::filesystem::path& relative) {
    std::ifstream stream(root / relative);
    assert(stream);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  };

  const std::string particle_resources = read_source(
      "src/rendering/renderer/backends/diligent/passes/particles.cpp");
  const size_t compiler_probe = particle_resources.find(
      "bool isParticleGlobalShaderCompilerAvailable()");
  const size_t compiler_guard = particle_resources.find(
      "if (isParticleGlobalShaderCompilerAvailable())");
  const size_t global_shader_compile = particle_resources.find(
      "global_vs = device_with_cache_.CreateShader(shader_ci);");
  assert(compiler_probe != std::string::npos);
  assert(particle_resources.find("Diligent::CreateDXCompiler(", compiler_probe) !=
         std::string::npos);
  assert(particle_resources.find("compiler->IsLoaded()", compiler_probe) !=
         std::string::npos);
  assert(compiler_guard != std::string::npos);
  assert(global_shader_compile != std::string::npos);
  assert(compiler_guard < global_shader_compile);

  const size_t clear_vars = particle_resources.find(
      "kParticleGpuClearVars[]");
  const size_t clear_pipeline = particle_resources.find(
      "create_gpu_compute_pipeline(\"Karma Particle GPU Clear CS\"",
      clear_vars);
  const size_t clear_mesh_samples_var = particle_resources.find(
      "\"g_MeshSamples\"", clear_vars);
  assert(clear_vars != std::string::npos);
  assert(clear_pipeline != std::string::npos);
  assert(clear_mesh_samples_var == std::string::npos ||
         clear_mesh_samples_var >= clear_pipeline);

  const size_t simulate_vars = particle_resources.find(
      "kParticleGpuSimulateVars[]");
  const size_t simulate_pipeline = particle_resources.find(
      "create_gpu_compute_pipeline(\"Karma Particle GPU Simulate CS\"",
      simulate_vars);
  const size_t mesh_samples_var = particle_resources.find(
      "\"g_MeshSamples\"", simulate_vars);
  assert(simulate_vars != std::string::npos);
  assert(simulate_pipeline != std::string::npos);
  assert(mesh_samples_var != std::string::npos);
  assert(mesh_samples_var < simulate_pipeline);

  const std::string particle_draw = read_source(
      "src/rendering/renderer/backends/diligent/passes/particle_draw.cpp");
  assert(particle_draw.find(
             "set_var(particle_gpu_simulate_mesh_samples_var_, "
             "particle_gpu_mesh_sample_srv_.RawPtr())") !=
         std::string::npos);
  assert(particle_draw.find("global_pipeline_ready(") != std::string::npos);
}

void testRenderingCacheInvalidationContracts() {
  const std::filesystem::path root = findRepoRoot();
  assert(!root.empty());
  auto read_source = [&root](const std::filesystem::path& relative) {
    std::ifstream stream(root / relative);
    assert(stream);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  };

  const std::string environment = read_source(
      "src/rendering/renderer/backends/diligent/passes/environment.cpp");
  const size_t irradiance_create = environment.find("if (!env_irradiance_tex_)");
  const size_t irradiance_restore =
      environment.find("if (env_irradiance_tex_)", irradiance_create);
  const size_t prefilter_create = environment.find("if (!env_prefilter_tex_)");
  const size_t prefilter_restore =
      environment.find("if (env_prefilter_tex_)", prefilter_create);
  assert(irradiance_create != std::string::npos);
  assert(irradiance_restore != std::string::npos);
  assert(irradiance_create < irradiance_restore);
  assert(prefilter_create != std::string::npos);
  assert(prefilter_restore != std::string::npos);
  assert(prefilter_create < prefilter_restore);

  const std::string render_system =
      read_source("src/rendering/renderer/render_system.cpp");
  assert(render_system.find("lod_binding_changed ||") != std::string::npos);

  const std::string meshes = read_source(
      "src/rendering/renderer/backends/diligent/resources/meshes.cpp");
  const size_t mesh_update = meshes.find("void DiligentBackend::updateMesh");
  assert(mesh_update != std::string::npos);
  assert(meshes.find("directional_shadow_scene_dirty_ = true;", mesh_update) !=
         std::string::npos);
  assert(meshes.find("point_shadow_scene_dirty_ = true;", mesh_update) !=
         std::string::npos);

  const std::string deformations = read_source(
      "src/rendering/renderer/backends/diligent/resources/deformations.cpp");
  assert(deformations.find("affects_shadow_caster") != std::string::npos);
  assert(deformations.find("affected_shadow_caster") != std::string::npos);

  const std::string backend_init = read_source(
      "src/rendering/renderer/backends/diligent/backend_init.cpp");
  assert(backend_init.find("RasterizerDesc.DepthClipEnable = true;") !=
         std::string::npos);
  assert(backend_init.find("RasterizerDesc.DepthClipEnable = false;") ==
         std::string::npos);
  assert(backend_init.find("replacement_dsv_faces") != std::string::npos);
  const size_t initialize_device =
      backend_init.find("void DiligentBackend::initializeDevice()");
  assert(initialize_device != std::string::npos);
  assert(backend_init.find("recreatePointShadowMap();", initialize_device) ==
         std::string::npos);

  const std::string shadows = read_source(
      "src/rendering/renderer/backends/diligent/passes/shadows.cpp");
  assert(shadows.find("isPointShadowAllocationCandidate(source_light)") !=
         std::string::npos);
  assert(shadows.find("out_state.point_shadow_light_count > 0u && "
                      "!point_shadow_map_srv_") != std::string::npos);

  const std::string render_state = read_source(
      "src/rendering/renderer/backends/diligent/passes/render_state.cpp");
  assert(render_state.find("if (point_shadow_map_tex_) {") !=
         std::string::npos);
}

void testPointShadowAllocationPolicy() {
  karma::rendering::LightData light{};
  light.type = karma::rendering::LightType::Point;
  light.casts_shadows = false;
  assert(!karma::rendering::detail::isPointShadowAllocationCandidate(light));

  light.casts_shadows = true;
  assert(karma::rendering::detail::isPointShadowAllocationCandidate(light));
  light.intensity = 0.0f;
  assert(!karma::rendering::detail::isPointShadowAllocationCandidate(light));
  light.intensity = 1.0f;
  light.range = 0.0f;
  assert(!karma::rendering::detail::isPointShadowAllocationCandidate(light));

  light.range = 10.0f;
  light.type = karma::rendering::LightType::Spot;
  assert(!karma::rendering::detail::isPointShadowAllocationCandidate(light));
  light.type = karma::rendering::LightType::Directional;
  assert(!karma::rendering::detail::isPointShadowAllocationCandidate(light));
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
  writeText(dir / "environment.hdr", "placeholder");
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
  assert(texture->fallback_rgba8.empty());
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

  karma::rendering::FrameGraphDesc hdr_graph = mask_graph;
  hdr_graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "hdr_intermediate",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
      .format = karma::rendering::TextureFormat::RGBA16F,
  });
  assert(karma::rendering::validateFrameGraphDesc(hdr_graph).valid());

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

  karma::rendering::FrameGraphDesc non_finite_scale = graph;
  non_finite_scale.resources.front().width_scale =
      std::numeric_limits<float>::quiet_NaN();
  const auto non_finite_scale_result =
      karma::rendering::validateFrameGraphDesc(non_finite_scale);
  assert(!non_finite_scale_result.valid());
  assert(diagnosticsContain(non_finite_scale_result, "must have positive scale"));

  karma::rendering::FrameGraphDesc compressed_target = graph;
  compressed_target.resources.front().format =
      karma::rendering::TextureFormat::BC7_RGBA_UNORM;
  const auto compressed_target_result =
      karma::rendering::validateFrameGraphDesc(compressed_target);
  assert(!compressed_target_result.valid());
  assert(diagnosticsContain(compressed_target_result, "render-target-capable"));

  karma::rendering::FrameGraphDesc history_target = graph;
  history_target.resources.front().history_count = 2u;
  const auto history_target_result =
      karma::rendering::validateFrameGraphDesc(history_target);
  assert(!history_target_result.valid());
  assert(diagnosticsContain(history_target_result, "history resources are not implemented"));

  karma::rendering::FrameGraphDesc explicit_external = graph;
  explicit_external.resources.front().kind =
      karma::rendering::FrameGraphResourceKind::ExternalColor;
  const auto external_result =
      karma::rendering::validateFrameGraphDesc(explicit_external);
  assert(!external_result.valid());
  assert(diagnosticsContain(external_result, "external frame graph resources are implicit"));

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

void testFrameGraphStructuralEquivalence() {
  karma::rendering::FrameGraphDesc graph{};
  graph.frame_graph_key = "graphs/equivalence";
  graph.output_resource = "color";
  graph.resources.push_back(karma::rendering::FrameGraphResourceDesc{
      .name = "color",
      .kind = karma::rendering::FrameGraphResourceKind::ColorTexture,
      .width_scale = 0.5f,
      .height_scale = 0.5f,
  });
  graph.passes.push_back(karma::rendering::FrameGraphPassDesc{
      .name = "composite",
      .kind = karma::rendering::FrameGraphPassKind::Shader,
      .shader_pass_key = "passes/composite",
      .inputs = {{"source", std::string(karma::rendering::kFrameGraphCameraColor)}},
      .outputs = {{"target", "color"}},
      .params = {
          {"enabled", true},
          {"tint", karma::math::Color{0.1f, 0.2f, 0.3f, 0.4f}},
      },
  });
  graph.shader_pass_assets.push_back(karma::rendering::ShaderPassAssetDesc{
      .shader_pass_key = "passes/composite",
      .pipeline = karma::rendering::MaterialPipelineDesc{
          .name = "fullscreen",
          .vertex_shader_path = "shaders/fullscreen.vs",
          .fragment_shader_path = "shaders/composite.ps",
          .defines = {"TEST=1"},
      },
      .params = {{"exposure", 1.0f}},
  });

  karma::rendering::FrameGraphDesc copy = graph;
  assert(karma::rendering::frameGraphsEquivalent(graph, copy));
  copy.passes.front().params.erase("enabled");
  copy.passes.front().params.emplace("enabled", true);
  assert(karma::rendering::frameGraphsEquivalent(graph, copy));

  copy.resources.front().width_scale = 1.0f;
  assert(!karma::rendering::frameGraphsEquivalent(graph, copy));
  copy = graph;
  copy.passes.front().render_tags.push_back("selected");
  assert(!karma::rendering::frameGraphsEquivalent(graph, copy));
  copy = graph;
  copy.shader_pass_assets.front().params["exposure"] = 2.0f;
  assert(!karma::rendering::frameGraphsEquivalent(graph, copy));
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
                { "name": "post_ping", "kind": "color_texture", "scale": [1.0, 1.0], "format": "rgba16f" },
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
  const auto* cold_graph = cold_assets.findFrameGraph("graphs/composite");
  assert(cold_graph != nullptr);
  assert(!cold_graph->resources.empty());
  assert(cold_graph->resources.front().format ==
         karma::rendering::TextureFormat::RGBA16F);
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
  const auto* warm_graph = warm_assets.findFrameGraph("graphs/composite");
  assert(warm_graph != nullptr);
  assert(!warm_graph->resources.empty());
  assert(warm_graph->resources.front().format ==
         karma::rendering::TextureFormat::RGBA16F);
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
  writeText(dir / "environment.hdr", "placeholder");
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
                  "path": "environment.hdr"
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
  const karma::assets::TextureRuntimeCapabilities bc7_caps{
      .bc7_unorm = true,
      .bc7_srgb = true,
  };
  auto default_compressed_upload = karma::assets::prepareTextureUpload(
      *cold_texture,
      bc7_caps);
  assert(default_compressed_upload.has_value());
  assert(default_compressed_upload->desc.format ==
         karma::rendering::TextureFormat::BC7_RGBA_UNORM);
  assert(!default_compressed_upload->upload.subresources.empty());
  assert(default_compressed_upload->upload.subresources.front().row_stride > 0u);

  const std::string prepared_cache_key =
      karma::assets::preparedTextureUploadCacheKey(*cold_texture, bc7_caps);
  assert(!prepared_cache_key.empty());
  karma::assets::TextureAsset prepared_texture{};
  prepared_texture.desc = default_compressed_upload->desc;
  prepared_texture.payload_format =
      karma::assets::TextureAsset::PayloadFormat::PreparedUpload;
  prepared_texture.semantic = cold_texture->semantic;
  prepared_texture.subresources = default_compressed_upload->upload.subresources;
  prepared_texture.bytes = default_compressed_upload->upload.bytes;
  karma::assets::AssetCache prepared_cache(options.cache);
  assert(prepared_cache.writeTexture(prepared_cache_key, prepared_texture));
  auto cached_prepared_texture = prepared_cache.readTexture(prepared_cache_key);
  assert(cached_prepared_texture.has_value());
  auto cached_prepared_upload =
      karma::assets::prepareTextureUpload(*cached_prepared_texture, bc7_caps);
  assert(cached_prepared_upload.has_value());
  assert(cached_prepared_upload->desc.format ==
         karma::rendering::TextureFormat::BC7_RGBA_UNORM);
  assert(cached_prepared_upload->upload.bytes.size() ==
         default_compressed_upload->upload.bytes.size());
  assert(cached_prepared_upload->upload.subresources.size() ==
         default_compressed_upload->upload.subresources.size());

  setEnvVar("KARMA_TEXTURE_BC7", "1");
  auto bc7_upload = karma::assets::prepareTextureUpload(*cold_texture, bc7_caps);
  assert(bc7_upload.has_value());
  assert(bc7_upload->desc.format == karma::rendering::TextureFormat::BC7_RGBA_UNORM);
  assert(!bc7_upload->upload.subresources.empty());
  assert(bc7_upload->upload.subresources.front().row_stride > 0u);
  unsetEnvVar("KARMA_TEXTURE_BC7");

  setEnvVar("KARMA_TEXTURE_BC7", "0");
  auto bc7_disabled_upload = karma::assets::prepareTextureUpload(
      *cold_texture,
      bc7_caps);
  assert(bc7_disabled_upload.has_value());
  assert(bc7_disabled_upload->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(!bc7_disabled_upload->upload.subresources.empty());
  assert(!bc7_disabled_upload->upload.bytes.empty());
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

  const std::filesystem::path baked_dir = dir / "bakes" / "asset_cache" / "small_package";
  karma::assets::AssetPackageBakeOptions bake_options{};
  bake_options.package_id = "small_package";
  bake_options.scene_fingerprint = "test-scene-fingerprint";
  bake_options.import_options = options;
  diagnostic.clear();
  assert(karma::assets::bakeAssetPackage(dir, baked_dir, bake_options, &diagnostic));
  assert(diagnostic.empty());
  assert(std::filesystem::exists(baked_dir / "baked.package.json"));
  assert(!std::filesystem::exists(baked_dir / "index.json"));
  assert(karma::assets::checkBakedAssetPackage(dir, baked_dir, bake_options, &diagnostic));

  std::filesystem::remove(dir / "assets.package.json");
  std::filesystem::remove(dir / "textures/spark_atlas.png");
  std::filesystem::remove(dir / "particles/explosion_flash.kpeffect");

  karma::assets::AssetRegistry baked_assets;
  diagnostic.clear();
  auto baked_package =
      karma::assets::importBakedAssetPackage(baked_assets, baked_dir, &diagnostic);
  assert(baked_package.has_value());
  assert(diagnostic.empty());
  assert(baked_assets.findTextureAsset("cache/package/spark_atlas") != nullptr);
  assert(baked_assets.findParticleEffect("cache/package/flash") != nullptr);
  assert(baked_assets.findEnvironmentMap("cache/package/env") != nullptr);
  assert(karma::assets::unloadAssetPackage(baked_assets, *baked_package));
  assert(baked_assets.findTextureAsset("cache/package/spark_atlas") == nullptr);

  unsetEnvVar("KARMA_ASSET_CACHE_DIR");
  unsetEnvVar("KARMA_ASSET_CACHE");
  unsetEnvVar("KARMA_ASSET_CACHE_FLUSH");
  std::filesystem::remove_all(dir);
}

void testPreparedTextureCachePreservesGeneratedMips() {
  const std::filesystem::path dir =
      makeTempDir("karma_prepared_texture_generated_mips_tests");
  const std::filesystem::path cache_dir = dir / "cache";

  setEnvVar("KARMA_ASSET_CACHE_DIR", cache_dir.string().c_str());
  setEnvVar("KARMA_ASSET_CACHE", "1");
  setEnvVar("KARMA_ASSET_CACHE_FLUSH", "0");
  setEnvVar("KARMA_RENDER_TEXTURE_PREPARED_CACHE", "1");

  karma::assets::TextureAsset source{};
  source.desc.width = 4;
  source.desc.height = 4;
  source.desc.format = karma::rendering::TextureFormat::RGBA8;
  source.desc.srgb = false;
  source.desc.generate_mips = true;
  source.desc.mip_levels = 1u;
  source.payload_format = karma::assets::TextureAsset::PayloadFormat::RGBA8;
  source.semantic = karma::assets::TextureAsset::Semantic::Normal;
  source.content_hash = "prepared-generated-mips-regression";
  source.bytes.reserve(4u * 4u * 4u);
  for (std::size_t index = 0u; index < 16u; ++index) {
    source.bytes.insert(source.bytes.end(), {128u, 128u, 255u, 255u});
  }

  karma::assets::TextureAsset without_generated_mips = source;
  without_generated_mips.desc.generate_mips = false;
  const std::string prepared_cache_key =
      karma::assets::preparedTextureUploadCacheKey(source);
  assert(!prepared_cache_key.empty());
  assert(prepared_cache_key !=
         karma::assets::preparedTextureUploadCacheKey(without_generated_mips));

  karma::assets::AssetRegistry assets;
  assert(assets.registerTextureAsset("tests/textures/generated_mips", std::move(source)));

  {
    DummyWindow window;
    karma::rendering::GraphicsDevice device(window);
    karma::rendering::RenderSystem renderer(device, assets);
    renderer.prewarmAssets({}, {}, {"tests/textures/generated_mips"});
  }

  karma::assets::AssetCache cache({
      .root = cache_dir,
      .enabled = true,
      .flush = false,
  });
  auto cached_texture = cache.readTexture(prepared_cache_key);
  assert(cached_texture.has_value());
  assert(cached_texture->payload_format ==
         karma::assets::TextureAsset::PayloadFormat::PreparedUpload);
  assert(cached_texture->semantic == karma::assets::TextureAsset::Semantic::Normal);
  assert(cached_texture->desc.generate_mips);
  assert(cached_texture->desc.mip_levels == 1u);
  assert(cached_texture->subresources.size() == 1u);

  auto warm_upload = karma::assets::prepareTextureUpload(*cached_texture);
  assert(warm_upload.has_value());
  assert(warm_upload->desc.generate_mips);
  assert(warm_upload->desc.mip_levels == 1u);
  assert(warm_upload->upload.subresources.size() == 1u);

  unsetEnvVar("KARMA_RENDER_TEXTURE_PREPARED_CACHE");
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
	                  "generate_mips": false,
	                  "prefer_compressed": false
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
  assert(!prepared->upload.subresources.empty());
  const auto& base_subresource = prepared->upload.subresources.front();
  assert(base_subresource.mip_level == 0u);
  assert(rgbaDominantSubresource(prepared->upload.bytes, base_subresource, 0, 0, 1u));
  assert(rgbaDominantSubresource(prepared->upload.bytes,
                                 base_subresource,
                                 0,
                                 prepared->desc.height - 1,
                                 0u));

  std::filesystem::remove_all(dir);
}

void testGltfSceneImportsTextureAlphaMode() {
  const std::filesystem::path dir =
      makeTempDir("karma_gltf_alpha_material_tests");
  writeAlphaBlendGltf(dir);
  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                {
                  "type": "gltf_scene",
                  "key": "tests/gltf/alpha",
                  "path": "alpha.gltf",
                  "import_meshes": true,
                  "import_lights": false
                }
              ]
            })");

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(assets, dir, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());
  const karma::assets::GltfSceneAsset* scene =
      assets.findGltfSceneAsset("tests/gltf/alpha");
  assert(scene != nullptr);
  assert(scene->material_keys.size() == 1u);

  const auto resolved = assets.resolveMaterial(scene->material_keys.front());
  assert(resolved.has_value());
  assert(resolved->surface.alpha_mode ==
         karma::rendering::MaterialDesc::AlphaMode::Blend);
  assert(resolved->surface.transparent);
  assert(!resolved->surface.depth_write);
  assert(resolved->surface.double_sided);
  const auto base_texture_it = resolved->textures.find("base_color");
  assert(base_texture_it != resolved->textures.end());

  const karma::assets::TextureAsset* texture =
      assets.findTextureAsset(base_texture_it->second);
  assert(texture != nullptr);
  auto prepared = karma::assets::prepareTextureUpload(*texture, {});
  assert(prepared.has_value());
  assert(prepared->desc.format == karma::rendering::TextureFormat::RGBA8);
  assert(!prepared->upload.subresources.empty());
  const auto& base_subresource = prepared->upload.subresources.front();
  assert(subresourceHasAlphaBelow(prepared->upload.bytes, base_subresource, 64u));
  assert(subresourceHasAlphaAbove(prepared->upload.bytes, base_subresource, 220u));

  writeText(dir / "auto_cutout.package.json",
            R"({
              "version": 1,
              "assets": [
                {
                  "type": "gltf_scene",
                  "key": "tests/gltf/alpha_auto",
                  "path": "alpha.gltf",
                  "import_meshes": true,
                  "import_lights": false,
                  "alpha_mode_policy": "auto_cutout"
                }
              ]
            })");
  karma::assets::AssetRegistry auto_assets;
  diagnostic.clear();
  auto auto_package =
      karma::assets::importAssetPackage(auto_assets, dir / "auto_cutout.package.json", &diagnostic);
  assert(auto_package.has_value());
  assert(diagnostic.empty());
  const karma::assets::GltfSceneAsset* auto_scene =
      auto_assets.findGltfSceneAsset("tests/gltf/alpha_auto");
  assert(auto_scene != nullptr);
  assert(auto_scene->material_keys.size() == 1u);
  const auto auto_resolved = auto_assets.resolveMaterial(auto_scene->material_keys.front());
  assert(auto_resolved.has_value());
  assert(auto_resolved->surface.alpha_mode ==
         karma::rendering::MaterialDesc::AlphaMode::Masked);
  assert(!auto_resolved->surface.transparent);
  assert(auto_resolved->surface.depth_write);

  std::filesystem::remove_all(dir);
}

void testGltfSceneMaterialOverrideCastsShadows() {
  const std::filesystem::path dir =
      makeTempDir("karma_gltf_shadow_override_tests");
  writeAlphaBlendGltf(dir);
  writeText(dir / "assets.package.json",
            R"({
              "version": 1,
              "assets": [
                {
                  "type": "gltf_scene",
                  "key": "tests/gltf/shadow_override",
                  "path": "alpha.gltf",
                  "import_meshes": true,
                  "import_lights": false,
                  "material_overrides": [
                    {
                      "material_name": "alpha-leaf",
                      "casts_shadows": false
                    }
                  ]
                }
              ]
            })");

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(assets, dir, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());
  const karma::assets::GltfSceneAsset* scene_asset =
      assets.findGltfSceneAsset("tests/gltf/shadow_override");
  assert(scene_asset != nullptr);
  assert(scene_asset->valid());
  assert(scene_asset->nodes.size() == 1u);
  assert(scene_asset->nodes.front().primitives.size() == 1u);
  assert(!scene_asset->nodes.front().primitives.front().casts_shadows);

  const std::filesystem::path cache_dir =
      makeTempDir("karma_gltf_shadow_override_cache_tests");
  karma::assets::AssetCacheConfig cache_config{};
  cache_config.root = cache_dir;
  cache_config.enabled = true;
  cache_config.flush = true;
  karma::assets::AssetCache cache(cache_config);
  assert(cache.writeGltfScene("shadow_override_blob", *scene_asset));
  auto restored_scene = cache.readGltfScene("shadow_override_blob");
  assert(restored_scene.has_value());
  assert(restored_scene->valid());
  assert(!restored_scene->nodes.front().primitives.front().casts_shadows);
  std::filesystem::remove_all(cache_dir);

  karma::world::World world;
  karma::world::Scene scene;
  const karma::world::GltfSceneImportResult imported =
      karma::world::instantiateGltfSceneAsset(
          world,
          scene,
          assets,
          *restored_scene,
          karma::world::GltfSceneInstantiateOptions{
              .create_synthetic_root = false,
              .autoplay_animations = false,
          });
  assert(imported.valid());

  bool saw_mesh = false;
  for (const karma::world::Entity entity : imported.entities) {
    if (!world.isAlive(entity) ||
        !world.has<karma::components::MeshComponent>(entity)) {
      continue;
    }
    saw_mesh = true;
    const auto& mesh = world.get<karma::components::MeshComponent>(entity);
    assert(mesh.visible);
    assert(!mesh.shadow_visible);
  }
  assert(saw_mesh);

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
  testRenderFrameOwnershipAndRenderTargetValidation();
  testSkyboxShaderPreservesGeneratedCubemapOrientation();
  testHdrSceneColorPipelineContract();
  testNormalMapMinificationFilteringContract();
  testPerRenderTargetTemporalHistoryContract();
  testRenderCopyUnbindContract();
  testParticleShaderFallbackContracts();
  testRenderingCacheInvalidationContracts();
  testPointShadowAllocationPolicy();
  testKtxCubemapOrientationNormalization();
  testAssimpEmbeddedTextureCanonicalization();
  testTextureUploadValidation();
  testScreenPointToWorldRayValidation();
  testDebugWireScaleAndCapsuleDimensions();
  testAntiAliasingSettingsDefaultsAndClamp();
  testRendererSettingsClampNonFiniteValues();
  testUIDrawDataValidation();
  testCameraDataCarriesAntiAliasingSettings();
  testCameraAndLightExtractionSanitizesRuntimeData();
  testFrameGraphCopyAndSceneMaskContractsForAaCameras();
  testPrimitiveMeshAndDiffuseMaterialHelpers();
  testAssetRegistryMaterialInheritance();
  testMaterialFileLoading();
  testAssetKeyValidationAndPackages();
  testFrameGraphValidationAndRegistryFallback();
  testFrameGraphStructuralEquivalence();
  testFrameGraphAssetPackageLoadCacheAndUnload();
  testAssetCacheV2AndPackageWarmRestore();
  testPreparedTextureCachePreservesGeneratedMips();
  testTexturePreparedUploadPreservesRowOrder();
  testImportedMaterialTextureMatchesRendererOrigin();
  testGltfSceneImportsTextureAlphaMode();
  testGltfSceneMaterialOverrideCastsShadows();
  testAssetPackageAsyncCommitAndStore();
  testGltfSceneInstantiationRegistersLogicalMeshKeys();
  testAssetRegistryRegisterResolveUnregister();

  testTerrainHeadlessNoopApi();
  testDeformationHeadlessNoopApi();

  return 0;
}
