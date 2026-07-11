#include "demo_asset_paths.h"
#include "karma/karma.h"
#include "karma/components.h"

#include <array>
#include <cstdio>
#include <string>

#if defined(KARMA_ENABLE_IMGUI)
#include "karma/ui_imgui.h"
#endif

namespace karma::demo {

struct RadarOverlayState {
  app::UITextureHandle radar_texture = 0;
  int radar_width = 0;
  int radar_height = 0;
};

#if defined(KARMA_ENABLE_IMGUI)
std::unique_ptr<app::UiLayer> createRadarImGuiLayer(RadarOverlayState& state) {
  return ui::imgui::createUiLayer(
      [&state](app::UIContext&) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
        ImGui::Begin("Radar", nullptr,
                     ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize);
        if (state.radar_texture != 0) {
          constexpr float radar_size = 256.0f;
          ImGui::Image(ui::imgui::toTextureId(state.radar_texture),
                       ImVec2(radar_size, radar_size),
                       ImVec2(0.0f, 1.0f),
                       ImVec2(1.0f, 0.0f));
        } else {
          ImGui::TextUnformatted("Radar target unavailable");
        }
        ImGui::End();
      });
}
#endif

class DemoGame : public app::GameInterface {
 public:
  explicit DemoGame(RadarOverlayState& radar_state) : radar_state_(&radar_state) {}

  void onStart() override {
    input->bindKey("player_forward", platform::Key::W);
    input->bindKey("player_forward", platform::Key::Up);
    input->bindKey("player_backward", platform::Key::S);
    input->bindKey("player_backward", platform::Key::Down);
    input->bindKey("player_turn_left", platform::Key::A);
    input->bindKey("player_turn_left", platform::Key::Left);
    input->bindKey("player_turn_right", platform::Key::D);
    input->bindKey("player_turn_right", platform::Key::Right);
    input->bindKey("player_reset", platform::Key::R, app::Trigger::Down);

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
        .mesh_asset_key = importExampleMeshAsset(assets, "world.glb")});
    world->add(world_entity, components::ColliderComponent::mesh());

    auto player = world->createEntity();
    world->setName(player, "Player");
    world->add(player, components::TransformComponent{});
    world->add(player, components::MeshComponent{
        .mesh_asset_key = importExampleMeshAsset(assets, "tank_final.glb")});
    world->add(player,
               components::ColliderComponent::box(
                   components::BoxColliderShape{
                       .center = {0.0f, 1.0f, 0.0f},
                       .half_extents = {1.0f, 1.0f, 1.0f}}));
    world->add(player, components::CharacterControllerComponent{});
    player_entity_ = player;

    auto camera = world->createEntity();
    world->setName(camera, "Camera");
    components::TransformComponent camera_xform{};
    camera_xform.setPosition(camera_follow_offset_);
    camera_xform.setRotation(math::fromYawPitch(0.0f, camera_pitch_));
    world->add(camera, camera_xform);
    world->add(camera, components::CameraComponent{.is_primary = true});
    world->add(camera, components::AudioListenerComponent{});
    camera_entity_ = camera;

    if (graphics) {
      rendering::RenderTargetDesc radar_target_desc{};
      radar_target_desc.width = radar_target_size_;
      radar_target_desc.height = radar_target_size_;
      radar_target_desc.depth = true;
      radar_target_desc.stencil = false;
      radar_target_ = graphics->createRenderTarget(radar_target_desc);
      if (radar_state_ && radar_target_ != rendering::kDefaultRenderTarget) {
        radar_state_->radar_texture =
            static_cast<app::UITextureHandle>(graphics->getRenderTargetTextureId(radar_target_));
        radar_state_->radar_width = radar_target_size_;
        radar_state_->radar_height = radar_target_size_;
      }
    }

    auto radar_camera = world->createEntity();
    world->setName(radar_camera, "Radar Camera");
    components::TransformComponent radar_camera_xform{};
    radar_camera_xform.setPosition({0.0f, 60.0f, 0.0f});
    radar_camera_xform.setRotation(math::fromYawPitch(0.0f, -1.5702f));
    world->add(radar_camera, radar_camera_xform);
    world->add(radar_camera, components::CameraComponent{
        .perspective = false,
        .render_shadows = false,
        .near_clip = 1.0f,
        .far_clip = 200.0f,
        .ortho_left = -35.0f,
        .ortho_right = 35.0f,
        .ortho_top = 35.0f,
        .ortho_bottom = -35.0f,
        .is_primary = false,
        .render_to_texture = true,
        .render_target = radar_target_,
        .render_target_key = "radar",
        .shader_override_vertex_path = resolveExampleShaderPath("radar_override_vs.hlsl").string(),
        .shader_override_fragment_path =
            resolveExampleShaderPath("radar_override_ps.hlsl").string(),
        .shader_user_params = {
            {"height_range", {-2.0f, 20.0f, 0.0f, 0.0f}},
            {"low_color", {0.07f, 0.30f, 0.95f, 1.0f}},
            {"high_color", {1.0f, 0.28f, 0.08f, 1.0f}}
        }});
    radar_camera_entity_ = radar_camera;

    auto light = world->createEntity();
    world->setName(light, "Sun Light");
    components::TransformComponent light_xform{};
    light_xform.setPosition({0.0f, 50.0f, 0.0f});
    light_xform.setRotation(math::fromYawPitch(0.5f, -0.9f));
    world->add(light, light_xform);
    world->add(light, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .intensity = 1.6f,
        .casts_shadows = true,
        .shadow_extent = 60.0f});

    auto point_warm = world->createEntity();
    world->setName(point_warm, "Point Light Warm");
    components::TransformComponent point_warm_xform{};
    point_warm_xform.setPosition({-9.0f, 4.0f, 1.0f});
    world->add(point_warm, point_warm_xform);
    world->add(point_warm, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {1.0f, 0.65f, 0.35f, 1.0f},
        .intensity = 28.0f,
        .range = 24.0f,
        .casts_shadows = true});

    auto point_cool = world->createEntity();
    world->setName(point_cool, "Point Light Cool");
    components::TransformComponent point_cool_xform{};
    point_cool_xform.setPosition({8.0f, 3.5f, -6.0f});
    world->add(point_cool, point_cool_xform);
    world->add(point_cool, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {0.35f, 0.6f, 1.0f, 1.0f},
        .intensity = 24.0f,
        .range = 22.0f,
        .casts_shadows = true});

    auto point_fill = world->createEntity();
    world->setName(point_fill, "Point Light Fill");
    components::TransformComponent point_fill_xform{};
    point_fill_xform.setPosition({0.0f, 6.0f, 12.0f});
    world->add(point_fill, point_fill_xform);
    world->add(point_fill, components::LightComponent{
        .type = components::LightComponent::Type::Point,
        .color = {0.55f, 1.0f, 0.7f, 1.0f},
        .intensity = 16.0f,
        .range = 26.0f});

    auto skybox = world->createEntity();
    world->setName(skybox, "Environment");
    world->add(skybox, components::EnvironmentComponent{
        .environment_map_asset_key = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
        .intensity = 0.4f,
        .draw_skybox = true});

#if defined(KARMA_ENABLE_NATIVE_UI)
    openNativeHud();
#endif
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
    if (!world->isAlive(player_entity_)) {
      return;
    }

    auto& player_input = world->get<components::CharacterControllerComponent>(player_entity_);

    float forward_input = 0.0f;
    if (input->actionDown("player_forward")) forward_input += 1.0f;
    if (input->actionDown("player_backward")) forward_input -= 1.0f;

    float turn_input = 0.0f;
    if (input->actionDown("player_turn_left")) turn_input += 1.0f;
    if (input->actionDown("player_turn_right")) turn_input -= 1.0f;

    math::Vec3 move_forward = math::normalize(
        math::Vec3{player_input.forward.x, 0.0f, player_input.forward.z});
    if (math::lengthSquared(move_forward) <= 0.0001f) {
      move_forward = {0.0f, 0.0f, -1.0f};
    }
    const float vertical_velocity = player_input.velocity.y;
    player_input.setDesiredAngularVelocity({0.0f, turn_input * turn_speed_rad_, 0.0f});

    player_input.setDesiredVelocity({
        move_forward.x * forward_input * move_speed_,
        vertical_velocity,
        move_forward.z * forward_input * move_speed_});

    const bool reset_down = input->actionDown("player_reset");
    if (reset_down && !reset_down_prev_) {
      player_input.setDesiredVelocity({});
      player_input.setDesiredAngularVelocity({});
      player_input.setAddVelocity({});
      auto& player_xform = world->get<components::TransformComponent>(player_entity_);
      player_xform.setPosition({0.0f, 8.0f, 0.0f});
      player_xform.setRotation({});
      if (world->has<components::AudioSourceComponent>(player_entity_)) {
        auto& player_audio = world->get<components::AudioSourceComponent>(player_entity_);
        player_audio.play();
      }
    }
    reset_down_prev_ = reset_down;
  }

  void onUpdate(float dt) override {
    (void)dt;
    if (world->isAlive(camera_entity_) && world->isAlive(player_entity_)) {
      auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
      const auto& player_xform = world->get<components::TransformComponent>(player_entity_);
      const math::Vec3 player_pos =
          player_xform.getInterpolatedPosition(renderInterpolationAlpha());
      camera_xform.setPosition({player_pos.x + camera_follow_offset_.x,
                                player_pos.y + camera_follow_offset_.y,
                                player_pos.z + camera_follow_offset_.z});
      camera_xform.setRotation(math::fromYawPitch(0.0f, camera_pitch_));
    }

    if (world->isAlive(radar_camera_entity_) && world->isAlive(player_entity_)) {
      auto& radar_xform = world->get<components::TransformComponent>(radar_camera_entity_);
      const auto& player_xform = world->get<components::TransformComponent>(player_entity_);
      const math::Vec3 player_pos =
          player_xform.getInterpolatedPosition(renderInterpolationAlpha());
      radar_xform.setPosition({player_pos.x, 60.0f, player_pos.z});
      radar_xform.setRotation(math::fromYawPitch(0.0f, -1.5702f));
    }

#if defined(KARMA_ENABLE_NATIVE_UI)
    updateNativeHud();
#endif

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

  void onShutdown() override {
#if defined(KARMA_ENABLE_NATIVE_UI)
    if (ui != nullptr && hud_) {
      ui->close(hud_);
      hud_ = {};
      radar_element_ = {};
    }
#endif
    if (graphics && radar_target_ != rendering::kDefaultRenderTarget) {
      graphics->destroyRenderTarget(radar_target_);
      radar_target_ = rendering::kDefaultRenderTarget;
    }
    if (radar_state_) {
      radar_state_->radar_texture = 0;
      radar_state_->radar_width = 0;
      radar_state_->radar_height = 0;
    }
  }

 private:
#if defined(KARMA_ENABLE_NATIVE_UI)
  void openNativeHud() {
    if (ui == nullptr) return;
    const auto opened = ui->open("ui/pilots/tank-hud", {.layer = 90});
    hud_ = opened.document;
    if (!hud_) return;
    radar_element_ = ui->findById(hud_, "radar-image");
    const char* radar_status = "UNAVAILABLE";
    if (radar_element_ && radar_target_ != rendering::kDefaultRenderTarget) {
      ui->setImage(radar_element_, ui::ImageSource::renderTarget(radar_target_));
      radar_status = "LIVE";
    }
    ui->setMany(hud_, {{"radar.status", radar_status},
                       {"telemetry.position", "Position: --"},
                       {"telemetry.speed", "Speed: --"}});
  }

  static std::string telemetryText(const char* label,
                                   const math::Vec3& value,
                                   bool magnitude_only = false) {
    std::array<char, 128> text{};
    if (magnitude_only) {
      std::snprintf(text.data(), text.size(), "%s: %.1f", label, math::length(value));
    } else {
      std::snprintf(text.data(), text.size(), "%s: %.1f  %.1f  %.1f", label,
                    value.x, value.y, value.z);
    }
    return text.data();
  }

  void updateNativeHud() {
    if (ui == nullptr || !hud_ || !world->isAlive(player_entity_)) return;
    const auto& transform = world->get<components::TransformComponent>(player_entity_);
    const auto& controller = world->get<components::CharacterControllerComponent>(player_entity_);
    ui->setMany(
        hud_,
        {{"telemetry.position",
          telemetryText("Position", transform.getInterpolatedPosition(
                                        renderInterpolationAlpha()))},
         {"telemetry.speed", telemetryText("Speed", controller.velocity, true)}});
  }
#endif

  RadarOverlayState* radar_state_ = nullptr;
  world::Entity camera_entity_{};
  world::Entity radar_camera_entity_{};
  world::Entity player_entity_{};
  rendering::RenderTargetId radar_target_ = rendering::kDefaultRenderTarget;
  int radar_target_size_ = 512;
  math::Vec3 camera_follow_offset_{0.0f, 10.0f, 18.0f};
  float camera_pitch_ = -0.5f;
  float move_speed_ = 8.0f;
  float turn_speed_rad_ = 2.4f;
  bool reset_down_prev_ = false;
#if defined(KARMA_ENABLE_NATIVE_UI)
  ui::DocumentHandle hud_{};
  ui::ElementHandle radar_element_{};
#endif
};

}  // namespace karma::demo

int main() {
  karma::demo::RadarOverlayState radar_state;
  karma::app::EngineApp engine;
  karma::demo::DemoGame game(radar_state);
#if defined(KARMA_ENABLE_IMGUI) && !defined(KARMA_ENABLE_NATIVE_UI)
  engine.setUi(karma::demo::createRadarImGuiLayer(radar_state));
#endif

  karma::app::EngineConfig config;
  config.window.title = "Karma Example";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.shadow_bias = 0.0006f;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;
#if defined(KARMA_ENABLE_NATIVE_UI)
  config.startup_asset_packages.push_back(
      karma::demo::resolveExamplePath("examples/assets/ui/tank_hud"));
#endif

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
