#include "karma/ui_rmlui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::ui::rmlui {

namespace {

constexpr size_t kMaxGeneratedTextureBytes = 256u * 1024u * 1024u;

bool validCodepoint(uint32_t codepoint) {
  return codepoint <= 0x10ffffu &&
         !(codepoint >= 0xd800u && codepoint <= 0xdfffu);
}

int toRmlCoordinate(double value) {
  if (!std::isfinite(value)) {
    return 0;
  }
  return static_cast<int>(std::clamp(
      value,
      static_cast<double>(std::numeric_limits<int>::min()),
      static_cast<double>(std::numeric_limits<int>::max())));
}

float toRmlScroll(double value) {
  return static_cast<float>(std::clamp(
      value,
      -static_cast<double>(std::numeric_limits<float>::max()),
      static_cast<double>(std::numeric_limits<float>::max())));
}

Rml::Input::KeyIdentifier toRmlKey(platform::Key key) {
  switch (key) {
    case platform::Key::A: return Rml::Input::KI_A;
    case platform::Key::B: return Rml::Input::KI_B;
    case platform::Key::C: return Rml::Input::KI_C;
    case platform::Key::D: return Rml::Input::KI_D;
    case platform::Key::E: return Rml::Input::KI_E;
    case platform::Key::F: return Rml::Input::KI_F;
    case platform::Key::G: return Rml::Input::KI_G;
    case platform::Key::H: return Rml::Input::KI_H;
    case platform::Key::I: return Rml::Input::KI_I;
    case platform::Key::J: return Rml::Input::KI_J;
    case platform::Key::K: return Rml::Input::KI_K;
    case platform::Key::L: return Rml::Input::KI_L;
    case platform::Key::M: return Rml::Input::KI_M;
    case platform::Key::N: return Rml::Input::KI_N;
    case platform::Key::O: return Rml::Input::KI_O;
    case platform::Key::P: return Rml::Input::KI_P;
    case platform::Key::Q: return Rml::Input::KI_Q;
    case platform::Key::R: return Rml::Input::KI_R;
    case platform::Key::S: return Rml::Input::KI_S;
    case platform::Key::T: return Rml::Input::KI_T;
    case platform::Key::U: return Rml::Input::KI_U;
    case platform::Key::V: return Rml::Input::KI_V;
    case platform::Key::W: return Rml::Input::KI_W;
    case platform::Key::X: return Rml::Input::KI_X;
    case platform::Key::Y: return Rml::Input::KI_Y;
    case platform::Key::Z: return Rml::Input::KI_Z;
    case platform::Key::Num0: return Rml::Input::KI_0;
    case platform::Key::Num1: return Rml::Input::KI_1;
    case platform::Key::Num2: return Rml::Input::KI_2;
    case platform::Key::Num3: return Rml::Input::KI_3;
    case platform::Key::Num4: return Rml::Input::KI_4;
    case platform::Key::Num5: return Rml::Input::KI_5;
    case platform::Key::Num6: return Rml::Input::KI_6;
    case platform::Key::Num7: return Rml::Input::KI_7;
    case platform::Key::Num8: return Rml::Input::KI_8;
    case platform::Key::Num9: return Rml::Input::KI_9;
    case platform::Key::Left: return Rml::Input::KI_LEFT;
    case platform::Key::Right: return Rml::Input::KI_RIGHT;
    case platform::Key::Up: return Rml::Input::KI_UP;
    case platform::Key::Down: return Rml::Input::KI_DOWN;
    case platform::Key::Escape: return Rml::Input::KI_ESCAPE;
    case platform::Key::Enter: return Rml::Input::KI_RETURN;
    case platform::Key::Tab: return Rml::Input::KI_TAB;
    case platform::Key::Backspace: return Rml::Input::KI_BACK;
    case platform::Key::Delete: return Rml::Input::KI_DELETE;
    case platform::Key::Space: return Rml::Input::KI_SPACE;
    case platform::Key::Home: return Rml::Input::KI_HOME;
    case platform::Key::End: return Rml::Input::KI_END;
    case platform::Key::PageUp: return Rml::Input::KI_PRIOR;
    case platform::Key::PageDown: return Rml::Input::KI_NEXT;
    case platform::Key::LeftShift: return Rml::Input::KI_LSHIFT;
    case platform::Key::RightShift: return Rml::Input::KI_RSHIFT;
    case platform::Key::LeftControl: return Rml::Input::KI_LCONTROL;
    case platform::Key::RightControl: return Rml::Input::KI_RCONTROL;
    case platform::Key::LeftAlt: return Rml::Input::KI_LMENU;
    case platform::Key::RightAlt: return Rml::Input::KI_RMENU;
    default: return Rml::Input::KI_UNKNOWN;
  }
}

int toRmlModifiers(const platform::Modifiers& mods) {
  int flags = 0;
  if (mods.shift) flags |= Rml::Input::KM_SHIFT;
  if (mods.control) flags |= Rml::Input::KM_CTRL;
  if (mods.alt) flags |= Rml::Input::KM_ALT;
  if (mods.super) flags |= Rml::Input::KM_META;
  return flags;
}

int toRmlMouseButton(platform::MouseButton button) {
  switch (button) {
    case platform::MouseButton::Left: return 0;
    case platform::MouseButton::Right: return 1;
    case platform::MouseButton::Middle: return 2;
    default: return -1;
  }
}

template <typename ColorT>
uint32_t packColor(const ColorT& c) {
  return static_cast<uint32_t>(c.red) |
         (static_cast<uint32_t>(c.green) << 8) |
         (static_cast<uint32_t>(c.blue) << 16) |
         (static_cast<uint32_t>(c.alpha) << 24);
}

class RmlUiLayer final : public app::UiLayer,
                         public Rml::RenderInterface,
                         public Rml::SystemInterface,
                         public Rml::FileInterface {
 public:
  RmlUiLayer(RmlUiLayerCallbacks callbacks, RmlUiLayerConfig config)
      : callbacks_(std::move(callbacks)), config_(std::move(config)) {
    if (config_.initialize_rmlui != config_.shutdown_rmlui) {
      throw std::invalid_argument(
          "RmlUi lifecycle must be either fully owned or fully borrowed.");
    }
    if (config_.initialize_rmlui) {
      previous_system_interface_ = Rml::GetSystemInterface();
      previous_render_interface_ = Rml::GetRenderInterface();
      previous_file_interface_ = Rml::GetFileInterface();
      Rml::SetSystemInterface(this);
      Rml::SetRenderInterface(this);
      Rml::SetFileInterface(this);
      if (!Rml::Initialise()) {
        restoreInterfaces();
        throw std::runtime_error("Failed to initialize RmlUi.");
      }
      initialized_rmlui_ = true;
    }
#ifndef RMLUI_SVG_PLUGIN
    spdlog::warn("RmlUi: SVG plugin is not enabled; <svg> elements will not render.");
#endif
  }

  ~RmlUiLayer() override {
    try {
      onShutdown();
    } catch (const std::exception& e) {
      spdlog::error("RmlUi shutdown failed: {}", e.what());
    } catch (...) {
      spdlog::error("RmlUi shutdown failed with an unknown exception.");
    }
  }

  app::UiEventDisposition onEvent(const platform::Event& event) override {
    if (!context_) {
      return app::UiEventDisposition::Ignored;
    }
    bool not_consumed = true;
    const int mods = toRmlModifiers(event.mods);
    switch (event.type) {
      case platform::EventType::KeyDown:
        if (const auto key = toRmlKey(event.key); key != Rml::Input::KI_UNKNOWN) {
          not_consumed = context_->ProcessKeyDown(key, mods);
          keyboard_capture_ = !not_consumed;
        }
        break;
      case platform::EventType::KeyUp:
        if (const auto key = toRmlKey(event.key); key != Rml::Input::KI_UNKNOWN) {
          not_consumed = context_->ProcessKeyUp(key, mods);
          keyboard_capture_ = !not_consumed;
        }
        break;
      case platform::EventType::TextInput:
        if (event.codepoint != 0 && validCodepoint(event.codepoint)) {
          not_consumed =
              context_->ProcessTextInput(static_cast<Rml::Character>(event.codepoint));
          keyboard_capture_ = !not_consumed;
        }
        break;
      case platform::EventType::MouseMove:
        if (std::isfinite(event.x) && std::isfinite(event.y)) {
          not_consumed = context_->ProcessMouseMove(toRmlCoordinate(event.x),
                                                    toRmlCoordinate(event.y),
                                                    mods);
        }
        break;
      case platform::EventType::MouseButtonDown: {
        const int button = toRmlMouseButton(event.mouseButton);
        if (button >= 0) {
          not_consumed = context_->ProcessMouseButtonDown(button, mods);
        }
        break;
      }
      case platform::EventType::MouseButtonUp: {
        const int button = toRmlMouseButton(event.mouseButton);
        if (button >= 0) {
          not_consumed = context_->ProcessMouseButtonUp(button, mods);
        }
        break;
      }
      case platform::EventType::MouseScroll:
        if (std::isfinite(event.scrollX) && std::isfinite(event.scrollY)) {
          not_consumed = context_->ProcessMouseWheel(
              Rml::Vector2f(-toRmlScroll(event.scrollX),
                            -toRmlScroll(event.scrollY)),
              mods);
        }
        break;
      case platform::EventType::WindowFocus:
        if (!event.focused) {
          not_consumed = context_->ProcessMouseLeave();
          keyboard_capture_ = false;
        }
        break;
      default:
        break;
    }
    return not_consumed ? app::UiEventDisposition::Ignored
                        : app::UiEventDisposition::Consumed;
  }

  app::UiInputCapture inputCapture() const override {
    return app::UiInputCapture{.keyboard = keyboard_capture_,
                               .pointer = context_ && context_->IsMouseInteracting(),
                               .gamepad = false};
  }

  void onFrame(app::UIContext& ctx) override {
    rendering::UIDrawData& out = ctx.drawData();
    out.clear();
    if (shutdown_) {
      return;
    }
    ctx_ = &ctx;

    const app::UIFrameInfo frame = ctx.frame();
    width_ = std::max(frame.logical_width > 0 ? frame.logical_width : frame.viewport_w, 0);
    height_ = std::max(frame.logical_height > 0 ? frame.logical_height : frame.viewport_h, 0);
    framebuffer_width_ = std::max(
        frame.framebuffer_width > 0 ? frame.framebuffer_width : frame.viewport_w, 0);
    framebuffer_height_ = std::max(
        frame.framebuffer_height > 0 ? frame.framebuffer_height : frame.viewport_h, 0);
    scale_x_ = frame.scale_x > 0.0f ? frame.scale_x : frame.dpi_scale;
    scale_y_ = frame.scale_y > 0.0f ? frame.scale_y : frame.dpi_scale;
    if (std::isfinite(frame.dt) && frame.dt > 0.0f &&
        time_ <= std::numeric_limits<double>::max() - frame.dt) {
      time_ += frame.dt;
    }

    if (!context_) {
      context_ = Rml::CreateContext(config_.context_name,
                                    Rml::Vector2i(width_, height_),
                                    this);
      if (context_ && callbacks_.on_context_ready) {
        callbacks_.on_context_ready(*context_);
      }
    }
    if (context_) {
      context_->SetDimensions(Rml::Vector2i(width_, height_));
      if (callbacks_.on_frame) {
        callbacks_.on_frame(*context_, ctx);
      }
      context_->Update();
      context_->Render();
    }
  }

  void onShutdown() override {
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
    std::exception_ptr callback_error;
    if (context_ && callbacks_.on_shutdown) {
      try {
        callbacks_.on_shutdown(*context_);
      } catch (...) {
        callback_error = std::current_exception();
      }
    }
    if (context_) {
      Rml::RemoveContext(context_->GetName());
      context_ = nullptr;
    }
    Rml::ReleaseTextures(this);
    Rml::ReleaseCompiledGeometry(this);
    for (auto& entry : textures_) {
      if (ctx_) {
        ctx_->destroyTexture(entry.second);
      }
    }
    textures_.clear();
    geometries_.clear();
    if (initialized_rmlui_ && config_.shutdown_rmlui) {
      Rml::Shutdown();
      initialized_rmlui_ = false;
      restoreInterfaces();
    }
    ctx_ = nullptr;
    if (callback_error) {
      std::rethrow_exception(callback_error);
    }
  }

  double GetElapsedTime() override {
    return time_;
  }

  void SetMouseCursor(const Rml::String& cursor_name) override {
    if (!ctx_) {
      return;
    }
    platform::CursorShape shape = platform::CursorShape::Default;
    if (cursor_name == "pointer") {
      shape = platform::CursorShape::Pointer;
    } else if (cursor_name == "text") {
      shape = platform::CursorShape::Text;
    } else if (cursor_name == "cross" || cursor_name == "crosshair") {
      shape = platform::CursorShape::Crosshair;
    } else if (cursor_name == "move") {
      shape = platform::CursorShape::Move;
    } else if (cursor_name == "resize") {
      shape = platform::CursorShape::ResizeDiagonalNwSe;
    } else if (cursor_name == "unavailable") {
      shape = platform::CursorShape::NotAllowed;
    }
    ctx_->setCursorShape(shape);
  }

  void SetClipboardText(const Rml::String& text) override {
    if (ctx_) {
      ctx_->setClipboardText(text);
    }
  }

  void GetClipboardText(Rml::String& text) override {
    text = ctx_ ? ctx_->clipboardText() : std::string{};
  }

  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
    switch (type) {
      case Rml::Log::LT_ERROR:
        spdlog::error("RmlUi: {}", message);
        break;
      case Rml::Log::LT_WARNING:
        spdlog::warn("RmlUi: {}", message);
        break;
      default:
        spdlog::info("RmlUi: {}", message);
        break;
    }
    return true;
  }

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override {
    if (vertices.empty() || indices.empty() ||
        vertices.size() > rendering::kMaxUIVertices ||
        indices.size() > rendering::kMaxUIIndices ||
        std::any_of(indices.begin(), indices.end(), [&](int index) {
          return index < 0 || static_cast<size_t>(index) >= vertices.size();
        })) {
      return 0;
    }
    Geometry geom{};
    geom.vertices.assign(vertices.begin(), vertices.end());
    geom.indices.assign(indices.begin(), indices.end());
    if (next_geometry_handle_ == 0) {
      return 0;
    }
    const auto handle = next_geometry_handle_++;
    geometries_.emplace(handle, std::move(geom));
    return handle;
  }

  void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation,
                      Rml::TextureHandle texture) override {
    auto it = geometries_.find(geometry);
    if (it == geometries_.end() || !ctx_ ||
        !std::isfinite(translation.x) || !std::isfinite(translation.y)) {
      return;
    }

    rendering::UIDrawData& out = ctx_->drawData();
    const size_t base_vertex = out.vertices.size();
    const size_t base_index = out.indices.size();
    if (base_vertex > rendering::kMaxUIVertices ||
        it->second.vertices.size() > rendering::kMaxUIVertices - base_vertex ||
        base_index > rendering::kMaxUIIndices ||
        it->second.indices.size() > rendering::kMaxUIIndices - base_index ||
        out.commands.size() >= rendering::kMaxUIDrawCommands) {
      return;
    }

    rendering::UIDrawCmd cmd{};
    cmd.blend_mode = rendering::UIBlendMode::PremultipliedAlpha;
    cmd.sampler_mode = rendering::UISamplerMode::Linear;
    cmd.texture_mode = rendering::UITextureMode::Color;
    cmd.scissor_enabled = scissor_enabled_;
    if (scissor_enabled_) {
      const int64_t left = std::max<int64_t>(
          static_cast<int64_t>(std::floor(scissor_.Left() * scale_x_)), 0);
      const int64_t top = std::max<int64_t>(
          static_cast<int64_t>(std::floor(scissor_.Top() * scale_y_)), 0);
      const int64_t right = std::min<int64_t>(
          static_cast<int64_t>(std::ceil(scissor_.Right() * scale_x_)),
          framebuffer_width_);
      const int64_t bottom = std::min<int64_t>(
          static_cast<int64_t>(std::ceil(scissor_.Bottom() * scale_y_)),
          framebuffer_height_);
      if (right <= left || bottom <= top) {
        return;
      }
      cmd.scissor_x = static_cast<int>(left);
      cmd.scissor_y = static_cast<int>(top);
      cmd.scissor_w = static_cast<int>(right - left);
      cmd.scissor_h = static_cast<int>(bottom - top);
    }

    out.vertices.reserve(base_vertex + it->second.vertices.size());
    out.indices.reserve(base_index + it->second.indices.size());
    out.commands.reserve(out.commands.size() + 1u);

    for (const auto& v : it->second.vertices) {
      Rml::Vector2f pos = v.position + translation;
      if (has_transform_) {
        const float x = pos.x;
        const float y = pos.y;
        const float tx = transform_[0][0] * x + transform_[1][0] * y + transform_[3][0];
        const float ty = transform_[0][1] * x + transform_[1][1] * y + transform_[3][1];
        pos.x = tx;
        pos.y = ty;
      }
      if (!std::isfinite(pos.x) || !std::isfinite(pos.y) ||
          !std::isfinite(v.tex_coord.x) || !std::isfinite(v.tex_coord.y)) {
        out.vertices.resize(base_vertex);
        return;
      }

      rendering::UIVertex out_v{};
      out_v.x = pos.x * scale_x_;
      out_v.y = pos.y * scale_y_;
      out_v.u = v.tex_coord.x;
      out_v.v = v.tex_coord.y;
      out_v.rgba = packColor(v.colour);
      out.vertices.push_back(out_v);
    }

    for (const int idx : it->second.indices) {
      out.indices.push_back(static_cast<uint32_t>(idx) + static_cast<uint32_t>(base_vertex));
    }

    cmd.index_offset = static_cast<uint32_t>(base_index);
    cmd.index_count = static_cast<uint32_t>(it->second.indices.size());
    cmd.texture = resolveTexture(texture);
    out.commands.push_back(cmd);
  }

  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
    geometries_.erase(geometry);
  }

  void EnableScissorRegion(bool enable) override {
    scissor_enabled_ = enable;
  }

  void SetScissorRegion(Rml::Rectanglei region) override {
    scissor_ = region;
  }

  Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                 const Rml::String& source) override {
    if (!ctx_) {
      texture_dimensions = Rml::Vector2i(0, 0);
      return 0;
    }

    const std::filesystem::path path = resolveAssetPath(source.c_str());
    if (path.extension() == ".svg") {
      spdlog::warn("RmlUi: SVG not supported by texture loader ({})", path.string());
      texture_dimensions = Rml::Vector2i(0, 0);
      return 0;
    }

    app::UITexture tex = ctx_->loadTextureRGBA8FromPng(path, true);
    if (!tex) {
      texture_dimensions = Rml::Vector2i(0, 0);
      return 0;
    }

    if (next_texture_handle_ == 0) {
      ctx_->destroyTexture(tex.handle);
      texture_dimensions = Rml::Vector2i(0, 0);
      return 0;
    }
    const auto handle = next_texture_handle_++;
    textures_[handle] = tex.handle;
    texture_dimensions = Rml::Vector2i(tex.width, tex.height);
    return handle;
  }

  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                     Rml::Vector2i source_dimensions) override {
    if (!ctx_ || source.empty() || source_dimensions.x <= 0 ||
        source_dimensions.y <= 0) {
      return 0;
    }
    const size_t width = static_cast<size_t>(source_dimensions.x);
    const size_t height = static_cast<size_t>(source_dimensions.y);
    if (height > kMaxGeneratedTextureBytes / 4u / width) {
      return 0;
    }
    const size_t expected_a8 = width * height;
    const size_t expected_rgba = expected_a8 * 4u;
    if (expected_rgba > kMaxGeneratedTextureBytes) {
      return 0;
    }
    const Rml::byte* data = source.data();
    std::vector<Rml::byte> expanded;
    if (source.size() == expected_a8) {
      expanded.resize(expected_rgba);
      for (size_t i = 0; i < expected_a8; ++i) {
        const Rml::byte a = source[i];
        const size_t o = i * 4;
        expanded[o + 0] = a;
        expanded[o + 1] = a;
        expanded[o + 2] = a;
        expanded[o + 3] = a;
      }
      data = expanded.data();
    } else if (source.size() != expected_rgba) {
      return 0;
    }

    if (next_texture_handle_ == 0) {
      return 0;
    }
    const auto handle = next_texture_handle_++;
    const app::UITextureHandle tex =
        ctx_->createTextureRGBA8(source_dimensions.x, source_dimensions.y, data);
    if (tex != 0) {
      textures_[handle] = tex;
      return handle;
    }
    spdlog::warn("RmlUi: failed to create texture {}x{} bytes={}",
                 source_dimensions.x,
                 source_dimensions.y,
                 source.size());
    return 0;
  }

  void ReleaseTexture(Rml::TextureHandle texture_handle) override {
    auto it = textures_.find(texture_handle);
    if (it == textures_.end()) {
      return;
    }
    if (ctx_) {
      ctx_->destroyTexture(it->second);
    }
    textures_.erase(it);
  }

  void EnableClipMask(bool /*enable*/) override {}

  void RenderToClipMask(Rml::ClipMaskOperation /*operation*/,
                        Rml::CompiledGeometryHandle /*geometry*/,
                        Rml::Vector2f /*translation*/) override {}

  void SetTransform(const Rml::Matrix4f* transform) override {
    if (transform) {
      transform_ = *transform;
      has_transform_ = true;
    } else {
      has_transform_ = false;
    }
  }

  Rml::FileHandle Open(const Rml::String& path) override {
    const std::filesystem::path resolved = resolveFilePath(path);
    FILE* file = std::fopen(resolved.string().c_str(), "rb");
    if (!file) {
      return Rml::FileHandle(0);
    }
    return reinterpret_cast<Rml::FileHandle>(file);
  }

  void Close(Rml::FileHandle file) override {
    if (!file) {
      return;
    }
    std::fclose(reinterpret_cast<FILE*>(file));
  }

  size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
    if (!file || !buffer || size == 0) {
      return 0;
    }
    return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
  }

  bool Seek(Rml::FileHandle file, long offset, int origin) override {
    if (!file) {
      return false;
    }
    return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
  }

  size_t Tell(Rml::FileHandle file) override {
    if (!file) {
      return 0;
    }
    const long pos = std::ftell(reinterpret_cast<FILE*>(file));
    return pos < 0 ? 0u : static_cast<size_t>(pos);
  }

  size_t Length(Rml::FileHandle file) override {
    if (!file) {
      return 0;
    }
    const long cur = std::ftell(reinterpret_cast<FILE*>(file));
    if (cur < 0) {
      return 0;
    }
    std::fseek(reinterpret_cast<FILE*>(file), 0, SEEK_END);
    const long end = std::ftell(reinterpret_cast<FILE*>(file));
    std::fseek(reinterpret_cast<FILE*>(file), cur, SEEK_SET);
    return end < 0 ? 0u : static_cast<size_t>(end);
  }

 private:
  struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
  };

  std::filesystem::path resolveAssetPath(const std::filesystem::path& source) const {
    if (source.is_absolute() && std::filesystem::exists(source)) {
      return source;
    }
    for (const auto& root : config_.asset_roots) {
      const std::filesystem::path candidate = root / source;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }

    std::filesystem::path cwd = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
      const std::filesystem::path direct = cwd / source;
      if (std::filesystem::exists(direct)) {
        return direct;
      }
      if (!cwd.has_parent_path()) {
        break;
      }
      cwd = cwd.parent_path();
    }
    return source;
  }

  std::filesystem::path resolveFilePath(Rml::String path) const {
    constexpr std::string_view file_prefix = "file://";
    if (path.rfind(file_prefix.data(), 0) == 0) {
      path = path.substr(file_prefix.size());
    }

    std::filesystem::path resolved(path.c_str());
    if (resolved.is_absolute()) {
      return resolved;
    }
    if (path.rfind("home/", 0) == 0) {
      return std::filesystem::path("/") / resolved;
    }
    return resolveAssetPath(resolved);
  }

  app::UITextureHandle resolveTexture(Rml::TextureHandle texture) const {
    if (texture == 0) {
      return 0;
    }
    auto it = textures_.find(texture);
    if (it == textures_.end()) {
      return 0;
    }
    return it->second;
  }

  void restoreInterfaces() {
    Rml::SetSystemInterface(previous_system_interface_);
    Rml::SetRenderInterface(previous_render_interface_);
    Rml::SetFileInterface(previous_file_interface_);
  }

  RmlUiLayerCallbacks callbacks_{};
  RmlUiLayerConfig config_{};
  app::UIContext* ctx_ = nullptr;
  Rml::Context* context_ = nullptr;
  Rml::SystemInterface* previous_system_interface_ = nullptr;
  Rml::RenderInterface* previous_render_interface_ = nullptr;
  Rml::FileInterface* previous_file_interface_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int framebuffer_width_ = 0;
  int framebuffer_height_ = 0;
  float scale_x_ = 1.0f;
  float scale_y_ = 1.0f;
  double time_ = 0.0;

  Rml::CompiledGeometryHandle next_geometry_handle_ = 1;
  Rml::TextureHandle next_texture_handle_ = 1;
  std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometries_;
  std::unordered_map<Rml::TextureHandle, app::UITextureHandle> textures_;
  bool shutdown_ = false;
  bool initialized_rmlui_ = false;
  bool keyboard_capture_ = false;
  bool scissor_enabled_ = false;
  Rml::Rectanglei scissor_{};
  bool has_transform_ = false;
  Rml::Matrix4f transform_{};
};

}  // namespace

std::unique_ptr<app::UiLayer> createUiLayer(RmlUiLayerConfig config) {
  return createUiLayer(RmlUiLayerCallbacks{}, std::move(config));
}

std::unique_ptr<app::UiLayer> createUiLayer(ContextCallback on_context_ready,
                                            RmlUiLayerConfig config) {
  RmlUiLayerCallbacks callbacks{};
  callbacks.on_context_ready = std::move(on_context_ready);
  return createUiLayer(std::move(callbacks), std::move(config));
}

std::unique_ptr<app::UiLayer> createUiLayer(RmlUiLayerCallbacks callbacks,
                                            RmlUiLayerConfig config) {
  return std::make_unique<RmlUiLayer>(std::move(callbacks), std::move(config));
}

}  // namespace karma::ui::rmlui
