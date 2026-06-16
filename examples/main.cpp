#include "demo_asset_paths.h"
#include "karma/karma.h"
#include "karma/world/components/environment.h"

#include <imgui.h>

namespace karma::demo {

namespace {
ImTextureID toImTextureId(karma::app::UITextureHandle handle) {
  return static_cast<ImTextureID>(handle);
}

karma::app::UITextureHandle fromImTextureId(ImTextureID id) {
  return static_cast<karma::app::UITextureHandle>(id);
}
}  // namespace

struct RadarOverlayState {
  app::UITextureHandle radar_texture = 0;
  int radar_width = 0;
  int radar_height = 0;
};

class RadarUiLayer final : public app::UiLayer {
 public:
  explicit RadarUiLayer(RadarOverlayState& state) : state_(state) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "karma";
    io.BackendRendererName = "karma_ui_draw";
  }

  ~RadarUiLayer() override = default;

  void onFrame(app::UIContext& ctx) override {
    pending_ctx_ = &ctx;
    ImGuiIO& io = ImGui::GetIO();
    const auto frame = ctx.frame();
    io.DisplaySize = ImVec2(static_cast<float>(frame.viewport_w), static_cast<float>(frame.viewport_h));
    io.DisplayFramebufferScale = ImVec2(frame.dpi_scale, frame.dpi_scale);
    io.DeltaTime = frame.dt > 0.0f ? frame.dt : (1.0f / 60.0f);

    if (!font_texture_) {
      unsigned char* pixels = nullptr;
      int width = 0;
      int height = 0;
      io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
      font_texture_ = ctx.createTextureRGBA8(width, height, pixels);
      io.Fonts->SetTexID(toImTextureId(font_texture_));
    }

    ImGui::NewFrame();
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::Begin("Radar", nullptr,
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize);
    if (state_.radar_texture != 0) {
      const float radar_size = 256.0f;
      ImGui::Image(toImTextureId(state_.radar_texture),
                   ImVec2(radar_size, radar_size),
                   ImVec2(0.0f, 1.0f),
                   ImVec2(1.0f, 0.0f));
    } else {
      ImGui::TextUnformatted("Radar target unavailable");
    }
    ImGui::End();
    ImGui::Render();

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) {
      return;
    }

    renderer::UIDrawData& out = ctx.drawData();
    out.clear();
    out.vertices.reserve(static_cast<size_t>(draw_data->TotalVtxCount));
    out.indices.reserve(static_cast<size_t>(draw_data->TotalIdxCount));
    out.commands.reserve(static_cast<size_t>(draw_data->CmdListsCount));

    int global_vtx_offset = 0;
    uint32_t global_idx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
      const ImDrawList* cmd_list = draw_data->CmdLists[n];
      for (int i = 0; i < cmd_list->VtxBuffer.Size; ++i) {
        const ImDrawVert& v = cmd_list->VtxBuffer[i];
        renderer::UIVertex out_v{};
        out_v.x = v.pos.x;
        out_v.y = v.pos.y;
        out_v.u = v.uv.x;
        out_v.v = v.uv.y;
        out_v.rgba = v.col;
        out.vertices.push_back(out_v);
      }
      for (int i = 0; i < cmd_list->IdxBuffer.Size; ++i) {
        out.indices.push_back(static_cast<uint32_t>(cmd_list->IdxBuffer[i] + global_vtx_offset));
      }
      for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
        const ImDrawCmd& cmd = cmd_list->CmdBuffer[cmd_i];
        if (cmd.UserCallback) {
          cmd.UserCallback(cmd_list, &cmd);
          global_idx_offset += cmd.ElemCount;
          continue;
        }
        ImVec4 clip = cmd.ClipRect;
        clip.x = (clip.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
        clip.y = (clip.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
        clip.z = (clip.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
        clip.w = (clip.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
        if (clip.z <= clip.x || clip.w <= clip.y) {
          global_idx_offset += cmd.ElemCount;
          continue;
        }

        renderer::UIDrawCmd out_cmd{};
        out_cmd.index_offset = global_idx_offset;
        out_cmd.index_count = cmd.ElemCount;
        out_cmd.scissor_enabled = true;
        out_cmd.scissor_x = static_cast<int>(clip.x);
        out_cmd.scissor_y = static_cast<int>(clip.y);
        out_cmd.scissor_w = static_cast<int>(clip.z - clip.x);
        out_cmd.scissor_h = static_cast<int>(clip.w - clip.y);
        out_cmd.texture = fromImTextureId(cmd.GetTexID());
        out.commands.push_back(out_cmd);
        global_idx_offset += cmd.ElemCount;
      }
      global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
  }

  void onShutdown() override {
    if (font_texture_ != 0) {
      if (pending_ctx_) {
        pending_ctx_->destroyTexture(font_texture_);
      }
      font_texture_ = 0;
    }
    ImGui::DestroyContext();
  }

 private:
  RadarOverlayState& state_;
  app::UITextureHandle font_texture_ = 0;
  app::UIContext* pending_ctx_ = nullptr;
};

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
    input->bindKey("player_reset", platform::Key::R, input::Trigger::Down);

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
        .mesh_key = resolveExampleAssetPath("world.glb").string()});
    world->add(world_entity, components::MeshColliderComponent{});
    
    auto player = world->createEntity();
    world->setName(player, "Player");
    world->add(player, components::TransformComponent{});
    world->add(player, components::MeshComponent{
        .mesh_key = resolveExampleAssetPath("tank_final.glb").string()});
    world->add(player, components::BoxColliderComponent{
        .center = {0.0f, 0.0f, 0.0f},
        .half_extents = {1.0f, 1.0f, 1.0f}});
    world->add(player, components::PlayerControllerComponent{});
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
      renderer::RenderTargetDesc radar_target_desc{};
      radar_target_desc.width = radar_target_size_;
      radar_target_desc.height = radar_target_size_;
      radar_target_desc.depth = true;
      radar_target_desc.stencil = false;
      radar_target_ = graphics->createRenderTarget(radar_target_desc);
      if (radar_state_ && radar_target_ != renderer::kDefaultRenderTarget) {
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
        .environment_map = resolveExampleAssetPath("golden_gate_hills_4k.hdr").string(),
        .intensity = 0.4f,
        .draw_skybox = true});
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
    if (!world->isAlive(player_entity_)) {
      return;
    }

    auto& player_input = world->get<components::PlayerControllerComponent>(player_entity_);

    float forward_input = 0.0f;
    if (input->actionDown("player_forward")) forward_input += 1.0f;
    if (input->actionDown("player_backward")) forward_input -= 1.0f;

    float turn_input = 0.0f;
    if (input->actionDown("player_turn_left")) turn_input += 1.0f;
    if (input->actionDown("player_turn_right")) turn_input -= 1.0f;

    math::Vec3 move_forward{0.0f, 0.0f, -1.0f};
    float vertical_velocity = 0.0f;
    if (physics) {
      if (auto* controller = physics->playerController()) {
        const glm::vec3 controller_forward = controller->getForwardVector();
        move_forward = math::normalize(math::Vec3{controller_forward.x, 0.0f, controller_forward.z});
        const glm::vec3 controller_velocity = controller->getVelocity();
        vertical_velocity = controller_velocity.y;
        controller->setAngularVelocity(glm::vec3{0.0f, turn_input * turn_speed_rad_, 0.0f});
      }
    }

    player_input.setDesiredVelocity({
        move_forward.x * forward_input * move_speed_,
        vertical_velocity,
        move_forward.z * forward_input * move_speed_});

    const bool reset_down = input->actionDown("player_reset");
    if (reset_down && !reset_down_prev_) {
      player_input.setDesiredVelocity({});
      if (physics) {
        if (auto* controller = physics->playerController()) {
          controller->setPosition(glm::vec3{0.0f, 8.0f, 0.0f});
          controller->setVelocity(glm::vec3{0.0f, 0.0f, 0.0f});
          controller->setAngularVelocity(glm::vec3{0.0f, 0.0f, 0.0f});
          controller->setRotation(glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
        }
      }
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
    if (graphics && radar_target_ != renderer::kDefaultRenderTarget) {
      graphics->destroyRenderTarget(radar_target_);
      radar_target_ = renderer::kDefaultRenderTarget;
    }
    if (radar_state_) {
      radar_state_->radar_texture = 0;
      radar_state_->radar_width = 0;
      radar_state_->radar_height = 0;
    }
  }

 private:
  RadarOverlayState* radar_state_ = nullptr;
  ecs::Entity camera_entity_{};
  ecs::Entity radar_camera_entity_{};
  ecs::Entity player_entity_{};
  renderer::RenderTargetId radar_target_ = renderer::kDefaultRenderTarget;
  int radar_target_size_ = 512;
  math::Vec3 camera_follow_offset_{0.0f, 10.0f, 18.0f};
  float camera_pitch_ = -0.5f;
  float move_speed_ = 8.0f;
  float turn_speed_rad_ = 2.4f;
  bool reset_down_prev_ = false;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::RadarOverlayState radar_state;
  karma::demo::DemoGame game(radar_state);
  engine.setUi(std::make_unique<karma::demo::RadarUiLayer>(radar_state));

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

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
