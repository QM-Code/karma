#include "karma/ui/rmlui_overlay.h"

#include "karma/renderer/backends/diligent/backend.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../../third_party/stb_image.h"

namespace karma::ui {

namespace {
struct LoadedImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

std::vector<unsigned char> readFileBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return {};
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size <= 0) {
    return {};
  }
  stream.seekg(0, std::ios::beg);
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

LoadedImage loadImageFromFile(const std::filesystem::path& path) {
  LoadedImage image{};
  const auto bytes = readFileBytes(path);
  if (bytes.empty()) {
    return image;
  }
  int w = 0;
  int h = 0;
  int comp = 0;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                           &w, &h, &comp, 4);
  stbi_set_flip_vertically_on_load(1);
  if (!decoded || w <= 0 || h <= 0) {
    stbi_image_free(decoded);
    return image;
  }
  image.width = w;
  image.height = h;
  image.pixels.assign(decoded, decoded + (static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
  stbi_image_free(decoded);
  return image;
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
  if (mods.shift) {
    flags |= Rml::Input::KM_SHIFT;
  }
  if (mods.control) {
    flags |= Rml::Input::KM_CTRL;
  }
  if (mods.alt) {
    flags |= Rml::Input::KM_ALT;
  }
  if (mods.super) {
    flags |= Rml::Input::KM_META;
  }
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

}  // namespace

RmlUiOverlay::RmlUiOverlay() {
  Rml::SetSystemInterface(this);
  Rml::SetRenderInterface(this);
  Rml::SetFileInterface(this);
  Rml::Initialise();
}

RmlUiOverlay::~RmlUiOverlay() {
  onShutdown();
}

void RmlUiOverlay::setDocumentRml(std::string rml) {
  addDocumentRml("main", std::move(rml), true);
}

bool RmlUiOverlay::setDocumentFile(const std::filesystem::path& path) {
  auto& doc = documents_["main"];
  doc.from_file = true;
  doc.source_path = resolveAssetPath(path);
  doc.show = true;
  return reloadDocumentFromFile(doc);
}

void RmlUiOverlay::addDocumentRml(std::string name, std::string rml, bool show) {
  auto& doc = documents_[std::move(name)];
  doc.rml = std::move(rml);
  doc.show = show;
  doc.dirty = true;
}

bool RmlUiOverlay::addDocumentFile(std::string name, const std::filesystem::path& path, bool show) {
  auto& doc = documents_[std::move(name)];
  doc.from_file = true;
  doc.source_path = resolveAssetPath(path);
  doc.show = show;
  return reloadDocumentFromFile(doc);
}

void RmlUiOverlay::setHotReloadEnabled(bool enabled) {
  hot_reload_enabled_ = enabled;
}

void RmlUiOverlay::setHotReloadInterval(double seconds) {
  hot_reload_interval_ = seconds > 0.0 ? seconds : 0.0;
}

void RmlUiOverlay::showDocument(std::string name) {
  auto it = documents_.find(name);
  if (it == documents_.end()) {
    return;
  }
  it->second.show = true;
  if (it->second.doc) {
    it->second.doc->Show();
  }
}

void RmlUiOverlay::closeDocument(std::string name) {
  auto it = documents_.find(name);
  if (it == documents_.end()) {
    return;
  }
  it->second.show = false;
  if (it->second.doc) {
    it->second.doc->Close();
  }
}

void RmlUiOverlay::setContextReadyCallback(std::function<void(Rml::Context&)> callback) {
  context_ready_cb_ = std::move(callback);
  context_ready_called_ = false;
}

void RmlUiOverlay::setAssetRoot(std::string root) {
  asset_root_ = std::filesystem::path(std::move(root));
}

void RmlUiOverlay::addFontFaceFile(std::string path, bool fallback, Rml::Style::FontWeight weight) {
  font_requests_.push_back(FontRequest{std::move(path), fallback, weight});
}

void RmlUiOverlay::clearFonts() {
  font_requests_.clear();
}

void RmlUiOverlay::onFrameBegin(int width, int height, float dt) {
  width_ = width;
  height_ = height;
  dt_ = dt > 0.0f ? dt : (1.0f / 60.0f);
  time_ += static_cast<double>(dt_);
  if (!context) {
    context = Rml::CreateContext("karma", Rml::Vector2i(width_, height_));
  }
  if (context) {
    context->SetDimensions(Rml::Vector2i(width_, height_));
  }
}

void RmlUiOverlay::onEvent(const platform::Event& event) {
  if (!context) {
    return;
  }
  const int mods = toRmlModifiers(event.mods);
  switch (event.type) {
    case platform::EventType::KeyDown:
      context->ProcessKeyDown(toRmlKey(event.key), mods);
      break;
    case platform::EventType::KeyUp:
      context->ProcessKeyUp(toRmlKey(event.key), mods);
      break;
    case platform::EventType::TextInput:
      if (event.codepoint != 0) {
        context->ProcessTextInput(static_cast<Rml::Character>(event.codepoint));
      }
      break;
    case platform::EventType::MouseMove:
      context->ProcessMouseMove(static_cast<int>(event.x), static_cast<int>(event.y), mods);
      break;
    case platform::EventType::MouseButtonDown: {
      const int button = toRmlMouseButton(event.mouseButton);
      if (button >= 0) {
        context->ProcessMouseButtonDown(button, mods);
      }
      break;
    }
    case platform::EventType::MouseButtonUp: {
      const int button = toRmlMouseButton(event.mouseButton);
      if (button >= 0) {
        context->ProcessMouseButtonUp(button, mods);
      }
      break;
    }
    case platform::EventType::MouseScroll:
      context->ProcessMouseWheel(static_cast<float>(event.scrollY), mods);
      break;
    default:
      break;
  }
}

void RmlUiOverlay::onRender(renderer::GraphicsDevice& device) {
  device_ = &device;
  if (hot_reload_enabled_) {
    if ((time_ - last_hot_reload_check_) >= hot_reload_interval_) {
      last_hot_reload_check_ = time_;
      for (auto& [name, doc] : documents_) {
        if (!doc.from_file) {
          continue;
        }
        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(doc.source_path, ec);
        if (ec) {
          if (!doc.missing_warned) {
            spdlog::warn("RmlUiOverlay: missing document '{}'", doc.source_path.string());
            doc.missing_warned = true;
          }
          continue;
        }
        if (doc.last_write_time != write_time) {
          reloadDocumentFromFile(doc);
        }
      }
    }
  }
  ensurePipeline(device);
  loadFonts();
  if (context) {
    if (!context_ready_called_ && context_ready_cb_) {
      context_ready_cb_(*context);
      context_ready_called_ = true;
    }
    for (auto& [name, doc] : documents_) {
      if (doc.dirty) {
        if (doc.doc) {
          doc.doc->Close();
          doc.doc = nullptr;
        }
        if (!doc.rml.empty()) {
          doc.doc = context->LoadDocumentFromMemory(doc.rml);
          if (doc.doc && doc.show) {
            doc.doc->Show();
          }
        }
        doc.dirty = false;
      } else if (doc.doc) {
        if (doc.show) {
          doc.doc->Show();
        } else {
          doc.doc->Hide();
        }
      }
    }
    context->Update();
    context->Render();
  }
}

void RmlUiOverlay::onShutdown() {
  if (context) {
    Rml::RemoveContext(context->GetName());
    context = nullptr;
  }
  for (auto& [name, doc] : documents_) {
    if (doc.doc) {
      doc.doc->Close();
      doc.doc = nullptr;
    }
  }
  documents_.clear();
  geometries_.clear();
  textures_.clear();
  color_pso_ = {};
  color_pso_scissor_ = {};
  texture_pso_ = {};
  texture_pso_scissor_ = {};
  constants_.Release();
  sampler_.Release();
  render_device_ = nullptr;
  context_ = nullptr;
  swap_chain_ = nullptr;
  device_ = nullptr;
  Rml::Shutdown();
}

// SystemInterface

double RmlUiOverlay::GetElapsedTime() {
  return time_;
}

bool RmlUiOverlay::LogMessage(Rml::Log::Type type, const Rml::String& message) {
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

// RenderInterface

namespace {
struct alignas(16) RmlConstants {
  float transform[16];
  float translate[2];
  float pad[2];
};

void mul4x4(const float* a, const float* b, float* out) {
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += a[row * 4 + k] * b[k * 4 + col];
      }
      out[row * 4 + col] = sum;
    }
  }
}
}

Rml::CompiledGeometryHandle RmlUiOverlay::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                          Rml::Span<const int> indices) {
  if (!render_device_ || vertices.empty() || indices.empty()) {
    return 0;
  }
  Geometry geom;
  Diligent::BufferDesc vb_desc{};
  vb_desc.Name = "RmlUi Vertex Buffer";
  vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
  vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
  vb_desc.Size = static_cast<Diligent::Uint32>(vertices.size() * sizeof(Rml::Vertex));
  Diligent::BufferData vb_data{vertices.data(), vb_desc.Size};
  render_device_->CreateBuffer(vb_desc, &vb_data, &geom.vb);

  Diligent::BufferDesc ib_desc{};
  ib_desc.Name = "RmlUi Index Buffer";
  ib_desc.Usage = Diligent::USAGE_IMMUTABLE;
  ib_desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
  ib_desc.Size = static_cast<Diligent::Uint32>(indices.size() * sizeof(int));
  Diligent::BufferData ib_data{indices.data(), ib_desc.Size};
  render_device_->CreateBuffer(ib_desc, &ib_data, &geom.ib);

  geom.num_indices = static_cast<int>(indices.size());
  const auto handle = next_geometry_handle_++;
  geometries_[handle] = std::move(geom);
  return handle;
}

void RmlUiOverlay::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                  Rml::Vector2f translation,
                                  Rml::TextureHandle texture) {
  auto it = geometries_.find(geometry);
  if (it == geometries_.end()) {
    return;
  }
  const Geometry& geom = it->second;
  if (!render_device_ || !context_ || !swap_chain_) {
    return;
  }
  if (!geom.vb || !geom.ib) {
    return;
  }

  if (constants_) {
    Diligent::MapHelper<RmlConstants> cb_map(context_, constants_,
                                             Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    const float L = 0.0f;
    const float R = static_cast<float>(width_);
    const float T = 0.0f;
    const float B = static_cast<float>(height_);
    const float proj[16] = {
        2.0f / (R - L), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (T - B), 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f
    };
    RmlConstants c{};
    if (has_transform_) {
      float combined[16];
      mul4x4(transform_.data(), proj, combined);
      std::memcpy(c.transform, combined, sizeof(combined));
    } else {
      std::memcpy(c.transform, proj, sizeof(proj));
    }
    c.translate[0] = translation.x;
    c.translate[1] = translation.y;
    c.pad[0] = 0.0f;
    c.pad[1] = 0.0f;
    *cb_map = c;
  }

  Diligent::ITextureView* rtv = swap_chain_->GetCurrentBackBufferRTV();
  Diligent::ITextureView* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(width_);
  vp.Height = static_cast<float>(height_);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context_->SetViewports(1, &vp, static_cast<Diligent::Uint32>(width_),
                         static_cast<Diligent::Uint32>(height_));

  const bool use_scissor = scissor_enabled_;
  if (clip_mask_enabled_) {
    context_->SetStencilRef(static_cast<Diligent::Uint32>(stencil_ref_));
  }
  if (texture) {
    auto tex_it = textures_.find(texture);
    if (tex_it == textures_.end()) {
      return;
    }
    PsoPair& pair = clip_mask_enabled_
                        ? (use_scissor ? texture_pso_scissor_clip_ : texture_pso_clip_)
                        : (use_scissor ? texture_pso_scissor_ : texture_pso_);
    context_->SetPipelineState(pair.pipeline_state);
    if (auto* var = pair.shader_resource_binding->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "_texture")) {
      var->Set(tex_it->second.srv);
    }
    context_->CommitShaderResources(pair.shader_resource_binding,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  } else {
    PsoPair& pair = clip_mask_enabled_
                        ? (use_scissor ? color_pso_scissor_clip_ : color_pso_clip_)
                        : (use_scissor ? color_pso_scissor_ : color_pso_);
    context_->SetPipelineState(pair.pipeline_state);
    context_->CommitShaderResources(pair.shader_resource_binding,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }

  Diligent::Uint64 offset = 0;
  Diligent::IBuffer* vbs[] = {geom.vb};
  context_->SetVertexBuffers(0, 1, vbs, &offset,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
  context_->SetIndexBuffer(geom.ib, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::DrawIndexedAttribs draw{};
  draw.IndexType = Diligent::VT_UINT32;
  draw.NumIndices = static_cast<Diligent::Uint32>(geom.num_indices);
  draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
  context_->DrawIndexed(draw);
}

void RmlUiOverlay::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  geometries_.erase(geometry);
}

void RmlUiOverlay::EnableScissorRegion(bool enable) {
  scissor_enabled_ = enable;
}

void RmlUiOverlay::SetScissorRegion(Rml::Rectanglei region) {
  if (!context_) {
    return;
  }
  const int x = region.Left();
  const int y = region.Top();
  const int width = region.Width();
  const int height = region.Height();
  Diligent::Rect scissor{};
  scissor.left = x;
  scissor.right = x + width;
  scissor.top = y;
  scissor.bottom = y + height;
  context_->SetScissorRects(1, &scissor, static_cast<Diligent::Uint32>(width_),
                            static_cast<Diligent::Uint32>(height_));
}

Rml::TextureHandle RmlUiOverlay::LoadTexture(Rml::Vector2i& texture_dimensions,
                                             const Rml::String& source) {
  if (!render_device_) {
    return 0;
  }

  std::filesystem::path source_path = resolveAssetPath(source);
  if (source_path.empty() || !std::filesystem::exists(source_path)) {
    spdlog::warn("RmlUiOverlay: missing texture '{}'", source);
    return 0;
  }

  const LoadedImage image = loadImageFromFile(source_path);
  if (image.pixels.empty()) {
    spdlog::warn("RmlUiOverlay: failed to decode texture '{}'", source);
    return 0;
  }
  texture_dimensions.x = image.width;
  texture_dimensions.y = image.height;

  Texture tex;
  Diligent::TextureDesc desc{};
  desc.Name = source.c_str();
  desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width = static_cast<Diligent::Uint32>(image.width);
  desc.Height = static_cast<Diligent::Uint32>(image.height);
  desc.MipLevels = 1;
  desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
  desc.Usage = Diligent::USAGE_IMMUTABLE;

  Diligent::TextureSubResData subres{};
  subres.pData = image.pixels.data();
  subres.Stride = static_cast<Diligent::Uint32>(image.width * 4);
  Diligent::TextureData init_data{};
  init_data.pSubResources = &subres;
  init_data.NumSubresources = 1;

  render_device_->CreateTexture(desc, &init_data, &tex.texture);
  if (!tex.texture) {
    spdlog::warn("RmlUiOverlay: failed to create texture '{}'", source);
    return 0;
  }
  tex.srv = tex.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  if (!tex.srv) {
    spdlog::warn("RmlUiOverlay: missing texture SRV for '{}'", source);
    return 0;
  }
  const auto handle = next_texture_handle_++;
  textures_[handle] = std::move(tex);
  return handle;
}

Rml::TextureHandle RmlUiOverlay::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                 Rml::Vector2i source_dimensions) {
  if (!render_device_) {
    return 0;
  }
  Texture tex;
  Diligent::TextureDesc desc{};
  desc.Name = "RmlUi Texture";
  desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width = static_cast<Diligent::Uint32>(source_dimensions.x);
  desc.Height = static_cast<Diligent::Uint32>(source_dimensions.y);
  desc.MipLevels = 1;
  desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
  desc.Usage = Diligent::USAGE_IMMUTABLE;

  Diligent::TextureSubResData subres{};
  subres.pData = source.data();
  subres.Stride = static_cast<Diligent::Uint32>(source_dimensions.x * 4);
  Diligent::TextureData init_data{};
  init_data.pSubResources = &subres;
  init_data.NumSubresources = 1;

  render_device_->CreateTexture(desc, &init_data, &tex.texture);
  if (!tex.texture) {
    return 0;
  }
  tex.srv = tex.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  const auto handle = next_texture_handle_++;
  textures_[handle] = std::move(tex);
  return handle;
}

void RmlUiOverlay::ReleaseTexture(Rml::TextureHandle texture_handle) {
  textures_.erase(texture_handle);
}

void RmlUiOverlay::EnableClipMask(bool enable) {
  if (enable && !clip_mask_supported_) {
    if (!clip_mask_warned_) {
      spdlog::warn("RmlUiOverlay: clip masks require a stencil buffer.");
      clip_mask_warned_ = true;
    }
    clip_mask_enabled_ = false;
    return;
  }
  clip_mask_enabled_ = enable;
  if (clip_mask_enabled_ && context_) {
    stencil_ref_ = 1;
    context_->SetStencilRef(static_cast<Diligent::Uint32>(stencil_ref_));
  }
}

void RmlUiOverlay::RenderToClipMask(Rml::ClipMaskOperation operation,
                                    Rml::CompiledGeometryHandle geometry,
                                    Rml::Vector2f translation) {
  if (!clip_mask_enabled_ || !context_ || !swap_chain_) {
    return;
  }
  auto geom_it = geometries_.find(geometry);
  if (geom_it == geometries_.end()) {
    return;
  }
  auto* dsv = swap_chain_->GetDepthBufferDSV();
  if (!dsv) {
    return;
  }

  const bool clear_stencil = (operation == Rml::ClipMaskOperation::Set ||
                              operation == Rml::ClipMaskOperation::SetInverse);
  if (clear_stencil) {
    context_->ClearDepthStencil(dsv,
                                Diligent::CLEAR_STENCIL_FLAG,
                                1.0f,
                                0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    stencil_ref_ = 1;
  }

  const bool use_scissor = scissor_enabled_;
  PsoPair* pair = nullptr;
  if (operation == Rml::ClipMaskOperation::Intersect) {
    pair = use_scissor ? &mask_pso_incr_scissor_ : &mask_pso_incr_;
  } else {
    pair = use_scissor ? &mask_pso_replace_scissor_ : &mask_pso_replace_;
  }
  if (!pair || !pair->pipeline_state) {
    return;
  }

  Diligent::Uint32 mask_ref = 1;
  if (operation == Rml::ClipMaskOperation::SetInverse) {
    mask_ref = 0;
  }
  context_->SetStencilRef(mask_ref);

  {
    Diligent::MapHelper<RmlConstants> cb_map(context_, constants_, Diligent::MAP_WRITE,
                                             Diligent::MAP_FLAG_DISCARD);
    if (cb_map) {
      const float L = 0.0f;
      const float R = static_cast<float>(width_);
      const float T = 0.0f;
      const float B = static_cast<float>(height_);
      const float proj[16] = {
          2.0f / (R - L), 0.0f, 0.0f, 0.0f,
          0.0f, 2.0f / (T - B), 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f
      };
      RmlConstants c{};
      if (has_transform_) {
        float combined[16];
        mul4x4(transform_.data(), proj, combined);
        std::memcpy(c.transform, combined, sizeof(combined));
      } else {
        std::memcpy(c.transform, proj, sizeof(proj));
      }
      c.translate[0] = translation.x;
      c.translate[1] = translation.y;
      c.pad[0] = 0.0f;
      c.pad[1] = 0.0f;
      *cb_map = c;
    }
  }

  Diligent::ITextureView* rtv = swap_chain_->GetCurrentBackBufferRTV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(width_);
  vp.Height = static_cast<float>(height_);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context_->SetViewports(1, &vp, static_cast<Diligent::Uint32>(width_),
                         static_cast<Diligent::Uint32>(height_));

  context_->SetPipelineState(pair->pipeline_state);
  context_->CommitShaderResources(pair->shader_resource_binding,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Uint64 offset = 0;
  Diligent::IBuffer* vbs[] = {geom_it->second.vb};
  context_->SetVertexBuffers(0, 1, vbs, &offset,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
  context_->SetIndexBuffer(geom_it->second.ib, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::DrawIndexedAttribs draw{};
  draw.IndexType = Diligent::VT_UINT32;
  draw.NumIndices = static_cast<Diligent::Uint32>(geom_it->second.num_indices);
  draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
  context_->DrawIndexed(draw);

  if (operation == Rml::ClipMaskOperation::Intersect) {
    stencil_ref_ += 1;
  } else if (operation == Rml::ClipMaskOperation::SetInverse) {
    stencil_ref_ = 0;
  } else {
    stencil_ref_ = 1;
  }
  context_->SetStencilRef(static_cast<Diligent::Uint32>(stencil_ref_));
}

void RmlUiOverlay::SetTransform(const Rml::Matrix4f* transform) {
  if (transform) {
    transform_ = *transform;
    has_transform_ = true;
  } else {
    has_transform_ = false;
  }
}

void RmlUiOverlay::loadFonts() {
  for (auto& request : font_requests_) {
    if (request.loaded) {
      continue;
    }
    const std::filesystem::path font_path = request.path;
    if (!std::filesystem::exists(font_path)) {
      if (!request.warned) {
        spdlog::warn("RmlUiOverlay: missing font '{}'", request.path);
        request.warned = true;
      }
      continue;
    }
    request.loaded = Rml::LoadFontFace(font_path.string(), request.fallback, request.weight);
    if (!request.loaded && !request.warned) {
      spdlog::warn("RmlUiOverlay: failed to load font '{}'", request.path);
      request.warned = true;
    }
  }
}

bool RmlUiOverlay::reloadDocumentFromFile(DocumentDef& doc) {
  if (doc.source_path.empty()) {
    return false;
  }
  const std::filesystem::path resolved = resolveAssetPath(doc.source_path);
  if (resolved.empty() || !std::filesystem::exists(resolved)) {
    if (!doc.missing_warned) {
      spdlog::warn("RmlUiOverlay: missing document '{}'", doc.source_path.string());
      doc.missing_warned = true;
    }
    return false;
  }
  std::ifstream stream(resolved);
  if (!stream) {
    spdlog::warn("RmlUiOverlay: failed to open document '{}'", resolved.string());
    return false;
  }
  std::string rml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  doc.rml = std::move(rml);
  doc.dirty = true;
  doc.missing_warned = false;
  std::error_code ec;
  doc.last_write_time = std::filesystem::last_write_time(resolved, ec);
  return true;
}

Rml::FileHandle RmlUiOverlay::Open(const Rml::String& path) {
  const std::filesystem::path resolved = resolveAssetPath(path);
  if (resolved.empty()) {
    return Rml::FileHandle(0);
  }
  FILE* file = std::fopen(resolved.string().c_str(), "rb");
  if (!file) {
    return Rml::FileHandle(0);
  }
  return reinterpret_cast<Rml::FileHandle>(file);
}

void RmlUiOverlay::Close(Rml::FileHandle file) {
  if (!file) {
    return;
  }
  std::fclose(reinterpret_cast<FILE*>(file));
}

size_t RmlUiOverlay::Read(void* buffer, size_t size, Rml::FileHandle file) {
  if (!file || !buffer || size == 0) {
    return 0;
  }
  return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
}

bool RmlUiOverlay::Seek(Rml::FileHandle file, long offset, int origin) {
  if (!file) {
    return false;
  }
  return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
}

size_t RmlUiOverlay::Tell(Rml::FileHandle file) {
  if (!file) {
    return 0;
  }
  const long pos = std::ftell(reinterpret_cast<FILE*>(file));
  return pos < 0 ? 0u : static_cast<size_t>(pos);
}

size_t RmlUiOverlay::Length(Rml::FileHandle file) {
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

std::filesystem::path RmlUiOverlay::resolveAssetPath(const std::filesystem::path& source) const {
  if (source.empty()) {
    return {};
  }
  if (source.is_absolute()) {
    return source;
  }
  if (!asset_root_.empty()) {
    const std::filesystem::path candidate = asset_root_ / source;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  const std::filesystem::path cwd = std::filesystem::current_path() / source;
  if (std::filesystem::exists(cwd)) {
    return cwd;
  }
  return source;
}

void RmlUiOverlay::ensurePipeline(renderer::GraphicsDevice& device) {
  if (color_pso_.pipeline_state) {
    return;
  }
  auto* backend = dynamic_cast<renderer_backend::DiligentBackend*>(device.backend());
  if (!backend) {
    spdlog::error("RmlUiOverlay requires Diligent backend.");
    return;
  }

  render_device_ = backend->getDevice();
  context_ = backend->getContext();
  swap_chain_ = backend->getSwapChain();
  if (!render_device_ || !context_ || !swap_chain_) {
    return;
  }
  {
    const auto fmt = swap_chain_->GetDesc().DepthBufferFormat;
    const auto info = render_device_->GetTextureFormatInfo(fmt);
    clip_mask_supported_ = (info.ComponentType == Diligent::COMPONENT_TYPE_DEPTH_STENCIL);
  }

  static constexpr const char* kVS = R"(
cbuffer constants {
    row_major float4x4 transform;
    float2 translate;
    float2 _pad;
};

struct vs_input {
    float2 position   : ATTRIB0;
    float4 color0     : ATTRIB1;
    float2 tex_coord0 : ATTRIB2;
};

struct ps_input {
    float4 position : SV_POSITION;
    float2 tex_coord : TEX_COORD;
    float4 color : COLOR0;
};

void main(in vs_input vs_in, out ps_input ps_in) {
    ps_in.tex_coord = vs_in.tex_coord0;
    ps_in.color = vs_in.color0;
    float2 translated_pos = vs_in.position + translate;
    ps_in.position = mul(float4(translated_pos, 0, 1), transform);
}
)";

  static constexpr const char* kColorPS = R"(
struct ps_input {
    float4 position : SV_POSITION;
    float2 tex_coord : TEX_COORD;
    float4 color : COLOR0;
};

struct ps_output {
    float4 color : SV_TARGET;
};

void main(in ps_input ps_in, out ps_output ps_out) {
    ps_out.color = ps_in.color;
}
)";

  static constexpr const char* kTexturePS = R"(
Texture2D    _texture;
SamplerState _texture_sampler;

struct ps_input {
    float4 position : SV_POSITION;
    float2 tex_coord : TEX_COORD;
    float4 color : COLOR0;
};

struct ps_output {
    float4 color : SV_TARGET;
};

void main(in ps_input ps_in, out ps_output ps_out) {
    float4 tex_color = _texture.Sample(_texture_sampler, ps_in.tex_coord);
    ps_out.color = tex_color * ps_in.color;
}
)";

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  {
    Diligent::ShaderCreateInfo ci{};
    ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ci.Desc.Name = "RmlUi VS";
    ci.EntryPoint = "main";
    ci.Source = kVS;
    render_device_->CreateShader(ci, &vs);
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> color_ps;
  {
    Diligent::ShaderCreateInfo ci{};
    ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    ci.Desc.Name = "RmlUi Color PS";
    ci.EntryPoint = "main";
    ci.Source = kColorPS;
    render_device_->CreateShader(ci, &color_ps);
  }

  Diligent::RefCntAutoPtr<Diligent::IShader> texture_ps;
  {
    Diligent::ShaderCreateInfo ci{};
    ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    ci.Desc.Name = "RmlUi Texture PS";
    ci.EntryPoint = "main";
    ci.Source = kTexturePS;
    render_device_->CreateShader(ci, &texture_ps);
  }

  if (!vs || !color_ps || !texture_ps) {
    return;
  }

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "RmlUi Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(RmlConstants);
  render_device_->CreateBuffer(cb_desc, nullptr, &constants_);

  Diligent::GraphicsPipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso_ci.GraphicsPipeline.NumRenderTargets = 1;
  pso_ci.GraphicsPipeline.RTVFormats[0] = swap_chain_->GetDesc().ColorBufferFormat;
  pso_ci.GraphicsPipeline.DSVFormat = swap_chain_->GetDesc().DepthBufferFormat;
  pso_ci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pso_ci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;

  auto& blend = pso_ci.GraphicsPipeline.BlendDesc.RenderTargets[0];
  blend.BlendEnable = true;
  blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
  blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
  blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
  blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(Rml::Vertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(Rml::Vertex))},
      Diligent::LayoutElement{1, 0, 4, Diligent::VT_UINT8, true,
                              static_cast<Diligent::Uint32>(offsetof(Rml::Vertex, colour)),
                              static_cast<Diligent::Uint32>(sizeof(Rml::Vertex))},
      Diligent::LayoutElement{2, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(Rml::Vertex, tex_coord)),
                              static_cast<Diligent::Uint32>(sizeof(Rml::Vertex))}
  };
  pso_ci.GraphicsPipeline.InputLayout.LayoutElements = layout;
  pso_ci.GraphicsPipeline.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));

  pso_ci.pVS = vs;
  pso_ci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

  auto create_pair = [&](PsoPair& pair,
                         Diligent::IShader* ps,
                         bool scissor,
                         const Diligent::DepthStencilStateDesc& depth_stencil,
                         Diligent::COLOR_MASK write_mask) {
    pso_ci.pPS = ps;
    pso_ci.GraphicsPipeline.RasterizerDesc.ScissorEnable = scissor;
    pso_ci.GraphicsPipeline.DepthStencilDesc = depth_stencil;
    pso_ci.GraphicsPipeline.BlendDesc.RenderTargets[0].RenderTargetWriteMask = write_mask;
    render_device_->CreateGraphicsPipelineState(pso_ci, &pair.pipeline_state);
    if (pair.pipeline_state) {
      if (auto* var = pair.pipeline_state->GetStaticVariableByName(
              Diligent::SHADER_TYPE_VERTEX, "constants")) {
        var->Set(constants_);
      }
      pair.pipeline_state->CreateShaderResourceBinding(&pair.shader_resource_binding, true);
    }
  };

  Diligent::DepthStencilStateDesc ds_no_stencil{};
  ds_no_stencil.DepthEnable = false;
  ds_no_stencil.DepthWriteEnable = false;
  ds_no_stencil.StencilEnable = false;

  Diligent::DepthStencilStateDesc ds_clip{};
  ds_clip.DepthEnable = false;
  ds_clip.DepthWriteEnable = false;
  ds_clip.StencilEnable = true;
  ds_clip.StencilReadMask = 0xFF;
  ds_clip.StencilWriteMask = 0x00;
  ds_clip.FrontFace.StencilFunc = Diligent::COMPARISON_FUNC_EQUAL;
  ds_clip.FrontFace.StencilFailOp = Diligent::STENCIL_OP_KEEP;
  ds_clip.FrontFace.StencilDepthFailOp = Diligent::STENCIL_OP_KEEP;
  ds_clip.FrontFace.StencilPassOp = Diligent::STENCIL_OP_KEEP;
  ds_clip.BackFace = ds_clip.FrontFace;

  Diligent::DepthStencilStateDesc ds_mask_replace{};
  ds_mask_replace.DepthEnable = false;
  ds_mask_replace.DepthWriteEnable = false;
  ds_mask_replace.StencilEnable = true;
  ds_mask_replace.StencilReadMask = 0xFF;
  ds_mask_replace.StencilWriteMask = 0xFF;
  ds_mask_replace.FrontFace.StencilFunc = Diligent::COMPARISON_FUNC_ALWAYS;
  ds_mask_replace.FrontFace.StencilFailOp = Diligent::STENCIL_OP_KEEP;
  ds_mask_replace.FrontFace.StencilDepthFailOp = Diligent::STENCIL_OP_KEEP;
  ds_mask_replace.FrontFace.StencilPassOp = Diligent::STENCIL_OP_REPLACE;
  ds_mask_replace.BackFace = ds_mask_replace.FrontFace;

  Diligent::DepthStencilStateDesc ds_mask_incr = ds_mask_replace;
  ds_mask_incr.FrontFace.StencilPassOp = Diligent::STENCIL_OP_INCR_SAT;
  ds_mask_incr.BackFace = ds_mask_incr.FrontFace;

  create_pair(color_pso_, color_ps, false, ds_no_stencil, Diligent::COLOR_MASK_ALL);
  create_pair(color_pso_scissor_, color_ps, true, ds_no_stencil, Diligent::COLOR_MASK_ALL);
  create_pair(color_pso_clip_, color_ps, false, ds_clip, Diligent::COLOR_MASK_ALL);
  create_pair(color_pso_scissor_clip_, color_ps, true, ds_clip, Diligent::COLOR_MASK_ALL);
  create_pair(mask_pso_replace_, color_ps, false, ds_mask_replace, Diligent::COLOR_MASK_NONE);
  create_pair(mask_pso_replace_scissor_, color_ps, true, ds_mask_replace, Diligent::COLOR_MASK_NONE);
  create_pair(mask_pso_incr_, color_ps, false, ds_mask_incr, Diligent::COLOR_MASK_NONE);
  create_pair(mask_pso_incr_scissor_, color_ps, true, ds_mask_incr, Diligent::COLOR_MASK_NONE);

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_PIXEL, "_texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
  };
  pso_ci.PSODesc.ResourceLayout.Variables = vars;
  pso_ci.PSODesc.ResourceLayout.NumVariables = static_cast<Diligent::Uint32>(std::size(vars));

  Diligent::SamplerDesc sampler{};
  sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
  render_device_->CreateSampler(sampler, &sampler_);

  Diligent::ImmutableSamplerDesc immut_samplers[] = {
      {Diligent::SHADER_TYPE_PIXEL, "_texture_sampler", sampler}
  };
  pso_ci.PSODesc.ResourceLayout.ImmutableSamplers = immut_samplers;
  pso_ci.PSODesc.ResourceLayout.NumImmutableSamplers =
      static_cast<Diligent::Uint32>(std::size(immut_samplers));

  create_pair(texture_pso_, texture_ps, false, ds_no_stencil, Diligent::COLOR_MASK_ALL);
  create_pair(texture_pso_scissor_, texture_ps, true, ds_no_stencil, Diligent::COLOR_MASK_ALL);
  create_pair(texture_pso_clip_, texture_ps, false, ds_clip, Diligent::COLOR_MASK_ALL);
  create_pair(texture_pso_scissor_clip_, texture_ps, true, ds_clip, Diligent::COLOR_MASK_ALL);
}

}  // namespace karma::ui
