#include "karma/debug/debug_overlay.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <imgui.h>

#include "karma/ecs/world.h"
#include "karma/components/tag.h"
#include "karma/components/audio_listener.h"
#include "karma/components/audio_source.h"
#include "karma/components/camera.h"
#include "karma/components/collider.h"
#include "karma/components/environment.h"
#include "karma/components/layers.h"
#include "karma/components/light.h"
#include "karma/components/mesh.h"
#include "karma/components/player_controller.h"
#include "karma/components/rigidbody.h"
#include "karma/components/script.h"
#include "karma/components/transform.h"
#include "karma/components/visibility.h"
#include "karma/scene/scene.h"
#include "karma/scene/node.h"
#include "karma/systems/system_graph.h"

namespace karma::debug {

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

const char* lightTypeName(components::LightComponent::Type type) {
  switch (type) {
    case components::LightComponent::Type::Directional: return "Directional";
    case components::LightComponent::Type::Point: return "Point";
    case components::LightComponent::Type::Spot: return "Spot";
    default: return "Unknown";
  }
}

bool editVec3(const char* label, math::Vec3& v) {
  ImGui::InputFloat3(label, &v.x, "%.3f");
  return ImGui::IsItemDeactivatedAfterEdit();
}

bool editQuat(const char* label, math::Quat& q) {
  ImGui::InputFloat4(label, &q.x, "%.3f");
  return ImGui::IsItemDeactivatedAfterEdit();
}

math::Vec3 quatToEulerDegrees(const math::Quat& q) {
  const glm::quat gq(q.w, q.x, q.y, q.z);
  const glm::vec3 euler = glm::degrees(glm::eulerAngles(gq));
  return {euler.x, euler.y, euler.z};
}

math::Quat eulerDegreesToQuat(const math::Vec3& euler_degrees) {
  const glm::vec3 euler_rad = glm::radians(glm::vec3(euler_degrees.x, euler_degrees.y, euler_degrees.z));
  const glm::quat q = glm::quat(euler_rad);
  return {q.x, q.y, q.z, q.w};
}

bool editColor(const char* label, math::Color& c) {
  ImGui::InputFloat4(label, &c.r, "%.3f");
  return ImGui::IsItemDeactivatedAfterEdit();
}

bool editFloat(const char* label, float& v, const char* format = "%.3f") {
  ImGui::InputFloat(label, &v, 0.0f, 0.0f, format);
  return ImGui::IsItemDeactivatedAfterEdit();
}

bool editInt(const char* label, int& v) {
  ImGui::InputInt(label, &v, 0, 0);
  return ImGui::IsItemDeactivatedAfterEdit();
}

bool editEnumCombo(const char* label, int& value, const char* const* items, int count) {
  ImGui::Combo(label, &value, items, count);
  return ImGui::IsItemDeactivatedAfterEdit();
}

bool editBool(const char* label, bool& v) {
  int temp = v ? 1 : 0;
  if (editInt(label, temp)) {
    v = temp != 0;
    return true;
  }
  return false;
}

int inputTextCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* str = static_cast<std::string*>(data->UserData);
    str->resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = str->data();
  }
  return 0;
}

bool inputTextString(const char* label, std::string& value) {
  if (value.capacity() < 64) {
    value.reserve(64);
  }
  ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
  ImGui::InputText(label, value.data(), value.capacity() + 1, flags, inputTextCallback, &value);
  return ImGui::IsItemDeactivatedAfterEdit();
}

void drawNode(const scene::Scene& scene,
              ecs::World* world,
              scene::NodeId id,
              scene::NodeId& selected) {
  if (!scene.isAlive(id)) {
    return;
  }
  const auto& node = scene.get(id);
  const bool has_children = !node.children.empty();
  ImGuiTreeNodeFlags flags =
      (has_children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf) |
      (selected == id ? ImGuiTreeNodeFlags_Selected : 0);
  const char* label = "Entity";
  if (world && node.entity.isValid() && world->has<components::TagComponent>(node.entity)) {
    const auto& tag = world->get<components::TagComponent>(node.entity);
    if (!tag.name.empty()) {
      label = tag.name.c_str();
    }
  }
  const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)),
                                      flags,
                                      "%s",
                                      label);
  if (ImGui::IsItemClicked()) {
    selected = id;
  }
  if (open) {
    for (scene::NodeId child : node.children) {
      drawNode(scene, world, child, selected);
    }
    ImGui::TreePop();
  }
}
}  // namespace

DebugOverlayLayer::DebugOverlayLayer(ecs::World* world,
                                     scene::Scene* scene,
                                     systems::SystemGraph* systems)
    : world_(world), scene_(scene), systems_(systems) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.BackendPlatformName = "karma";
  io.BackendRendererName = "karma_ui_draw";
}

void DebugOverlayLayer::onEvent(const platform::Event& event) {
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

void DebugOverlayLayer::onFrame(app::UIContext& ctx) {
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
  ImGui::Begin("Karma Debug");

  if (scene_) {
    if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
      const auto& nodes = scene_->nodes();
      for (scene::NodeId id = 0; id < nodes.size(); ++id) {
        if (!scene_->isAlive(id)) {
          continue;
        }
        if (nodes[id].parent != scene::Node::kInvalidId) {
          continue;
        }
        drawNode(*scene_, world_, id, selected_node_);
      }
    }
  }

  if (scene_ && world_ && selected_node_ != scene::Node::kInvalidId &&
      scene_->isAlive(selected_node_)) {
    const auto& node = scene_->get(selected_node_);
    if (node.entity.isValid()) {
      ImGui::Separator();
      ImGui::Text("Components");
      if (world_->has<components::TransformComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
          const auto& c = world_->get<components::TransformComponent>(node.entity);
          math::Vec3 pos = c.getPosition();
          if (editVec3("Position", pos)) {
            world_->get<components::TransformComponent>(node.entity).setPosition(pos);
          }
          math::Quat rot = c.getRotation();
          if (editQuat("Rotation (Quat)", rot)) {
            world_->get<components::TransformComponent>(node.entity).setRotation(rot);
          }
          math::Vec3 euler = quatToEulerDegrees(c.getRotation());
          if (editVec3("Rotation (Euler)", euler)) {
            world_->get<components::TransformComponent>(node.entity)
                .setRotation(eulerDegreesToQuat(euler));
          }
          math::Vec3 scale = c.getScale();
          if (editVec3("Scale", scale)) {
            world_->get<components::TransformComponent>(node.entity).setScale(scale);
          }
        }
      }
      if (world_->has<components::TagComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Tag")) {
          const auto& c = world_->get<components::TagComponent>(node.entity);
          std::string name = c.name;
          if (inputTextString("Name", name)) {
            world_->get<components::TagComponent>(node.entity).name = std::move(name);
          }
        }
      }
      if (world_->has<components::MeshComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Mesh")) {
          auto& c = world_->get<components::MeshComponent>(node.entity);
          std::string mesh_key = c.mesh_key;
          std::string material_key = c.material_key;
          std::string texture_key = c.texture_key;
          if (inputTextString("Mesh", mesh_key)) {
            c.mesh_key = std::move(mesh_key);
          }
          if (inputTextString("Material", material_key)) {
            c.material_key = std::move(material_key);
          }
          if (inputTextString("Texture", texture_key)) {
            c.texture_key = std::move(texture_key);
          }
          ImGui::Checkbox("Visible", &c.visible);
        }
      }
      if (world_->has<components::EnvironmentComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Environment")) {
          auto& c = world_->get<components::EnvironmentComponent>(node.entity);
          std::string map = c.environment_map;
          if (inputTextString("Map", map)) {
            c.environment_map = std::move(map);
          }
          editFloat("Intensity", c.intensity);
          editBool("Draw Skybox", c.draw_skybox);
          editBool("Enabled", c.enabled);
        }
      }
      if (world_->has<components::LightComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Light")) {
          auto& c = world_->get<components::LightComponent>(node.entity);
          int type = static_cast<int>(c.type);
          const char* types[] = {"Directional", "Point", "Spot"};
          if (editEnumCombo("Type", type, types, 3)) {
            if (type < 0) type = 0;
            if (type > 2) type = 2;
            c.type = static_cast<components::LightComponent::Type>(type);
          }
          editColor("Color", c.color);
          editFloat("Intensity", c.intensity);
          editFloat("Range", c.range);
          editFloat("Inner Cone", c.inner_cone_degrees);
          editFloat("Outer Cone", c.outer_cone_degrees);
          editBool("Casts Shadows", c.casts_shadows);
          editFloat("Shadow Extent", c.shadow_extent);
        }
      }
      if (world_->has<components::CameraComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Camera")) {
          auto& c = world_->get<components::CameraComponent>(node.entity);
          editFloat("FOV Y", c.fov_y_degrees);
          editFloat("Near", c.near_clip);
          editFloat("Far", c.far_clip);
          editBool("Primary", c.is_primary);
          editBool("Render To Texture", c.render_to_texture);
          std::string target = c.render_target_key;
          if (inputTextString("Render Target", target)) {
            c.render_target_key = std::move(target);
          }
        }
      }
      if (world_->has<components::RigidbodyComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Rigidbody")) {
          auto& c = world_->get<components::RigidbodyComponent>(node.entity);
          editFloat("Mass", c.mass);
          editVec3("Velocity", c.velocity);
          editVec3("Angular Velocity", c.angular_velocity);
          ImGui::Checkbox("Kinematic", &c.is_kinematic);
          ImGui::Checkbox("Use Gravity", &c.use_gravity);
          ImGui::Checkbox("Trigger", &c.is_trigger);
          if (!world_->has<components::TransformComponent>(node.entity)) {
            ImGui::Text("Position: (no Transform)");
          }
        }
      }
      if (world_->has<components::BoxColliderComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("BoxCollider")) {
          auto& c = world_->get<components::BoxColliderComponent>(node.entity);
          editVec3("Center", c.center);
          editVec3("Half Extents", c.half_extents);
          editBool("Trigger", c.is_trigger);
          ImGui::Checkbox("Debug Draw", &c.debug_draw);
        }
      }
      if (world_->has<components::SphereColliderComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("SphereCollider")) {
          auto& c = world_->get<components::SphereColliderComponent>(node.entity);
          editVec3("Center", c.center);
          editFloat("Radius", c.radius);
          editBool("Trigger", c.is_trigger);
          ImGui::Checkbox("Debug Draw", &c.debug_draw);
        }
      }
      if (world_->has<components::CapsuleColliderComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("CapsuleCollider")) {
          auto& c = world_->get<components::CapsuleColliderComponent>(node.entity);
          editVec3("Center", c.center);
          editFloat("Radius", c.radius);
          editFloat("Height", c.height);
          editBool("Trigger", c.is_trigger);
          ImGui::Checkbox("Debug Draw", &c.debug_draw);
        }
      }
      if (world_->has<components::MeshColliderComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("MeshCollider")) {
          auto& c = world_->get<components::MeshColliderComponent>(node.entity);
          editBool("Trigger", c.is_trigger);
          ImGui::Checkbox("Debug Draw", &c.debug_draw);
        }
      }
      if (world_->has<components::PlayerControllerComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("PlayerController")) {
          auto& c = world_->get<components::PlayerControllerComponent>(node.entity);
          editBool("Enabled", c.enabled);
          math::Vec3 desired = c.desiredVelocity();
          if (editVec3("Desired Velocity", desired)) {
            c.setDesiredVelocity(desired);
          }
          math::Vec3 add = c.addVelocity();
          if (editVec3("Add Velocity", add)) {
            c.setAddVelocity(add);
          }
        }
      }
      if (world_->has<components::AudioListenerComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("AudioListener")) {
          ImGui::Text("Active");
        }
      }
      if (world_->has<components::AudioSourceComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("AudioSource")) {
          auto& c = world_->get<components::AudioSourceComponent>(node.entity);
          std::string clip = c.clip_key;
          if (inputTextString("Clip", clip)) {
            c.clip_key = std::move(clip);
          }
          editFloat("Gain", c.gain);
          editFloat("Pitch", c.pitch);
          editFloat("Min Distance", c.min_distance);
          editFloat("Max Distance", c.max_distance);
          editBool("Looping", c.looping);
          editBool("Play On Start", c.play_on_start);
          editBool("Spatialized", c.spatialized);
          editInt("Max Instances", c.max_instances);
        }
      }
      if (world_->has<components::ScriptComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Script")) {
          auto& c = world_->get<components::ScriptComponent>(node.entity);
          std::string key = c.script_key;
          if (inputTextString("Key", key)) {
            c.script_key = std::move(key);
          }
          editBool("Enabled", c.enabled);
        }
      }
      if (world_->has<components::VisibilityComponent>(node.entity)) {
        if (ImGui::CollapsingHeader("Visibility")) {
          auto& c = world_->get<components::VisibilityComponent>(node.entity);
          editBool("Visible", c.visible);
          ImGui::InputScalar("Render Mask", ImGuiDataType_U32, &c.render_layer_mask,
                             nullptr, nullptr, "%08X");
          ImGui::InputScalar("Collision Mask", ImGuiDataType_U32, &c.collision_layer_mask,
                             nullptr, nullptr, "%08X");
        }
      }
    }
  }

  ImGui::End();
  ImGui::Render();

  const ImDrawData* draw_data = ImGui::GetDrawData();
  if (!draw_data) {
    return;
  }

  app::UIDrawData& out = ctx.drawData();
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
      app::UIVertex out_v{};
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

      app::UIDrawCmd out_cmd{};
      out_cmd.index_offset = global_idx_offset;
      out_cmd.index_count = cmd.ElemCount;
      out_cmd.scissor_enabled = true;
      out_cmd.scissor_x = static_cast<int>(clip.x);
      out_cmd.scissor_y = static_cast<int>(clip.y);
      out_cmd.scissor_w = static_cast<int>(clip.z - clip.x);
      out_cmd.scissor_h = static_cast<int>(clip.w - clip.y);
      out_cmd.texture = static_cast<app::UITextureHandle>(static_cast<uintptr_t>(cmd.GetTexID()));
      out.commands.push_back(out_cmd);
      global_idx_offset += cmd.ElemCount;
    }
    global_vtx_offset += cmd_list->VtxBuffer.Size;
  }
}

void DebugOverlayLayer::onShutdown() {
  if (font_texture_ != 0) {
    if (pending_ctx_) {
      pending_ctx_->destroyTexture(font_texture_);
    }
    font_texture_ = 0;
  }
  ImGui::DestroyContext();
}

}  // namespace karma::debug
