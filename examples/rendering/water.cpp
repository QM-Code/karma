#include "demo_asset_paths.h"
#include "scene_helpers.h"

#include "karma/karma.h"
#include "karma/ui_imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr const char* kWaterMaterial = "examples/rendering/water/surface";
constexpr const char* kLagoonMesh = "examples/rendering/water/lagoon";
constexpr const char* kWaterMesh = "examples/rendering/water/plane";
constexpr const char* kBoxMesh = "examples/rendering/water/box";
constexpr const char* kRockMesh = "examples/rendering/water/rock_mesh";
constexpr const char* kWaterNormalTexture = "examples/rendering/water/generated_normal";
constexpr const char* kSandMaterial = "examples/rendering/water/sand";
constexpr const char* kWetSandMaterial = "examples/rendering/water/wet_sand";
constexpr const char* kDrySandMaterial = "examples/rendering/water/dry_sand";
constexpr const char* kRockMaterial = "examples/rendering/water/rock";
constexpr const char* kWoodMaterial = "examples/rendering/water/wood";
constexpr const char* kRustStoneMaterial = "examples/rendering/water/rust_stone";
constexpr const char* kSandstoneMaterial = "examples/rendering/water/sandstone";
constexpr const char* kFrameGraph = "examples/rendering/water/frame_graph";
constexpr float kLagoonHalfWidth = 24.0f;
constexpr float kLagoonHalfDepth = 18.0f;
constexpr float kTerrainHalfWidth = 31.0f;
constexpr float kTerrainHalfDepth = 24.0f;
constexpr float kWaterLevel = 0.0f;

float smoothStep(float edge0, float edge1, float value) {
  const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float lagoonHeight(float x, float z) {
  const float normalized_x = std::abs(x) / kLagoonHalfWidth;
  const float normalized_z = std::abs(z) / kLagoonHalfDepth;
  constexpr float kCoastRoundness = 3.2f;
  const float rounded_edge =
      std::pow(std::pow(normalized_x, kCoastRoundness) +
                   std::pow(normalized_z, kCoastRoundness),
               1.0f / kCoastRoundness);
  const float coast_variation =
      std::sin(x * 0.17f + std::sin(z * 0.23f) * 0.8f) * 0.035f +
      std::sin(z * 0.21f - x * 0.08f) * 0.025f;
  const float edge = rounded_edge + coast_variation;
  const float beach = smoothStep(0.50f, 0.98f, edge) * 7.1f;

  const float island_x = (x + 7.5f) / 5.8f;
  const float island_z = (z + 1.5f) / 4.2f;
  const float island = std::exp(-(island_x * island_x + island_z * island_z)) * 6.2f;

  const float sandbar_x = (x - 9.0f) / 8.5f;
  const float sandbar_z = (z - 6.0f) / 2.8f;
  const float sandbar = std::exp(-(sandbar_x * sandbar_x + sandbar_z * sandbar_z)) * 2.0f;

  const float basin_ripple = std::sin(x * 0.21f) * std::cos(z * 0.18f) * 0.22f;
  return -5.25f + beach + island + sandbar + basin_ripple;
}

world::MeshData makeLagoonMesh() {
  constexpr uint32_t kSegmentsX = 256u;
  constexpr uint32_t kSegmentsZ = 192u;
  constexpr uint32_t kRow = kSegmentsX + 1u;
  world::MeshData mesh{};
  mesh.vertices.reserve(static_cast<size_t>(kRow) * (kSegmentsZ + 1u));
  mesh.normals.reserve(mesh.vertices.capacity());
  mesh.uvs.reserve(mesh.vertices.capacity());
  mesh.tangents.reserve(mesh.vertices.capacity());
  std::array<std::vector<uint32_t>, 3> material_indices;
  for (auto& indices : material_indices) {
    indices.reserve(static_cast<size_t>(kSegmentsX) * kSegmentsZ * 2u);
  }

  constexpr float kNormalSample = 0.08f;
  for (uint32_t z_index = 0u; z_index <= kSegmentsZ; ++z_index) {
    const float z_fraction = static_cast<float>(z_index) / static_cast<float>(kSegmentsZ);
    const float z = -kTerrainHalfDepth + z_fraction * kTerrainHalfDepth * 2.0f;
    for (uint32_t x_index = 0u; x_index <= kSegmentsX; ++x_index) {
      const float x_fraction = static_cast<float>(x_index) / static_cast<float>(kSegmentsX);
      const float x = -kTerrainHalfWidth + x_fraction * kTerrainHalfWidth * 2.0f;
      const float height = lagoonHeight(x, z);
      const float dx = (lagoonHeight(x + kNormalSample, z) -
                        lagoonHeight(x - kNormalSample, z)) /
                       (kNormalSample * 2.0f);
      const float dz = (lagoonHeight(x, z + kNormalSample) -
                        lagoonHeight(x, z - kNormalSample)) /
                       (kNormalSample * 2.0f);
      const glm::vec3 normal = glm::normalize(glm::vec3{-dx, 1.0f, -dz});
      mesh.vertices.push_back({x, height, z});
      mesh.normals.push_back(normal);
      mesh.uvs.push_back({x_fraction * 9.0f, z_fraction * 7.0f});
      mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
    }
  }

  for (uint32_t z_index = 0u; z_index < kSegmentsZ; ++z_index) {
    for (uint32_t x_index = 0u; x_index < kSegmentsX; ++x_index) {
      const uint32_t a = z_index * kRow + x_index;
      const uint32_t b = a + 1u;
      const uint32_t d = a + kRow;
      const uint32_t c = d + 1u;
      const float average_height = (mesh.vertices[a].y + mesh.vertices[b].y +
                                    mesh.vertices[c].y + mesh.vertices[d].y) *
                                   0.25f;
      const size_t material_slot = average_height < -0.35f
                                       ? 0u
                                       : (average_height < 0.32f ? 1u : 2u);
      material_indices[material_slot].insert(material_indices[material_slot].end(),
                                             {a, c, b, a, d, c});
    }
  }

  mesh.material_slots = {
      world::MeshMaterialSlot{.name = "submerged sand",
                              .default_material_key = kSandMaterial},
      world::MeshMaterialSlot{.name = "wet shoreline",
                              .default_material_key = kWetSandMaterial},
      world::MeshMaterialSlot{.name = "dry beach",
                              .default_material_key = kDrySandMaterial},
  };
  for (size_t material_slot = 0u; material_slot < material_indices.size(); ++material_slot) {
    const uint32_t index_offset = static_cast<uint32_t>(mesh.indices.size());
    mesh.indices.insert(mesh.indices.end(),
                        material_indices[material_slot].begin(),
                        material_indices[material_slot].end());
    mesh.submeshes.push_back(world::MeshSubmesh{
        .index_offset = index_offset,
        .index_count = static_cast<uint32_t>(material_indices[material_slot].size()),
        .material_slot = static_cast<uint32_t>(material_slot),
    });
  }
  return mesh;
}

world::MeshData makeWaterPlane() {
  world::MeshData mesh{};
  mesh.vertices = {
      {-kLagoonHalfWidth, kWaterLevel, -kLagoonHalfDepth},
      {kLagoonHalfWidth, kWaterLevel, -kLagoonHalfDepth},
      {kLagoonHalfWidth, kWaterLevel, kLagoonHalfDepth},
      {-kLagoonHalfWidth, kWaterLevel, kLagoonHalfDepth},
  };
  mesh.normals.assign(4u, glm::vec3{0.0f, 1.0f, 0.0f});
  mesh.tangents.assign(4u, glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
  mesh.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  mesh.indices = {0u, 2u, 1u, 0u, 3u, 2u};
  return mesh;
}

world::MeshData makeRockMesh() {
  world::MeshData mesh = world::createSphereMesh(world::SphereMeshDesc{
      .radius = 0.5f,
      .segments = 28u,
      .rings = 14u,
  });
  for (size_t index = 0u; index < mesh.vertices.size(); ++index) {
    glm::vec3& vertex = mesh.vertices[index];
    const float variation = 1.0f +
                            std::sin(vertex.x * 17.0f + vertex.y * 9.0f) * 0.085f +
                            std::sin(vertex.z * 23.0f - vertex.y * 13.0f) * 0.055f;
    vertex *= variation;
    if (index < mesh.normals.size()) {
      mesh.normals[index] = glm::normalize(vertex);
    }
  }
  return mesh;
}

assets::TextureAsset makeWaterNormalTexture() {
  constexpr int kSize = 256;
  constexpr float kTwoPi = 6.28318530717958647692f;
  constexpr std::array<glm::ivec2, 18> kFrequencies{{
      {1, 2},   {2, -3},  {3, 1},   {-4, 3},  {5, 2},   {-3, 6},
      {7, -4},  {5, 8},   {-9, 2},  {8, 7},   {-6, 11}, {12, -5},
      {9, 13},  {-14, 7}, {16, 3},  {-11, 17}, {19, -8}, {13, 21},
  }};

  std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4u, 255u);
  for (int y = 0; y < kSize; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(kSize);
    for (int x = 0; x < kSize; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(kSize);
      float height = 0.0f;
      float derivative_u = 0.0f;
      float derivative_v = 0.0f;
      float amplitude_sum = 0.0f;
      for (size_t wave = 0u; wave < kFrequencies.size(); ++wave) {
        const glm::ivec2 frequency = kFrequencies[wave];
        const float frequency_length =
            std::sqrt(static_cast<float>(frequency.x * frequency.x +
                                         frequency.y * frequency.y));
        const float amplitude = 1.0f / std::pow(frequency_length, 1.82f);
        const float angle = kTwoPi *
                                (static_cast<float>(frequency.x) * u +
                                 static_cast<float>(frequency.y) * v) +
                            static_cast<float>(wave) * 2.39996323f;
        const float sine = std::sin(angle);
        const float cosine = std::cos(angle);
        height += sine * amplitude;
        derivative_u += cosine * amplitude * static_cast<float>(frequency.x) * kTwoPi;
        derivative_v += cosine * amplitude * static_cast<float>(frequency.y) * kTwoPi;
        amplitude_sum += amplitude;
      }

      const glm::vec3 normal =
          glm::normalize(glm::vec3{-derivative_u * 0.026f,
                                   -derivative_v * 0.026f,
                                   1.0f});
      const float normalized_height =
          std::clamp(0.5f + height / std::max(amplitude_sum, 0.001f) * 0.46f,
                     0.0f,
                     1.0f);
      const size_t offset = (static_cast<size_t>(y) * kSize + x) * 4u;
      auto encode = [](float value) {
        return static_cast<uint8_t>(
            std::clamp(std::lround(value * 255.0f), 0l, 255l));
      };
      pixels[offset + 0u] = encode(normal.x * 0.5f + 0.5f);
      pixels[offset + 1u] = encode(normal.y * 0.5f + 0.5f);
      pixels[offset + 2u] = encode(normal.z * 0.5f + 0.5f);
      pixels[offset + 3u] = encode(normalized_height);
    }
  }

  assets::TextureAsset texture{};
  texture.desc.width = kSize;
  texture.desc.height = kSize;
  texture.desc.format = rendering::TextureFormat::RGBA8;
  texture.desc.generate_mips = true;
  texture.semantic = assets::TextureAsset::Semantic::Normal;
  texture.bytes = std::move(pixels);
  return texture;
}

rendering::MaterialDesc litMaterial(const math::Color& color,
                                    float roughness,
                                    float metallic = 0.0f) {
  rendering::MaterialDesc material{};
  material.base_color = color;
  material.metallic = metallic;
  material.roughness = roughness;
  return material;
}

struct WaterSettings {
  math::Color shallow_color{0.018f, 0.18f, 0.27f, 1.0f};
  math::Color deep_color{0.002f, 0.018f, 0.060f, 1.0f};
  math::Color foam_color{0.90f, 0.93f, 0.90f, 1.0f};
  float clarity = 0.92f;
  float absorption_density = 0.16f;
  float foam_width = 0.34f;
  float wave_scale = 0.46f;
  float wave_strength = 0.20f;
  float wave_speed = 0.58f;
  float flow_angle = 0.35f;
  float refraction = 0.45f;
  float roughness = 0.075f;
  float fresnel_power = 5.0f;
  float reflection_strength = 1.0f;
  float foam_intensity = 0.48f;
  float caustic_strength = 0.20f;
  float depth_range = 8.0f;
  float detail = 1.65f;
  float flow_speed = 0.16f;
  float specular_strength = 1.35f;
  bool animate = true;
  bool foam_enabled = true;
  int debug_mode = 0;
};

WaterSettings waterPreset(int preset) {
  WaterSettings settings{};
  switch (preset) {
    case 1:  // Clear lake
      settings.shallow_color = {0.035f, 0.20f, 0.20f, 1.0f};
      settings.deep_color = {0.006f, 0.035f, 0.045f, 1.0f};
      settings.foam_color = {0.88f, 0.92f, 0.89f, 1.0f};
      settings.clarity = 0.96f;
      settings.absorption_density = 0.12f;
      settings.foam_width = 0.24f;
      settings.wave_scale = 0.58f;
      settings.wave_strength = 0.065f;
      settings.wave_speed = 0.30f;
      settings.refraction = 0.26f;
      settings.roughness = 0.055f;
      settings.reflection_strength = 1.05f;
      settings.foam_intensity = 0.18f;
      settings.caustic_strength = 0.16f;
      settings.depth_range = 10.0f;
      settings.flow_speed = 0.035f;
      settings.specular_strength = 1.5f;
      break;
    case 2:  // Open ocean
      settings.shallow_color = {0.012f, 0.18f, 0.26f, 1.0f};
      settings.deep_color = {0.002f, 0.018f, 0.055f, 1.0f};
      settings.foam_color = {0.90f, 0.94f, 0.96f, 1.0f};
      settings.clarity = 0.66f;
      settings.absorption_density = 0.34f;
      settings.foam_width = 0.72f;
      settings.wave_scale = 0.24f;
      settings.wave_strength = 0.38f;
      settings.wave_speed = 0.92f;
      settings.flow_angle = 0.75f;
      settings.refraction = 0.70f;
      settings.roughness = 0.14f;
      settings.fresnel_power = 4.2f;
      settings.reflection_strength = 1.15f;
      settings.foam_intensity = 0.92f;
      settings.caustic_strength = 0.04f;
      settings.depth_range = 6.0f;
      settings.detail = 1.35f;
      settings.flow_speed = 0.28f;
      settings.specular_strength = 1.6f;
      break;
    case 3:  // Murky river
      settings.shallow_color = {0.16f, 0.22f, 0.12f, 1.0f};
      settings.deep_color = {0.035f, 0.050f, 0.025f, 1.0f};
      settings.foam_color = {0.68f, 0.67f, 0.52f, 1.0f};
      settings.clarity = 0.16f;
      settings.absorption_density = 0.78f;
      settings.foam_width = 0.32f;
      settings.wave_scale = 0.66f;
      settings.wave_strength = 0.11f;
      settings.wave_speed = 0.68f;
      settings.flow_angle = -0.20f;
      settings.refraction = 0.18f;
      settings.roughness = 0.28f;
      settings.fresnel_power = 5.5f;
      settings.reflection_strength = 0.58f;
      settings.foam_intensity = 0.30f;
      settings.caustic_strength = 0.0f;
      settings.depth_range = 3.4f;
      settings.detail = 2.2f;
      settings.flow_speed = 0.88f;
      settings.specular_strength = 0.55f;
      break;
    default:  // Tropical lagoon
      break;
  }
  return settings;
}

}  // namespace

class WaterExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindMouse("orbit", platform::MouseButton::Right);
    input->bindKey("reset_camera", platform::Key::R, app::Trigger::Pressed);
    input->bindKey("toggle_orbit", platform::Key::Space, app::Trigger::Pressed);

    registerAssets();
    spawnLagoon();
    spawnLighting();
    spawnCamera();
    applyWaterMaterial();
  }

  void onUpdate(float dt) override {
    const float safe_dt = std::max(dt, 0.0f);
    if (input->actionPressed("reset_camera")) {
      camera_yaw_ = 0.22f;
      camera_pitch_ = -0.24f;
      camera_distance_ = 34.0f;
    }
    if (input->actionPressed("toggle_orbit")) {
      auto_orbit_ = !auto_orbit_;
    }
    if (input->actionDown("orbit")) {
      camera_yaw_ -= input->mouseDeltaX() * 0.0012f;
      camera_pitch_ -= input->mouseDeltaY() * 0.0012f;
      camera_pitch_ = std::clamp(camera_pitch_, -1.30f, -0.08f);
    } else if (auto_orbit_) {
      camera_yaw_ += safe_dt * 0.075f;
    }
    updateCamera();

    material_apply_cooldown_ = std::max(0.0f, material_apply_cooldown_ - safe_dt);
    if (material_dirty_ && material_apply_cooldown_ <= 0.0f) {
      applyWaterMaterial();
      material_dirty_ = false;
      material_apply_cooldown_ = 0.045f;
    }
  }

  void onFixedUpdate(float dt) override { (void)dt; }
  void onShutdown() override {}

  void drawUi(app::UIContext& context) {
    (void)context;
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(423.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Procedural Water Lab");

    ImGui::TextUnformatted("Flat plane - physical optics - generated ripples");
    ImGui::TextDisabled("Right drag: orbit | R: reset | Space: auto-orbit");
    ImGui::Separator();

    bool water_changed = false;
    const char* presets[] = {"Tropical lagoon", "Clear lake", "Open ocean", "Murky river"};
    if (ImGui::Combo("Water type", &preset_, presets, IM_ARRAYSIZE(presets))) {
      water_settings_ = waterPreset(preset_);
      water_changed = true;
    }

    if (ImGui::CollapsingHeader("Color and depth", ImGuiTreeNodeFlags_DefaultOpen)) {
      water_changed |= ImGui::ColorEdit3("Shallow color", &water_settings_.shallow_color.r);
      water_changed |= ImGui::ColorEdit3("Deep color", &water_settings_.deep_color.r);
      water_changed |= ImGui::SliderFloat("Clarity", &water_settings_.clarity, 0.0f, 1.0f,
                                          "%.2f");
      water_changed |= ImGui::SliderFloat("Absorption density",
                                          &water_settings_.absorption_density,
                                          0.01f, 1.75f, "%.2f");
      water_changed |= ImGui::SliderFloat("Depth color range", &water_settings_.depth_range,
                                          0.5f, 14.0f, "%.1f m");
      water_changed |= ImGui::SliderFloat("Refraction", &water_settings_.refraction,
                                          0.0f, 1.5f, "%.2f");
      water_changed |= ImGui::SliderFloat("Caustics", &water_settings_.caustic_strength,
                                          0.0f, 1.5f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Surface and flow", ImGuiTreeNodeFlags_DefaultOpen)) {
      water_changed |= ImGui::Checkbox("Animate surface", &water_settings_.animate);
      water_changed |= ImGui::SliderFloat("Wave scale", &water_settings_.wave_scale,
                                          0.08f, 1.5f, "%.2f");
      water_changed |= ImGui::SliderFloat("Wave strength", &water_settings_.wave_strength,
                                          0.0f, 0.75f, "%.2f");
      water_changed |= ImGui::SliderFloat("Wave speed", &water_settings_.wave_speed,
                                          0.0f, 2.5f, "%.2f");
      water_changed |= ImGui::SliderFloat("Fine detail", &water_settings_.detail,
                                          0.4f, 4.0f, "%.2f");
      water_changed |= ImGui::SliderAngle("Flow direction", &water_settings_.flow_angle,
                                          -180.0f, 180.0f);
      water_changed |= ImGui::SliderFloat("Flow speed", &water_settings_.flow_speed,
                                          0.0f, 1.5f, "%.2f");
      water_changed |= ImGui::SliderFloat("Roughness", &water_settings_.roughness,
                                          0.02f, 0.8f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Reflection and foam", ImGuiTreeNodeFlags_DefaultOpen)) {
      water_changed |= ImGui::SliderFloat("Reflection", &water_settings_.reflection_strength,
                                          0.0f, 2.0f, "%.2f");
      water_changed |= ImGui::SliderFloat("Fresnel power", &water_settings_.fresnel_power,
                                          1.0f, 10.0f, "%.1f");
      water_changed |= ImGui::SliderFloat("Sun glint", &water_settings_.specular_strength,
                                          0.0f, 3.0f, "%.2f");
      water_changed |= ImGui::Checkbox("Shore foam", &water_settings_.foam_enabled);
      water_changed |= ImGui::ColorEdit3("Foam color", &water_settings_.foam_color.r);
      water_changed |= ImGui::SliderFloat("Foam width", &water_settings_.foam_width,
                                          0.05f, 2.5f, "%.2f m");
      water_changed |= ImGui::SliderFloat("Foam amount", &water_settings_.foam_intensity,
                                          0.0f, 2.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Lighting and view", ImGuiTreeNodeFlags_DefaultOpen)) {
      bool lighting_changed = false;
      lighting_changed |= ImGui::SliderAngle("Sun azimuth", &sun_azimuth_, -180.0f, 180.0f);
      lighting_changed |= ImGui::SliderAngle("Sun elevation", &sun_elevation_, 6.0f, 82.0f);
      lighting_changed |= ImGui::SliderFloat("Sun intensity", &sun_intensity_, 0.0f, 4.0f,
                                             "%.2f");
      lighting_changed |= ImGui::SliderFloat("Sky reflection", &environment_intensity_,
                                             0.0f, 2.0f, "%.2f");
      if (lighting_changed) {
        updateLighting();
      }
      ImGui::Checkbox("Auto-orbit camera", &auto_orbit_);
      ImGui::SliderFloat("Camera distance", &camera_distance_, 18.0f, 58.0f, "%.1f m");
    }

    const char* debug_views[] = {"Final water", "Water depth", "Surface normals", "Shore mask"};
    water_changed |= ImGui::Combo("Shader view", &water_settings_.debug_mode,
                                  debug_views, IM_ARRAYSIZE(debug_views));

    if (water_changed) {
      material_dirty_ = true;
    }
    if (!water_material_loaded_) {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                         "Water material failed to load; see the log.");
    }
    ImGui::End();
  }

 private:
  void registerAssets() {
    assets->registerMeshAsset(kLagoonMesh, makeLagoonMesh());
    assets->registerMeshAsset(kWaterMesh, makeWaterPlane());
    assets->registerMeshAsset(kBoxMesh, helpers::makeBoxMesh({0.5f, 0.5f, 0.5f}));
    assets->registerMeshAsset(kRockMesh, makeRockMesh());
    assets->registerTextureAsset(kWaterNormalTexture, makeWaterNormalTexture());

    assets->registerMaterialAsset(kSandMaterial,
                                  litMaterial({0.46f, 0.39f, 0.27f, 1.0f}, 0.94f));
    assets->registerMaterialAsset(kWetSandMaterial,
                                  litMaterial({0.43f, 0.36f, 0.24f, 1.0f}, 0.88f));
    assets->registerMaterialAsset(kDrySandMaterial,
                                  litMaterial({0.50f, 0.43f, 0.29f, 1.0f}, 0.96f));
    assets->registerMaterialAsset(kRockMaterial,
                                  litMaterial({0.13f, 0.15f, 0.14f, 1.0f}, 0.82f));
    assets->registerMaterialAsset(kWoodMaterial,
                                  litMaterial({0.18f, 0.075f, 0.026f, 1.0f}, 0.72f));
    assets->registerMaterialAsset(kRustStoneMaterial,
                                  litMaterial({0.23f, 0.11f, 0.075f, 1.0f}, 0.90f));
    assets->registerMaterialAsset(kSandstoneMaterial,
                                  litMaterial({0.25f, 0.20f, 0.12f, 1.0f}, 0.88f));

    const std::filesystem::path material_path =
        resolveExampleAssetPath("rendering/water/materials/water.mat");
    const assets::MaterialLoadResult result =
        assets::loadMaterialFile(*assets, kWaterMaterial, material_path);
    if (!result.success) {
      spdlog::error("Water example material failed to load from '{}': {}",
                    material_path.string(), result.diagnostic);
      rendering::MaterialDesc fallback{};
      fallback.base_color = {0.03f, 0.45f, 0.55f, 0.58f};
      fallback.metallic = 0.0f;
      fallback.roughness = 0.15f;
      fallback.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
      fallback.transparent = true;
      fallback.depth_write = false;
      assets->registerMaterialAsset(kWaterMaterial, fallback);
      return;
    }
    if (const rendering::MaterialAssetDesc* material =
            assets->findMaterialAsset(kWaterMaterial)) {
      water_material_template_ = *material;
      water_material_loaded_ = true;
    }

    rendering::PostProcessSettings post_process{};
    post_process.tone_mapping_enabled = true;
    post_process.tone_exposure = 1.02f;
    post_process.tone_contrast = 1.05f;
    post_process.tone_saturation = 1.06f;
    post_process.bloom_enabled = true;
    post_process.bloom_threshold = 1.3f;
    post_process.bloom_intensity = 0.08f;
    post_process.bloom_radius = 1.4f;
    post_process.temporal_antialiasing_enabled = true;
    post_process.taa_feedback = 0.87f;
    post_process.taa_sharpening = 0.05f;
    assets->registerFrameGraph(
        kFrameGraph, rendering::frameGraphFromPostProcessSettings(post_process, kFrameGraph));
  }

  world::Entity spawnMesh(std::string name,
                          const char* mesh_key,
                          const char* material_key,
                          const math::Vec3& position,
                          const math::Vec3& scale = {1.0f, 1.0f, 1.0f},
                          const math::Quat& rotation = {},
                          bool casts_shadow = true) {
    const world::Entity entity = world->createEntity();
    world->setName(entity, std::move(name));
    components::TransformComponent transform{};
    transform.setPosition(position);
    transform.setScale(scale);
    transform.setRotation(rotation);
    world->add(entity, transform);
    world->add(entity, components::MeshComponent{
                           .mesh_asset_key = mesh_key,
                           .materials = {components::MeshMaterialAssignment{
                               .slot = 0u,
                               .material_key = material_key,
                           }},
                           .visible = true,
                           .shadow_visible = casts_shadow,
                       });
    return entity;
  }

  void spawnLagoon() {
    spawnMesh("Sculpted lagoon floor", kLagoonMesh, kSandMaterial, {});

    // Above- and below-water reference objects make refraction, absorption and
    // intersection behavior easy to evaluate from a single camera angle.
    spawnMesh("Deep sandstone", kRockMesh, kSandstoneMaterial,
              {7.2f, -3.55f, -4.5f}, {2.4f, 0.75f, 1.7f},
              math::fromYawPitch(0.42f, 0.0f));
    spawnMesh("Mid rust stone", kRockMesh, kRustStoneMaterial,
              {12.0f, -1.75f, 1.2f}, {1.8f, 1.0f, 1.45f},
              math::fromYawPitch(-0.28f, 0.0f));
    spawnMesh("Shallow sandstone", kRockMesh, kSandstoneMaterial,
              {14.8f, -0.42f, 7.4f}, {1.55f, 0.55f, 1.25f},
              math::fromYawPitch(0.77f, 0.0f));

    for (int index = 0; index < 5; ++index) {
      const float z = -8.0f + static_cast<float>(index) * 3.0f;
      spawnMesh("Pier post " + std::to_string(index + 1), kBoxMesh, kWoodMaterial,
                {-1.0f, -0.55f, z}, {0.38f, 3.4f, 0.38f});
    }
    spawnMesh("Pier deck", kBoxMesh, kWoodMaterial,
              {-1.0f, 1.25f, -2.0f}, {2.6f, 0.28f, 14.2f});

    const std::array<math::Vec3, 7> rocks{{
        {-8.8f, 1.25f, -1.5f}, {-6.2f, 0.72f, -3.2f}, {-5.5f, 0.42f, 1.3f},
        {19.8f, 0.95f, -12.8f}, {-20.2f, 0.62f, 12.0f}, {3.0f, -1.1f, 8.4f},
        {5.2f, -2.2f, 5.1f},
    }};
    for (size_t index = 0; index < rocks.size(); ++index) {
      const float size = 0.75f + static_cast<float>(index % 3u) * 0.34f;
      spawnMesh("Rock " + std::to_string(index + 1), kRockMesh, kRockMaterial,
                rocks[index], {size * 1.5f, size, size * 1.1f},
                math::fromYawPitch(0.37f * static_cast<float>(index), 0.0f));
    }

    // Draw water last through the transparent scene-sampling path. Geometry is
    // still just four vertices and never deforms.
    water_entity_ = spawnMesh("Flat procedural water", kWaterMesh, kWaterMaterial,
                              {0.0f, 0.035f, 0.0f}, {1.0f, 1.0f, 1.0f}, {}, false);
  }

  void spawnLighting() {
    const std::string environment_map =
        registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");
    environment_entity_ = helpers::spawnEnvironment(
        *world, assets, "Lagoon sky", environment_map, environment_intensity_, true);

    sun_entity_ = helpers::spawnDirectionalLight(
        *world, "Sun", {-18.0f, 28.0f, 15.0f},
        math::fromYawPitch(sun_azimuth_, -sun_elevation_),
        components::LightComponent{
            .type = components::LightComponent::Type::Directional,
            .color = {1.0f, 0.91f, 0.76f, 1.0f},
            .intensity = sun_intensity_,
            .casts_shadows = true,
            .shadow_extent = 70.0f,
        });

    // These fills illuminate the beach, posts and submerged reference pieces;
    // the water itself responds to the sun and HDR environment in its shader.
    helpers::spawnPointLight(
        *world, "Warm beach fill", {-15.0f, 7.0f, 8.0f},
        components::LightComponent{
            .type = components::LightComponent::Type::Point,
            .color = {1.0f, 0.60f, 0.30f, 1.0f},
            .intensity = 32.0f,
            .range = 22.0f,
        });
    helpers::spawnPointLight(
        *world, "Cool water fill", {11.0f, 4.5f, -7.0f},
        components::LightComponent{
            .type = components::LightComponent::Type::Point,
            .color = {0.24f, 0.58f, 1.0f, 1.0f},
            .intensity = 22.0f,
            .range = 18.0f,
        });
  }

  void spawnCamera() {
    camera_entity_ = helpers::spawnCamera(
        *world, "Water camera", {0.0f, 12.0f, 35.0f},
        math::fromYawPitch(camera_yaw_, camera_pitch_),
        components::CameraComponent{
            .render_shadows = true,
            .fov_y_degrees = 51.0f,
            .near_clip = 0.08f,
            .far_clip = 180.0f,
            .is_primary = true,
            .frame_graph_key = kFrameGraph,
        });
    updateCamera();
  }

  void updateCamera() {
    if (!world->isAlive(camera_entity_)) {
      return;
    }
    const math::Quat rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 target{0.0f, -0.25f, 0.0f};
    const math::Vec3 position =
        math::subtract(target, math::scale(forward, camera_distance_));
    auto& transform = world->get<components::TransformComponent>(camera_entity_);
    transform.setPosition(position);
    transform.setRotation(rotation);
  }

  void updateLighting() {
    if (world->isAlive(sun_entity_)) {
      auto& transform = world->get<components::TransformComponent>(sun_entity_);
      transform.setRotation(math::fromYawPitch(sun_azimuth_, -sun_elevation_));
      world->get<components::LightComponent>(sun_entity_).intensity = sun_intensity_;
    }
    if (world->isAlive(environment_entity_)) {
      world->get<components::EnvironmentComponent>(environment_entity_).intensity =
          environment_intensity_;
    }
  }

  void applyWaterMaterial() {
    if (!water_material_loaded_) {
      return;
    }
    rendering::MaterialAssetDesc material = water_material_template_;
    material.surface.roughness = water_settings_.roughness;
    material.params["material_params0"] = glm::vec4{
        water_settings_.shallow_color.r,
        water_settings_.shallow_color.g,
        water_settings_.shallow_color.b,
        water_settings_.clarity,
    };
    material.params["material_params1"] = glm::vec4{
        water_settings_.deep_color.r,
        water_settings_.deep_color.g,
        water_settings_.deep_color.b,
        water_settings_.absorption_density,
    };
    material.params["material_params2"] = glm::vec4{
        water_settings_.foam_color.r,
        water_settings_.foam_color.g,
        water_settings_.foam_color.b,
        water_settings_.foam_width,
    };
    material.params["material_params3"] = glm::vec4{
        water_settings_.wave_scale,
        water_settings_.wave_strength,
        water_settings_.animate ? water_settings_.wave_speed : -1.0f,
        water_settings_.flow_angle,
    };
    material.params["material_params4"] = glm::vec4{
        water_settings_.refraction,
        water_settings_.roughness,
        water_settings_.fresnel_power,
        water_settings_.reflection_strength,
    };
    material.params["material_params5"] = glm::vec4{
        water_settings_.foam_intensity,
        water_settings_.caustic_strength,
        water_settings_.depth_range,
        water_settings_.detail,
    };
    material.params["material_params6"] = glm::vec4{
        water_settings_.animate ? water_settings_.flow_speed : 0.0f,
        water_settings_.specular_strength,
        static_cast<float>(water_settings_.debug_mode),
        water_settings_.foam_enabled ? 1.0f : 0.0f,
    };
    assets->registerMaterialAsset(kWaterMaterial, std::move(material));
  }

  rendering::MaterialAssetDesc water_material_template_{};
  WaterSettings water_settings_ = waterPreset(0);
  world::Entity water_entity_{};
  world::Entity camera_entity_{};
  world::Entity sun_entity_{};
  world::Entity environment_entity_{};
  int preset_ = 0;
  float camera_yaw_ = 0.22f;
  float camera_pitch_ = -0.24f;
  float camera_distance_ = 34.0f;
  float sun_azimuth_ = 0.62f;
  float sun_elevation_ = 0.82f;
  float sun_intensity_ = 1.45f;
  float environment_intensity_ = 0.62f;
  float material_apply_cooldown_ = 0.0f;
  bool water_material_loaded_ = false;
  bool material_dirty_ = false;
  bool auto_orbit_ = true;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::WaterExample game;
  engine.setUi(karma::ui::imgui::createUiLayer(
      [&game](karma::app::UIContext& context) { game.drawUi(context); }));

  karma::app::EngineConfig config{};
  config.window.title = "Karma Procedural Water Example";
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 64;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.lighting_exposure = 1.0f;
  config.background_color = {0.16f, 0.31f, 0.43f, 1.0f};

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
