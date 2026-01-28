#pragma once

#ifndef KARMA_WITH_RMLUI
#define KARMA_WITH_RMLUI 0
#endif

#if !KARMA_WITH_RMLUI
#error "RmlUi backend not enabled. Build with KARMA_WITH_RMLUI=ON and provide RmlUi."
#endif

#include "karma/ui/overlay.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/StyleTypes.h>
#include <RmlUi/Core/Vertex.h>

#include <Common/interface/RefCntAutoPtr.hpp>
#include <filesystem>
#include <functional>

namespace Diligent {
class IBuffer;
class IDeviceContext;
class IPipelineState;
class IRenderDevice;
class IShaderResourceBinding;
class ISampler;
class ISwapChain;
class ITexture;
class ITextureView;
}  // namespace Diligent

#include <unordered_map>
#include <vector>

namespace karma::ui {

class RmlUiOverlay final : public Overlay,
                           public Rml::RenderInterface,
                           public Rml::FileInterface,
                           public Rml::SystemInterface {
 public:
  RmlUiOverlay();
  ~RmlUiOverlay() override;
  void setDocumentRml(std::string rml);
  bool setDocumentFile(const std::filesystem::path& path);
  void addDocumentRml(std::string name, std::string rml, bool show = true);
  bool addDocumentFile(std::string name, const std::filesystem::path& path, bool show = true);
  void showDocument(std::string name);
  void closeDocument(std::string name);
  void setHotReloadEnabled(bool enabled);
  void setHotReloadInterval(double seconds);
  void setContextReadyCallback(std::function<void(Rml::Context&)> callback);
  void addFontFaceFile(std::string path, bool fallback = false,
                       Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto);
  void clearFonts();
  void setAssetRoot(std::string root);

  void onFrameBegin(int width, int height, float dt) override;
  void onEvent(const platform::Event& event) override;
  void onRender(renderer::GraphicsDevice& device) override;
  void onShutdown() override;

  // Rml::SystemInterface
  double GetElapsedTime() override;
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

  // Rml::RenderInterface
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;
  Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                 const Rml::String& source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                     Rml::Vector2i source_dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture_handle) override;
  void EnableClipMask(bool enable) override;
  void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation) override;
  void SetTransform(const Rml::Matrix4f* transform) override;

  // Rml::FileInterface
  Rml::FileHandle Open(const Rml::String& path) override;
  void Close(Rml::FileHandle file) override;
  size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
  bool Seek(Rml::FileHandle file, long offset, int origin) override;
  size_t Tell(Rml::FileHandle file) override;
  size_t Length(Rml::FileHandle file) override;

 private:
  struct Geometry {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vb;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> ib;
    int num_indices = 0;
  };

  struct Texture {
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  };

  void ensurePipeline(renderer::GraphicsDevice& device);
  void loadFonts();
  std::filesystem::path resolveAssetPath(const std::filesystem::path& source) const;

  renderer::GraphicsDevice* device_ = nullptr;
  Diligent::IRenderDevice* render_device_ = nullptr;
  Diligent::IDeviceContext* context_ = nullptr;
  Diligent::ISwapChain* swap_chain_ = nullptr;

  Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_;

  std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometries_;
  std::unordered_map<Rml::TextureHandle, Texture> textures_;
  Rml::Context* context = nullptr;

  struct DocumentDef {
    std::string rml;
    bool show = true;
    bool dirty = true;
    Rml::ElementDocument* doc = nullptr;
    bool from_file = false;
    bool missing_warned = false;
    std::filesystem::path source_path;
    std::filesystem::file_time_type last_write_time{};
  };

  bool reloadDocumentFromFile(DocumentDef& doc);

  struct FontRequest {
    std::string path;
    bool fallback = false;
    Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto;
    bool loaded = false;
    bool warned = false;
  };

  struct PsoPair {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_state;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shader_resource_binding;
  };

  PsoPair color_pso_;
  PsoPair color_pso_scissor_;
  PsoPair color_pso_clip_;
  PsoPair color_pso_scissor_clip_;
  PsoPair texture_pso_;
  PsoPair texture_pso_scissor_;
  PsoPair texture_pso_clip_;
  PsoPair texture_pso_scissor_clip_;
  PsoPair mask_pso_replace_;
  PsoPair mask_pso_replace_scissor_;
  PsoPair mask_pso_incr_;
  PsoPair mask_pso_incr_scissor_;

  bool scissor_enabled_ = false;
  int width_ = 0;
  int height_ = 0;
  float dt_ = 1.0f / 60.0f;
  double time_ = 0.0;
  Rml::TextureHandle next_texture_handle_ = 1;
  std::unordered_map<std::string, DocumentDef> documents_;
  std::vector<FontRequest> font_requests_;
  bool context_ready_called_ = false;
  std::function<void(Rml::Context&)> context_ready_cb_;
  Rml::CompiledGeometryHandle next_geometry_handle_ = 1;
  bool clip_mask_enabled_ = false;
  bool clip_mask_warned_ = false;
  bool clip_mask_supported_ = true;
  int stencil_ref_ = 1;
  bool has_transform_ = false;
  Rml::Matrix4f transform_{};
  std::filesystem::path asset_root_;
  bool hot_reload_enabled_ = true;
  double hot_reload_interval_ = 0.5;
  double last_hot_reload_check_ = 0.0;
};

}  // namespace karma::ui
