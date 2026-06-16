#include "demo_asset_paths.h"

#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace karma::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTerrainSize = 2400.0f;

}  // namespace

class TerrainExample final : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    spawnTerrain();
    spawnLighting();
    spawnPrimaryCamera();
    spawnOffscreenCamera();
  }

  void onFixedUpdate(float) override {}

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    if (input->actionDown("cam_look")) {
      target_yaw_ -= input->mouseDeltaX() * 0.0008f;
      target_pitch_ -= input->mouseDeltaY() * 0.0008f;
    }
    target_pitch_ = std::clamp(target_pitch_, -1.45f, 1.1f);

    const float alpha = 1.0f - std::exp(-18.0f * dt);
    yaw_ += (target_yaw_ - yaw_) * alpha;
    pitch_ += (target_pitch_ - pitch_) * alpha;

    auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat camera_rotation = math::fromYawPitch(yaw_, pitch_);
    const math::Vec3 forward =
        math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    const math::Vec3 right = math::normalize(math::cross(forward, up));

    float forward_input = 0.0f;
    float right_input = 0.0f;
    float up_input = 0.0f;
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;
    if (input->actionDown("cam_up")) up_input += 1.0f;
    if (input->actionDown("cam_down")) up_input -= 1.0f;

    math::Vec3 position = camera_transform.getPosition();
    const float speed = input->actionDown("cam_look") ? 850.0f : 430.0f;
    position.x += (forward.x * forward_input + right.x * right_input) * speed * dt;
    position.y += (forward.y * forward_input + up.y * up_input) * speed * dt;
    position.z += (forward.z * forward_input + right.z * right_input) * speed * dt;
    position.y = std::max(position.y, 45.0f);

    camera_transform.setPosition(position);
    camera_transform.setRotation(camera_rotation);
  }

  void onShutdown() override {
    if (graphics && offscreen_target_ != renderer::kDefaultRenderTarget) {
      graphics->destroyRenderTarget(offscreen_target_);
      offscreen_target_ = renderer::kDefaultRenderTarget;
    }
  }

 private:
  void spawnTerrain() {
    const ecs::Entity terrain = world->createEntity();
    world->setName(terrain, "Heightmap Terrain");
    components::TransformComponent terrain_transform{};
    terrain_transform.setPosition({-kTerrainSize * 0.5f, 0.0f, -kTerrainSize * 0.5f});
    world->add(terrain, terrain_transform);
    world->add(terrain, components::TerrainComponent{
                            .source = components::TerrainSourceType::SingleImage,
                            .heatmap_image = resolveExampleAssetPath("Heightmap.png"),
                            .terrain_size = kTerrainSize,
                            .tile_resolution = 257u,
                            .origin_tile_x = 0,
                            .origin_tile_z = 0,
                            .height_scale = 420.0f,
                            .height_offset = -80.0f,
                            .base_patch_size = 16u,
                            .tessellation_factor = 32.0f,
                            .target_tessellated_edge_size = 18.0f,
                            .layer = 0u,
                            .visible = true,
                            .cpu_fallback_enabled = true,
                        });
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
    world->setName(camera_entity_, "Primary Flyover Camera");

    yaw_ = 0.72f;
    pitch_ = -0.22f;
    target_yaw_ = yaw_;
    target_pitch_ = pitch_;

    components::TransformComponent camera_transform{};
    camera_transform.setPosition({-760.0f, 360.0f, 920.0f});
    camera_transform.setRotation(math::fromYawPitch(yaw_, pitch_));
    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, components::CameraComponent{
                                  .near_clip = 2.0f,
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

  ecs::Entity camera_entity_{};
  renderer::RenderTargetId offscreen_target_ = renderer::kDefaultRenderTarget;
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  float target_yaw_ = 0.0f;
  float target_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  engine.addRuntimeModule(std::make_unique<karma::terrain::TerrainRuntimeModule>());
  karma::demo::TerrainExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Terrain Example";
  config.window.samples = 1;
  config.cursor_visible = true;
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
