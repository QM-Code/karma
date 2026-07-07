#include "karma/karma.h"

#if defined(KARMA_PARTICLE_PREVIEW_GENERATION)
#include "particle_effect_tools.h"
#endif

#include <array>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

namespace {

using Json = nlohmann::json;

constexpr std::string_view kDefaultSpecPath =
    "examples/particles/specs/fire_ray.kpspec.json";

struct PreviewOptions {
  bool top_view = false;
  bool scenery = false;
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
    } else if (arg == "--scene" || arg == "--scenery") {
      parsed.options.scenery = true;
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

constexpr std::string_view kPreviewFrameGraphKey =
    "runtime/generated_preview/post_process";
constexpr std::string_view kScaleCapsuleMeshKey = "runtime/generated_preview/scale_capsule_mesh";
constexpr std::string_view kScaleCapsuleMaterialKey =
    "runtime/generated_preview/scale_capsule_material";
constexpr std::string_view kSceneryPlaneMeshKey =
    "runtime/generated_preview/scenery/plane_mesh";
constexpr std::string_view kSceneryCubeMeshKey =
    "runtime/generated_preview/scenery/cube_mesh";
constexpr std::string_view kSceneryPlaneMaterialKey =
    "runtime/generated_preview/scenery/plane_material";
constexpr std::string_view kSceneryRedMaterialKey =
    "runtime/generated_preview/scenery/red_material";
constexpr std::string_view kSceneryTealMaterialKey =
    "runtime/generated_preview/scenery/teal_material";
constexpr std::string_view kSceneryYellowMaterialKey =
    "runtime/generated_preview/scenery/yellow_material";
constexpr std::string_view kSceneryVioletMaterialKey =
    "runtime/generated_preview/scenery/violet_material";
constexpr float kFeetToMeters = 0.3048f;
constexpr float kMediumReferenceHeight = 6.0f * kFeetToMeters;
constexpr float kMediumReferenceRadius = 0.34f;
constexpr float kEffectHeightScale = 0.7f;
constexpr float kEffectRadiusScale = 0.3f;
constexpr float kChromaticRayBaseLength = 6.4f;
constexpr float kChromaticRayAxisY = 1.3f;
constexpr float kChromaticHelixRadius = 0.25f;
constexpr float kChromaticBaseHelixTurns = 2.45f;
constexpr float kFireballFlightDistance = 50.0f * kFeetToMeters;
constexpr float kFireballStartHeight = 1.35f;
constexpr float kFireballExplosionHeight = 1.15f;

#if defined(KARMA_PARTICLE_SCALE_REFERENCE_PREVIEW)
constexpr bool kScaleReferencePreview = true;
#else
constexpr bool kScaleReferencePreview = false;
#endif

struct ScaleReference {
  std::string_view name;
  float height = 0.0f;
  float radius = 0.0f;
  karma::math::Vec3 position{};
};

constexpr std::array<ScaleReference, 3> kScaleReferences{{
    {
        .name = "Small",
        .height = 3.0f * kFeetToMeters,
        .radius = kMediumReferenceRadius * 0.5f,
        .position = {-4.2f, 0.0f, 0.0f},
    },
    {
        .name = "Medium",
        .height = kMediumReferenceHeight,
        .radius = kMediumReferenceRadius,
        .position = {0.0f, 0.0f, 0.0f},
    },
    {
        .name = "Large",
        .height = 12.0f * kFeetToMeters,
        .radius = kMediumReferenceRadius * 2.0f,
        .position = {4.8f, 0.0f, 0.0f},
    },
}};

std::filesystem::path resolvePrefabDocumentPath(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return path / "prefab.json";
  }
  if (path.extension().empty()) {
    return path / "prefab.json";
  }
  return path;
}

std::unordered_set<std::string> readPrefabVariableNames(
    const std::filesystem::path& package_path) {
  std::unordered_set<std::string> names;
  std::ifstream stream(resolvePrefabDocumentPath(package_path));
  if (!stream) {
    return names;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception&) {
    return names;
  }

  const auto variables_it = json.find("variables");
  if (variables_it == json.end() || !variables_it->is_object()) {
    return names;
  }
  names.reserve(variables_it->size());
  for (auto it = variables_it->begin(); it != variables_it->end(); ++it) {
    names.insert(it.key());
  }
  return names;
}

bool prefabContainsNodeName(const std::filesystem::path& package_path,
                            std::string_view node_name) {
  std::ifstream stream(resolvePrefabDocumentPath(package_path));
  if (!stream) {
    return false;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception&) {
    return false;
  }

  const auto nodes_it = json.find("nodes");
  if (nodes_it == json.end() || !nodes_it->is_array()) {
    return false;
  }
  for (const Json& node : *nodes_it) {
    const auto name_it = node.find("name");
    if (name_it != node.end() && name_it->is_string() &&
        name_it->get<std::string>() == node_name) {
      return true;
    }
  }
  return false;
}

bool hasSizeVariable(const std::unordered_set<std::string>& variables) {
  return variables.contains("height") ||
         variables.contains("radius") ||
         variables.contains("width") ||
         variables.contains("length");
}

bool usesCapsuleScaleVariables(const std::filesystem::path& package_path) {
  return prefabContainsNodeName(package_path, "base_speed_ring") &&
         prefabContainsNodeName(package_path, "haste_glow");
}

bool usesCastCyclePreview(const std::filesystem::path& package_path) {
  return (prefabContainsNodeName(package_path, "base_healing_ring") &&
          prefabContainsNodeName(package_path, "heal_glow")) ||
         (prefabContainsNodeName(package_path, "base_speed_ring") &&
          prefabContainsNodeName(package_path, "haste_glow"));
}

std::filesystem::path fireballProjectilePackagePath(const std::filesystem::path& package_path) {
  return package_path / "projectile";
}

std::filesystem::path fireballExplosionPackagePath(const std::filesystem::path& package_path) {
  return package_path / "explosion";
}

bool usesFireballSequencePreview(const std::filesystem::path& package_path) {
  return std::filesystem::exists(fireballProjectilePackagePath(package_path) / "prefab.json") &&
         std::filesystem::exists(fireballExplosionPackagePath(package_path) / "prefab.json");
}

bool usesFixedAreaPreview(const std::filesystem::path& package_path) {
  return prefabContainsNodeName(package_path, "shimmer_volume") &&
         prefabContainsNodeName(package_path, "detect_magic_glow");
}

karma::math::Vec3 fireballStartPosition() {
  return {0.48f, kFireballStartHeight, 0.0f};
}

karma::math::Vec3 fireballEndPosition() {
  return {0.48f + kFireballFlightDistance, kFireballExplosionHeight, 0.0f};
}

karma::math::Vec3 fireballProjectilePosition(float t) {
  const float u = std::clamp(t, 0.0f, 1.0f);
  return karma::math::lerp(fireballStartPosition(), fireballEndPosition(), u);
}

float smoothStep01(float value) {
  const float t = std::clamp(value, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float smootherStep01(float value) {
  const float t = std::clamp(value, 0.0f, 1.0f);
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

karma::math::Color alphaScaledColor(karma::math::Color color, float alpha) {
  color.a *= alpha;
  return color;
}

karma::math::Vec3 scaledVec3(const karma::math::Vec3& value, float scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

void setPrefabVariableIfDeclared(karma::prefabs::PrefabInstantiateDesc& desc,
                                 const std::unordered_set<std::string>& variables,
                                 std::string_view name,
                                 float value) {
  const std::string key{name};
  if (variables.contains(key)) {
    desc.variables[key] = value;
  }
}

bool isBlueCloudEffectName(std::string_view name) {
  return name.find("mist") != std::string_view::npos ||
         name.find("haze") != std::string_view::npos ||
         name.find("cloud") != std::string_view::npos;
}

float chromaticHelixTurnsForLength(float length) {
  return (std::max(length, 0.25f) / kChromaticRayBaseLength) *
         kChromaticBaseHelixTurns;
}

std::vector<karma::math::Vec3> makeChromaticRayPath(float length) {
  const float half_length = std::max(length, 0.25f) * 0.5f;
  const karma::math::Vec3 start{-half_length, kChromaticRayAxisY, 0.0f};
  const karma::math::Vec3 end{half_length, kChromaticRayAxisY, 0.0f};
  return {
      karma::math::lerp(start, end, 0.0f),
      karma::math::lerp(start, end, 1.0f / 3.0f),
      karma::math::lerp(start, end, 2.0f / 3.0f),
      karma::math::lerp(start, end, 1.0f),
  };
}

std::vector<karma::math::Vec3> makeChromaticHelixPath(float length,
                                                      float phase,
                                                      std::size_t segments = 24u) {
  constexpr float kPi = 3.14159265358979323846f;
  const float half_length = std::max(length, 0.25f) * 0.5f;
  const karma::math::Vec3 start{-half_length, kChromaticRayAxisY, 0.0f};
  const karma::math::Vec3 end{half_length, kChromaticRayAxisY, 0.0f};
  const karma::math::Vec3 axis =
      karma::math::normalize(karma::math::subtract(end, start));
  const karma::math::Vec3 normal =
      karma::math::normalize(karma::math::cross(axis, {0.0f, 1.0f, 0.0f}));
  const karma::math::Vec3 binormal =
      karma::math::normalize(karma::math::cross(normal, axis));
  const float turns = chromaticHelixTurnsForLength(length);

  std::vector<karma::math::Vec3> path;
  path.reserve(segments + 1u);
  for (std::size_t i = 0u; i <= segments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(segments);
    const float angle = phase + u * turns * kPi * 2.0f;
    const karma::math::Vec3 center = karma::math::lerp(start, end, u);
    const karma::math::Vec3 offset = karma::math::add(
        karma::math::scale(normal, std::cos(angle) * kChromaticHelixRadius),
        karma::math::scale(binormal, std::sin(angle) * kChromaticHelixRadius));
    path.push_back(karma::math::add(center, offset));
  }
  return path;
}

void drawScaleGrid(karma::rendering::GraphicsDevice& graphics) {
  constexpr int kGridHalfExtent = 24;
  constexpr float kGridY = 0.0f;
  constexpr karma::math::Color kMinorColor{0.075f, 0.085f, 0.095f, 0.34f};
  constexpr karma::math::Color kMajorColor{0.15f, 0.16f, 0.17f, 0.52f};
  constexpr karma::math::Color kXAxisColor{0.36f, 0.10f, 0.075f, 0.76f};
  constexpr karma::math::Color kZAxisColor{0.08f, 0.20f, 0.36f, 0.76f};

  for (int i = -kGridHalfExtent; i <= kGridHalfExtent; ++i) {
    const float p = static_cast<float>(i);
    const bool major = (i % 5) == 0;
    const karma::math::Color row_color =
        i == 0 ? kXAxisColor : (major ? kMajorColor : kMinorColor);
    const karma::math::Color column_color =
        i == 0 ? kZAxisColor : (major ? kMajorColor : kMinorColor);
    graphics.drawLine({-static_cast<float>(kGridHalfExtent), kGridY, p},
                      {static_cast<float>(kGridHalfExtent), kGridY, p},
                      row_color,
                      true,
                      1.0f);
    graphics.drawLine({p, kGridY, -static_cast<float>(kGridHalfExtent)},
                      {p, kGridY, static_cast<float>(kGridHalfExtent)},
                      column_color,
                      true,
                      1.0f);
  }
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
    if (options_.scenery) {
      spawnScenery();
    }
    if (usesFireballSequencePreview(package_path_)) {
      spawnFireballSequencePreview();
    } else if constexpr (kScaleReferencePreview) {
      if (usesFixedAreaPreview(package_path_)) {
        spawnPreviewPrefab();
      } else {
        spawnScaleReference();
        spawnScalePreviewPrefabs();
      }
    } else {
      spawnPreviewPrefab();
    }
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    updateCamera(dt);
    updateCastPreview(dt);
    updateFireballSequence(dt);
    if (kScaleReferencePreview || usesFireballSequencePreview(package_path_)) {
      if (graphics != nullptr) {
        drawScaleGrid(*graphics);
      }
    }
  }

  void onShutdown() override {}

 private:
  struct BeamCastState {
    world::Entity entity{};
    math::Color start_color{};
    math::Color end_color{};
    float start_width = 0.0f;
    float end_width = 0.0f;
  };

  struct EffectCastState {
    world::Entity entity{};
    components::ParticleEffectOverrideComponent base_override{};
  };

  struct LightCastState {
    world::Entity entity{};
    float intensity = 0.0f;
    float range = 0.0f;
  };

  struct CastPreviewInstance {
    world::Entity root{};
    math::Vec3 root_scale{1.0f, 1.0f, 1.0f};
    std::vector<BeamCastState> beams;
    std::vector<EffectCastState> effects;
    std::vector<LightCastState> lights;
    float particle_drain_duration = 1.42f;
    int cycle_index = -1;
  };

  struct FireballSequenceState {
    world::Entity root{};
    std::vector<EffectCastState> effects;
    std::vector<LightCastState> lights;
  };

  struct FireballSequencePreview {
    FireballSequenceState projectile;
    FireballSequenceState explosion;
    int cycle_index = -1;
    bool explosion_started = false;
  };

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
    if (usesFireballSequencePreview(package_path_)) {
      return CameraPose{
          .position = {8.0f, 4.8f, 18.0f},
          .yaw = 0.0f,
          .pitch = -0.24f,
      };
    }
    return CameraPose{
        .position = {2.8f, 3.6f, 12.0f},
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
                   .frame_graph_key = std::string{kPreviewFrameGraphKey},
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

  void registerSceneryMaterial(std::string_view key,
                               math::Color color,
                               float roughness = 0.82f) {
    rendering::DiffuseMaterialDesc desc{};
    desc.base_color = color;
    desc.roughness = roughness;
    desc.double_sided = true;
    assets->registerMaterialAsset(
        std::string{key},
        rendering::createDiffuseMaterialAsset(std::string{key}, desc));
  }

  void spawnSceneryMesh(std::string name,
                        std::string mesh_key,
                        math::Vec3 position,
                        math::Vec3 scale) {
    const world::Entity entity = world->createEntity();
    world->setName(entity, std::move(name));
    components::TransformComponent transform{};
    transform.setPosition(position);
    transform.setScale(scale);
    world->add(entity, transform);
    world->add(entity,
               components::MeshComponent{
                   .mesh_asset_key = std::move(mesh_key),
                   .materials = {},
                   .visible = true,
                   .shadow_visible = true,
               });
  }

  void spawnScenery() {
    if (assets == nullptr) {
      return;
    }

    registerSceneryMaterial(kSceneryPlaneMaterialKey,
                            {0.36f, 0.38f, 0.40f, 1.0f},
                            0.88f);
    registerSceneryMaterial(kSceneryRedMaterialKey,
                            {0.78f, 0.22f, 0.18f, 1.0f},
                            0.72f);
    registerSceneryMaterial(kSceneryTealMaterialKey,
                            {0.08f, 0.58f, 0.62f, 1.0f},
                            0.66f);
    registerSceneryMaterial(kSceneryYellowMaterialKey,
                            {0.86f, 0.68f, 0.16f, 1.0f},
                            0.70f);
    registerSceneryMaterial(kSceneryVioletMaterialKey,
                            {0.46f, 0.30f, 0.78f, 1.0f},
                            0.74f);

    assets->registerMeshAsset(
        std::string{kSceneryPlaneMeshKey},
        world::createPlaneMesh(24.0f, 24.0f, std::string{kSceneryPlaneMaterialKey}));

    const std::array<std::pair<std::string_view, std::string_view>, 4> cube_materials{{
        {"red", kSceneryRedMaterialKey},
        {"teal", kSceneryTealMaterialKey},
        {"yellow", kSceneryYellowMaterialKey},
        {"violet", kSceneryVioletMaterialKey},
    }};
    for (const auto& [suffix, material_key] : cube_materials) {
      assets->registerMeshAsset(
          std::string{kSceneryCubeMeshKey} + "/" + std::string{suffix},
          world::createCubeMesh(1.0f, std::string{material_key}));
    }

    spawnSceneryMesh("Scenery Plane",
                     std::string{kSceneryPlaneMeshKey},
                     {0.0f, -0.01f, 0.0f},
                     {1.0f, 1.0f, 1.0f});
    spawnSceneryMesh("Scenery Red Cube",
                     std::string{kSceneryCubeMeshKey} + "/red",
                     {-3.6f, 0.72f, -2.8f},
                     {1.4f, 1.4f, 1.4f});
    spawnSceneryMesh("Scenery Teal Column",
                     std::string{kSceneryCubeMeshKey} + "/teal",
                     {2.7f, 1.20f, -3.4f},
                     {1.0f, 2.4f, 1.0f});
    spawnSceneryMesh("Scenery Yellow Block",
                     std::string{kSceneryCubeMeshKey} + "/yellow",
                     {-1.2f, 0.45f, 3.4f},
                     {2.1f, 0.9f, 1.1f});
    spawnSceneryMesh("Scenery Violet Block",
                     std::string{kSceneryCubeMeshKey} + "/violet",
                     {4.6f, 0.55f, 2.4f},
                     {1.1f, 1.1f, 2.0f});
  }

  void spawnScaleReference() {
    if (assets == nullptr) {
      return;
    }

    rendering::DiffuseMaterialDesc capsule_material_desc{};
    capsule_material_desc.base_color = {0.56f, 0.70f, 0.86f, 0.36f};
    capsule_material_desc.roughness = 0.82f;
    capsule_material_desc.double_sided = true;
    assets->registerMaterialAsset(
        std::string{kScaleCapsuleMaterialKey},
        rendering::createDiffuseMaterialAsset(std::string{kScaleCapsuleMaterialKey},
                                               capsule_material_desc));

    for (const ScaleReference& reference : kScaleReferences) {
      const std::string mesh_key =
          std::string{kScaleCapsuleMeshKey} + "/" + std::string{reference.name};
      assets->registerMeshAsset(
          mesh_key,
          world::createCapsuleMesh(world::CapsuleMeshDesc{
              .radius = reference.radius,
              .cylinder_height = std::max(reference.height - reference.radius * 2.0f, 0.0f),
              .segments = 24u,
              .hemisphere_rings = 8u,
              .material_key = std::string{kScaleCapsuleMaterialKey},
          }));

      const world::Entity actor = world->createEntity();
      world->setName(actor, std::string{"Scale Capsule "} + std::string{reference.name});
      components::TransformComponent transform{};
      transform.setPosition({reference.position.x,
                             reference.height * 0.5f,
                             reference.position.z});
      world->add(actor, transform);
      world->add(actor,
                 components::MeshComponent{
                     .mesh_asset_key = mesh_key,
                     .materials = {},
                     .visible = true,
                     .shadow_visible = false,
                 });
    }
  }

  void spawnFireballReference() {
    if (assets == nullptr) {
      return;
    }

    rendering::DiffuseMaterialDesc capsule_material_desc{};
    capsule_material_desc.base_color = {0.62f, 0.68f, 0.74f, 0.34f};
    capsule_material_desc.roughness = 0.82f;
    capsule_material_desc.double_sided = true;
    assets->registerMaterialAsset(
        std::string{kScaleCapsuleMaterialKey},
        rendering::createDiffuseMaterialAsset(std::string{kScaleCapsuleMaterialKey},
                                               capsule_material_desc));

    const std::string mesh_key = std::string{kScaleCapsuleMeshKey} + "/fireball_caster";
    assets->registerMeshAsset(
        mesh_key,
        world::createCapsuleMesh(world::CapsuleMeshDesc{
            .radius = kMediumReferenceRadius,
            .cylinder_height =
                std::max(kMediumReferenceHeight - kMediumReferenceRadius * 2.0f, 0.0f),
            .segments = 24u,
            .hemisphere_rings = 8u,
            .material_key = std::string{kScaleCapsuleMaterialKey},
        }));

    const world::Entity caster = world->createEntity();
    world->setName(caster, "Fireball Caster Capsule");
    components::TransformComponent transform{};
    transform.setPosition({0.0f, kMediumReferenceHeight * 0.5f, 0.0f});
    world->add(caster, transform);
    world->add(caster,
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
    const std::string graph_key{kPreviewFrameGraphKey};
    assets->registerFrameGraph(
        graph_key,
        rendering::frameGraphFromPostProcessSettings(settings, graph_key));
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
    if (usesCastCyclePreview(package_path_)) {
      registerCastPreviewInstance(*instance);
    }
    spdlog::info("Loaded generated particle package '{}'", package_path_.string());
  }

  FireballSequenceState collectFireballSequenceState(
      const prefabs::PrefabInstance& instance) {
    FireballSequenceState state{};
    state.root = instance.root;
    for (const world::Entity entity : instance.entities) {
      if (!world->isAlive(entity)) {
        continue;
      }
      if (world->has<components::ParticleEffectComponent>(entity)) {
        components::ParticleEffectOverrideComponent base_override =
            world->has<components::ParticleEffectOverrideComponent>(entity)
                ? world->get<components::ParticleEffectOverrideComponent>(entity)
                : components::ParticleEffectOverrideComponent{};
        state.effects.push_back(EffectCastState{
            .entity = entity,
            .base_override = std::move(base_override),
        });
      }
      if (world->has<components::LightComponent>(entity)) {
        const auto& light = world->get<components::LightComponent>(entity);
        state.lights.push_back(LightCastState{
            .entity = entity,
            .intensity = light.intensity,
            .range = light.range,
        });
      }
    }
    return state;
  }

  void setFireballRootPosition(const FireballSequenceState& state,
                               const math::Vec3& position) {
    if (world->isAlive(state.root) &&
        world->has<components::TransformComponent>(state.root)) {
      world->get<components::TransformComponent>(state.root).setPosition(position);
    }
  }

  void restartFireballEffects(const FireballSequenceState& state) {
    for (const EffectCastState& effect_state : state.effects) {
      if (!world->isAlive(effect_state.entity)) {
        continue;
      }
      if (world->has<components::ParticleEffectComponent>(effect_state.entity)) {
        world->get<components::ParticleEffectComponent>(effect_state.entity).restart_count += 1u;
      }
      if (world->has<components::ParticleEmitterComponent>(effect_state.entity)) {
        auto& emitter = world->get<components::ParticleEmitterComponent>(effect_state.entity);
        emitter.enabled = true;
        emitter.playing = true;
      }
    }
  }

  void setFireballEffects(const FireballSequenceState& state,
                          float alpha,
                          float spawn_rate,
                          bool enabled,
                          bool playing) {
    for (const EffectCastState& effect_state : state.effects) {
      if (!world->isAlive(effect_state.entity) ||
          !world->has<components::ParticleEffectComponent>(effect_state.entity)) {
        continue;
      }

      components::ParticleEffectOverrideComponent effect_override =
          effect_state.base_override;
      effect_override.alpha_scale *= std::max(alpha, 0.0f);
      effect_override.spawn_rate_scale *= std::max(spawn_rate, 0.0f);
      world->add(effect_state.entity, std::move(effect_override));

      if (world->has<components::ParticleEmitterComponent>(effect_state.entity)) {
        auto& emitter = world->get<components::ParticleEmitterComponent>(effect_state.entity);
        emitter.enabled = enabled;
        emitter.playing = playing;
      }
    }
  }

  void setFireballLights(const FireballSequenceState& state,
                         float intensity_scale,
                         float range_scale) {
    for (const LightCastState& light_state : state.lights) {
      if (!world->isAlive(light_state.entity) ||
          !world->has<components::LightComponent>(light_state.entity)) {
        continue;
      }
      auto& light = world->get<components::LightComponent>(light_state.entity);
      light.intensity = light_state.intensity * std::max(intensity_scale, 0.0f);
      light.range = light_state.range * std::max(range_scale, 0.0f);
    }
  }

  void resetFireballSequence(FireballSequencePreview& preview) {
    preview.explosion_started = false;
    setFireballRootPosition(preview.projectile, fireballStartPosition());
    setFireballRootPosition(preview.explosion, fireballEndPosition());
    restartFireballEffects(preview.projectile);
    setFireballEffects(preview.projectile, 1.0f, 1.0f, true, true);
    setFireballLights(preview.projectile, 1.0f, 1.0f);
    setFireballEffects(preview.explosion, 0.0f, 0.0f, false, false);
    setFireballLights(preview.explosion, 0.0f, 0.0f);
  }

  void triggerFireballExplosion(FireballSequencePreview& preview) {
    preview.explosion_started = true;
    setFireballRootPosition(preview.explosion, fireballEndPosition());
    restartFireballEffects(preview.explosion);
    setFireballEffects(preview.explosion, 1.0f, 1.0f, true, true);
  }

  void spawnFireballSequencePreview() {
    spawnFireballReference();

    prefabs::PrefabInstantiateDesc projectile_desc{};
    projectile_desc.assets = assets;
    projectile_desc.name_override = "Fireball Projectile Preview";
    projectile_desc.root_transform.setPosition(fireballStartPosition());
    const auto projectile = prefabs::instantiatePrefab(
        *world,
        *scene,
        fireballProjectilePackagePath(package_path_),
        projectile_desc);
    if (!projectile.has_value()) {
      spdlog::error("Failed to instantiate fireball projectile package '{}'",
                    fireballProjectilePackagePath(package_path_).string());
      return;
    }

    prefabs::PrefabInstantiateDesc explosion_desc{};
    explosion_desc.assets = assets;
    explosion_desc.name_override = "Fireball Explosion Preview";
    explosion_desc.root_transform.setPosition(fireballEndPosition());
    const auto explosion = prefabs::instantiatePrefab(
        *world,
        *scene,
        fireballExplosionPackagePath(package_path_),
        explosion_desc);
    if (!explosion.has_value()) {
      spdlog::error("Failed to instantiate fireball explosion package '{}'",
                    fireballExplosionPackagePath(package_path_).string());
      return;
    }

    FireballSequencePreview preview{};
    preview.projectile = collectFireballSequenceState(*projectile);
    preview.explosion = collectFireballSequenceState(*explosion);
    fireball_sequence_ = std::move(preview);
    resetFireballSequence(*fireball_sequence_);
    spdlog::info("Loaded fireball projectile '{}' and explosion '{}'",
                 fireballProjectilePackagePath(package_path_).string(),
                 fireballExplosionPackagePath(package_path_).string());
  }

  world::Entity findBlueCloudEffect(const prefabs::PrefabInstance& instance) const {
    const world::Entity named_mist = instance.find("mist");
    if (named_mist.isValid() && world->isAlive(named_mist) &&
        world->has<components::ParticleEffectComponent>(named_mist)) {
      return named_mist;
    }

    for (const world::Entity entity : instance.entities) {
      if (!world->isAlive(entity) ||
          !world->has<components::ParticleEffectComponent>(entity)) {
        continue;
      }
      const auto& effect = world->get<components::ParticleEffectComponent>(entity);
      if (isBlueCloudEffectName(effect.effect_key)) {
        return entity;
      }
      if (world->has<components::TagComponent>(entity) &&
          isBlueCloudEffectName(world->get<components::TagComponent>(entity).name)) {
        return entity;
      }
    }

    return {};
  }

  void applyBlueCloudEmissionScale(const prefabs::PrefabInstance& instance,
                                   float emission_scale) {
    if (!std::isfinite(emission_scale) || emission_scale <= 0.0f) {
      return;
    }

    const world::Entity cloud = findBlueCloudEffect(instance);
    if (!cloud.isValid()) {
      return;
    }

    components::ParticleEffectOverrideComponent effect_override =
        world->has<components::ParticleEffectOverrideComponent>(cloud)
            ? world->get<components::ParticleEffectOverrideComponent>(cloud)
            : components::ParticleEffectOverrideComponent{};
    effect_override.emission_scale *= emission_scale;
    world->add(cloud, std::move(effect_override));
  }

  void setBeamPath(const prefabs::PrefabInstance& instance,
                   std::string_view name,
                   const std::vector<math::Vec3>& path) {
    const world::Entity entity = instance.find(name);
    if (!entity.isValid() || !world->isAlive(entity) ||
        !world->has<components::ParticleBeamComponent>(entity)) {
      return;
    }

    auto& beam = world->get<components::ParticleBeamComponent>(entity);
    beam.local_path_points = path;
    beam.restart_count += 1u;
  }

  void setEffectPathOverride(const prefabs::PrefabInstance& instance,
                             std::string_view name,
                             const std::vector<math::Vec3>& path) {
    const world::Entity entity = instance.find(name);
    if (!entity.isValid() || !world->isAlive(entity) ||
        !world->has<components::ParticleEffectComponent>(entity)) {
      return;
    }

    components::ParticleEffectOverrideComponent effect_override =
        world->has<components::ParticleEffectOverrideComponent>(entity)
            ? world->get<components::ParticleEffectOverrideComponent>(entity)
            : components::ParticleEffectOverrideComponent{};
    effect_override.source_path_points = path;
    world->add(entity, std::move(effect_override));
  }

  void applyChromaticRayLength(const prefabs::PrefabInstance& instance, float length) {
    const std::vector<math::Vec3> ray_path = makeChromaticRayPath(length);
    for (std::string_view name : {"chromatic_core", "chromatic_ribbon", "chromatic_haze"}) {
      setBeamPath(instance, name, ray_path);
    }

    constexpr std::string_view kThreadNames[] = {
        "red_fire_thread",
        "orange_acid_thread",
        "yellow_electric_thread",
        "green_poison_thread",
        "blue_stone_thread",
        "indigo_mind_thread",
        "violet_shift_thread",
    };
    constexpr std::size_t kThreadCount =
        sizeof(kThreadNames) / sizeof(kThreadNames[0]);
    constexpr float kPi = 3.14159265358979323846f;
    for (std::size_t i = 0u; i < kThreadCount; ++i) {
      const float phase =
          (static_cast<float>(i) / static_cast<float>(kThreadCount)) *
          kPi * 2.0f;
      setBeamPath(instance, kThreadNames[i], makeChromaticHelixPath(length, phase));
    }

    if (ray_path.size() >= 2u) {
      setEffectPathOverride(instance, "flares", {ray_path.front(), ray_path.back()});
    }
    for (std::string_view name : {"sparks", "wisps", "distortion"}) {
      setEffectPathOverride(instance, name, ray_path);
    }

    const world::Entity glow = instance.find("chromatic_glow");
    if (glow.isValid() && world->isAlive(glow) &&
        world->has<components::TransformComponent>(glow) && !ray_path.empty()) {
      world->get<components::TransformComponent>(glow).setPosition(ray_path.back());
    }
  }

  void registerCastPreviewInstance(const prefabs::PrefabInstance& instance) {
    CastPreviewInstance preview{};
    preview.root = instance.root;
    const bool haste_preview = instance.find("base_speed_ring").isValid() &&
                               instance.find("haste_glow").isValid();
    if (haste_preview) {
      preview.particle_drain_duration = 2.42f;
    }
    if (world->isAlive(instance.root) &&
        world->has<components::TransformComponent>(instance.root)) {
      preview.root_scale =
          world->get<components::TransformComponent>(instance.root).localScale();
    }

    for (const world::Entity entity : instance.entities) {
      if (!world->isAlive(entity)) {
        continue;
      }
      if (world->has<components::ParticleBeamComponent>(entity)) {
        const auto& beam = world->get<components::ParticleBeamComponent>(entity);
        preview.beams.push_back(BeamCastState{
            .entity = entity,
            .start_color = beam.start_color,
            .end_color = beam.end_color,
            .start_width = beam.start_width,
            .end_width = beam.end_width,
        });
      }
      if (world->has<components::ParticleEffectComponent>(entity)) {
        components::ParticleEffectOverrideComponent base_override =
            world->has<components::ParticleEffectOverrideComponent>(entity)
                ? world->get<components::ParticleEffectOverrideComponent>(entity)
                : components::ParticleEffectOverrideComponent{};
        preview.effects.push_back(EffectCastState{
            .entity = entity,
            .base_override = std::move(base_override),
        });
      }
      if (world->has<components::LightComponent>(entity)) {
        const auto& light = world->get<components::LightComponent>(entity);
        preview.lights.push_back(LightCastState{
            .entity = entity,
            .intensity = light.intensity,
            .range = light.range,
        });
      }
    }

    cast_preview_instances_.push_back(std::move(preview));
  }

  void restartCastPreviewInstance(CastPreviewInstance& preview) {
    if (world->isAlive(preview.root) &&
        world->has<components::TransformComponent>(preview.root)) {
      world->get<components::TransformComponent>(preview.root)
          .setScale(scaledVec3(preview.root_scale, 0.84f));
    }

    for (const BeamCastState& state : preview.beams) {
      if (!world->isAlive(state.entity) ||
          !world->has<components::ParticleBeamComponent>(state.entity)) {
        continue;
      }
      auto& beam = world->get<components::ParticleBeamComponent>(state.entity);
      beam.visible = true;
      beam.restart_count += 1u;
    }

    for (const EffectCastState& state : preview.effects) {
      if (!world->isAlive(state.entity)) {
        continue;
      }
      if (world->has<components::ParticleEffectComponent>(state.entity)) {
        world->get<components::ParticleEffectComponent>(state.entity).restart_count += 1u;
      }
      if (world->has<components::ParticleEmitterComponent>(state.entity)) {
        auto& emitter = world->get<components::ParticleEmitterComponent>(state.entity);
        emitter.enabled = true;
        emitter.playing = true;
      }
    }
  }

  void applyCastPreviewEnvelope(CastPreviewInstance& preview, float local_time) {
    constexpr float kDuration = 1.42f;
    constexpr float kFadeStart = 0.40f;
    constexpr float kEmissionFadeStart = 0.82f;
    const bool active = local_time < kDuration;
    const float attack = smoothStep01(local_time / 0.13f);
    const float fade = active
                           ? 1.0f - smootherStep01((local_time - kFadeStart) /
                                                   (kDuration - kFadeStart))
                           : 0.0f;
    const float emission_fade =
        active ? 1.0f - smoothStep01((local_time - kEmissionFadeStart) /
                                     (kDuration - kEmissionFadeStart))
               : 0.0f;
    const float alpha = active ? attack * fade : 0.0f;
    const float emission = active ? attack * emission_fade : 0.0f;
    const float settle = smoothStep01(std::max(local_time - 0.08f, 0.0f) / 0.26f);
    const float burst_scale =
        active ? (0.82f + 0.18f * attack + 0.08f * (1.0f - settle) * attack)
               : 1.0f;
    const bool visible = alpha > 0.0001f;
    const bool particle_active = local_time < preview.particle_drain_duration;

    if (world->isAlive(preview.root) &&
        world->has<components::TransformComponent>(preview.root)) {
      world->get<components::TransformComponent>(preview.root)
          .setScale(scaledVec3(preview.root_scale, burst_scale));
    }

    for (const BeamCastState& state : preview.beams) {
      if (!world->isAlive(state.entity) ||
          !world->has<components::ParticleBeamComponent>(state.entity)) {
        continue;
      }
      auto& beam = world->get<components::ParticleBeamComponent>(state.entity);
      beam.visible = visible;
      beam.start_color = alphaScaledColor(state.start_color, alpha);
      beam.end_color = alphaScaledColor(state.end_color, alpha);
      beam.start_width = state.start_width * burst_scale;
      beam.end_width = state.end_width * burst_scale;
    }

    for (const EffectCastState& state : preview.effects) {
      if (!world->isAlive(state.entity) ||
          !world->has<components::ParticleEffectComponent>(state.entity)) {
        continue;
      }
      components::ParticleEffectOverrideComponent effect_override = state.base_override;
      effect_override.alpha_scale *= alpha;
      effect_override.spawn_rate_scale *= emission;
      effect_override.size_scale *= burst_scale;
      world->add(state.entity, std::move(effect_override));

      if (world->has<components::ParticleEmitterComponent>(state.entity)) {
        auto& emitter = world->get<components::ParticleEmitterComponent>(state.entity);
        emitter.enabled = particle_active;
        emitter.playing = particle_active;
      }
    }

    const float light_alpha = std::pow(alpha, 1.15f);
    const float range_alpha = visible ? 0.35f + 0.65f * std::sqrt(alpha) : 0.0f;
    for (const LightCastState& state : preview.lights) {
      if (!world->isAlive(state.entity) ||
          !world->has<components::LightComponent>(state.entity)) {
        continue;
      }
      auto& light = world->get<components::LightComponent>(state.entity);
      light.intensity = state.intensity * light_alpha;
      light.range = state.range * range_alpha;
    }
  }

  void updateCastPreview(float dt) {
    if (cast_preview_instances_.empty()) {
      return;
    }

    constexpr float kCycleDuration = 2.85f;
    cast_preview_time_ += std::max(dt, 0.0f);
    const int cycle_index =
        static_cast<int>(std::floor(cast_preview_time_ / kCycleDuration));
    const float local_time =
        cast_preview_time_ -
        static_cast<float>(cycle_index) * kCycleDuration;

    for (CastPreviewInstance& preview : cast_preview_instances_) {
      if (preview.cycle_index != cycle_index) {
        preview.cycle_index = cycle_index;
        restartCastPreviewInstance(preview);
      }
      applyCastPreviewEnvelope(preview, local_time);
    }
  }

  void updateFireballSequence(float dt) {
    if (!fireball_sequence_.has_value()) {
      return;
    }

    constexpr float kFlightDuration = 2.18f;
    constexpr float kProjectileTailDuration = 1.10f;
    constexpr float kExplosionLightDuration = 1.18f;
    constexpr float kCycleDuration = 6.10f;

    fireball_sequence_time_ += std::max(dt, 0.0f);
    const int cycle_index =
        static_cast<int>(std::floor(fireball_sequence_time_ / kCycleDuration));
    const float local_time =
        fireball_sequence_time_ -
        static_cast<float>(cycle_index) * kCycleDuration;

    FireballSequencePreview& preview = *fireball_sequence_;
    if (preview.cycle_index != cycle_index) {
      preview.cycle_index = cycle_index;
      resetFireballSequence(preview);
    }

    if (local_time < kFlightDuration) {
      const float u = std::clamp(local_time / kFlightDuration, 0.0f, 1.0f);
      setFireballRootPosition(preview.projectile, fireballProjectilePosition(u));
      setFireballEffects(preview.projectile, 1.0f, 1.0f, true, true);
      const float pulse = 0.88f + 0.12f * std::sin(local_time * 18.0f);
      setFireballLights(preview.projectile, pulse, 0.92f + 0.08f * pulse);
      setFireballEffects(preview.explosion, 0.0f, 0.0f, false, false);
      setFireballLights(preview.explosion, 0.0f, 0.0f);
      return;
    }

    setFireballRootPosition(preview.projectile, fireballEndPosition());
    if (!preview.explosion_started) {
      triggerFireballExplosion(preview);
    }

    const float tail_t = (local_time - kFlightDuration) / kProjectileTailDuration;
    const float projectile_tail = 1.0f - smoothStep01(tail_t);
    const bool projectile_active = projectile_tail > 0.001f;
    setFireballEffects(preview.projectile,
                       projectile_tail,
                       0.0f,
                       projectile_active,
                       projectile_active);
    setFireballLights(preview.projectile,
                      projectile_tail * projectile_tail,
                      std::sqrt(std::max(projectile_tail, 0.0f)));

    const float explosion_t = (local_time - kFlightDuration) / kExplosionLightDuration;
    const float flash = 1.0f - smootherStep01(explosion_t);
    const float blast_growth =
        smoothStep01((local_time - kFlightDuration) / 0.22f);
    const float range_growth = 0.03f + 0.97f * blast_growth;
    const float range_fade = 0.35f + 0.65f * std::sqrt(std::max(flash, 0.0f));
    const float ember_glow = 0.18f * (1.0f - smoothStep01((local_time - kFlightDuration) / 2.6f));
    setFireballEffects(preview.explosion, 1.0f, 1.0f, true, true);
    setFireballLights(preview.explosion,
                      std::max(flash, ember_glow),
                      range_growth * range_fade);
  }

  void spawnScalePreviewPrefabs() {
    if (package_path_.empty()) {
      spdlog::error(
          "Generated particle scale preview requires argv[1] or "
          "KARMA_GENERATED_PARTICLE_PACKAGE");
      return;
    }

    const std::unordered_set<std::string> variables =
        readPrefabVariableNames(package_path_);
    const bool use_size_variables = hasSizeVariable(variables);
    const bool use_capsule_size_variables =
        use_size_variables && usesCapsuleScaleVariables(package_path_);
    const bool use_cast_cycle_preview = usesCastCyclePreview(package_path_);
    const bool preserve_chromatic_helix =
        prefabContainsNodeName(package_path_, "red_fire_thread") &&
        prefabContainsNodeName(package_path_, "violet_shift_thread");

    for (const ScaleReference& reference : kScaleReferences) {
      prefabs::PrefabInstantiateDesc desc{};
      desc.assets = assets;
      desc.name_override = std::string{"Scale Preview "} + std::string{reference.name};
      desc.root_transform.setPosition(reference.position);
      const float effect_height = reference.height * kEffectHeightScale;
      const float effect_radius = reference.radius * kEffectRadiusScale;
      const float variable_height =
          use_capsule_size_variables ? reference.height : effect_height;
      const float variable_radius =
          use_capsule_size_variables ? reference.radius : effect_radius;
      const float medium_variable_radius =
          use_capsule_size_variables
              ? kMediumReferenceRadius
              : kMediumReferenceRadius * kEffectRadiusScale;
      const float cloud_emission_scale =
          variable_radius / medium_variable_radius;
      const float chromatic_axis_scale = effect_radius / kMediumReferenceRadius;
      const float chromatic_ray_length =
          kChromaticRayBaseLength * chromatic_axis_scale;

      if (use_size_variables) {
        setPrefabVariableIfDeclared(desc, variables, "height", variable_height);
        setPrefabVariableIfDeclared(desc, variables, "radius", variable_radius);
        setPrefabVariableIfDeclared(desc, variables, "width", variable_radius * 2.0f);
        setPrefabVariableIfDeclared(desc, variables, "length", variable_height);
      } else if (preserve_chromatic_helix) {
        desc.root_transform.setScale({1.0f, 1.0f, 1.0f});
      } else {
        const float horizontal_scale = effect_radius / kMediumReferenceRadius;
        const float vertical_scale = effect_height / kMediumReferenceHeight;
        desc.root_transform.setScale({horizontal_scale, vertical_scale, horizontal_scale});
      }

      const auto instance =
          prefabs::instantiatePrefab(*world, *scene, package_path_, desc);
      if (!instance.has_value()) {
        spdlog::error("Failed to instantiate '{}' generated particle package '{}'",
                      reference.name,
                      package_path_.string());
        continue;
      }
      applyBlueCloudEmissionScale(*instance, cloud_emission_scale);
      if (preserve_chromatic_helix) {
        applyChromaticRayLength(*instance, chromatic_ray_length);
      }
      if (use_cast_cycle_preview) {
        registerCastPreviewInstance(*instance);
      }
      spdlog::info("Loaded '{}' generated particle package '{}'",
                   reference.name,
                   package_path_.string());
    }
  }

  std::filesystem::path package_path_;
  PreviewOptions options_;
  world::Entity camera_entity_{};
  std::vector<CastPreviewInstance> cast_preview_instances_;
  std::optional<FireballSequencePreview> fireball_sequence_;
  float cast_preview_time_ = 0.0f;
  float fireball_sequence_time_ = 0.0f;
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
  config.window.title = kScaleReferencePreview ? "Karma Generated Particle Scale Preview"
                                               : "Karma Generated Particle Preview";
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

  engine.addRuntimeModule(std::make_unique<karma::visual::volumes::VolumeRuntimeModule>());

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
