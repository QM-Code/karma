#include "karma/runtime/debug/debug_overlay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <string_view>

#include <imgui.h>

#include "karma/world/ecs/world.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/audio_listener.h"
#include "karma/world/components/audio_source.h"
#include "karma/world/components/animator.h"
#include "karma/world/components/camera.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/environment.h"
#include "karma/world/components/layers.h"
#include "karma/world/components/light.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/player_controller.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/script.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/scene/scene.h"
#include "karma/world/scene/node.h"
#include "karma/world/systems/system_graph.h"
#include "karma/rendering/renderer/device.h"

namespace karma::debug {

namespace {
class ScopedImGuiContext {
 public:
  explicit ScopedImGuiContext(ImGuiContext* context)
      : previous_(ImGui::GetCurrentContext()) {
    ImGui::SetCurrentContext(context);
  }

  ~ScopedImGuiContext() {
    ImGui::SetCurrentContext(previous_);
  }

 private:
  ImGuiContext* previous_ = nullptr;
};

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

const char* skinningPathName(components::SkinningPath path) {
  switch (path) {
    case components::SkinningPath::Cpu: return "CPU";
    case components::SkinningPath::Gpu: return "GPU";
    case components::SkinningPath::GpuUnavailableCpuFallback: return "CPU fallback";
  }
  return "Unknown";
}

const char* rendererMeshStateName(const components::SkinnedMeshComponent& skin) {
  return skin.renderer_mesh_is_bind_pose ? "bind pose" : "CPU skinned";
}

const char* rootMotionModeName(components::RootMotionMode mode) {
  switch (mode) {
    case components::RootMotionMode::Disabled: return "Disabled";
    case components::RootMotionMode::ApplyToLocalTransform: return "Apply to local";
    case components::RootMotionMode::ExposeDelta: return "Expose delta";
  }
  return "Disabled";
}

const char* parameterTypeName(components::AnimatorParameterType type) {
  switch (type) {
    case components::AnimatorParameterType::Bool: return "Bool";
    case components::AnimatorParameterType::Int: return "Int";
    case components::AnimatorParameterType::Float: return "Float";
    case components::AnimatorParameterType::Trigger: return "Trigger";
  }
  return "Unknown";
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
  ImGui::PushID(&v);
  const bool changed = ImGui::Button(v ? "On" : "Off", ImVec2(48.0f, 0.0f));
  ImGui::PopID();
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
  if (changed) {
    v = !v;
  }
  return changed;
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

std::string nodeDisplayName(const scene::Node& node, ecs::World* world) {
  if (world && node.entity.isValid() && world->has<components::TagComponent>(node.entity)) {
    const auto& tag = world->get<components::TagComponent>(node.entity);
    if (!tag.name.empty()) {
      return tag.name;
    }
  }
  return "Entity";
}

std::string lowerCopy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

std::string nodeSearchText(const scene::Node& node, ecs::World* world) {
  std::string text = nodeDisplayName(node, world);
  text += " node ";
  text += std::to_string(node.id);
  if (node.entity.isValid()) {
    text += " entity ";
    text += std::to_string(node.entity.index);
    text += ":";
    text += std::to_string(node.entity.generation);
  }
  return text;
}

bool nodeMatchesFilter(const scene::Node& node,
                       ecs::World* world,
                       std::string_view filter_lower) {
  if (filter_lower.empty()) {
    return true;
  }
  const std::string search_text = lowerCopy(nodeSearchText(node, world));
  return search_text.find(filter_lower) != std::string::npos;
}

bool nodeHasFilterMatch(const scene::Scene& scene,
                        ecs::World* world,
                        scene::NodeId id,
                        std::string_view filter_lower) {
  if (!scene.isAlive(id)) {
    return false;
  }
  const auto& node = scene.get(id);
  if (nodeMatchesFilter(node, world, filter_lower)) {
    return true;
  }
  for (scene::NodeId child : node.children) {
    if (nodeHasFilterMatch(scene, world, child, filter_lower)) {
      return true;
    }
  }
  return false;
}

bool drawNode(const scene::Scene& scene,
              ecs::World* world,
              scene::NodeId id,
              scene::NodeId& selected,
              std::string_view filter_lower) {
  if (!scene.isAlive(id)) {
    return false;
  }
  const auto& node = scene.get(id);
  const bool filter_active = !filter_lower.empty();
  if (filter_active && !nodeHasFilterMatch(scene, world, id, filter_lower)) {
    return false;
  }

  bool has_visible_children = false;
  for (scene::NodeId child : node.children) {
    if (!filter_active || nodeHasFilterMatch(scene, world, child, filter_lower)) {
      has_visible_children = true;
      break;
    }
  }

  ImGuiTreeNodeFlags flags =
      (has_visible_children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf) |
      (selected == id ? ImGuiTreeNodeFlags_Selected : 0);
  if (filter_active && has_visible_children) {
    flags |= ImGuiTreeNodeFlags_DefaultOpen;
  }
  const std::string label = nodeDisplayName(node, world);
  const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)),
                                      flags,
                                      "%s",
                                      label.c_str());
  if (ImGui::IsItemClicked()) {
    selected = id;
  }
  if (open) {
    for (scene::NodeId child : node.children) {
      drawNode(scene, world, child, selected, filter_lower);
    }
    ImGui::TreePop();
  }
  return true;
}
}  // namespace

DebugOverlayLayer::DebugOverlayLayer(ecs::World* world,
                                     scene::Scene* scene,
                                     systems::SystemGraph* systems,
                                     renderer::GraphicsDevice* graphics,
                                     int shadow_map_size,
                                     float shadow_bias,
                                     int shadow_pcf_radius,
                                     int shadow_raster_depth_bias,
                                     float shadow_raster_slope_bias,
                                     float shadow_receiver_bias_scale,
                                     float shadow_normal_bias_scale,
                                     float point_shadow_constant_bias,
                                     float point_shadow_slope_bias_scale,
                                     float point_shadow_normal_bias_scale,
                                     float point_shadow_receiver_bias_scale,
                                     float local_light_distance_damping,
                                     float local_light_range_falloff_exponent,
                                     bool ao_affects_local_lights,
                                     float local_light_directional_shadow_lift_strength,
                                     float lighting_exposure,
                                     int forward_plus_max_local_lights)
    : world_(world),
      scene_(scene),
      systems_(systems),
      graphics_(graphics),
      shadow_map_size_(shadow_map_size),
      shadow_bias_(shadow_bias),
      shadow_pcf_radius_(shadow_pcf_radius),
      shadow_raster_depth_bias_(shadow_raster_depth_bias),
      shadow_raster_slope_bias_(shadow_raster_slope_bias),
      shadow_receiver_bias_scale_(shadow_receiver_bias_scale),
      shadow_normal_bias_scale_(shadow_normal_bias_scale),
      point_shadow_constant_bias_(point_shadow_constant_bias),
      point_shadow_slope_bias_scale_(point_shadow_slope_bias_scale),
      point_shadow_normal_bias_scale_(point_shadow_normal_bias_scale),
      point_shadow_receiver_bias_scale_(point_shadow_receiver_bias_scale),
      local_light_distance_damping_(local_light_distance_damping),
      local_light_range_falloff_exponent_(local_light_range_falloff_exponent),
      ao_affects_local_lights_(ao_affects_local_lights),
      local_light_directional_shadow_lift_strength_(local_light_directional_shadow_lift_strength),
      lighting_exposure_(lighting_exposure),
      forward_plus_max_local_lights_(forward_plus_max_local_lights) {
  IMGUI_CHECKVERSION();
  imgui_context_ = ImGui::CreateContext();
  ScopedImGuiContext context_scope(imgui_context_);
  ImGuiIO& io = ImGui::GetIO();
  io.BackendPlatformName = "karma";
  io.BackendRendererName = "karma_ui_draw";
  if (graphics_) {
    const renderer::ForwardPlusStats stats = graphics_->getForwardPlusStats();
    forward_plus_tile_size_ = std::max(4, static_cast<int>(stats.tile_size));
    forward_plus_max_lights_per_tile_ =
        std::max(8, static_cast<int>(stats.max_lights_per_tile));
    forward_plus_max_local_lights_ =
        std::max(1, static_cast<int>(stats.max_local_lights));
  }
}

void DebugOverlayLayer::onEvent(const platform::Event& event) {
  if (!imgui_context_) {
    return;
  }
  ScopedImGuiContext context_scope(imgui_context_);
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
  if (!imgui_context_) {
    return;
  }
  ScopedImGuiContext context_scope(imgui_context_);
  pending_ctx_ = &ctx;
  ImGuiIO& io = ImGui::GetIO();
  const auto frame = ctx.frame();
  io.DisplaySize = ImVec2(static_cast<float>(frame.viewport_w),
                          static_cast<float>(frame.viewport_h));
  io.DisplayFramebufferScale = ImVec2(frame.dpi_scale, frame.dpi_scale);
  io.DeltaTime = frame.dt > 0.0f ? frame.dt : (1.0f / 60.0f);
  const float frame_ms = io.DeltaTime * 1000.0f;
  frame_time_history_ms_[frame_time_history_cursor_] = frame_ms;
  frame_time_history_cursor_ = (frame_time_history_cursor_ + 1) % kFrameHistorySize;
  frame_time_history_count_ = std::min(frame_time_history_count_ + 1, kFrameHistorySize);
  worst_frame_ms_ = std::max(worst_frame_ms_, frame_ms);
  if (frame_ms >= hitch_threshold_ms_) {
    hitch_count_ += 1;
  }

  if (!font_texture_) {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    font_texture_ = ctx.createTextureRGBA8(width, height, pixels);
    io.Fonts->SetTexID(toImTextureId(font_texture_));
  }

  ImGui::NewFrame();
  drawDebugWindow(frame_ms, io.Framerate);
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

void DebugOverlayLayer::drawDebugWindow(float frame_ms, float framerate) {
  if (ImGui::Begin("Karma Debug")) {
    if (ImGui::BeginTabBar("KarmaDebugTabs")) {
      if (ImGui::BeginTabItem("Scene")) {
        drawSceneTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Renderer")) {
        drawRendererTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Particles")) {
        drawParticlesTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Performance")) {
        drawPerformanceTab(frame_ms, framerate);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

void DebugOverlayLayer::drawSceneTab() {
  if (!scene_) {
    ImGui::TextDisabled("No scene.");
    return;
  }

  const float available_width = ImGui::GetContentRegionAvail().x;
  const float hierarchy_width = std::clamp(available_width * 0.34f, 220.0f, 360.0f);

  ImGui::BeginChild("SceneHierarchyPane", ImVec2(hierarchy_width, 0.0f), true);
  drawSceneHierarchyPane();
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("SceneInspectorPane", ImVec2(0.0f, 0.0f), true);
  drawSelectedInspectorPane();
  ImGui::EndChild();
}

void DebugOverlayLayer::drawSceneHierarchyPane() {
  ImGui::TextUnformatted("Hierarchy");

  std::array<char, 128> filter_buffer{};
  const size_t copy_count = std::min(hierarchy_filter_.size(), filter_buffer.size() - 1);
  std::copy_n(hierarchy_filter_.c_str(), copy_count, filter_buffer.data());

  const ImGuiStyle& style = ImGui::GetStyle();
  const float clear_width =
      ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f;
  const float input_width =
      std::max(80.0f, ImGui::GetContentRegionAvail().x - clear_width - style.ItemSpacing.x);
  ImGui::SetNextItemWidth(input_width);
  if (ImGui::InputTextWithHint("##HierarchyFilter",
                               "Search hierarchy",
                               filter_buffer.data(),
                               filter_buffer.size())) {
    hierarchy_filter_ = filter_buffer.data();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    hierarchy_filter_.clear();
  }

  ImGui::Separator();

  if (selected_node_ != scene::Node::kInvalidId && !scene_->isAlive(selected_node_)) {
    selected_node_ = scene::Node::kInvalidId;
  }

  const std::string filter_lower = lowerCopy(hierarchy_filter_);
  bool drew_any_node = false;
  const auto& nodes = scene_->nodes();
  for (scene::NodeId id = 0; id < nodes.size(); ++id) {
    if (!scene_->isAlive(id)) {
      continue;
    }
    if (nodes[id].parent != scene::Node::kInvalidId) {
      continue;
    }
    drew_any_node |= drawNode(*scene_, world_, id, selected_node_, filter_lower);
  }

  if (!drew_any_node) {
    ImGui::TextDisabled(filter_lower.empty() ? "No scene nodes." : "No matching nodes.");
  }
}

void DebugOverlayLayer::drawSelectedInspectorPane() {
  ImGui::TextUnformatted("Inspector");

  if (!scene_) {
    ImGui::TextDisabled("No scene.");
    return;
  }
  if (selected_node_ == scene::Node::kInvalidId || !scene_->isAlive(selected_node_)) {
    ImGui::TextDisabled("No node selected.");
    return;
  }

  const auto& node = scene_->get(selected_node_);
  drawSelectedSummary(node);

  if (!world_) {
    ImGui::Separator();
    ImGui::TextDisabled("No ECS world.");
    return;
  }
  if (!node.entity.isValid()) {
    ImGui::Separator();
    ImGui::TextDisabled("Selected node has no entity.");
    return;
  }

  ImGui::Separator();
  ImGui::Text("Components");
  drawComponentInspector(node);
}

void DebugOverlayLayer::drawSelectedSummary(const scene::Node& node) {
  const std::string name = nodeDisplayName(node, world_);
  ImGui::Text("Selected: %s", name.c_str());
  ImGui::Text("Node: %u", static_cast<unsigned int>(node.id));
  if (node.entity.isValid()) {
    ImGui::Text("Entity: %u:%u",
                static_cast<unsigned int>(node.entity.index),
                static_cast<unsigned int>(node.entity.generation));
  } else {
    ImGui::TextUnformatted("Entity: (none)");
  }
  if (node.parent != scene::Node::kInvalidId) {
    ImGui::Text("Parent: %u", static_cast<unsigned int>(node.parent));
  } else {
    ImGui::TextUnformatted("Parent: (root)");
  }
  ImGui::Text("Children: %zu", node.children.size());

  if (!world_ || !node.entity.isValid()) {
    return;
  }

  std::string components;
  auto append_component = [&components](const char* label) {
    if (!components.empty()) {
      components += ", ";
    }
    components += label;
  };
  if (world_->has<components::TransformComponent>(node.entity)) append_component("Transform");
  if (world_->has<components::LocalTransformComponent>(node.entity)) append_component("LocalTransform");
  if (world_->has<components::TagComponent>(node.entity)) append_component("Tag");
  if (world_->has<components::MeshComponent>(node.entity)) append_component("Mesh");
  if (world_->has<components::AnimatorComponent>(node.entity)) append_component("Animator");
  if (world_->has<components::SkinnedMeshComponent>(node.entity)) append_component("SkinnedMesh");
  if (world_->has<components::EnvironmentComponent>(node.entity)) append_component("Environment");
  if (world_->has<components::LightComponent>(node.entity)) append_component("Light");
  if (world_->has<components::CameraComponent>(node.entity)) append_component("Camera");
  if (world_->has<components::RigidbodyComponent>(node.entity)) append_component("Rigidbody");
  if (world_->has<components::BoxColliderComponent>(node.entity)) append_component("BoxCollider");
  if (world_->has<components::SphereColliderComponent>(node.entity)) append_component("SphereCollider");
  if (world_->has<components::CapsuleColliderComponent>(node.entity)) append_component("CapsuleCollider");
  if (world_->has<components::MeshColliderComponent>(node.entity)) append_component("MeshCollider");
  if (world_->has<components::PlayerControllerComponent>(node.entity)) append_component("PlayerController");
  if (world_->has<components::AudioListenerComponent>(node.entity)) append_component("AudioListener");
  if (world_->has<components::AudioSourceComponent>(node.entity)) append_component("AudioSource");
  if (world_->has<components::ScriptComponent>(node.entity)) append_component("Script");
  if (world_->has<components::VisibilityComponent>(node.entity)) append_component("Visibility");

  if (components.empty()) {
    components = "None";
  }
  ImGui::TextWrapped("Component Summary: %s", components.c_str());
}

void DebugOverlayLayer::drawComponentInspector(const scene::Node& node) {
  if (!world_ || !node.entity.isValid()) {
    return;
  }

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
  if (world_->has<components::LocalTransformComponent>(node.entity)) {
    if (ImGui::CollapsingHeader("Local Transform")) {
      auto& c = world_->get<components::LocalTransformComponent>(node.entity);
      editVec3("Local Position", c.position);
      editQuat("Local Rotation (Quat)", c.rotation);
      math::Vec3 euler = quatToEulerDegrees(c.rotation);
      if (editVec3("Local Rotation (Euler)", euler)) {
        c.rotation = eulerDegreesToQuat(euler);
      }
      editVec3("Local Scale", c.scale);
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
      editBool("Visible", c.visible);
    }
  }
  if (world_->has<components::AnimatorComponent>(node.entity)) {
    if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
      auto& c = world_->get<components::AnimatorComponent>(node.entity);
      ImGui::Text("Clips: %zu", c.clips.size());
      if (!c.clips.empty()) {
        const size_t current = std::min(c.current_clip_index, c.clips.size() - 1);
        const std::string current_label =
            c.clips[current].name.empty() ? ("Clip " + std::to_string(current))
                                          : c.clips[current].name;
        if (ImGui::BeginCombo("Clip", current_label.c_str())) {
          for (size_t clip_index = 0; clip_index < c.clips.size(); ++clip_index) {
            const std::string label =
                c.clips[clip_index].name.empty() ? ("Clip " + std::to_string(clip_index))
                                                 : c.clips[clip_index].name;
            const bool selected = clip_index == c.current_clip_index;
            if (ImGui::Selectable(label.c_str(), selected)) {
              components::setAnimatorClip(c, clip_index, true);
            }
            if (selected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }
      }
      if (ImGui::Button("Play")) {
        components::playAnimator(c);
      }
      ImGui::SameLine();
      if (ImGui::Button("Pause")) {
        components::pauseAnimator(c);
      }
      ImGui::SameLine();
      if (ImGui::Button("Stop")) {
        components::stopAnimator(c);
      }
      editFloat("Time", c.time_seconds);
      editFloat("Speed", c.speed);
      editBool("Loop", c.loop);
      ImGui::Text("Playing: %s", c.playing ? "yes" : "no");
      ImGui::Text("Blend: %s", c.blend_active ? "active" : "inactive");

      int root_mode = static_cast<int>(c.root_motion_mode);
      const char* root_modes[] = {"Disabled", "Apply to local", "Expose delta"};
      if (editEnumCombo("Root Motion", root_mode, root_modes, 3)) {
        root_mode = std::clamp(root_mode, 0, 2);
        c.root_motion_mode = static_cast<components::RootMotionMode>(root_mode);
      }
      ImGui::Text("Root Motion Mode: %s", rootMotionModeName(c.root_motion_mode));
      int root_node = static_cast<int>(
          c.root_motion_node_index == animation::kInvalidAnimationIndex ? -1
                                                                        : c.root_motion_node_index);
      if (editInt("Root Motion Node", root_node)) {
        c.root_motion_node_index =
            root_node < 0 ? animation::kInvalidAnimationIndex : static_cast<uint32_t>(root_node);
      }
      if (c.root_motion_delta.position) {
        const auto delta = *c.root_motion_delta.position;
        ImGui::Text("Root Delta Pos: %.3f %.3f %.3f", delta.x, delta.y, delta.z);
      }

      if (!c.state_machine.states.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("State Machine");
        ImGui::Text("Current State: %u", static_cast<unsigned int>(c.current_state_index));
        if (c.current_state_index < c.state_machine.states.size()) {
          ImGui::Text("Name: %s", c.state_machine.states[c.current_state_index].name.c_str());
        }
        ImGui::Text("State Time: %.3f", c.state_time_seconds);
        if (c.transition.active) {
          ImGui::Text("Transition: %u -> %u (%.3f / %.3f)",
                      static_cast<unsigned int>(c.transition.from_state_index),
                      static_cast<unsigned int>(c.transition.to_state_index),
                      c.transition.elapsed_seconds,
                      c.transition.duration_seconds);
        } else {
          ImGui::TextUnformatted("Transition: none");
        }
        for (auto& parameter : c.state_machine.parameters) {
          ImGui::PushID(parameter.name.c_str());
          ImGui::Text("%s (%s)", parameter.name.c_str(), parameterTypeName(parameter.type));
          switch (parameter.type) {
            case components::AnimatorParameterType::Bool:
              editBool("Value", parameter.bool_value);
              break;
            case components::AnimatorParameterType::Int:
              editInt("Value", parameter.int_value);
              break;
            case components::AnimatorParameterType::Float:
              editFloat("Value", parameter.float_value);
              break;
            case components::AnimatorParameterType::Trigger:
              if (ImGui::Button("Fire")) {
                parameter.trigger_value = true;
              }
              ImGui::SameLine();
              if (ImGui::Button("Reset")) {
                parameter.trigger_value = false;
              }
              ImGui::SameLine();
              ImGui::Text("Armed: %s", parameter.trigger_value ? "yes" : "no");
              break;
          }
          ImGui::PopID();
        }
      }

      if (!c.skeletons.empty() || !c.skins.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Rig");
        ImGui::Text("Skeletons: %zu", c.skeletons.size());
        for (size_t skeleton_index = 0; skeleton_index < c.skeletons.size(); ++skeleton_index) {
          const auto& skeleton = c.skeletons[skeleton_index];
          ImGui::Text("Skeleton %zu: %s (%zu joints)",
                      skeleton_index,
                      skeleton.name.c_str(),
                      skeleton.joints.size());
        }
        ImGui::Text("Skins: %zu", c.skins.size());
        for (size_t skin_index = 0; skin_index < c.skins.size(); ++skin_index) {
          const auto& skin = c.skins[skin_index];
          ImGui::Text("Skin %zu: %s (%zu joints)",
                      skin_index,
                      skin.name.c_str(),
                      skin.joint_node_indices.size());
        }
      }

      if (!c.event_queue.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Events");
        for (const auto& event : c.event_queue) {
          ImGui::Text("%s @ %.3f", event.name.c_str(), event.time_seconds);
        }
      }
    }
  }
  if (world_->has<components::SkinnedMeshComponent>(node.entity)) {
    if (ImGui::CollapsingHeader("Skinned Mesh")) {
      auto& c = world_->get<components::SkinnedMeshComponent>(node.entity);
      editBool("Enabled", c.enabled);
      int path = static_cast<int>(c.skinning_path);
      const char* paths[] = {"CPU", "GPU", "CPU fallback"};
      if (editEnumCombo("Skinning Path", path, paths, 3)) {
        path = std::clamp(path, 0, 2);
        c.skinning_path = static_cast<components::SkinningPath>(path);
      }
      const bool gpu_requested = c.skinning_path == components::SkinningPath::Gpu;
      const bool palette_within_capacity =
          c.joint_palette.size() <= components::kMaxSkinningJointsPerDraw;
      const bool gpu_draw_ready =
          c.enabled && gpu_requested && c.palette_valid && !c.joint_palette.empty() &&
          palette_within_capacity;
      ImGui::Text("Runtime: %s path, palette %s, GPU draw %s",
                  skinningPathName(c.skinning_path),
                  c.palette_valid ? "valid" : "invalid",
                  gpu_draw_ready ? "ready" : "not ready");
      ImGui::Text("Renderer Mesh: %s", rendererMeshStateName(c));
      ImGui::Text("Skin Index: %u", static_cast<unsigned int>(c.skin_index));
      ImGui::Text("Bind Vertices: %zu", c.bind_mesh.vertices.size());
      ImGui::Text("Influences: %zu", c.vertex_influences.size());
      ImGui::Text("Joints: %zu", c.joint_entities.size());
      ImGui::Text("Palette: %s (%zu/%u matrices)",
                  c.palette_valid ? "valid" : "invalid",
                  c.joint_palette.size(),
                  static_cast<unsigned int>(components::kMaxSkinningJointsPerDraw));
      ImGui::Text("Inverse Bind Matrices: %zu", c.inverse_bind_matrices.size());
      if (!c.diagnostic.empty()) {
        ImGui::TextWrapped("Diagnostic: %s", c.diagnostic.c_str());
      }
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
      editBool("Render Shadows", c.render_shadows);
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
      editBool("Kinematic", c.is_kinematic);
      editBool("Use Gravity", c.use_gravity);
      editBool("Trigger", c.is_trigger);
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
      editBool("Debug Draw", c.debug_draw);
    }
  }
  if (world_->has<components::SphereColliderComponent>(node.entity)) {
    if (ImGui::CollapsingHeader("SphereCollider")) {
      auto& c = world_->get<components::SphereColliderComponent>(node.entity);
      editVec3("Center", c.center);
      editFloat("Radius", c.radius);
      editBool("Trigger", c.is_trigger);
      editBool("Debug Draw", c.debug_draw);
    }
  }
  if (world_->has<components::CapsuleColliderComponent>(node.entity)) {
    if (ImGui::CollapsingHeader("CapsuleCollider")) {
      auto& c = world_->get<components::CapsuleColliderComponent>(node.entity);
      editVec3("Center", c.center);
      editFloat("Radius", c.radius);
      editFloat("Height", c.height);
      editBool("Trigger", c.is_trigger);
      editBool("Debug Draw", c.debug_draw);
    }
  }
  if (world_->has<components::MeshColliderComponent>(node.entity)) {
    if (ImGui::CollapsingHeader("MeshCollider")) {
      auto& c = world_->get<components::MeshColliderComponent>(node.entity);
      editBool("Trigger", c.is_trigger);
      editBool("Debug Draw", c.debug_draw);
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

void DebugOverlayLayer::drawRendererTab() {
  if (!graphics_) {
    ImGui::TextDisabled("No graphics device.");
    return;
  }

  if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool shadow_changed = false;
    shadow_changed |= editInt("Map Size", shadow_map_size_);
    shadow_changed |= editFloat("Bias", shadow_bias_, "%.6f");
    shadow_changed |= editInt("PCF Radius", shadow_pcf_radius_);
    shadow_changed |= editInt("Raster Depth Bias", shadow_raster_depth_bias_);
    shadow_changed |= editFloat("Raster Slope Bias", shadow_raster_slope_bias_, "%.3f");
    shadow_changed |= editFloat("Receiver Bias Scale", shadow_receiver_bias_scale_, "%.3f");
    shadow_changed |= editFloat("Normal Bias Scale", shadow_normal_bias_scale_, "%.3f");
    if (shadow_changed) {
      shadow_map_size_ = std::max(256, shadow_map_size_);
      shadow_pcf_radius_ = std::clamp(shadow_pcf_radius_, 0, 4);
      shadow_receiver_bias_scale_ = std::max(0.0f, shadow_receiver_bias_scale_);
      shadow_normal_bias_scale_ = std::max(0.0f, shadow_normal_bias_scale_);
      graphics_->setShadowSettings(shadow_bias_,
                                   shadow_map_size_,
                                   shadow_pcf_radius_,
                                   shadow_raster_depth_bias_,
                                   shadow_raster_slope_bias_,
                                   shadow_receiver_bias_scale_,
                                   shadow_normal_bias_scale_);
    }
    bool point_shadow_changed = false;
    point_shadow_changed |= editFloat("Point Const Bias", point_shadow_constant_bias_, "%.6f");
    point_shadow_changed |=
        editFloat("Point Slope Bias Scale", point_shadow_slope_bias_scale_, "%.3f");
    point_shadow_changed |=
        editFloat("Point Normal Bias Scale", point_shadow_normal_bias_scale_, "%.3f");
    point_shadow_changed |=
        editFloat("Point Receiver Bias Scale", point_shadow_receiver_bias_scale_, "%.3f");
    if (point_shadow_changed) {
      point_shadow_constant_bias_ = std::max(0.0f, point_shadow_constant_bias_);
      point_shadow_slope_bias_scale_ = std::max(0.0f, point_shadow_slope_bias_scale_);
      point_shadow_normal_bias_scale_ = std::max(0.0f, point_shadow_normal_bias_scale_);
      point_shadow_receiver_bias_scale_ = std::max(0.0f, point_shadow_receiver_bias_scale_);
      graphics_->setPointShadowSettings(point_shadow_constant_bias_,
                                        point_shadow_slope_bias_scale_,
                                        point_shadow_normal_bias_scale_,
                                        point_shadow_receiver_bias_scale_);
    }
  }

  if (ImGui::CollapsingHeader("Local Lights (Forward+)", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool fp_changed = false;
    fp_changed |= editInt("Tile Size", forward_plus_tile_size_);
    fp_changed |= editInt("Max Lights / Tile", forward_plus_max_lights_per_tile_);
    fp_changed |= editInt("Max Local Lights", forward_plus_max_local_lights_);
    if (fp_changed) {
      forward_plus_tile_size_ = std::clamp(forward_plus_tile_size_, 4, 64);
      forward_plus_max_lights_per_tile_ =
          std::clamp(forward_plus_max_lights_per_tile_, 8, 2048);
      forward_plus_max_local_lights_ =
          std::clamp(forward_plus_max_local_lights_, 1, 65536);
      graphics_->setForwardPlusSettings(forward_plus_tile_size_,
                                        forward_plus_max_lights_per_tile_,
                                        forward_plus_max_local_lights_);
    }
    bool local_changed = false;
    local_changed |= editFloat("InvSq Softening", local_light_distance_damping_, "%.3f");
    local_changed |=
        editFloat("Range Falloff Exponent", local_light_range_falloff_exponent_, "%.3f");
    local_changed |= editBool("AO Affects Local Lights", ao_affects_local_lights_);
    local_changed |=
        editFloat("Dir Shadow Lift", local_light_directional_shadow_lift_strength_, "%.3f");
    if (local_changed) {
      local_light_distance_damping_ = std::max(0.0f, local_light_distance_damping_);
      local_light_range_falloff_exponent_ =
          std::max(0.1f, local_light_range_falloff_exponent_);
      local_light_directional_shadow_lift_strength_ =
          std::max(0.0f, local_light_directional_shadow_lift_strength_);
      graphics_->setLocalLightingSettings(local_light_distance_damping_,
                                          local_light_range_falloff_exponent_,
                                          ao_affects_local_lights_,
                                          local_light_directional_shadow_lift_strength_);
    }
    bool exposure_changed = editFloat("Exposure", lighting_exposure_, "%.3f");
    if (exposure_changed) {
      lighting_exposure_ = std::max(0.01f, lighting_exposure_);
      graphics_->setExposure(lighting_exposure_);
    }
    const renderer::ForwardPlusStats stats = graphics_->getForwardPlusStats();
    ImGui::Text("Active: %s", stats.active ? "yes" : "no");
    ImGui::Text("CPU Fallback: %s", stats.cpu_fallback ? "yes" : "no");
    ImGui::Text("Local Lights: %u", static_cast<unsigned int>(stats.local_light_count));
    ImGui::Text("Tiles: %u x %u", static_cast<unsigned int>(stats.tiles_x),
                static_cast<unsigned int>(stats.tiles_y));
    ImGui::Text("Tile Size: %u", static_cast<unsigned int>(stats.tile_size));
    ImGui::Text("Max Lights / Tile: %u",
                static_cast<unsigned int>(stats.max_lights_per_tile));
    ImGui::Text("Max Local Lights: %u",
                static_cast<unsigned int>(stats.max_local_lights));
    if (stats.overflow_risk) {
      ImGui::Text("Warning: local light density may exceed per-tile capacity.");
    }
  }
}

void DebugOverlayLayer::drawParticlesTab() {
  if (!graphics_) {
    ImGui::TextDisabled("No graphics device.");
    return;
  }

  const renderer::ParticlePassStats stats = graphics_->getParticlePassStats();

  if (ImGui::CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Effect Binding Updates: %u",
                static_cast<unsigned int>(stats.effect_binding_updates));
    ImGui::Text("Emitters: simulated %u, visible %u, culled %u, submitted %u",
                static_cast<unsigned int>(stats.simulated_emitters),
                static_cast<unsigned int>(stats.visible_emitters),
                static_cast<unsigned int>(stats.culled_emitters),
                static_cast<unsigned int>(stats.submitted_emitters));
    ImGui::Text("Particles: simulated %u, packed %u, culled %u, ground collision %u",
                static_cast<unsigned int>(stats.simulated_particles),
                static_cast<unsigned int>(stats.packed_particles),
                static_cast<unsigned int>(stats.culled_particles),
                static_cast<unsigned int>(stats.ground_collision_particles));
  }

  if (ImGui::CollapsingHeader("Render Submission", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Submitted Batches: %u",
                static_cast<unsigned int>(stats.submitted_batches));
    ImGui::Text("Submitted Particles: %u",
                static_cast<unsigned int>(stats.submitted_particles));
    ImGui::Separator();
    ImGui::Text("Additive: %u batches, %u particles, %u draw calls",
                static_cast<unsigned int>(stats.additive_batches),
                static_cast<unsigned int>(stats.additive_particles),
                static_cast<unsigned int>(stats.additive_draw_calls));
    ImGui::Text("Alpha: %u batches, %u particles, %u draw calls",
                static_cast<unsigned int>(stats.alpha_batches),
                static_cast<unsigned int>(stats.alpha_particles),
                static_cast<unsigned int>(stats.alpha_draw_calls));
    ImGui::Text("Distortion: %u batches, %u particles, %u draw calls",
                static_cast<unsigned int>(stats.distortion_batches),
                static_cast<unsigned int>(stats.distortion_particles),
                static_cast<unsigned int>(stats.distortion_draw_calls));
  }

  if (ImGui::CollapsingHeader("GPU Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("GPU Capacity: %u",
                static_cast<unsigned int>(stats.gpu_particle_capacity));
    ImGui::Text("GPU Alive / Dead / Compacted: %u / %u / %u",
                static_cast<unsigned int>(stats.gpu_alive_particles),
                static_cast<unsigned int>(stats.gpu_dead_particles),
                static_cast<unsigned int>(stats.gpu_compacted_particles));
    ImGui::Text("GPU Spawned / Killed: %u / %u",
                static_cast<unsigned int>(stats.gpu_spawned_particles),
                static_cast<unsigned int>(stats.gpu_killed_particles));
    ImGui::Text("Compute Dispatches: %u",
                static_cast<unsigned int>(stats.gpu_compute_dispatches));
    ImGui::Text("Indirect Draws / Dispatches: %u / %u",
                static_cast<unsigned int>(stats.gpu_indirect_draws),
                static_cast<unsigned int>(stats.gpu_indirect_dispatches));
    ImGui::Text("Sort Keys: %u",
                static_cast<unsigned int>(stats.gpu_sort_key_count));
    ImGui::Text("Sort Passes: %u",
                static_cast<unsigned int>(stats.gpu_sort_passes));
    ImGui::Text("Buffer Resizes: %u",
                static_cast<unsigned int>(stats.gpu_buffer_resizes));
    ImGui::Text("Stats Readback Age: %u frame(s)",
                static_cast<unsigned int>(stats.gpu_stats_readback_age));
    ImGui::Text("GPU Culled Emitters / Particles: %u / %u",
                static_cast<unsigned int>(stats.gpu_culled_emitters),
                static_cast<unsigned int>(stats.gpu_culled_particles));
    ImGui::Text("GPU Culling Dispatches: %u",
                static_cast<unsigned int>(stats.gpu_culling_dispatches));
    ImGui::Separator();
    ImGui::Text("Allocator Live Emitters: %u",
                static_cast<unsigned int>(stats.gpu_allocator_live_emitters));
    ImGui::Text("Allocator Free Ranges: %u",
                static_cast<unsigned int>(stats.gpu_allocator_free_ranges));
    ImGui::Text("Allocator Active / High Water Capacity: %u / %u",
                static_cast<unsigned int>(stats.gpu_allocator_active_capacity),
                static_cast<unsigned int>(stats.gpu_allocator_high_water_capacity));
    ImGui::Text("Allocator Retired / Reused / Failures: %u / %u / %u",
                static_cast<unsigned int>(stats.gpu_allocator_retired_emitters),
                static_cast<unsigned int>(stats.gpu_allocator_reused_slots),
                static_cast<unsigned int>(stats.gpu_allocator_allocation_failures));
    ImGui::Text("GPU Sort Overflow: %s", stats.gpu_sort_overflow ? "yes" : "no");
    ImGui::Text("GPU Fallback Active: %s", stats.gpu_fallback_active ? "yes" : "no");
    ImGui::Text("CPU Fallback Particles: %u",
                static_cast<unsigned int>(stats.cpu_fallback_particles));
  }

  if (ImGui::CollapsingHeader("Sorting And Scene Copies")) {
    ImGui::Text("Alpha Sorted Particles: %u",
                static_cast<unsigned int>(stats.alpha_sorted_particles));
    ImGui::Text("Distortion Sorted Particles: %u",
                static_cast<unsigned int>(stats.distortion_sorted_particles));
    ImGui::Text("Alpha Invalid Depth: %u",
                static_cast<unsigned int>(stats.alpha_invalid_depth_particles));
    ImGui::Text("Distortion Invalid Depth: %u",
                static_cast<unsigned int>(stats.distortion_invalid_depth_particles));
    ImGui::Separator();
    ImGui::Text("GPU Global Sort Active: %s",
                stats.gpu_global_sort_active ? "yes" : "no");
    ImGui::Text("GPU Grouped Sort Fallback: %s",
                stats.gpu_grouped_sort_fallback ? "yes" : "no");
    ImGui::Separator();
    ImGui::Text("Pre-Particle Scene Sample Draws: %u",
                static_cast<unsigned int>(stats.pre_particle_scene_sample_draws));
    ImGui::Text("Post-Particle Scene Sample Draws: %u",
                static_cast<unsigned int>(stats.post_particle_scene_sample_draws));
    ImGui::Text("Scene Color Copy: %s", stats.scene_color_copy ? "yes" : "no");
    ImGui::Text("Post-Particle Scene Color Copy: %s",
                stats.post_particle_scene_color_copy ? "yes" : "no");
    ImGui::Text("Alpha Half Res: %s", stats.alpha_half_res ? "yes" : "no");
    ImGui::Text("Distortion Present: %s", stats.distortion_present ? "yes" : "no");
  }

  if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Sync Effect Bindings: %.3f ms", stats.sync_effect_bindings_ms);
    ImGui::Text("Simulation / Descriptor Submit: %.3f ms", stats.simulation_ms);
    ImGui::Text("Packing: %.3f ms", stats.packing_ms);
    ImGui::Text("Additive Grouping: %.3f ms", stats.additive_grouping_ms);
    ImGui::Text("Alpha Sort: %.3f ms", stats.alpha_sort_ms);
    ImGui::Text("Distortion Sort: %.3f ms", stats.distortion_sort_ms);
    ImGui::Text("Draw Submission: %.3f ms", stats.draw_submission_ms);
    ImGui::Separator();
    ImGui::Text("Alpha Collect / Sort / Span: %.3f / %.3f / %.3f ms",
                stats.alpha_collect_ms,
                stats.alpha_sort_only_ms,
                stats.alpha_span_ms);
    ImGui::Text("Distortion Collect / Sort / Span: %.3f / %.3f / %.3f ms",
                stats.distortion_collect_ms,
                stats.distortion_sort_only_ms,
                stats.distortion_span_ms);
  }
}

void DebugOverlayLayer::drawPerformanceTab(float frame_ms, float framerate) {
  if (ImGui::CollapsingHeader("Frame Pacing", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Button("Reset")) {
      resetFramePacingStats();
    }

    std::array<float, kFrameHistorySize> plot_values{};
    float average_ms = 0.0f;
    float max_recent_ms = 0.0f;
    for (size_t i = 0; i < frame_time_history_count_; ++i) {
      const size_t src_index =
          (frame_time_history_cursor_ + kFrameHistorySize - frame_time_history_count_ + i) %
          kFrameHistorySize;
      const float sample_ms = frame_time_history_ms_[src_index];
      plot_values[i] = sample_ms;
      average_ms += sample_ms;
      max_recent_ms = std::max(max_recent_ms, sample_ms);
    }
    if (frame_time_history_count_ > 0) {
      average_ms /= static_cast<float>(frame_time_history_count_);
    }
    ImGui::Text("Current: %.2f ms (%.1f FPS)", frame_ms, framerate);
    ImGui::Text("Recent Avg: %.2f ms", average_ms);
    ImGui::Text("Recent Max: %.2f ms", max_recent_ms);
    ImGui::Text("Worst: %.2f ms", worst_frame_ms_);
    ImGui::Text("Hitches >= %.1f ms: %llu", hitch_threshold_ms_,
                static_cast<unsigned long long>(hitch_count_));
    if (frame_time_history_count_ > 0) {
      ImGui::PlotLines("Frametime (ms)",
                       plot_values.data(),
                       static_cast<int>(frame_time_history_count_),
                       0,
                       nullptr,
                       0.0f,
                       std::max(40.0f, max_recent_ms * 1.1f),
                       ImVec2(0.0f, 80.0f));
    }
  }
}

void DebugOverlayLayer::resetFramePacingStats() {
  frame_time_history_ms_.fill(0.0f);
  frame_time_history_cursor_ = 0;
  frame_time_history_count_ = 0;
  hitch_count_ = 0;
  worst_frame_ms_ = 0.0f;
}

void DebugOverlayLayer::onShutdown() {
  if (!imgui_context_) {
    return;
  }
  ScopedImGuiContext context_scope(imgui_context_);
  if (font_texture_ != 0) {
    if (pending_ctx_) {
      pending_ctx_->destroyTexture(font_texture_);
    }
    font_texture_ = 0;
  }
  ImGui::DestroyContext(imgui_context_);
  imgui_context_ = nullptr;
}

}  // namespace karma::debug
