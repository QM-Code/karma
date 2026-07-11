#pragma once

#include "features/ui/native/text_engine.h"
#include "karma/rendering.h"
#include "karma/ui.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace karma::assets {
class AssetRegistry;
}

namespace karma::ui::native::runtime_dom {
struct Node;
}

namespace karma::ui::native {

struct PresentationResourceConfig {
  int glyph_atlas_page_width = 1024;
  int glyph_atlas_page_height = 1024;
  std::size_t glyph_atlas_budget_bytes = 64u * 1024u * 1024u;
  std::size_t svg_raster_budget_bytes = 64u * 1024u * 1024u;
};

/// Self-contained view of one glyph stored in a presentation-owned atlas.
/// The texture remains valid until resourceGeneration() changes or shutdown().
struct GlyphTexturePlacement {
  rendering::UITextureHandle texture = 0u;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int bearing_x = 0;
  int bearing_y = 0;
  int atlas_width = 0;
  int atlas_height = 0;
  GlyphPixelFormat format = GlyphPixelFormat::R8;
};

/// Owns the renderer-facing resources used by retained native-UI paint.
/// Asset, graphics, and text services are borrowed and must outlive this owner.
class PresentationResources {
 public:
  PresentationResources(assets::AssetRegistry& assets,
                        rendering::GraphicsDevice* graphics,
                        TextEngine& text_engine,
                        PresentationResourceConfig config = {});
  ~PresentationResources();

  PresentationResources(PresentationResources&&) = delete;
  PresentationResources& operator=(PresentationResources&&) = delete;
  PresentationResources(const PresentationResources&) = delete;
  PresentationResources& operator=(const PresentationResources&) = delete;

  /// Registers or refreshes authored fonts in runtime-tree order.
  /// Returns true when the available font set changed.
  bool ensureTreeFonts(const runtime_dom::Node& root);

  /// Returns a renderer-ready atlas placement, rasterizing/uploading on miss.
  [[nodiscard]] std::optional<GlyphTexturePlacement> ensureGlyphPlacement(
      const ShapedGlyph& glyph,
      float physical_pixel_size);

  /// Resolves an image source to a sampled texture. Asset SVGs are rasterized
  /// for the requested logical size, framebuffer scale, and authored tint.
  [[nodiscard]] rendering::UITextureHandle resolveTexture(
      const ImageSource& source,
      float logical_width,
      float logical_height,
      math::Color tint,
      float scale_x,
      float scale_y);

  /// Returns source pixels for object-fit and layout measurement when known.
  [[nodiscard]] std::optional<std::pair<float, float>> intrinsicSize(
      const ImageSource& source);

  [[nodiscard]] DynamicImageHandle createImage(
      const rendering::TextureDesc& desc,
      const rendering::TextureUploadData& upload);
  bool updateImage(DynamicImageHandle image,
                   const rendering::TextureUploadData& upload);
  bool destroyImage(DynamicImageHandle image);

  /// Advances resource LRU age and performs the existing periodic cache prune.
  void advanceFrame();
  void pruneCachedTextures();

  [[nodiscard]] std::uint64_t frame() const noexcept;
  [[nodiscard]] std::uint64_t resourceGeneration() const noexcept;

  /// Releases all owned renderer resources and fonts registered by this
  /// instance. Terminal and idempotent; later operations are safe no-ops.
  void shutdown();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
