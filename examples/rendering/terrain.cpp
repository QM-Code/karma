#include "demo_asset_paths.h"

#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace karma::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTerrainSize = 2400.0f;
constexpr float kTerrainHeightScale = 420.0f * 0.25f;
constexpr float kTerrainHeightOffset = -80.0f * 0.25f;
constexpr math::Vec3 kPlayerStart{0.0f, 120.0f, 0.0f};
constexpr math::Vec3 kPlayerEyeOffset{0.0f, 1.78f, 0.0f};
constexpr float kPlayerHalfWidth = 0.42f;
constexpr float kPlayerHalfHeight = 1.0f;
constexpr float kWalkSpeed = 28.0f;
constexpr float kSprintSpeed = 95.0f;
constexpr float kJumpSpeed = 15.0f;
constexpr float kLookSensitivity = 0.00105f;
constexpr const char* kTerrainGroundMaterial = "terrain/example_ground";

}  // namespace

class TerrainExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("player_forward", platform::Key::W);
    input->bindKey("player_forward", platform::Key::Up);
    input->bindKey("player_backward", platform::Key::S);
    input->bindKey("player_backward", platform::Key::Down);
    input->bindKey("player_left", platform::Key::A);
    input->bindKey("player_left", platform::Key::Left);
    input->bindKey("player_right", platform::Key::D);
    input->bindKey("player_right", platform::Key::Right);
    input->bindKey("player_jump", platform::Key::Space);
    input->bindKey("player_sprint", platform::Key::LeftShift);
    input->bindKey("player_reset", platform::Key::R);

    yaw_ = 0.72f;
    pitch_ = -0.22f;

    registerTerrainMaterials();
    spawnTerrain();
    spawnLighting();
    spawnPlayer();
    spawnPrimaryCamera();
    spawnOffscreenCamera();
  }

  void onFixedUpdate(float) override {
    if (!world->isAlive(player_entity_)) {
      return;
    }

    auto& player_input = world->get<components::CharacterControllerComponent>(player_entity_);

    float forward_input = 0.0f;
    float right_input = 0.0f;
    if (input->actionDown("player_forward")) forward_input += 1.0f;
    if (input->actionDown("player_backward")) forward_input -= 1.0f;
    if (input->actionDown("player_right")) right_input += 1.0f;
    if (input->actionDown("player_left")) right_input -= 1.0f;

    const math::Quat yaw_rotation = math::fromYawPitch(yaw_, 0.0f);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(yaw_rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 right = math::normalize(math::cross(forward, {0.0f, 1.0f, 0.0f}));

    math::Vec3 move{
        forward.x * forward_input + right.x * right_input,
        0.0f,
        forward.z * forward_input + right.z * right_input,
    };
    if (math::lengthSquared(move) > 0.0001f) {
      move = math::normalize(move);
    }

    float vertical_velocity = player_input.velocity.y;
    const bool grounded = player_input.grounded;
    auto& player_transform = world->get<components::TransformComponent>(player_entity_);
    player_transform.setRotation(math::fromYawPitch(yaw_, 0.0f));

    const bool jump_down = input->actionDown("player_jump");
    if (jump_down && !jump_down_prev_ && grounded) {
      vertical_velocity = kJumpSpeed;
    }
    jump_down_prev_ = jump_down;

    const float speed = input->actionDown("player_sprint") ? kSprintSpeed : kWalkSpeed;
    player_input.setDesiredVelocity({
        move.x * speed,
        vertical_velocity,
        move.z * speed,
    });

    const bool reset_down = input->actionDown("player_reset");
    if (reset_down && !reset_down_prev_) {
      resetPlayer();
    }
    reset_down_prev_ = reset_down;
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_) || !world->isAlive(player_entity_)) {
      return;
    }

    (void)dt;
    yaw_ -= input->mouseDeltaX() * kLookSensitivity;
    pitch_ = std::clamp(pitch_ - input->mouseDeltaY() * kLookSensitivity, -1.45f, 1.25f);

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat camera_rotation = math::fromYawPitch(yaw_, pitch_);
    const auto& player_transform = world->get<components::TransformComponent>(player_entity_);
    const math::Vec3 player_position =
        player_transform.getInterpolatedPosition(renderInterpolationAlpha());
    camera_transform.setPosition(math::add(player_position, kPlayerEyeOffset));
    camera_transform.setRotation(camera_rotation);
  }

  void onShutdown() override {
    if (graphics && offscreen_target_ != renderer::kDefaultRenderTarget) {
      graphics->destroyRenderTarget(offscreen_target_);
      offscreen_target_ = renderer::kDefaultRenderTarget;
    }
  }

 private:
  void registerTerrainMaterials() {
    if (!assets) {
      return;
    }

    renderer::MaterialDesc ground{};
    ground.base_color = math::Color{0.32f, 0.45f, 0.28f, 1.0f};
    ground.roughness = 0.92f;
    ground.metallic = 0.0f;
    assets->registerMaterialAsset(kTerrainGroundMaterial, ground);
  }

  void spawnTerrain() {
    const ecs::Entity terrain = world->createEntity();
    world->setName(terrain, "Heightmap Terrain");
    components::TransformComponent terrain_transform{};
    terrain_transform.setPosition({-kTerrainSize * 0.5f, 0.0f, -kTerrainSize * 0.5f});
    world->add(terrain, terrain_transform);
    world->add(terrain, components::ColliderComponent{});
    world->add(terrain, components::TerrainComponent{
                            .source = components::TerrainSourceType::SingleImage,
                            .heatmap_image = resolveExampleAssetPath("Heightmap.png"),
                            .height_format = components::TerrainHeightFormat::ImageFile,
                            .material_layers =
                                {
                                    components::TerrainMaterialLayer{
                                        .name = "ground",
                                        .material_key = kTerrainGroundMaterial,
                                        .uv_scale = 32.0f,
                                    },
                                },
                            .terrain_size = kTerrainSize,
                            .tile_resolution = 257u,
                            .origin_tile_x = 0,
                            .origin_tile_z = 0,
                            .height_scale = kTerrainHeightScale,
                            .height_offset = kTerrainHeightOffset,
                            .base_patch_size = 16u,
                            .tessellation_factor = 32.0f,
                            .target_tessellated_edge_size = 10.0f,
                            .layer = 0u,
                            .visible = true,
                            .cpu_fallback_enabled = true,
                        });
  }

  void spawnPlayer() {
    player_entity_ = world->createEntity();
    world->setName(player_entity_, "Terrain Player");

    components::TransformComponent player_transform{};
    player_transform.setPosition(kPlayerStart);
    player_transform.setRotation(math::fromYawPitch(yaw_, 0.0f));
    world->add(player_entity_, player_transform);
    world->add(player_entity_,
               components::ColliderComponent::box(components::BoxColliderShape{
                   .center = {0.0f, kPlayerHalfHeight, 0.0f},
                   .half_extents = {kPlayerHalfWidth, kPlayerHalfHeight, kPlayerHalfWidth},
               }));
    world->add(player_entity_, components::CharacterControllerComponent{});
    world->add(player_entity_, components::GroundContactComponent{});
  }

  void spawnLighting() {
    const ecs::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 900.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.72f, -0.88f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
                        .type = components::LightComponent::Type::Directional,
                        .color = {1.0f, 0.96f, 0.88f, 1.0f},
                        .intensity = 1.15f,
                        .casts_shadows = false,
                    });

    const ecs::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                                .intensity = 0.09f,
                                .draw_skybox = false,
                            });
  }

  void spawnPrimaryCamera() {
    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Player Camera");

    components::TransformComponent camera_transform{};
    camera_transform.setPosition(math::add(kPlayerStart, kPlayerEyeOffset));
    camera_transform.setRotation(math::fromYawPitch(yaw_, pitch_));
    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, components::CameraComponent{
                                  .near_clip = 0.08f,
                                  .far_clip = 12000.0f,
                                  .is_primary = true,
                              });
  }

  void spawnOffscreenCamera() {
    if (graphics) {
      renderer::RenderTargetDesc desc{};
      desc.width = 512;
      desc.height = 512;
      desc.depth = true;
      desc.stencil = false;
      offscreen_target_ = graphics->createRenderTarget(desc);
    }

    const ecs::Entity camera = world->createEntity();
    world->setName(camera, "Terrain Offscreen Camera");
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 1900.0f, 0.0f});
    camera_transform.setRotation(math::fromYawPitch(0.0f, -kPi * 0.5f));
    world->add(camera, camera_transform);
    world->add(camera, components::CameraComponent{
                           .perspective = false,
                           .render_shadows = false,
                           .near_clip = 10.0f,
                           .far_clip = 3600.0f,
                           .ortho_left = -kTerrainSize * 0.55f,
                           .ortho_right = kTerrainSize * 0.55f,
                           .ortho_top = kTerrainSize * 0.55f,
                           .ortho_bottom = -kTerrainSize * 0.55f,
                           .is_primary = false,
                           .render_to_texture = true,
                           .render_target = offscreen_target_,
                           .render_target_key = "terrain_overview",
                       });
  }

  void resetPlayer() {
    if (!world->isAlive(player_entity_)) {
      return;
    }

    auto& player_input = world->get<components::CharacterControllerComponent>(player_entity_);
    player_input.setDesiredVelocity({});
    player_input.setAddVelocity({});
    player_input.setDesiredAngularVelocity({});

    const math::Quat player_rotation = math::fromYawPitch(yaw_, 0.0f);
    auto& player_transform = world->get<components::TransformComponent>(player_entity_);
    player_transform.setPosition(kPlayerStart);
    player_transform.setRotation(player_rotation);

    player_input.velocity = {};
    player_input.angular_velocity = {};
    player_input.grounded = false;
  }

  ecs::Entity player_entity_{};
  ecs::Entity camera_entity_{};
  renderer::RenderTargetId offscreen_target_ = renderer::kDefaultRenderTarget;
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  bool jump_down_prev_ = false;
  bool reset_down_prev_ = false;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  engine.addRuntimeModule(std::make_unique<karma::terrain::TerrainRuntimeModule>());
  karma::demo::TerrainExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Terrain Example";
  config.window.samples = 1;
  config.cursor_visible = false;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 64;
  config.shadow_map_size = 1024;
  config.lighting_exposure = 1.0f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
