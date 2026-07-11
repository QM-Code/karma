#include "demo_asset_paths.h"
#include "karma/ui_imgui.h"
#include "karma/karma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr const char* kGroundMeshKey = "examples/rendering/grass_field/ground_mesh";
constexpr const char* kGrassMeshKey = "examples/rendering/grass_field/grass_cluster_mesh";
constexpr const char* kGrassBillboardMeshKey = "examples/rendering/grass_field/grass_billboard_mesh";
constexpr const char* kGroundMaterialKey = "examples/rendering/grass_field/ground_material";
constexpr const char* kGrassMaterialKey = "examples/rendering/grass_field/grass_material";
constexpr const char* kGrassBillboardMaterialKey =
    "examples/rendering/grass_field/grass_billboard_material";
constexpr const char* kGrassTextureKey = "examples/rendering/grass_field/grass_texture";
constexpr const char* kGrassFrameGraphKey =
    "examples/rendering/grass_field/post_process";

constexpr float kGroundWidth = 100.0f;
constexpr float kGroundDepth = 80.0f;
constexpr uint32_t kGrassInstanceCount = 6500u;
constexpr int kMinGrassInstances = 0;
constexpr int kMaxGrassInstances = 50000;
constexpr uint32_t kGrassChunkColumns = 1u;
constexpr uint32_t kGrassChunkRows = 1u;
constexpr uint32_t kGrassChunkCount = kGrassChunkColumns * kGrassChunkRows;
constexpr uint32_t kGrassChunkReserve =
    static_cast<uint32_t>(kMaxGrassInstances) / kGrassChunkCount + 128u;
constexpr float kGrassSpawnWidth = kGroundWidth * 0.96f;
constexpr float kGrassSpawnDepth = kGroundDepth * 0.96f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kLookSensitivity = 0.0008f;
constexpr float kCameraMoveSpeed = 10.0f;
constexpr float kCameraBoostMultiplier = 3.0f;
constexpr float kCameraSmoothing = 20.0f;
constexpr float kDefaultGrassLodDistance = 28.0f;
constexpr std::array<uint32_t, 3> kMsaaSamples = {2u, 4u, 8u};

enum class GrassAaMode : int {
  None = 0,
  MSAA = 1,
  SSAA = 2,
  TAA = 3,
};

struct GrassFieldOptions {
  uint32_t instance_count = kGrassInstanceCount;
  GrassAaMode aa_mode = GrassAaMode::TAA;
  uint32_t msaa_samples = 4u;
  float ssaa_scale = 2.0f;
  bool show_help = false;
  bool valid = true;
};

const char* grassAaModeName(GrassAaMode mode) {
  switch (mode) {
    case GrassAaMode::MSAA: return "MSAA";
    case GrassAaMode::SSAA: return "SSAA";
    case GrassAaMode::TAA: return "TAA";
    case GrassAaMode::None:
    default: return "None";
  }
}

const char* rasterAaModeName(rendering::AntiAliasingMode mode) {
  switch (mode) {
    case rendering::AntiAliasingMode::MSAA: return "MSAA";
    case rendering::AntiAliasingMode::SSAA: return "SSAA";
    case rendering::AntiAliasingMode::None:
    default: return "None";
  }
}

bool parseGrassAaMode(std::string_view value, GrassAaMode& mode) {
  if (value == "none") {
    mode = GrassAaMode::None;
    return true;
  }
  if (value == "msaa") {
    mode = GrassAaMode::MSAA;
    return true;
  }
  if (value == "ssaa") {
    mode = GrassAaMode::SSAA;
    return true;
  }
  if (value == "taa") {
    mode = GrassAaMode::TAA;
    return true;
  }
  return false;
}

bool parseInstanceCount(std::string_view value, uint32_t& count) {
  const std::string text(value);
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || end == nullptr || *end != '\0') {
    return false;
  }
  count = static_cast<uint32_t>(
      std::clamp(parsed,
                 static_cast<long>(kMinGrassInstances),
                 static_cast<long>(kMaxGrassInstances)));
  return true;
}

bool parseMsaaSamples(std::string_view value, uint32_t& samples) {
  uint32_t parsed = 0u;
  if (!parseInstanceCount(value, parsed) ||
      std::find(kMsaaSamples.begin(), kMsaaSamples.end(), parsed) == kMsaaSamples.end()) {
    return false;
  }
  samples = parsed;
  return true;
}

bool parseSsaaScale(std::string_view value, float& scale) {
  const std::string text(value);
  char* end = nullptr;
  const float parsed = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || end == nullptr || *end != '\0' ||
      !std::isfinite(parsed) || parsed < 1.0f || parsed > 4.0f) {
    return false;
  }
  scale = parsed;
  return true;
}

int msaaSampleIndex(uint32_t samples) {
  const auto it = std::find(kMsaaSamples.begin(), kMsaaSamples.end(), samples);
  return it == kMsaaSamples.end()
             ? 1
             : static_cast<int>(it - kMsaaSamples.begin());
}

GrassFieldOptions parseGrassFieldOptions(int argc, char** argv) {
  GrassFieldOptions options{};
  bool positional_instance_count_seen = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg =
        argv != nullptr && argv[index] != nullptr ? std::string_view(argv[index])
                                                  : std::string_view{};
    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      continue;
    }
    if ((arg == "--aa" || arg == "--mode") && index + 1 < argc) {
      options.valid = parseGrassAaMode(argv[++index], options.aa_mode) && options.valid;
      continue;
    }
    if (arg == "--msaa-samples" && index + 1 < argc) {
      options.valid = parseMsaaSamples(argv[++index], options.msaa_samples) && options.valid;
      continue;
    }
    if (arg == "--ssaa-scale" && index + 1 < argc) {
      options.valid = parseSsaaScale(argv[++index], options.ssaa_scale) && options.valid;
      continue;
    }
    if (arg == "--instances" && index + 1 < argc) {
      options.valid = parseInstanceCount(argv[++index], options.instance_count) && options.valid;
      positional_instance_count_seen = true;
      continue;
    }
    if (!arg.empty() && arg.front() != '-' && !positional_instance_count_seen) {
      options.valid = parseInstanceCount(arg, options.instance_count) && options.valid;
      positional_instance_count_seen = true;
      continue;
    }
    options.valid = false;
  }
  return options;
}

void printGrassFieldUsage(const char* executable) {
  std::fprintf(
      stderr,
      "Usage: %s [--instances 0..50000] [--aa none|msaa|ssaa|taa] "
      "[--msaa-samples 2|4|8] [--ssaa-scale 1.0..4.0]\n",
      executable != nullptr ? executable : "grass_field");
}

bool demoEnvFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

bool demoEnvFlagDisabled(const char* value) {
  return value != nullptr &&
         value[0] != '\0' &&
         !demoEnvFlagEnabled(value);
}

float demoEnvFloat(const char* name, float fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const float parsed = std::strtof(value, &end);
  return end == value ? fallback : parsed;
}

rendering::PostProcessSettings makeGrassPostProcessSettings(bool taa_enabled) {
  rendering::PostProcessSettings settings{};
  settings.temporal_antialiasing_enabled = taa_enabled;
  settings.taa_feedback = 0.82f;
  settings.taa_sharpening = 0.04f;
  return settings;
}

void appendVertex(world::MeshData& mesh,
                  const glm::vec3& position,
                  const glm::vec3& normal,
                  const glm::vec2& uv,
                  const glm::vec4& tangent) {
  mesh.vertices.push_back(position);
  mesh.normals.push_back(normal);
  mesh.uvs.push_back(uv);
  mesh.tangents.push_back(tangent);
}

world::MeshData makeGroundPlane(float width, float depth, std::string material_key) {
  const float half_width = width * 0.5f;
  const float half_depth = depth * 0.5f;
  world::MeshData mesh{};
  appendVertex(mesh,
               {-half_width, 0.0f, -half_depth},
               {0.0f, 1.0f, 0.0f},
               {0.0f, 0.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {half_width, 0.0f, -half_depth},
               {0.0f, 1.0f, 0.0f},
               {10.0f, 0.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {half_width, 0.0f, half_depth},
               {0.0f, 1.0f, 0.0f},
               {10.0f, 8.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {-half_width, 0.0f, half_depth},
               {0.0f, 1.0f, 0.0f},
               {0.0f, 8.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  mesh.indices = {0u, 2u, 1u, 0u, 3u, 2u};
  mesh.submeshes.push_back(world::MeshSubmesh{
      .index_offset = 0u,
      .index_count = static_cast<uint32_t>(mesh.indices.size()),
      .material_slot = 0u,
  });
  mesh.material_slots.push_back(world::MeshMaterialSlot{
      .name = "Ground",
      .default_material_key = std::move(material_key),
  });
  return mesh;
}

void appendGrassBladeCard(world::MeshData& mesh,
                          float angle,
                          float width,
                          float height,
                          const glm::vec3& normal) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  const float half_width = width * 0.5f;
  const glm::vec3 axis{std::cos(angle), 0.0f, std::sin(angle)};
  const glm::vec3 left = -axis * half_width;
  const glm::vec3 right = axis * half_width;
  const glm::vec4 tangent{axis.x, axis.y, axis.z, 1.0f};

  appendVertex(mesh, left, normal, {0.0f, 1.0f}, tangent);
  appendVertex(mesh, right, normal, {1.0f, 1.0f}, tangent);
  appendVertex(mesh, right + glm::vec3{0.0f, height, 0.0f}, normal, {1.0f, 0.0f}, tangent);
  appendVertex(mesh, left + glm::vec3{0.0f, height, 0.0f}, normal, {0.0f, 0.0f}, tangent);
  mesh.indices.insert(mesh.indices.end(), {base, base + 1u, base + 2u, base, base + 2u, base + 3u});
}

world::MeshData makeGrassClusterMesh(float width, float height, std::string material_key) {
  world::MeshData mesh{};
  appendGrassBladeCard(mesh, 0.0f, width, height, {0.0f, 0.0f, 1.0f});
  appendGrassBladeCard(mesh, kPi * 0.5f, width, height, {1.0f, 0.0f, 0.0f});
  mesh.submeshes.push_back(world::MeshSubmesh{
      .index_offset = 0u,
      .index_count = static_cast<uint32_t>(mesh.indices.size()),
      .material_slot = 0u,
  });
  mesh.material_slots.push_back(world::MeshMaterialSlot{
      .name = "Grass",
      .default_material_key = std::move(material_key),
  });
  return mesh;
}

world::MeshData makeGrassBillboardMesh(float width, float height, std::string material_key) {
  world::MeshData mesh{};
  appendGrassBladeCard(mesh, 0.0f, width, height, {0.0f, 0.0f, 1.0f});
  mesh.submeshes.push_back(world::MeshSubmesh{
      .index_offset = 0u,
      .index_count = static_cast<uint32_t>(mesh.indices.size()),
      .material_slot = 0u,
  });
  mesh.material_slots.push_back(world::MeshMaterialSlot{
      .name = "Grass Billboard",
      .default_material_key = std::move(material_key),
  });
  return mesh;
}

uint32_t grassChunkIndexForPosition(float x, float z) {
  const float normalized_x = (x + kGrassSpawnWidth * 0.5f) / kGrassSpawnWidth;
  const float normalized_z = (z + kGrassSpawnDepth * 0.5f) / kGrassSpawnDepth;
  const uint32_t column = static_cast<uint32_t>(
      std::clamp(std::floor(normalized_x * static_cast<float>(kGrassChunkColumns)),
                 0.0f,
                 static_cast<float>(kGrassChunkColumns - 1u)));
  const uint32_t row = static_cast<uint32_t>(
      std::clamp(std::floor(normalized_z * static_cast<float>(kGrassChunkRows)),
                 0.0f,
                 static_cast<float>(kGrassChunkRows - 1u)));
  return row * kGrassChunkColumns + column;
}

void prepareGrassInstanceChunks(
    uint32_t count,
    std::vector<std::vector<components::PlanarMeshInstance>>& chunks) {
  chunks.resize(kGrassChunkCount);
  const uint32_t reserve_per_chunk =
      count / kGrassChunkCount + (count % kGrassChunkCount == 0u ? 0u : 1u);
  const uint32_t target_reserve = std::max(kGrassChunkReserve, reserve_per_chunk);
  for (auto& chunk : chunks) {
    chunk.clear();
    if (chunk.capacity() < target_reserve) {
      chunk.reserve(target_reserve);
    }
  }
}

void fillGrassInstanceChunks(
    uint32_t count,
    std::vector<std::vector<components::PlanarMeshInstance>>& chunks) {
  std::mt19937 rng(0x5EED1234u);
  std::uniform_real_distribution<float> x_dist(-kGrassSpawnWidth * 0.5f, kGrassSpawnWidth * 0.5f);
  std::uniform_real_distribution<float> z_dist(-kGrassSpawnDepth * 0.5f, kGrassSpawnDepth * 0.5f);
  std::uniform_real_distribution<float> yaw_dist(0.0f, kPi * 2.0f);
  std::uniform_real_distribution<float> scale_dist(0.75f, 1.35f);
  std::uniform_real_distribution<float> height_dist(0.85f, 1.25f);

  prepareGrassInstanceChunks(count, chunks);

  for (uint32_t i = 0; i < count; ++i) {
    const float x = x_dist(rng);
    const float z = z_dist(rng);
    const float yaw = yaw_dist(rng);
    const float scale = scale_dist(rng);
    const float height_scale = height_dist(rng);

    components::PlanarMeshInstance instance{};
    instance.position = {x, 0.0f, z};
    instance.yaw_radians = yaw;
    instance.scale = {scale, height_scale, scale};
    instance.params = {x, z, yaw, scale};
    chunks[grassChunkIndexForPosition(x, z)].push_back(instance);
  }
}

world::Entity spawnMeshEntity(world::World& world,
                            std::string name,
                            std::string mesh_key,
                            const math::Vec3& position,
                            bool shadow_visible) {
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform{};
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, components::MeshComponent{
                        .mesh_asset_key = std::move(mesh_key),
                        .visible = true,
                        .shadow_visible = shadow_visible,
                    });
  return entity;
}

}  // namespace

class GrassFieldExample final : public app::GameInterface {
 public:
  explicit GrassFieldExample(const GrassFieldOptions& options = {})
      : aa_mode_(static_cast<int>(options.aa_mode)),
        msaa_sample_index_(msaaSampleIndex(options.msaa_samples)),
        ssaa_scale_(options.ssaa_scale),
        post_process_settings_(makeGrassPostProcessSettings(options.aa_mode == GrassAaMode::TAA)),
        initial_grass_instance_count_(options.instance_count),
        requested_grass_instances_(static_cast<int>(options.instance_count)),
        grass_lod_enabled_(!demoEnvFlagDisabled(std::getenv("KARMA_GRASS_LOD"))),
        grass_lod_distance_(std::max(1.0f,
                                     demoEnvFloat("KARMA_GRASS_LOD_DISTANCE",
                                                  kDefaultGrassLodDistance))) {}

  void onStart() override {
    bindCameraControls();
    registerAssets();
    spawnScene();
    spawnEnvironment();
    spawnLighting();
    spawnCamera();
    applyAntiAliasingSettings();
    configureAutoFly();

    spdlog::info(
        "Grass field controls: hold RMB to look, WASD to move, Q/E vertical, Left Shift to boost");
  }

  void onUpdate(float dt) override {
    bool aa_changed = false;
    if (input->actionPressed("aa_none")) {
      aa_mode_ = static_cast<int>(GrassAaMode::None);
      aa_changed = true;
    }
    if (input->actionPressed("aa_msaa")) {
      aa_mode_ = static_cast<int>(GrassAaMode::MSAA);
      aa_changed = true;
    }
    if (input->actionPressed("aa_ssaa")) {
      aa_mode_ = static_cast<int>(GrassAaMode::SSAA);
      aa_changed = true;
    }
    if (input->actionPressed("aa_taa")) {
      aa_mode_ = static_cast<int>(GrassAaMode::TAA);
      aa_changed = true;
    }
    if (aa_changed) {
      applyAntiAliasingSettings();
    }
    updateCamera(dt);
    logFrameDiagnostics(dt);
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onShutdown() override {}

  void drawUi(app::UIContext& ctx) {
    (void)ctx;
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Grass Field");
    ImGui::Text("Active instances: %u", grass_instance_count_);
    ImGui::InputInt("Instances", &requested_grass_instances_, 100, 1000);
    requested_grass_instances_ =
        std::clamp(requested_grass_instances_, kMinGrassInstances, kMaxGrassInstances);
    if (ImGui::Button("Submit")) {
      applyGrassInstanceCount(static_cast<uint32_t>(requested_grass_instances_));
    }
    bool lod_changed = ImGui::Checkbox("Billboard LOD", &grass_lod_enabled_);
    lod_changed |= ImGui::SliderFloat("LOD Distance", &grass_lod_distance_, 6.0f, 90.0f, "%.1f");
    if (lod_changed) {
      grass_lod_distance_ = std::max(1.0f, grass_lod_distance_);
      applyGrassLodSettings();
    }
    ImGui::Separator();
    bool aa_changed = false;
    const char* aa_modes[] = {"None", "MSAA", "SSAA", "TAA"};
    aa_changed |= ImGui::Combo("Anti-aliasing", &aa_mode_, aa_modes, IM_ARRAYSIZE(aa_modes));
    const GrassAaMode aa_mode = static_cast<GrassAaMode>(aa_mode_);
    if (aa_mode == GrassAaMode::MSAA) {
      const char* sample_counts[] = {"2x", "4x", "8x"};
      aa_changed |= ImGui::Combo("MSAA Samples",
                                 &msaa_sample_index_,
                                 sample_counts,
                                 IM_ARRAYSIZE(sample_counts));
    } else if (aa_mode == GrassAaMode::SSAA) {
      aa_changed |= ImGui::SliderFloat("SSAA Scale", &ssaa_scale_, 1.0f, 4.0f, "%.2f");
    } else if (aa_mode == GrassAaMode::TAA) {
      aa_changed |= ImGui::SliderFloat(
          "TAA Feedback",
          &post_process_settings_.taa_feedback,
          0.70f,
          0.96f,
          "%.2f");
      aa_changed |= ImGui::SliderFloat(
          "TAA Sharpen",
          &post_process_settings_.taa_sharpening,
          0.0f,
          0.20f,
          "%.2f");
    }
    if (aa_changed) {
      applyAntiAliasingSettings();
    }
    const rendering::RendererFrameTimingStats timing =
        graphics ? graphics->getRendererFrameTimingStats()
                 : rendering::RendererFrameTimingStats{};
    ImGui::Text("Raster AA: %s", rasterAaModeName(timing.anti_aliasing_mode));
    ImGui::Text("Raster: %ux%u",
                static_cast<unsigned int>(timing.raster_width),
                static_cast<unsigned int>(timing.raster_height));
    ImGui::End();
  }

 private:
  void bindCameraControls() {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_fast", platform::Key::LeftShift);
    input->bindKey("aa_none", platform::Key::Num1, app::Trigger::Pressed);
    input->bindKey("aa_msaa", platform::Key::Num2, app::Trigger::Pressed);
    input->bindKey("aa_ssaa", platform::Key::Num3, app::Trigger::Pressed);
    input->bindKey("aa_taa", platform::Key::Num4, app::Trigger::Pressed);
    input->bindMouse("cam_look", platform::MouseButton::Right);
  }

  void registerAssets() {
    environment_map_ = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");

    rendering::MaterialDesc ground_material{};
    ground_material.base_color = {0.30f, 0.41f, 0.28f, 1.0f};
    ground_material.metallic = 0.0f;
    ground_material.roughness = 0.88f;
    assets->registerMaterialAsset(kGroundMaterialKey, ground_material);

    if (assets->findTextureAsset(kGrassTextureKey) == nullptr) {
      spdlog::error("Grass field texture '{}' was not available from the startup asset package",
                    kGrassTextureKey);
    }

    rendering::MaterialAssetDesc grass_material{};
    grass_material.pipeline.name = "foliage";
    grass_material.surface.base_color = {0.78f, 0.90f, 0.56f, 1.0f};
    grass_material.surface.metallic = 0.0f;
    grass_material.surface.roughness = 0.82f;
    grass_material.surface.unlit = false;
    grass_material.surface.alpha_mode = rendering::MaterialDesc::AlphaMode::Masked;
    grass_material.surface.alpha_cutoff = 0.28f;
    grass_material.surface.alpha_softness = 0.16f;
    grass_material.surface.alpha_dither = true;
    grass_material.surface.alpha_to_coverage = true;
    grass_material.surface.transparent = false;
    grass_material.surface.depth_write = true;
    grass_material.surface.double_sided = true;
    grass_material.textures["base_color"] = kGrassTextureKey;
    auto grass_billboard_material = grass_material;
    grass_billboard_material.surface.double_sided = false;
    assets->registerMaterialAsset(kGrassMaterialKey, grass_material);
    assets->registerMaterialAsset(kGrassBillboardMaterialKey, std::move(grass_billboard_material));

    assets->registerMeshAsset(kGroundMeshKey, makeGroundPlane(kGroundWidth, kGroundDepth, kGroundMaterialKey));
    assets->registerMeshAsset(kGrassMeshKey, makeGrassClusterMesh(0.95f, 1.25f, kGrassMaterialKey));
    assets->registerMeshAsset(kGrassBillboardMeshKey,
                              makeGrassBillboardMesh(0.95f, 1.25f, kGrassBillboardMaterialKey));
  }

  void applyAntiAliasingSettings() {
    aa_mode_ = std::clamp(aa_mode_, 0, 3);
    msaa_sample_index_ = std::clamp(msaa_sample_index_, 0, 2);
    ssaa_scale_ = std::clamp(ssaa_scale_, 1.0f, 4.0f);
    const GrassAaMode mode = static_cast<GrassAaMode>(aa_mode_);
    post_process_settings_.temporal_antialiasing_enabled = mode == GrassAaMode::TAA;

    if (assets != nullptr) {
      assets->registerFrameGraph(
          kGrassFrameGraphKey,
          rendering::frameGraphFromPostProcessSettings(post_process_settings_,
                                                       kGrassFrameGraphKey));
    }
    if (!world->isAlive(camera_entity_) ||
        !world->has<components::CameraComponent>(camera_entity_)) {
      return;
    }

    rendering::AntiAliasingSettings raster_aa =
        rendering::AntiAliasingSettings::none();
    if (mode == GrassAaMode::MSAA) {
      raster_aa = rendering::AntiAliasingSettings::msaa(
          kMsaaSamples[static_cast<size_t>(msaa_sample_index_)]);
    } else if (mode == GrassAaMode::SSAA) {
      raster_aa = rendering::AntiAliasingSettings::ssaa(ssaa_scale_);
    }
    auto& camera = world->get<components::CameraComponent>(camera_entity_);
    camera.anti_aliasing = raster_aa;
    camera.frame_graph_key = kGrassFrameGraphKey;
    spdlog::info("Grass field anti-aliasing: mode={} msaa={}x ssaa={:.2f}",
                 grassAaModeName(mode),
                 kMsaaSamples[static_cast<size_t>(msaa_sample_index_)],
                 ssaa_scale_);
  }

  void spawnScene() {
    spawnMeshEntity(*world, "Large Ground Plane", kGroundMeshKey, {0.0f, 0.0f, 0.0f}, true);

    fillGrassInstanceChunks(initial_grass_instance_count_, grass_chunk_scratch_);
    grass_entities_.clear();
    grass_entities_.reserve(kGrassChunkCount);
    components::InstancedMeshComponent instanced_grass{};
    instanced_grass.mesh_asset_key = kGrassMeshKey;
    instanced_grass.gpu_layout = rendering::InstanceGpuLayout::PositionYawScaleParams;
    instanced_grass.instance_revision = 1;
    instanced_grass.dynamic = false;
    instanced_grass.visible = true;
    instanced_grass.shadow_visible = false;
    configureGrassLods(instanced_grass);
    grass_instance_count_ = 0u;
    for (uint32_t index = 0; index < kGrassChunkCount; ++index) {
      const world::Entity grass = world->createEntity();
      world->setName(grass, "Instanced Grass Chunk " + std::to_string(index));
      world->add(grass, components::TransformComponent{});
      auto chunk_component = instanced_grass;
      chunk_component.planar_instances.reserve(kGrassChunkReserve);
      chunk_component.planar_instances.assign(grass_chunk_scratch_[index].begin(),
                                              grass_chunk_scratch_[index].end());
      grass_instance_count_ += static_cast<uint32_t>(chunk_component.planar_instances.size());
      world->add(grass, std::move(chunk_component));
      grass_entities_.push_back(grass);
    }
    requested_grass_instances_ = static_cast<int>(grass_instance_count_);
  }

  void applyGrassInstanceCount(uint32_t count) {
    fillGrassInstanceChunks(count, grass_chunk_scratch_);
    grass_instance_count_ = 0u;
    for (uint32_t index = 0; index < kGrassChunkCount && index < grass_entities_.size(); ++index) {
      const world::Entity grass = grass_entities_[index];
      if (!world->isAlive(grass) || !world->has<components::InstancedMeshComponent>(grass)) {
        continue;
      }
      auto& instanced_grass = world->get<components::InstancedMeshComponent>(grass);
      instanced_grass.gpu_layout = rendering::InstanceGpuLayout::PositionYawScaleParams;
      configureGrassLods(instanced_grass);
      if (instanced_grass.planar_instances.capacity() < kGrassChunkReserve) {
        instanced_grass.planar_instances.reserve(kGrassChunkReserve);
      }
      instanced_grass.planar_instances.assign(grass_chunk_scratch_[index].begin(),
                                              grass_chunk_scratch_[index].end());
      instanced_grass.instances.clear();
      ++instanced_grass.instance_revision;
      grass_instance_count_ += static_cast<uint32_t>(instanced_grass.planar_instances.size());
    }
    requested_grass_instances_ = static_cast<int>(grass_instance_count_);
    spdlog::info("Grass field regenerated with {} instances across {} chunks",
                 grass_instance_count_,
                 grass_entities_.size());
  }

  void configureGrassLods(components::InstancedMeshComponent& instanced_grass) const {
    instanced_grass.lods.clear();
    if (!grass_lod_enabled_) {
      return;
    }
    instanced_grass.lods.push_back(components::InstancedMeshLodLevel{
        .start_distance = grass_lod_distance_,
        .mesh_asset_key = kGrassBillboardMeshKey,
        .materials = {components::MeshMaterialAssignment{
            .slot = 0u,
            .material_key = kGrassBillboardMaterialKey,
        }},
        .render_mode = rendering::InstanceLodRenderMode::UprightBillboard,
        .shadow_visible = false,
    });
  }

  void applyGrassLodSettings() {
    for (const world::Entity grass : grass_entities_) {
      if (!world->isAlive(grass) || !world->has<components::InstancedMeshComponent>(grass)) {
        continue;
      }
      auto& instanced_grass = world->get<components::InstancedMeshComponent>(grass);
      configureGrassLods(instanced_grass);
    }
  }

  void spawnEnvironment() {
    const world::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                               .environment_map_asset_key = environment_map_,
                               .intensity = 0.42f,
                               .draw_skybox = true,
                           });
  }

  void spawnLighting() {
    const world::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 12.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.65f, -0.85f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 0.96f, 0.88f, 1.0f},
        .intensity = 1.2f,
        .casts_shadows = true,
        .shadow_extent = 80.0f,
    });

    const world::Entity fill = world->createEntity();
    world->setName(fill, "Soft Fill");
    components::TransformComponent fill_transform{};
    fill_transform.setPosition({-8.0f, 4.0f, 7.0f});
    world->add(fill, fill_transform);
    world->add(fill, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {0.50f, 0.64f, 1.0f, 1.0f},
        .intensity = 12.0f,
        .range = 20.0f,
    });
  }

  void spawnCamera() {
    camera_yaw_ = 0.0f;
    camera_pitch_ = -0.22f;
    target_camera_yaw_ = camera_yaw_;
    target_camera_pitch_ = camera_pitch_;

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 3.2f, 18.0f});
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, components::CameraComponent{
                                  .fov_y_degrees = 58.0f,
                                  .near_clip = 0.05f,
                                  .far_clip = 180.0f,
                                  .is_primary = true,
                              });
    world->add(camera_entity_, components::AudioListenerComponent{});
  }

  void configureAutoFly() {
    auto_fly_enabled_ = demoEnvFlagEnabled(std::getenv("KARMA_GRASS_AUTO_FLY"));
    auto_fly_speed_ = std::max(0.1f, demoEnvFloat("KARMA_GRASS_AUTO_FLY_SPEED", 10.0f));
    auto_fly_span_ = std::max(1.0f, demoEnvFloat("KARMA_GRASS_AUTO_FLY_SPAN", 80.0f));
    auto_fly_elapsed_ = 0.0f;
    if (auto_fly_enabled_) {
      spdlog::info("KARMA_GRASS_AUTO_FLY enabled; speed={:.2f} span={:.2f}",
                   auto_fly_speed_,
                   auto_fly_span_);
    }
  }

  void updateCamera(float dt) {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    const bool ui_wants_mouse =
        ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    const bool ui_wants_keyboard =
        ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;

    if (!ui_wants_mouse && input->actionDown("cam_look")) {
      target_camera_yaw_ -= input->mouseDeltaX() * kLookSensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * kLookSensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.45f, 1.25f);

    const float alpha = 1.0f - std::exp(-kCameraSmoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat camera_rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 world_up{0.0f, 1.0f, 0.0f};
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    math::Vec3 right = math::normalize(math::cross(forward, world_up));
    if (math::lengthSquared(right) <= 0.0001f) {
      right = {1.0f, 0.0f, 0.0f};
    }

    float forward_input = 0.0f;
    float right_input = 0.0f;
    float vertical_input = 0.0f;
    if (!ui_wants_keyboard) {
      if (input->actionDown("cam_forward")) forward_input += 1.0f;
      if (input->actionDown("cam_backward")) forward_input -= 1.0f;
      if (input->actionDown("cam_right")) right_input += 1.0f;
      if (input->actionDown("cam_left")) right_input -= 1.0f;
      if (input->actionDown("cam_up")) vertical_input += 1.0f;
      if (input->actionDown("cam_down")) vertical_input -= 1.0f;
    }

    math::Vec3 movement_dir = math::add(math::scale(forward, forward_input),
                                        math::scale(right, right_input));
    movement_dir = math::add(movement_dir, math::scale(world_up, vertical_input));
    math::Vec3 movement{0.0f, 0.0f, 0.0f};
    if (math::lengthSquared(movement_dir) > 0.0001f) {
      const float speed = kCameraMoveSpeed *
                          (input->actionDown("cam_fast") ? kCameraBoostMultiplier : 1.0f);
      movement = math::add(movement, math::scale(math::normalize(movement_dir), speed * dt));
    }
    if (auto_fly_enabled_) {
      auto_fly_elapsed_ += std::max(dt, 0.0f);
      const float half_period = std::max(auto_fly_span_ / auto_fly_speed_, 0.1f);
      const int half_cycle = static_cast<int>(std::floor(auto_fly_elapsed_ / half_period));
      const float direction = (half_cycle % 2 == 0) ? -1.0f : 1.0f;
      movement = math::add(movement, math::scale(forward, direction * auto_fly_speed_ * dt));
    }
    if (math::lengthSquared(movement) > 0.0001f) {
      camera_transform.setPosition(math::add(camera_transform.getPosition(), movement));
    }
    camera_transform.setRotation(camera_rotation);
  }

  void logFrameDiagnostics(float dt) {
    if (!grass_frame_diag_initialized_) {
      grass_frame_diag_initialized_ = true;
      grass_frame_diag_enabled_ = demoEnvFlagEnabled(std::getenv("KARMA_GRASS_FRAME_DIAG"));
      grass_frame_diag_threshold_ms_ =
          std::max(0.0f,
                   demoEnvFloat("KARMA_GRASS_FRAME_DIAG_THRESHOLD_MS",
                                grass_frame_diag_threshold_ms_));
      if (grass_frame_diag_enabled_) {
        spdlog::info("KARMA_GRASS_FRAME_DIAG enabled; logging frames >= {:.2f} ms",
                     grass_frame_diag_threshold_ms_);
      }
    }
    if (!grass_frame_diag_enabled_) {
      return;
    }
    const float frame_ms = dt * 1000.0f;
    if (frame_ms < grass_frame_diag_threshold_ms_) {
      return;
    }
    math::Vec3 camera_position{};
    if (world->isAlive(camera_entity_) &&
        world->has<components::TransformComponent>(camera_entity_)) {
      camera_position = world->get<components::TransformComponent>(camera_entity_).getPosition();
    }
    spdlog::info(
        "Grass frame diag: dt={:.3f}ms instances={} chunks={} taa={} camera=({:.2f},{:.2f},{:.2f}) yaw={:.3f} pitch={:.3f}",
        frame_ms,
        grass_instance_count_,
        grass_entities_.size(),
        post_process_settings_.temporal_antialiasing_enabled,
        camera_position.x,
        camera_position.y,
        camera_position.z,
        camera_yaw_,
        camera_pitch_);
  }

  world::Entity camera_entity_{};
  std::vector<world::Entity> grass_entities_;
  std::vector<std::vector<components::PlanarMeshInstance>> grass_chunk_scratch_;
  std::string environment_map_;
  int aa_mode_ = static_cast<int>(GrassAaMode::TAA);
  int msaa_sample_index_ = 1;
  float ssaa_scale_ = 2.0f;
  rendering::PostProcessSettings post_process_settings_{};
  uint32_t initial_grass_instance_count_ = kGrassInstanceCount;
  uint32_t grass_instance_count_ = 0;
  int requested_grass_instances_ = static_cast<int>(kGrassInstanceCount);
  bool grass_frame_diag_initialized_ = false;
  bool grass_frame_diag_enabled_ = false;
  float grass_frame_diag_threshold_ms_ = 25.0f;
  bool grass_lod_enabled_ = true;
  float grass_lod_distance_ = kDefaultGrassLodDistance;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  bool auto_fly_enabled_ = false;
  float auto_fly_elapsed_ = 0.0f;
  float auto_fly_speed_ = 10.0f;
  float auto_fly_span_ = 80.0f;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  const karma::demo::GrassFieldOptions options =
      karma::demo::parseGrassFieldOptions(argc, argv);
  if (options.show_help || !options.valid) {
    karma::demo::printGrassFieldUsage(argc > 0 && argv != nullptr ? argv[0] : nullptr);
    return options.valid ? 0 : 2;
  }

  karma::app::EngineApp engine;
  karma::demo::GrassFieldExample game(options);
  engine.setUi(karma::ui::imgui::createUiLayer(
      [&game](karma::app::UIContext& ctx) { game.drawUi(ctx); }));

  karma::app::EngineConfig config{};
  config.window.title = "Karma Grass Field Example";
  config.window.samples = 1;
  config.present_mode = karma::rendering::PresentMode::Immediate;
  config.skip_present_on_mouse_button = true;
  config.mouse_button_present_skip_frames = 2u;
  config.renderer_warmup_camera_sweep_steps = 8u;
  if (options.instance_count >= 50000u) {
    config.frame_pacing_fps = 30.0f;
  }
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.lighting_exposure = 1.0f;
  config.default_frame_graph = karma::rendering::frameGraphFromPostProcessSettings(
      karma::demo::makeGrassPostProcessSettings(
          options.aa_mode == karma::demo::GrassAaMode::TAA),
      karma::demo::kGrassFrameGraphKey);
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("rendering/grass_field"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
