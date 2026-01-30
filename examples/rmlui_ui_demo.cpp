#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "karma/karma.h"

#include <RmlUi/Core.h>
#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {
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

const char* kDemoRml =
    "<rml><body>"
    "<div style=\"width:320px;padding:12px;background:#1b2433;border-width:1px;border-color:#32435f;\">"
    "<div style=\"font-family:Roboto;font-weight:900;font-size:20px;\">Karma RmlUi</div>"
    "<div style=\"font-family:Roboto;font-weight:900;margin-top:6px;\">Hello from the minimal demo</div>"
    "</div>"
    "</body></rml>";
}  // namespace

class RmlUiLayer final : public app::UiLayer,
                         public Rml::RenderInterface,
                         public Rml::SystemInterface,
                         public Rml::FileInterface {
 public:
  RmlUiLayer() {
    Rml::SetSystemInterface(this);
    Rml::SetRenderInterface(this);
    Rml::SetFileInterface(this);
    Rml::Initialise();
  }

  ~RmlUiLayer() override {
    onShutdown();
  }

  void onEvent(const platform::Event& event) override {
    if (!context_) {
      return;
    }
    const int mods = toRmlModifiers(event.mods);
    switch (event.type) {
      case platform::EventType::KeyDown:
        context_->ProcessKeyDown(toRmlKey(event.key), mods);
        break;
      case platform::EventType::KeyUp:
        context_->ProcessKeyUp(toRmlKey(event.key), mods);
        break;
      case platform::EventType::TextInput:
        if (event.codepoint != 0) {
          context_->ProcessTextInput(static_cast<Rml::Character>(event.codepoint));
        }
        break;
      case platform::EventType::MouseMove:
        context_->ProcessMouseMove(static_cast<int>(event.x), static_cast<int>(event.y), mods);
        break;
      case platform::EventType::MouseButtonDown: {
        const int button = toRmlMouseButton(event.mouseButton);
        if (button >= 0) {
          context_->ProcessMouseButtonDown(button, mods);
        }
        break;
      }
      case platform::EventType::MouseButtonUp: {
        const int button = toRmlMouseButton(event.mouseButton);
        if (button >= 0) {
          context_->ProcessMouseButtonUp(button, mods);
        }
        break;
      }
      case platform::EventType::MouseScroll:
        context_->ProcessMouseWheel(static_cast<float>(event.scrollY), mods);
        break;
      default:
        break;
    }
  }

  void onFrame(app::UIContext& ctx) override {
    ctx_ = &ctx;
    const auto frame = ctx.frame();
    width_ = frame.viewport_w;
    height_ = frame.viewport_h;
    time_ += frame.dt;

    if (!context_) {
      context_ = Rml::CreateContext("karma", Rml::Vector2i(width_, height_));
      createDocument();
    }
    if (context_) {
      context_->SetDimensions(Rml::Vector2i(width_, height_));
      context_->Update();
      context_->Render();
    }
  }

  void onShutdown() override {
    if (context_) {
      Rml::RemoveContext(context_->GetName());
      context_ = nullptr;
    }
    for (auto& entry : textures_) {
      if (ctx_) {
        ctx_->destroyTexture(entry.second);
      }
    }
    textures_.clear();
    geometries_.clear();
    Rml::Shutdown();
  }

  double GetElapsedTime() override {
    return time_;
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
    if (vertices.empty() || indices.empty()) {
      return 0;
    }
    Geometry geom{};
    geom.vertices.assign(vertices.begin(), vertices.end());
    geom.indices.assign(indices.begin(), indices.end());
    const auto handle = next_geometry_handle_++;
    geometries_[handle] = std::move(geom);
    return handle;
  }

  void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation,
                      Rml::TextureHandle texture) override {
    auto it = geometries_.find(geometry);
    if (it == geometries_.end() || !ctx_) {
      return;
    }

    app::UIDrawData& out = ctx_->drawData();
    const size_t base_vertex = out.vertices.size();
    const size_t base_index = out.indices.size();

    out.vertices.reserve(base_vertex + it->second.vertices.size());
    out.indices.reserve(base_index + it->second.indices.size());

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

      app::UIVertex out_v{};
      out_v.x = pos.x;
      out_v.y = pos.y;
      out_v.u = v.tex_coord.x;
      out_v.v = v.tex_coord.y;
      out_v.rgba = packColor(v.colour);
      out.vertices.push_back(out_v);
    }

    for (const int idx : it->second.indices) {
      out.indices.push_back(static_cast<uint32_t>(idx) + static_cast<uint32_t>(base_vertex));
    }

    app::UIDrawCmd cmd{};
    cmd.index_offset = static_cast<uint32_t>(base_index);
    cmd.index_count = static_cast<uint32_t>(it->second.indices.size());
    cmd.scissor_enabled = scissor_enabled_;
    if (scissor_enabled_) {
      cmd.scissor_x = scissor_.Left();
      cmd.scissor_y = scissor_.Top();
      cmd.scissor_w = scissor_.Width();
      cmd.scissor_h = scissor_.Height();
    }
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
    (void)source;
    texture_dimensions = Rml::Vector2i(0, 0);
    return 0;
  }

  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                     Rml::Vector2i source_dimensions) override {
    if (!ctx_ || source.empty()) {
      return 0;
    }
    const size_t expected_rgba = static_cast<size_t>(source_dimensions.x) *
                                 static_cast<size_t>(source_dimensions.y) * 4;
    const size_t expected_a8 = static_cast<size_t>(source_dimensions.x) *
                               static_cast<size_t>(source_dimensions.y);
    const Rml::byte* data = source.data();
    std::vector<Rml::byte> expanded;
    if (source.size() == expected_a8) {
      expanded.resize(expected_rgba);
      for (size_t i = 0; i < expected_a8; ++i) {
        const Rml::byte a = source[i];
        const size_t o = i * 4;
        expanded[o + 0] = 255;
        expanded[o + 1] = 255;
        expanded[o + 2] = 255;
        expanded[o + 3] = a;
      }
      data = expanded.data();
    } else if (source.size() != expected_rgba) {
      return 0;
    }

    const auto handle = next_texture_handle_++;
    app::UITextureHandle tex = ctx_->createTextureRGBA8(source_dimensions.x,
                                                        source_dimensions.y,
                                                        data);
    if (tex != 0) {
      textures_[handle] = tex;
      return handle;
    }
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
    std::filesystem::path resolved(path);
    if (!resolved.is_absolute()) {
      const std::filesystem::path cwd = std::filesystem::current_path() / resolved;
      if (std::filesystem::exists(cwd)) {
        resolved = cwd;
      }
    }
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

  void createDocument() {
    if (!context_) {
      return;
    }
    const auto weight = static_cast<Rml::Style::FontWeight>(900);
    Rml::LoadFontFace("examples/assets/Roboto-Black.ttf", false, weight);
    Rml::LoadFontFace("examples/assets/Roboto-Black.ttf", true, weight);

    document_ = context_->LoadDocumentFromMemory(kDemoRml, "[rmlui-minimal]");
    if (!document_) {
      spdlog::warn("RmlUi: failed to load demo RML");
      return;
    }
    document_->Show();
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

  app::UIContext* ctx_ = nullptr;
  Rml::Context* context_ = nullptr;
  Rml::ElementDocument* document_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  double time_ = 0.0;

  Rml::CompiledGeometryHandle next_geometry_handle_ = 1;
  Rml::TextureHandle next_texture_handle_ = 1;
  std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometries_;
  std::unordered_map<Rml::TextureHandle, app::UITextureHandle> textures_;
  bool scissor_enabled_ = false;
  Rml::Rectanglei scissor_{};
  bool has_transform_ = false;
  Rml::Matrix4f transform_{};
};

class DemoGame : public app::GameInterface {
 public:
  void onStart() override {}
  void onFixedUpdate(float /*dt*/) override {}
  void onUpdate(float /*dt*/) override {}
  void onShutdown() override {}
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp app;
  karma::demo::DemoGame game;
  app.setUi(std::make_unique<karma::demo::RmlUiLayer>());

  karma::app::EngineConfig config;
  config.window.title = "Karma RmlUi";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;

  app.start(game, config);
  while (app.isRunning()) {
    app.tick();
  }

  return 0;
}
