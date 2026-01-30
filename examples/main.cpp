#include <cmath>


#include "karma/karma.h"
#include "karma/components/environment.h"

namespace karma::demo {

class DemoGame : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);
    input->bindKey("tank_reset", platform::Key::R, input::Trigger::Down);

    auto world_entity = world->createEntity();
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{.mesh_key = "/home/quinn/Documents/bz3/data/common/models/world.glb"});
    world->add(world_entity, components::ColliderComponent{.shape = components::ColliderComponent::Shape::Mesh});

    auto tank = world->createEntity();
    world->add(tank, components::TransformComponent{});
    world->add(tank, components::MeshComponent{.mesh_key = "/home/quinn/Documents/bz3/data/common/models/tank_final.glb"});
    world->add(tank, components::ColliderComponent{
        .shape = components::ColliderComponent::Shape::Box,
        .half_extents = {1.0f, 1.0f, 2.0f}});
    world->add(tank, components::RigidbodyComponent{});
    components::AudioSourceComponent tank_audio{};
    tank_audio.clip_key = "/home/quinn/Documents/bz3/data/client/audio/fire.wav";
    tank_audio.gain = 1.0f;
    tank_audio.spatialized = false;
    world->add(tank, std::move(tank_audio));
    tank_entity_ = tank;

    auto camera = world->createEntity();
    components::TransformComponent camera_xform{};
    camera_xform.setPosition({0.0f, 12.0f, 12.0f});
    const float pitch = -0.65f;
    camera_pitch_ = pitch;
    target_camera_pitch_ = pitch;
    camera_yaw_ = 3.14159f;
    target_camera_yaw_ = 3.14159f;
    camera_xform.setRotation(math::fromYawPitch(camera_yaw_, camera_pitch_));
    world->add(camera, camera_xform);
    world->add(camera, components::CameraComponent{.is_primary = true});
    world->add(camera, components::AudioListenerComponent{});
    camera_entity_ = camera;

    auto light = world->createEntity();
    components::TransformComponent light_xform{};
    light_xform.setPosition({0.0f, 50.0f, 0.0f});
    light_xform.setRotation(math::fromYawPitch(0.5f, -0.9f));
    world->add(light, light_xform);
    world->add(light, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .intensity = 1.0f,
        .shadow_extent = 60.0f});

    auto environment = world->createEntity();
    world->add(environment, components::EnvironmentComponent{
        .environment_map = "/home/quinn/Documents/bz3/data/client/ludwikowice_farmland_4k.hdr",
        .intensity = 0.6f});
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
    const bool reset_down = input->actionDown("tank_reset");
    if (reset_down && !reset_down_prev_ && world->isAlive(tank_entity_)) {
      auto& tank_xform = world->get<components::TransformComponent>(tank_entity_);
      math::Vec3 pos = tank_xform.position();
      pos.y = 10.0f;
      auto& tank_body = world->get<components::RigidbodyComponent>(tank_entity_);
      tank_body.setPosition(pos);
      auto& tank_audio = world->get<components::AudioSourceComponent>(tank_entity_);
      tank_audio.play();
    }
    reset_down_prev_ = reset_down;
  }

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }
    const float look_sensitivity = 0.0008f;
    const float move_speed = 8.0f;
    const float smoothing = 20.0f;
    if (input->actionDown("cam_look")) {
      target_camera_yaw_ -= input->mouseDeltaX() * look_sensitivity;
      target_camera_pitch_ -= input->mouseDeltaY() * look_sensitivity;
    }
    if (target_camera_pitch_ > 1.55f) target_camera_pitch_ = 1.55f;
    if (target_camera_pitch_ < -1.55f) target_camera_pitch_ = -1.55f;

    const float alpha = 1.0f - std::exp(-smoothing * dt);
    camera_yaw_ += (target_camera_yaw_ - camera_yaw_) * alpha;
    camera_pitch_ += (target_camera_pitch_ - camera_pitch_) * alpha;

    auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat cam_rot = math::fromYawPitch(camera_yaw_, camera_pitch_);
    math::Vec3 forward = math::normalize(math::rotateVec(cam_rot, {0.0f, 0.0f, -1.0f}));
    const math::Vec3 up{0.0f, 1.0f, 0.0f};
    math::Vec3 right = math::normalize(math::cross(forward, up));

    float forward_input = 0.0f;
    float right_input = 0.0f;
    if (input->actionDown("cam_forward")) forward_input += 1.0f;
    if (input->actionDown("cam_backward")) forward_input -= 1.0f;
    if (input->actionDown("cam_right")) right_input += 1.0f;
    if (input->actionDown("cam_left")) right_input -= 1.0f;

    math::Vec3 cam_pos = camera_xform.position();
    cam_pos.x += (forward.x * forward_input + right.x * right_input) * move_speed * dt;
    cam_pos.y += (forward.y * forward_input) * move_speed * dt;
    cam_pos.z += (forward.z * forward_input + right.z * right_input) * move_speed * dt;
    camera_xform.setPosition(cam_pos);

    camera_xform.setRotation(cam_rot);

    if (graphics) {
      const float axis_len = 5.0f;
      graphics->drawLine(math::Vec3{0.0f, 0.0f, 0.0f}, math::Vec3{axis_len, 0.0f, 0.0f},
                         math::Color{1.0f, 0.0f, 0.0f, 1.0f});
      graphics->drawLine(math::Vec3{0.0f, 0.0f, 0.0f}, math::Vec3{0.0f, axis_len, 0.0f},
                         math::Color{0.0f, 1.0f, 0.0f, 1.0f});
      graphics->drawLine(math::Vec3{0.0f, 0.0f, 0.0f}, math::Vec3{0.0f, 0.0f, axis_len},
                         math::Color{0.0f, 0.0f, 1.0f, 1.0f});
    }
  }

  void onShutdown() override {}

 private:
  ecs::Entity camera_entity_{};
  ecs::Entity tank_entity_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
  bool reset_down_prev_ = false;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::DemoGame game;

  karma::app::EngineConfig config;
  config.window.title = "Karma Example";
  config.window.samples = 1;
  config.cursor_visible = false;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
