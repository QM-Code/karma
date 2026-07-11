#include "scene_helpers.h"
#include "karma/karma.h"
#include "karma/ui_imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

namespace karma::demo {
namespace {

constexpr const char* kBoxMeshKey = "examples/rendering/antialiasing/box";
constexpr const char* kMatBackdrop = "examples/rendering/antialiasing/backdrop";
constexpr const char* kMatFloorDark = "examples/rendering/antialiasing/floor_dark";
constexpr const char* kMatFloorLight = "examples/rendering/antialiasing/floor_light";
constexpr const char* kMatWhite = "examples/rendering/antialiasing/white";
constexpr const char* kMatCyan = "examples/rendering/antialiasing/cyan";
constexpr const char* kMatAmber = "examples/rendering/antialiasing/amber";
constexpr const char* kMatRed = "examples/rendering/antialiasing/red";
constexpr const char* kTaaFrameGraphKey = "examples/rendering/antialiasing/taa";
constexpr float kPi = 3.14159265358979323846f;

enum class ExampleAaMode : int {
  None = 0,
  MSAA = 1,
  SSAA = 2,
  TAA = 3,
};

struct ExampleOptions {
  ExampleAaMode mode = ExampleAaMode::None;
  bool show_help = false;
  bool valid = true;
};

rendering::MaterialDesc makeUnlitMaterial(const math::Color& color) {
  rendering::MaterialDesc material{};
  material.base_color = color;
  material.emissive_color = color;
  material.emissive_strength = 0.15f;
  material.metallic = 0.0f;
  material.roughness = 0.82f;
  material.unlit = true;
  return material;
}

math::Quat fromAxisAngle(const math::Vec3& axis, float radians) {
  const math::Vec3 n = math::normalize(axis);
  const float half = radians * 0.5f;
  const float s = std::sin(half);
  return {n.x * s, n.y * s, n.z * s, std::cos(half)};
}

const char* rasterAaModeName(rendering::AntiAliasingMode mode) {
  switch (mode) {
    case rendering::AntiAliasingMode::MSAA: return "MSAA";
    case rendering::AntiAliasingMode::SSAA: return "SSAA";
    case rendering::AntiAliasingMode::None:
    default: return "None";
  }
}

const char* aaModeName(ExampleAaMode mode) {
  switch (mode) {
    case ExampleAaMode::MSAA: return "MSAA";
    case ExampleAaMode::SSAA: return "SSAA";
    case ExampleAaMode::TAA: return "TAA";
    case ExampleAaMode::None:
    default: return "None";
  }
}

bool parseAaMode(std::string_view value, ExampleAaMode& mode) {
  if (value == "none") {
    mode = ExampleAaMode::None;
    return true;
  }
  if (value == "msaa") {
    mode = ExampleAaMode::MSAA;
    return true;
  }
  if (value == "ssaa") {
    mode = ExampleAaMode::SSAA;
    return true;
  }
  if (value == "taa") {
    mode = ExampleAaMode::TAA;
    return true;
  }
  return false;
}

ExampleOptions parseOptions(int argc, char** argv) {
  ExampleOptions options{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg =
        argv[i] ? std::string_view(argv[i]) : std::string_view{};
    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      continue;
    }
    if (arg == "--mode" && i + 1 < argc && argv[i + 1]) {
      options.valid = parseAaMode(argv[++i], options.mode) && options.valid;
      continue;
    }
    options.valid = false;
  }
  return options;
}

void printUsage(const char* executable) {
  std::fprintf(stderr,
               "Usage: %s [--mode none|msaa|ssaa|taa]\n",
               executable ? executable : "antialiasing");
}

}  // namespace

class AntiAliasingExample final : public app::GameInterface {
 public:
  explicit AntiAliasingExample(ExampleAaMode initial_mode)
      : aa_mode_(static_cast<int>(initial_mode)) {}

  void onStart() override {
    input->bindKey("aa_none", platform::Key::Num1, app::Trigger::Pressed);
    input->bindKey("aa_msaa", platform::Key::Num2, app::Trigger::Pressed);
    input->bindKey("aa_ssaa", platform::Key::Num3, app::Trigger::Pressed);
    input->bindKey("aa_taa", platform::Key::Num4, app::Trigger::Pressed);
    input->bindKey("animate", platform::Key::Space, app::Trigger::Pressed);

    registerAssets();
    spawnScene();
    spawnCamera();
    applyAntiAliasing();
  }

  void onUpdate(float dt) override {
    if (input->actionPressed("aa_none")) {
      aa_mode_ = static_cast<int>(ExampleAaMode::None);
      applyAntiAliasing();
    }
    if (input->actionPressed("aa_msaa")) {
      aa_mode_ = static_cast<int>(ExampleAaMode::MSAA);
      applyAntiAliasing();
    }
    if (input->actionPressed("aa_ssaa")) {
      aa_mode_ = static_cast<int>(ExampleAaMode::SSAA);
      applyAntiAliasing();
    }
    if (input->actionPressed("aa_taa")) {
      aa_mode_ = static_cast<int>(ExampleAaMode::TAA);
      applyAntiAliasing();
    }
    if (input->actionPressed("animate")) {
      animate_spokes_ = !animate_spokes_;
    }

    if (animate_spokes_) {
      time_ += std::max(dt, 0.0f);
    }
    updateSpokes();
    drawReferenceLines();
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onShutdown() override {}

  void drawUi(app::UIContext& ctx) {
    (void)ctx;
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Anti-Aliasing");

    bool changed = false;
    const char* mode_items[] = {"None", "MSAA", "SSAA", "TAA"};
    changed |= ImGui::Combo("Mode", &aa_mode_, mode_items, IM_ARRAYSIZE(mode_items));

    const char* sample_items[] = {"2x", "4x", "8x"};
    changed |= ImGui::Combo("MSAA Samples", &msaa_sample_index_, sample_items,
                            IM_ARRAYSIZE(sample_items));
    changed |= ImGui::SliderFloat("SSAA Scale", &ssaa_scale_, 1.0f, 4.0f, "%.2f");
    if (changed) {
      applyAntiAliasing();
    }

    ImGui::Separator();
    ImGui::Checkbox("Animate spokes", &animate_spokes_);

    const rendering::RendererFrameTimingStats timing =
        graphics ? graphics->getRendererFrameTimingStats()
                 : rendering::RendererFrameTimingStats{};
    ImGui::Separator();
    ImGui::Text("Requested: %s", aaModeName(static_cast<ExampleAaMode>(aa_mode_)));
    ImGui::Text("Raster AA: %s", rasterAaModeName(timing.anti_aliasing_mode));
    ImGui::Text("MSAA: %ux", static_cast<unsigned int>(timing.anti_aliasing_msaa_samples));
    ImGui::Text("SSAA: %.2f", timing.anti_aliasing_ssaa_scale);
    ImGui::Text("Raster: %ux%u",
                static_cast<unsigned int>(timing.raster_width),
                static_cast<unsigned int>(timing.raster_height));
    ImGui::Text("Output: %ux%u",
                static_cast<unsigned int>(timing.output_width),
                static_cast<unsigned int>(timing.output_height));
    ImGui::End();
  }

 private:
  void registerAssets() {
    assets->registerMeshAsset(kBoxMeshKey, helpers::makeBoxMesh({0.5f, 0.5f, 0.5f}));
    assets->registerMaterialAsset(kMatBackdrop,
                                  makeUnlitMaterial({0.015f, 0.018f, 0.024f, 1.0f}));
    assets->registerMaterialAsset(kMatFloorDark,
                                  makeUnlitMaterial({0.035f, 0.038f, 0.043f, 1.0f}));
    assets->registerMaterialAsset(kMatFloorLight,
                                  makeUnlitMaterial({0.82f, 0.84f, 0.78f, 1.0f}));
    assets->registerMaterialAsset(kMatWhite,
                                  makeUnlitMaterial({0.96f, 0.97f, 0.93f, 1.0f}));
    assets->registerMaterialAsset(kMatCyan,
                                  makeUnlitMaterial({0.22f, 0.92f, 1.0f, 1.0f}));
    assets->registerMaterialAsset(kMatAmber,
                                  makeUnlitMaterial({1.0f, 0.64f, 0.18f, 1.0f}));
    assets->registerMaterialAsset(kMatRed,
                                  makeUnlitMaterial({1.0f, 0.18f, 0.16f, 1.0f}));

    rendering::PostProcessSettings taa{};
    taa.temporal_antialiasing_enabled = true;
    taa.taa_feedback = 0.90f;
    taa.taa_sharpening = 0.04f;
    assets->registerFrameGraph(
        kTaaFrameGraphKey,
        rendering::frameGraphFromPostProcessSettings(taa, kTaaFrameGraphKey));
  }

  world::Entity spawnBox(const std::string& name,
                         const std::string& material_key,
                         const math::Vec3& position,
                         const math::Vec3& scale,
                         const math::Quat& rotation = {},
                         bool shadow_visible = false) {
    const world::Entity entity = world->createEntity();
    world->setName(entity, name);

    components::TransformComponent transform{};
    transform.setPosition(position);
    transform.setScale(scale);
    transform.setRotation(rotation);
    world->add(entity, transform);
    world->add(entity, components::MeshComponent{
                           .mesh_asset_key = kBoxMeshKey,
                           .materials = {components::MeshMaterialAssignment{
                               .slot = 0u,
                               .material_key = material_key,
                           }},
                           .visible = true,
                           .shadow_visible = shadow_visible,
                       });
    return entity;
  }

  void spawnScene() {
    spawnBox("Backdrop", kMatBackdrop, {0.0f, 1.8f, -2.75f}, {8.0f, 4.2f, 0.05f});
    spawnBox("Floor", kMatFloorDark, {0.0f, -0.04f, 0.6f}, {9.0f, 0.04f, 8.0f});

    for (int z = 0; z < 12; ++z) {
      for (int x = 0; x < 12; ++x) {
        const bool light = ((x + z) & 1) == 0;
        const float px = (static_cast<float>(x) - 5.5f) * 0.48f;
        const float pz = (static_cast<float>(z) - 5.5f) * 0.48f + 0.35f;
        spawnBox(light ? "Light tile" : "Dark tile",
                 light ? kMatFloorLight : kMatFloorDark,
                 {px, 0.005f, pz},
                 {0.46f, 0.018f, 0.46f});
      }
    }

    for (int i = -9; i <= 9; ++i) {
      const float x = static_cast<float>(i) * 0.44f;
      const float y = 0.90f + static_cast<float>((i + 9) % 3) * 0.33f;
      const float angle = (i & 1) == 0 ? 0.42f : -0.36f;
      spawnBox("Diagonal slat",
               (i & 1) == 0 ? kMatWhite : kMatCyan,
               {x, y, -2.68f},
               {2.2f, 0.028f, 0.08f},
               fromAxisAngle({0.0f, 0.0f, 1.0f}, angle));
    }

    for (int i = 0; i < 18; ++i) {
      const float angle = (static_cast<float>(i) / 18.0f) * kPi;
      const world::Entity spoke = spawnBox("Rotating spoke",
                                           i % 3 == 0 ? kMatAmber : kMatWhite,
                                           {0.0f, 2.55f, -2.62f},
                                           {3.35f, 0.018f, 0.08f},
                                           fromAxisAngle({0.0f, 0.0f, 1.0f}, angle));
      spokes_.push_back(spoke);
    }

    spawnBox("Needle horizontal", kMatRed, {0.0f, 0.48f, -2.58f}, {7.4f, 0.012f, 0.08f});
    spawnBox("Needle vertical",
             kMatCyan,
             {-3.35f, 2.0f, -2.56f},
             {2.8f, 0.012f, 0.08f},
             fromAxisAngle({0.0f, 0.0f, 1.0f}, kPi * 0.5f));

    const world::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({-3.0f, 5.0f, 3.0f});
    sun_transform.setRotation(math::fromYawPitch(0.56f, -0.92f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 0.96f, 0.88f, 1.0f},
                        .intensity = 0.8f,
                        .casts_shadows = false,
                        .shadow_extent = 12.0f,
                    });
  }

  void spawnCamera() {
    const world::Entity camera = world->createEntity();
    world->setName(camera, "AA Camera");
    components::TransformComponent transform{};
    transform.setPosition({0.0f, 2.35f, 6.4f});
    transform.setRotation(math::fromYawPitch(0.0f, -0.18f));
    world->add(camera, transform);
    world->add(camera, components::CameraComponent{
                           .render_shadows = false,
                           .fov_y_degrees = 45.0f,
                           .near_clip = 0.05f,
                           .far_clip = 80.0f,
                           .is_primary = true,
                       });
    world->add(camera, components::AudioListenerComponent{});
    camera_entity_ = camera;
  }

  void applyAntiAliasing() {
    if (!world->isAlive(camera_entity_) ||
        !world->has<components::CameraComponent>(camera_entity_)) {
      return;
    }

    static constexpr std::array<uint32_t, 3> kSamples = {2u, 4u, 8u};
    aa_mode_ = std::clamp(aa_mode_, 0, 3);
    msaa_sample_index_ = std::clamp(msaa_sample_index_, 0, 2);
    ssaa_scale_ = std::clamp(ssaa_scale_, 1.0f, 4.0f);

    rendering::AntiAliasingSettings settings =
        rendering::AntiAliasingSettings::none();
    const ExampleAaMode mode = static_cast<ExampleAaMode>(aa_mode_);
    if (mode == ExampleAaMode::MSAA) {
      settings = rendering::AntiAliasingSettings::msaa(
          kSamples[static_cast<size_t>(msaa_sample_index_)]);
    } else if (mode == ExampleAaMode::SSAA) {
      settings = rendering::AntiAliasingSettings::ssaa(ssaa_scale_);
    }

    auto& camera = world->get<components::CameraComponent>(camera_entity_);
    camera.anti_aliasing = settings;
    camera.frame_graph_key = mode == ExampleAaMode::TAA ? kTaaFrameGraphKey : "";

    std::fprintf(stdout, "[Karma][AA Example] Requested mode: %s\n", aaModeName(mode));
    std::fflush(stdout);
  }

  void updateSpokes() {
    for (size_t i = 0; i < spokes_.size(); ++i) {
      const world::Entity entity = spokes_[i];
      if (!world->isAlive(entity) || !world->has<components::TransformComponent>(entity)) {
        continue;
      }
      const float base = (static_cast<float>(i) / static_cast<float>(spokes_.size())) * kPi;
      auto& transform = world->get<components::TransformComponent>(entity);
      transform.setRotation(fromAxisAngle({0.0f, 0.0f, 1.0f}, base + time_ * 0.42f));
    }
  }

  void drawReferenceLines() {
    if (!graphics) {
      return;
    }

    const math::Color white{0.95f, 0.96f, 0.92f, 1.0f};
    const math::Color cyan{0.15f, 0.90f, 1.0f, 1.0f};
    const math::Color amber{1.0f, 0.62f, 0.16f, 1.0f};
    for (int i = -20; i <= 20; ++i) {
      const float x = static_cast<float>(i) * 0.18f;
      graphics->drawLine({x, 0.035f, -2.2f},
                         {x + 1.0f, 0.035f, 2.8f},
                         (i & 1) == 0 ? white : cyan);
    }
    for (int i = 0; i < 36; ++i) {
      const float angle = (static_cast<float>(i) / 36.0f) * kPi * 2.0f + time_ * 0.18f;
      const math::Vec3 start{0.0f, 1.0f, -0.95f};
      const math::Vec3 end{std::cos(angle) * 1.75f,
                           1.0f + std::sin(angle) * 1.15f,
                           -0.95f};
      graphics->drawLine(start, end, (i % 3) == 0 ? amber : white);
    }
  }

  world::Entity camera_entity_{};
  std::vector<world::Entity> spokes_;
  int aa_mode_ = static_cast<int>(ExampleAaMode::None);
  int msaa_sample_index_ = 1;
  float ssaa_scale_ = 2.0f;
  float time_ = 0.0f;
  bool animate_spokes_ = true;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  const karma::demo::ExampleOptions options = karma::demo::parseOptions(argc, argv);
  if (options.show_help || !options.valid) {
    karma::demo::printUsage(argc > 0 ? argv[0] : nullptr);
    return options.valid ? 0 : 2;
  }

  karma::app::EngineApp engine;
  karma::demo::AntiAliasingExample game(options.mode);
  engine.setUi(karma::ui::imgui::createUiLayer(
      [&game](karma::app::UIContext& ctx) { game.drawUi(ctx); }));

  karma::app::EngineConfig config{};
  config.window.title = "Karma Anti-Aliasing Example";
  config.window.width = 1280;
  config.window.height = 720;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.lighting_exposure = 1.0f;
  config.default_frame_graph = karma::rendering::defaultFrameGraphDesc();

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
