#include "karma/karma.h"

#if defined(KARMA_PARTICLE_PREVIEW_GENERATION)
#include "particle_effect_tools.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace {

constexpr std::string_view kDefaultSpecPath =
    "examples/particles/specs/fire_ray.kpspec.json";

struct PreviewOptions {
  bool top_view = false;
};

struct PreviewArguments {
  PreviewOptions options;
  std::vector<std::filesystem::path> positional;
};

PreviewArguments parsePreviewArguments(int argc, char** argv) {
  PreviewArguments parsed{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--top") {
      parsed.options.top_view = true;
    } else {
      parsed.positional.emplace_back(argv[i]);
    }
  }
  return parsed;
}

bool isParticleSpecPath(const std::filesystem::path& path) {
  return path.filename().string().ends_with(".kpspec.json");
}

std::string specBaseName(const std::filesystem::path& spec_path) {
  std::string name = spec_path.filename().string();
  constexpr std::string_view suffix = ".kpspec.json";
  if (name.ends_with(suffix)) {
    name.resize(name.size() - suffix.size());
  } else {
    name = spec_path.stem().string();
  }
  return name.empty() ? "particle_effect" : name;
}

std::optional<std::filesystem::path> findRepoRootFrom(std::filesystem::path start) {
  std::error_code ec;
  if (start.empty()) {
    return std::nullopt;
  }
  start = std::filesystem::absolute(start, ec);
  if (ec) {
    return std::nullopt;
  }
  if (std::filesystem::is_regular_file(start, ec)) {
    start = start.parent_path();
  }
  const std::filesystem::path default_spec_path{kDefaultSpecPath};
  for (std::filesystem::path dir = start; !dir.empty(); dir = dir.parent_path()) {
    if (std::filesystem::exists(dir / default_spec_path, ec)) {
      return dir;
    }
    if (dir == dir.root_path()) {
      break;
    }
  }
  return std::nullopt;
}

std::filesystem::path findRepoRoot(const char* executable_path) {
  if (const char* env = std::getenv("KARMA_REPO_ROOT")) {
    if (auto root = findRepoRootFrom(env)) {
      return *root;
    }
  }
  if (auto root = findRepoRootFrom(std::filesystem::current_path())) {
    return *root;
  }
  if (executable_path != nullptr) {
    if (auto root = findRepoRootFrom(executable_path)) {
      return *root;
    }
  }
  return std::filesystem::current_path();
}

std::filesystem::path resolveExistingInputPath(const std::filesystem::path& input,
                                               const std::filesystem::path& repo_root) {
  if (input.is_absolute()) {
    return input;
  }

  std::error_code ec;
  if (std::filesystem::exists(input, ec)) {
    return input;
  }

  const std::filesystem::path repo_relative = repo_root / input;
  if (std::filesystem::exists(repo_relative, ec)) {
    return repo_relative;
  }

  return input;
}

std::filesystem::path resolvePreviewPackage(const std::vector<std::filesystem::path>& positional,
                                            const char* executable_path) {
  const std::filesystem::path repo_root = findRepoRoot(executable_path);

  std::filesystem::path input;
  bool using_default_spec = false;
  if (!positional.empty()) {
    input = positional[0];
  } else if (const char* env = std::getenv("KARMA_GENERATED_PARTICLE_PACKAGE")) {
    input = env;
  } else {
    input = repo_root / std::filesystem::path{kDefaultSpecPath};
    using_default_spec = true;
  }
  input = resolveExistingInputPath(input, repo_root);

  if (!isParticleSpecPath(input)) {
    return input;
  }

  std::filesystem::path output_dir =
      positional.size() >= 2u ? positional[1]
                              : repo_root / "generated" / specBaseName(input);

#if defined(KARMA_PARTICLE_PREVIEW_GENERATION)
  std::string diagnostic;
  if (!karma::tools::particles::generateParticleEffectPackage(input, output_dir, &diagnostic)) {
    spdlog::error("Failed to generate particle package from '{}': {}",
                  input.string(),
                  diagnostic);
    return {};
  }
  spdlog::info("Generated particle package '{}' from '{}'",
               output_dir.string(),
               input.string());
  return output_dir;
#else
  if (using_default_spec) {
    spdlog::error("No package argument was provided and this preview was built without "
                  "KARMA_BUILD_TOOLS=ON, so it cannot auto-generate the default package.");
  } else {
    spdlog::error("Spec input '{}' requires a preview build with KARMA_BUILD_TOOLS=ON",
                  input.string());
  }
  spdlog::error("Generate manually, then run the package directory: "
                "karma_particle_effect_generate {} {}",
                input.string(),
                output_dir.string());
  return {};
#endif
}

constexpr std::string_view kPreviewBackdropMeshKey = "runtime/generated_preview/backdrop_mesh";
constexpr std::string_view kPreviewBackdropMaterialKey =
    "runtime/generated_preview/backdrop_material";
constexpr std::string_view kPreviewPostProcessProfileKey =
    "runtime/generated_preview/post_process";

karma::world::MeshData buildPreviewBackdropMesh(std::string material_key) {
  karma::world::MeshData mesh{};
  constexpr float kHalfWidth = 18.0f;
  constexpr float kHalfHeight = 10.5f;
  mesh.vertices = {
      {-kHalfWidth, -kHalfHeight, 0.0f},
      {kHalfWidth, -kHalfHeight, 0.0f},
      {kHalfWidth, kHalfHeight, 0.0f},
      {-kHalfWidth, kHalfHeight, 0.0f},
  };
  mesh.normals = {
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
  };
  mesh.uvs = {
      {0.0f, 0.0f},
      {1.0f, 0.0f},
      {1.0f, 1.0f},
      {0.0f, 1.0f},
  };
  mesh.tangents = {
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
  };
  mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  mesh.submeshes.push_back({
      .index_offset = 0u,
      .index_count = 6u,
      .material_slot = 0u,
  });
  mesh.material_slots.push_back({
      .name = "Backdrop",
      .default_material_key = std::move(material_key),
  });
  return mesh;
}

}  // namespace

namespace karma::demo {

class GeneratedParticlePreview final : public app::GameInterface {
 public:
  GeneratedParticlePreview(std::filesystem::path package_path, PreviewOptions options)
      : package_path_(std::move(package_path)),
        options_(options) {}

  void onStart() override {
    bindCameraControls();
    configurePostProcess();
    spawnCamera();
    spawnLighting();
    spawnBackdrop();
    spawnPreviewPrefab();
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override { updateCamera(dt); }

  void onShutdown() override {}

 private:
  struct CameraPose {
    math::Vec3 position{};
    float yaw = 0.0f;
    float pitch = 0.0f;
  };

  CameraPose initialCameraPose() const {
    if (options_.top_view) {
      return CameraPose{
          .position = {0.0f, 10.5f, 0.0f},
          .yaw = 0.0f,
          .pitch = -1.55f,
      };
    }
    return CameraPose{
        .position = {3.2f, 3.0f, 9.5f},
        .yaw = 0.28f,
        .pitch = -0.25f,
    };
  }

  void bindCameraControls() {
    if (input == nullptr) {
      return;
    }
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_fast", platform::Key::LeftShift);
    input->bindMouse("cam_look", platform::MouseButton::Right);
  }

  void updateCamera(float dt) {
    if (input == nullptr || !world->isAlive(camera_entity_)) {
      return;
    }

    constexpr float kLookSensitivity = 0.0008f;
    constexpr float kMoveSpeed = 5.5f;
    constexpr float kBoostMultiplier = 4.0f;
    constexpr float kSmoothing = 20.0f;

    if (input->actionDown("cam_look")) {
      target_camera_yaw_ -= input->mouseDeltaX() * kLookSensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * kLookSensitivity;
    }
    target_camera_pitch_ = std::clamp(target_camera_pitch_, -1.55f, 1.55f);

    const float alpha = 1.0f - std::exp(-kSmoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat camera_rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    constexpr math::Vec3 world_up{0.0f, 1.0f, 0.0f};
    const math::Vec3 right = math::normalize(math::cross(forward, world_up));

    float forward_input = 0.0f;
    float right_input = 0.0f;
    float vertical_input = 0.0f;
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;
    if (input->actionDown("cam_up")) vertical_input += 1.0f;
    if (input->actionDown("cam_down")) vertical_input -= 1.0f;

    const float move_speed =
        kMoveSpeed * (input->actionDown("cam_fast") ? kBoostMultiplier : 1.0f);
    math::Vec3 camera_position = camera_transform.getPosition();
    camera_position.x +=
        (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    camera_position.y +=
        (forward.y * forward_input + world_up.y * vertical_input) * move_speed * dt;
    camera_position.z +=
        (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_transform.setPosition(camera_position);
    camera_transform.setRotation(camera_rotation);
  }

  void spawnCamera() {
    const world::Entity camera_entity = world->createEntity();
    world->setName(camera_entity, "Camera");
    camera_entity_ = camera_entity;
    const CameraPose pose = initialCameraPose();
    camera_yaw_ = pose.yaw;
    target_camera_yaw_ = camera_yaw_;
    camera_pitch_ = pose.pitch;
    target_camera_pitch_ = camera_pitch_;
    components::TransformComponent camera_transform{};
    camera_transform.setPosition(pose.position);
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera_entity, camera_transform);
    world->add(camera_entity,
               components::CameraComponent{
                   .near_clip = 0.05f,
                   .far_clip = 160.0f,
                   .is_primary = true,
                   .post_process_profile_key = std::string{kPreviewPostProcessProfileKey},
               });
  }

  void spawnLighting() {
    const world::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setRotation(math::fromYawPitch(0.45f, -0.75f));
    world->add(sun, sun_transform);
    world->add(sun,
               components::LightComponent{
                   .type = components::LightComponent::Type::Directional,
                   .color = {0.86f, 0.90f, 1.0f, 1.0f},
                   .intensity = 0.38f,
               });
  }

  void spawnBackdrop() {
    if (assets == nullptr) {
      return;
    }

    const std::string material_key{kPreviewBackdropMaterialKey};
    const std::string mesh_key{kPreviewBackdropMeshKey};
    rendering::DiffuseMaterialDesc material_desc{};
    material_desc.base_color = {0.0f, 0.0f, 0.0f, 1.0f};
    material_desc.roughness = 1.0f;
    material_desc.double_sided = true;
    material_desc.unlit = true;
    assets->registerMaterialAsset(
        material_key,
        rendering::createDiffuseMaterialAsset(material_key, material_desc));
    assets->registerMeshAsset(mesh_key, buildPreviewBackdropMesh(material_key));

    const world::Entity backdrop = world->createEntity();
    world->setName(backdrop, "Reference Backdrop");
    const CameraPose pose = initialCameraPose();
    const math::Vec3 camera_position = pose.position;
    const math::Quat camera_rotation = math::fromYawPitch(pose.yaw, pose.pitch);
    const math::Vec3 camera_forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    constexpr float kBackdropDepth = 13.2f;
    components::TransformComponent transform{};
    transform.setPosition({
        camera_position.x + camera_forward.x * kBackdropDepth,
        camera_position.y + camera_forward.y * kBackdropDepth,
        camera_position.z + camera_forward.z * kBackdropDepth,
    });
    transform.setRotation(camera_rotation);
    world->add(backdrop, transform);
    world->add(backdrop,
               components::MeshComponent{
                   .mesh_asset_key = mesh_key,
                   .materials = {},
                   .visible = true,
                   .shadow_visible = false,
               });
  }

  void configurePostProcess() {
    if (assets == nullptr) {
      return;
    }

    rendering::PostProcessSettings settings{};
    settings.bloom_enabled = true;
    settings.bloom_threshold = 0.58f;
    settings.bloom_intensity = 0.82f;
    settings.bloom_radius = 4.2f;
    settings.tone_mapping_enabled = false;
    assets->registerPostProcessProfile(std::string{kPreviewPostProcessProfileKey},
                                       settings);
  }

  void spawnPreviewPrefab() {
    if (package_path_.empty()) {
      spdlog::error(
          "Generated particle preview requires argv[1] or KARMA_GENERATED_PARTICLE_PACKAGE");
      return;
    }
    prefabs::PrefabInstantiateDesc desc{};
    desc.assets = assets;
    const auto instance = prefabs::instantiatePrefab(*world, *scene, package_path_, desc);
    if (!instance.has_value()) {
      spdlog::error("Failed to instantiate generated particle package '{}'",
                    package_path_.string());
      return;
    }
    spdlog::info("Loaded generated particle package '{}'", package_path_.string());
  }

  std::filesystem::path package_path_;
  PreviewOptions options_;
  world::Entity camera_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main(int argc, char** argv) {
  const PreviewArguments preview_args = parsePreviewArguments(argc, argv);
  std::filesystem::path package_path =
      resolvePreviewPackage(preview_args.positional, argc > 0 ? argv[0] : nullptr);
  if (package_path.empty()) {
    return 1;
  }

  karma::app::EngineApp engine;
  karma::demo::GeneratedParticlePreview game(package_path, preview_args.options);

  karma::app::EngineConfig config;
  config.window.title = "Karma Generated Particle Preview";
  config.window.samples = 1;
  config.loading_splash.enabled = false;
  config.cursor_visible = true;
  config.generate_mipmaps = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 8;
  config.environment_intensity = 0.0f;
  config.environment_draw_skybox = false;
  config.background_color = {0.0f, 0.0f, 0.0f, 1.0f};
  config.forward_plus_max_local_lights = 128;
  config.shadow_map_size = 512;
  config.point_shadow_max_lights = 1;
  config.lighting_exposure = 1.05f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
