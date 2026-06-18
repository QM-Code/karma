#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "demo_asset_paths.h"
#include "karma/features/ui/imgui/imgui_layer.h"
#include "karma/karma.h"
#include "karma/world/components/environment.h"

#include <imgui.h>

namespace karma::demo {

namespace {
std::filesystem::path resolveAssetPath(std::string_view relative) {
  std::filesystem::path candidate(relative);
  if (candidate.is_absolute() && std::filesystem::exists(candidate)) {
    return candidate;
  }
  std::filesystem::path cwd = std::filesystem::current_path();
  for (int depth = 0; depth < 6; ++depth) {
    const std::filesystem::path direct = cwd / candidate;
    if (std::filesystem::exists(direct)) {
      return direct;
    }
    const std::filesystem::path examples = cwd / "examples" / "assets" / candidate.filename();
    if (std::filesystem::exists(examples)) {
      return examples;
    }
    if (!cwd.has_parent_path()) {
      break;
    }
    cwd = cwd.parent_path();
  }
  return candidate;
}
}  // namespace

class DemoUiContent final {
 public:
  void draw(app::UIContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();

    if (!png_load_attempted_) {
      png_load_attempted_ = true;
      const auto png_path = resolveAssetPath("examples/assets/demo_image.png");
      png_texture_ = ctx.loadTextureRGBA8FromPng(png_path);
    }

    if (!svg_loaded_) {
      const auto svg_path = resolveAssetPath("examples/assets/demo_icon.svg");
      std::ifstream in(svg_path, std::ios::in | std::ios::binary);
      if (in) {
        svg_text_.assign(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
      }
      svg_loaded_ = true;
    }

    ImGui::Begin("Karma ImGui UI");
    ImGui::Text("Built-in Karma ImGui adapter");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::SliderFloat("Value", &slider_value_, 0.0f, 1.0f);
    ImGui::ColorEdit3("Tint", tint_);
    if (png_texture_) {
      ImGui::Text("PNG:");
      ImGui::Image(imgui::toTextureId(png_texture_.handle),
                   ImVec2(static_cast<float>(png_texture_.width),
                          static_cast<float>(png_texture_.height)));
    }
    ImGui::Separator();
    ImGui::Text("SVG loaded: %zu bytes", svg_text_.size());
    ImGui::End();
  }

 private:
  float slider_value_ = 0.25f;
  float tint_[3] = {0.2f, 0.7f, 0.9f};
  app::UITexture png_texture_{};
  std::string svg_text_;
  bool svg_loaded_ = false;
  bool png_load_attempted_ = false;
};

class DemoGame : public app::GameInterface {
 public:
  void onStart() override {
    input->bindKey("cam_forward", platform::Key::W);
    input->bindKey("cam_backward", platform::Key::S);
    input->bindKey("cam_left", platform::Key::A);
    input->bindKey("cam_right", platform::Key::D);
    input->bindMouse("cam_look", platform::MouseButton::Right);

    auto world_entity = world->createEntity();
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
        .mesh_key = resolveExampleAssetPath("world.glb").string()});
    world->add(world_entity, components::ColliderComponent::mesh());

    auto camera = world->createEntity();
    components::TransformComponent camera_xform{};
    camera_xform.setPosition({0.0f, 10.0f, 14.0f});
    const float pitch = -0.55f;
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
        .environment_map = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string(),
        .intensity = 0.6f,
        .draw_skybox = true});
  }

  void onFixedUpdate(float /*dt*/) override {}

  void onUpdate(float dt) override {
    if (!world->isAlive(camera_entity_)) {
      return;
    }
    const float look_sensitivity = 0.0008f;
    const float move_speed = 6.0f;
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

    math::Vec3 cam_pos = camera_xform.getPosition();
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
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::DemoGame game;

  auto ui_content = std::make_shared<karma::demo::DemoUiContent>();
  engine.setUi(karma::imgui::createUiLayer(
      [ui_content](karma::app::UIContext& ctx) { ui_content->draw(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Karma ImGui UI Demo";
  config.window.samples = 1;
  config.cursor_visible = true;
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
