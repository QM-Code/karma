#include "karma/features/ui/imgui/imgui_layer.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace karma::imgui {

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

template <typename TextureId>
TextureId toTextureIdImpl(app::UITextureHandle handle) {
  if constexpr (std::is_pointer_v<TextureId>) {
    return reinterpret_cast<TextureId>(static_cast<std::uintptr_t>(handle));
  } else {
    return static_cast<TextureId>(handle);
  }
}

template <typename TextureId>
app::UITextureHandle fromTextureIdImpl(TextureId id) {
  if constexpr (std::is_pointer_v<TextureId>) {
    return static_cast<app::UITextureHandle>(reinterpret_cast<std::uintptr_t>(id));
  } else {
    return static_cast<app::UITextureHandle>(id);
  }
}

void submitDrawData(const ImDrawData& draw_data, renderer::UIDrawData& out) {
  out.clear();
  out.vertices.reserve(static_cast<size_t>(draw_data.TotalVtxCount));
  out.indices.reserve(static_cast<size_t>(draw_data.TotalIdxCount));

  size_t command_count = 0;
  for (int n = 0; n < draw_data.CmdListsCount; ++n) {
    command_count += static_cast<size_t>(draw_data.CmdLists[n]->CmdBuffer.Size);
  }
  out.commands.reserve(command_count);

  int global_vtx_offset = 0;
  uint32_t global_idx_offset = 0;
  for (int n = 0; n < draw_data.CmdListsCount; ++n) {
    const ImDrawList* cmd_list = draw_data.CmdLists[n];
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
      clip.x = (clip.x - draw_data.DisplayPos.x) * draw_data.FramebufferScale.x;
      clip.y = (clip.y - draw_data.DisplayPos.y) * draw_data.FramebufferScale.y;
      clip.z = (clip.z - draw_data.DisplayPos.x) * draw_data.FramebufferScale.x;
      clip.w = (clip.w - draw_data.DisplayPos.y) * draw_data.FramebufferScale.y;
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
      out_cmd.texture = fromTextureId(cmd.GetTexID());
      out.commands.push_back(out_cmd);
      global_idx_offset += cmd.ElemCount;
    }
    global_vtx_offset += cmd_list->VtxBuffer.Size;
  }
}

class ImGuiUiLayer final : public app::UiLayer {
 public:
  ImGuiUiLayer(ImGuiLayerCallbacks callbacks, ImGuiLayerConfig config)
      : callbacks_(std::move(callbacks)), config_(config) {
    IMGUI_CHECKVERSION();
    if (config_.create_context) {
      imgui_context_ = ImGui::CreateContext();
      owns_context_ = true;
    } else {
      imgui_context_ = ImGui::GetCurrentContext();
    }

    if (imgui_context_) {
      ScopedImGuiContext context_scope(imgui_context_);
      ImGuiIO& io = ImGui::GetIO();
      io.BackendPlatformName = config_.backend_platform_name;
      io.BackendRendererName = config_.backend_renderer_name;
    }
  }

  void onEvent(const platform::Event& event) override {
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

  void onFrame(app::UIContext& ctx) override {
    renderer::UIDrawData& out = ctx.drawData();
    out.clear();
    if (!imgui_context_) {
      return;
    }

    pending_ctx_ = &ctx;
    ScopedImGuiContext context_scope(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    const app::UIFrameInfo frame = ctx.frame();
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
      io.Fonts->SetTexID(toTextureId(font_texture_));
    }

    ImGui::NewFrame();
    if (callbacks_.draw) {
      callbacks_.draw(ctx);
    }
    ImGui::Render();

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
      submitDrawData(*draw_data, out);
    }
  }

  void onShutdown() override {
    if (!imgui_context_) {
      return;
    }

    ScopedImGuiContext context_scope(imgui_context_);
    if (pending_ctx_ && callbacks_.shutdown) {
      callbacks_.shutdown(*pending_ctx_);
    }
    if (font_texture_ != 0) {
      if (pending_ctx_) {
        pending_ctx_->destroyTexture(font_texture_);
      }
      font_texture_ = 0;
      ImGui::GetIO().Fonts->SetTexID(toTextureId(0));
    }

    if (owns_context_ && config_.destroy_context) {
      ImGui::DestroyContext(imgui_context_);
    }
    imgui_context_ = nullptr;
  }

 private:
  ImGuiLayerCallbacks callbacks_{};
  ImGuiLayerConfig config_{};
  ImGuiContext* imgui_context_ = nullptr;
  app::UIContext* pending_ctx_ = nullptr;
  app::UITextureHandle font_texture_ = 0;
  bool owns_context_ = false;
};

}  // namespace

ImTextureID toTextureId(app::UITextureHandle handle) {
  return toTextureIdImpl<ImTextureID>(handle);
}

app::UITextureHandle fromTextureId(ImTextureID id) {
  return fromTextureIdImpl<ImTextureID>(id);
}

std::unique_ptr<app::UiLayer> createUiLayer(FrameCallback draw, ImGuiLayerConfig config) {
  ImGuiLayerCallbacks callbacks{};
  callbacks.draw = std::move(draw);
  return createUiLayer(std::move(callbacks), config);
}

std::unique_ptr<app::UiLayer> createUiLayer(ImGuiLayerCallbacks callbacks,
                                            ImGuiLayerConfig config) {
  return std::make_unique<ImGuiUiLayer>(std::move(callbacks), config);
}

std::unique_ptr<app::UiLayer> createUiLayer(ImGuiLayerConfig config) {
  return createUiLayer(ImGuiLayerCallbacks{}, config);
}

}  // namespace karma::imgui
