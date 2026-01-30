#include <cmath>
#include <cstdint>


#include "karma/karma.h"
#include "karma/components/environment.h"

#include <imgui.h>

namespace karma::demo {

namespace {
ImGuiKey toImGuiKey(platform::Key key) {
  switch (key) {
    case platform::Key::Tab: return ImGuiKey_Tab;
    case platform::Key::Left: return ImGuiKey_LeftArrow;
    case platform::Key::Right: return ImGuiKey_RightArrow;
    case platform::Key::Up: return ImGuiKey_UpArrow;
    case platform::Key::Down: return ImGuiKey_DownArrow;
    case platform::Key::PageUp: return ImGuiKey_PageUp;
    case platform::Key::PageDown: return ImGuiKey_PageDown;
    case platform::Key::Home: return ImGuiKey_Home;
    case platform::Key::End: return ImGuiKey_End;
    case platform::Key::Insert: return ImGuiKey_Insert;
    case platform::Key::Delete: return ImGuiKey_Delete;
    case platform::Key::Backspace: return ImGuiKey_Backspace;
    case platform::Key::Space: return ImGuiKey_Space;
    case platform::Key::Enter: return ImGuiKey_Enter;
    case platform::Key::Escape: return ImGuiKey_Escape;
    case platform::Key::Apostrophe: return ImGuiKey_Apostrophe;
    case platform::Key::Minus: return ImGuiKey_Minus;
    case platform::Key::Equal: return ImGuiKey_Equal;
    case platform::Key::LeftBracket: return ImGuiKey_LeftBracket;
    case platform::Key::RightBracket: return ImGuiKey_RightBracket;
    case platform::Key::GraveAccent: return ImGuiKey_GraveAccent;
    case platform::Key::CapsLock: return ImGuiKey_CapsLock;
    case platform::Key::ScrollLock: return ImGuiKey_ScrollLock;
    case platform::Key::NumLock: return ImGuiKey_NumLock;
    case platform::Key::LeftShift: return ImGuiKey_LeftShift;
    case platform::Key::LeftControl: return ImGuiKey_LeftCtrl;
    case platform::Key::LeftAlt: return ImGuiKey_LeftAlt;
    case platform::Key::LeftSuper: return ImGuiKey_LeftSuper;
    case platform::Key::RightShift: return ImGuiKey_RightShift;
    case platform::Key::RightControl: return ImGuiKey_RightCtrl;
    case platform::Key::RightAlt: return ImGuiKey_RightAlt;
    case platform::Key::RightSuper: return ImGuiKey_RightSuper;
    case platform::Key::Menu: return ImGuiKey_Menu;
    case platform::Key::Num0: return ImGuiKey_0;
    case platform::Key::Num1: return ImGuiKey_1;
    case platform::Key::Num2: return ImGuiKey_2;
    case platform::Key::Num3: return ImGuiKey_3;
    case platform::Key::Num4: return ImGuiKey_4;
    case platform::Key::Num5: return ImGuiKey_5;
    case platform::Key::Num6: return ImGuiKey_6;
    case platform::Key::Num7: return ImGuiKey_7;
    case platform::Key::Num8: return ImGuiKey_8;
    case platform::Key::Num9: return ImGuiKey_9;
    case platform::Key::A: return ImGuiKey_A;
    case platform::Key::B: return ImGuiKey_B;
    case platform::Key::C: return ImGuiKey_C;
    case platform::Key::D: return ImGuiKey_D;
    case platform::Key::E: return ImGuiKey_E;
    case platform::Key::F: return ImGuiKey_F;
    case platform::Key::G: return ImGuiKey_G;
    case platform::Key::H: return ImGuiKey_H;
    case platform::Key::I: return ImGuiKey_I;
    case platform::Key::J: return ImGuiKey_J;
    case platform::Key::K: return ImGuiKey_K;
    case platform::Key::L: return ImGuiKey_L;
    case platform::Key::M: return ImGuiKey_M;
    case platform::Key::N: return ImGuiKey_N;
    case platform::Key::O: return ImGuiKey_O;
    case platform::Key::P: return ImGuiKey_P;
    case platform::Key::Q: return ImGuiKey_Q;
    case platform::Key::R: return ImGuiKey_R;
    case platform::Key::S: return ImGuiKey_S;
    case platform::Key::T: return ImGuiKey_T;
    case platform::Key::U: return ImGuiKey_U;
    case platform::Key::V: return ImGuiKey_V;
    case platform::Key::W: return ImGuiKey_W;
    case platform::Key::X: return ImGuiKey_X;
    case platform::Key::Y: return ImGuiKey_Y;
    case platform::Key::Z: return ImGuiKey_Z;
    case platform::Key::F1: return ImGuiKey_F1;
    case platform::Key::F2: return ImGuiKey_F2;
    case platform::Key::F3: return ImGuiKey_F3;
    case platform::Key::F4: return ImGuiKey_F4;
    case platform::Key::F5: return ImGuiKey_F5;
    case platform::Key::F6: return ImGuiKey_F6;
    case platform::Key::F7: return ImGuiKey_F7;
    case platform::Key::F8: return ImGuiKey_F8;
    case platform::Key::F9: return ImGuiKey_F9;
    case platform::Key::F10: return ImGuiKey_F10;
    case platform::Key::F11: return ImGuiKey_F11;
    case platform::Key::F12: return ImGuiKey_F12;
    default: return ImGuiKey_None;
  }
}

int toImGuiMouseButton(platform::MouseButton button) {
  switch (button) {
    case platform::MouseButton::Left: return 0;
    case platform::MouseButton::Right: return 1;
    case platform::MouseButton::Middle: return 2;
    case platform::MouseButton::Button4: return 3;
    case platform::MouseButton::Button5: return 4;
    default: return -1;
  }
}

void applyModifierState(ImGuiIO& io, const platform::Modifiers& mods) {
  io.AddKeyEvent(ImGuiKey_LeftShift, mods.shift);
  io.AddKeyEvent(ImGuiKey_LeftCtrl, mods.control);
  io.AddKeyEvent(ImGuiKey_LeftAlt, mods.alt);
  io.AddKeyEvent(ImGuiKey_LeftSuper, mods.super);
}

ImTextureID toImTextureId(karma::app::UITextureHandle handle) {
  return static_cast<ImTextureID>(handle);
}

karma::app::UITextureHandle fromImTextureId(ImTextureID id) {
  return static_cast<karma::app::UITextureHandle>(id);
}
}  // namespace

class ImGuiUiLayer final : public karma::app::UiLayer {
 public:
  ImGuiUiLayer() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "karma";
    io.BackendRendererName = "karma_ui_draw";
  }

  ~ImGuiUiLayer() override = default;

  void onEvent(const platform::Event& event) override {
    ImGuiIO& io = ImGui::GetIO();
    applyModifierState(io, event.mods);
    switch (event.type) {
      case platform::EventType::KeyDown:
      case platform::EventType::KeyUp: {
        const ImGuiKey key = toImGuiKey(event.key);
        if (key != ImGuiKey_None) {
          io.AddKeyEvent(key, event.type == platform::EventType::KeyDown);
        }
        break;
      }
      case platform::EventType::TextInput:
        if (event.codepoint != 0) {
          io.AddInputCharacter(static_cast<unsigned int>(event.codepoint));
        }
        break;
      case platform::EventType::MouseButtonDown:
      case platform::EventType::MouseButtonUp: {
        const int button = toImGuiMouseButton(event.mouseButton);
        if (button >= 0) {
          io.AddMouseButtonEvent(button, event.type == platform::EventType::MouseButtonDown);
        }
        break;
      }
      case platform::EventType::MouseMove:
        io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
        break;
      case platform::EventType::MouseScroll:
        io.AddMouseWheelEvent(static_cast<float>(event.scrollX), static_cast<float>(event.scrollY));
        break;
      case platform::EventType::WindowFocus:
        io.AddFocusEvent(event.focused);
        break;
      default:
        break;
    }
  }

  void onFrame(karma::app::UIContext& ctx) override {
    pending_ctx_ = &ctx;
    ImGuiIO& io = ImGui::GetIO();
    const auto frame = ctx.frame();
    io.DisplaySize = ImVec2(static_cast<float>(frame.viewport_w),
                            static_cast<float>(frame.viewport_h));
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
    ImGui::Begin("Karma ImGui UI");
    ImGui::Text("ImGui draw data -> Karma UI bridge");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::SliderFloat("Value", &slider_value_, 0.0f, 1.0f);
    ImGui::ColorEdit3("Tint", tint_);
    ImGui::End();
    ImGui::Render();

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) {
      return;
    }

    karma::app::UIDrawData& out = ctx.drawData();
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
        karma::app::UIVertex out_v{};
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

        karma::app::UIDrawCmd out_cmd{};
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
  float slider_value_ = 0.25f;
  float tint_[3] = {0.2f, 0.7f, 0.9f};
  karma::app::UITextureHandle font_texture_ = 0;
  karma::app::UIContext* pending_ctx_ = nullptr;
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
    world->add(world_entity, components::MeshComponent{.mesh_key = "/home/quinn/Documents/bz3/data/common/models/world.glb"});
    world->add(world_entity, components::ColliderComponent{.shape = components::ColliderComponent::Shape::Mesh});

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
        .environment_map = "/home/quinn/Documents/bz3/data/client/ludwikowice_farmland_4k.hdr",
        .intensity = 0.6f});
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
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = 0.0f;
  float target_camera_yaw_ = 0.0f;
  float target_camera_pitch_ = 0.0f;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::DemoGame game;

  engine.setUi(std::make_unique<karma::demo::ImGuiUiLayer>());

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
