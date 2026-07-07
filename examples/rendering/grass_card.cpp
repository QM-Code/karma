#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace karma::demo {
namespace {

constexpr const char* kGroundMeshKey = "examples/rendering/grass_card/ground_mesh";
constexpr const char* kGrassCardMeshKey = "examples/rendering/grass_card/grass_card_mesh";
constexpr const char* kGroundMaterialKey = "examples/rendering/grass_card/ground_material";
constexpr const char* kGrassMaterialKey = "examples/rendering/grass_card/grass_material";
constexpr const char* kGrassTextureKey = "examples/rendering/grass_card/grass_texture";

constexpr float kLookSensitivity = 0.0008f;
constexpr float kCameraMoveSpeed = 5.0f;
constexpr float kCameraBoostMultiplier = 3.0f;
constexpr float kCameraSmoothing = 20.0f;

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
               {1.0f, 0.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {half_width, 0.0f, half_depth},
               {0.0f, 1.0f, 0.0f},
               {1.0f, 1.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {-half_width, 0.0f, half_depth},
               {0.0f, 1.0f, 0.0f},
               {0.0f, 1.0f},
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

world::MeshData makeUprightPlane(float width, float height, std::string material_key) {
  const float half_width = width * 0.5f;
  world::MeshData mesh{};
  appendVertex(mesh,
               {-half_width, 0.0f, 0.0f},
               {0.0f, 0.0f, 1.0f},
               {0.0f, 1.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {half_width, 0.0f, 0.0f},
               {0.0f, 0.0f, 1.0f},
               {1.0f, 1.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {half_width, height, 0.0f},
               {0.0f, 0.0f, 1.0f},
               {1.0f, 0.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  appendVertex(mesh,
               {-half_width, height, 0.0f},
               {0.0f, 0.0f, 1.0f},
               {0.0f, 0.0f},
               {1.0f, 0.0f, 0.0f, 1.0f});
  mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
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

class GrassCardExample final : public app::GameInterface {
 public:
  void onStart() override {
    bindCameraControls();
    registerAssets();
    spawnScene();
    spawnEnvironment();
    spawnLighting();
    spawnCamera();

    spdlog::info(
        "Grass card controls: hold RMB to look, WASD to move, Q/E vertical, Left Shift to boost");
  }

  void onUpdate(float dt) override {
    updateCamera(dt);
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onShutdown() override {}

 private:
  void bindCameraControls() {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindKey("cam_up", platform::Key::E);
    input->bindKey("cam_down", platform::Key::Q);
    input->bindKey("cam_fast", platform::Key::LeftShift);
    input->bindMouse("cam_look", platform::MouseButton::Right);
  }

  void registerAssets() {
    environment_map_ = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr");

    rendering::MaterialDesc ground_material{};
    ground_material.base_color = {0.42f, 0.48f, 0.39f, 1.0f};
    ground_material.metallic = 0.0f;
    ground_material.roughness = 0.82f;
    assets->registerMaterialAsset(kGroundMaterialKey, ground_material);

    if (assets->findTextureAsset(kGrassTextureKey) == nullptr) {
      spdlog::error("Grass card texture '{}' was not available from the startup asset package",
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
    assets->registerMaterialAsset(kGrassMaterialKey, std::move(grass_material));

    assets->registerMeshAsset(kGroundMeshKey, makeGroundPlane(10.0f, 8.0f, kGroundMaterialKey));
    assets->registerMeshAsset(kGrassCardMeshKey, makeUprightPlane(2.65f, 2.4f, kGrassMaterialKey));
  }

  void spawnScene() {
    spawnMeshEntity(*world, "Ground Plane", kGroundMeshKey, {0.0f, 0.0f, 0.0f}, true);
    spawnMeshEntity(*world, "Double-Sided Grass Card", kGrassCardMeshKey, {0.0f, 0.0f, -0.6f}, false);
  }

  void spawnEnvironment() {
    const world::Entity environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
                               .environment_map_asset_key = environment_map_,
                               .intensity = 0.35f,
                               .draw_skybox = true,
                           });
  }

  void spawnLighting() {
    const world::Entity sun = world->createEntity();
    world->setName(sun, "Sun");
    components::TransformComponent sun_transform{};
    sun_transform.setPosition({0.0f, 8.0f, 0.0f});
    sun_transform.setRotation(math::fromYawPitch(0.65f, -0.85f));
    world->add(sun, sun_transform);
    world->add(sun, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 0.96f, 0.88f, 1.0f},
        .intensity = 1.15f,
        .casts_shadows = true,
        .shadow_extent = 16.0f,
    });

    const world::Entity fill = world->createEntity();
    world->setName(fill, "Soft Fill");
    components::TransformComponent fill_transform{};
    fill_transform.setPosition({-3.0f, 2.0f, 4.0f});
    world->add(fill, fill_transform);
    world->add(fill, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {0.50f, 0.64f, 1.0f, 1.0f},
        .intensity = 6.0f,
        .range = 8.0f,
    });
  }

  void spawnCamera() {
    camera_yaw_ = 0.0f;
    camera_pitch_ = -0.18f;
    target_camera_yaw_ = camera_yaw_;
    target_camera_pitch_ = camera_pitch_;

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    components::TransformComponent camera_transform{};
    camera_transform.setPosition({0.0f, 1.45f, 6.0f});
    camera_transform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera_entity_, camera_transform);
    world->add(camera_entity_, components::CameraComponent{
                                  .fov_y_degrees = 58.0f,
                                  .near_clip = 0.05f,
                                  .far_clip = 80.0f,
                                  .is_primary = true,
                              });
    world->add(camera_entity_, components::AudioListenerComponent{});
  }

  void updateCamera(float dt) {
    if (!world->isAlive(camera_entity_)) {
      return;
    }

    if (input->actionDown("cam_look")) {
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
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;
    if (input->actionDown("cam_up")) vertical_input += 1.0f;
    if (input->actionDown("cam_down")) vertical_input -= 1.0f;

    math::Vec3 movement = math::add(math::scale(forward, forward_input),
                                    math::scale(right, right_input));
    movement = math::add(movement, math::scale(world_up, vertical_input));
    if (math::lengthSquared(movement) > 0.0001f) {
      const float speed = kCameraMoveSpeed *
                          (input->actionDown("cam_fast") ? kCameraBoostMultiplier : 1.0f);
      movement = math::scale(math::normalize(movement), speed * dt);
      camera_transform.setPosition(math::add(camera_transform.getPosition(), movement));
    }
    camera_transform.setRotation(camera_rotation);
  }

  world::Entity camera_entity_{};
  std::string environment_map_;
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::GrassCardExample game;

  karma::app::EngineConfig config{};
  config.window.title = "Karma Grass Card Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.lighting_exposure = 1.0f;
  karma::rendering::PostProcessSettings post_process{};
  post_process.temporal_antialiasing_enabled = true;
  post_process.taa_feedback = 0.82f;
  post_process.taa_sharpening = 0.04f;
  config.default_frame_graph =
      karma::rendering::frameGraphFromPostProcessSettings(post_process);
  config.startup_asset_packages.push_back(
      karma::demo::resolveExampleAssetPath("rendering/grass_card"));

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
