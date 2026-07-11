#include "features/ui/native/presentation_resources.h"

#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/svg_rasterizer.h"
#include "karma/assets.h"

#include <lunasvg.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace karma::ui::native {
namespace {

std::uint32_t nextDynamicImageGeneration() noexcept {
  static std::atomic<std::uint32_t> next_generation{1u};
  for (;;) {
    const std::uint32_t generation =
        next_generation.fetch_add(1u, std::memory_order_relaxed);
    if (generation != 0u) return generation;
  }
}

struct DynamicImageSlot {
  std::uint32_t generation = 0u;
  rendering::TextureId texture = rendering::kInvalidTexture;
  rendering::TextureDesc desc{};
};

struct RegisteredFont {
  std::string content_hash;
  FontId font = 0u;
};

struct SvgIntrinsicCacheEntry {
  std::string content_hash;
  std::optional<std::pair<float, float>> dimensions;
};

struct CachedTexture {
  std::string asset_key;
  std::string content_hash;
  rendering::TextureId texture = rendering::kInvalidTexture;
  std::size_t bytes = 0u;
  std::uint64_t last_use_frame = 0u;
};

struct GlyphAtlasKey {
  FontId font = 0u;
  std::uint32_t glyph = 0u;
  std::uint32_t pixel_size_bits = 0u;
  bool tofu = false;

  bool operator==(const GlyphAtlasKey&) const = default;
};

struct GlyphAtlasKeyHash {
  std::size_t operator()(const GlyphAtlasKey& key) const noexcept {
    std::size_t hash = std::hash<FontId>{}(key.font);
    const auto combine = [&](std::size_t value) {
      hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    };
    combine(key.glyph);
    combine(key.pixel_size_bits);
    combine(key.tofu);
    return hash;
  }
};

struct GlyphAtlasPage {
  GlyphPixelFormat format = GlyphPixelFormat::R8;
  int width = 0;
  int height = 0;
  int cursor_x = 1;
  int cursor_y = 1;
  int row_height = 0;
  std::vector<std::uint8_t> pixels;
  rendering::TextureId texture = rendering::kInvalidTexture;
  std::uint64_t last_use_frame = 0u;
};

struct GlyphAtlasPlacement {
  std::size_t page = 0u;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int bearing_x = 0;
  int bearing_y = 0;
  GlyphPixelFormat format = GlyphPixelFormat::R8;
};

rendering::TextureUploadData makeTextureUpload(
    rendering::TextureFormat format,
    int width,
    int height,
    std::size_t row_stride,
    const std::vector<std::uint8_t>& pixels) {
  rendering::TextureUploadData upload;
  upload.format = format;
  upload.bytes = pixels;
  upload.subresources.push_back({.mip_level = 0u,
                                 .array_layer = 0u,
                                 .width = width,
                                 .height = height,
                                 .offset = 0u,
                                 .size = upload.bytes.size(),
                                 .row_stride = row_stride});
  return upload;
}

bool reserveGlyphRect(GlyphAtlasPage& page,
                      int width,
                      int height,
                      int& output_x,
                      int& output_y) {
  int x = page.cursor_x;
  int y = page.cursor_y;
  int row_height = page.row_height;
  if (x + width + 1 > page.width) {
    x = 1;
    y += row_height + 1;
    row_height = 0;
  }
  if (y + height + 1 > page.height) return false;
  output_x = x;
  output_y = y;
  page.cursor_x = x + width + 1;
  page.cursor_y = y;
  page.row_height = std::max(row_height, height);
  return true;
}

std::optional<std::pair<float, float>> svgIntrinsicDimensions(
    std::string_view source) {
  std::unique_ptr<lunasvg::Document> document =
      lunasvg::Document::loadFromData(source.data(), source.size());
  if (!document || !std::isfinite(document->width()) ||
      !std::isfinite(document->height()) || document->width() <= 0.0f ||
      document->height() <= 0.0f) {
    return std::nullopt;
  }
  return std::pair{document->width(), document->height()};
}

}  // namespace

struct PresentationResources::Impl {
  assets::AssetRegistry& assets;
  rendering::GraphicsDevice* graphics = nullptr;
  TextEngine& text_engine;
  PresentationResourceConfig config{};
  std::uint64_t observed_asset_version = 0u;
  SvgRasterizer svg_rasterizer;
  std::unordered_map<std::string, RegisteredFont> registered_fonts;
  std::vector<DynamicImageSlot> images;
  std::vector<std::uint32_t> free_images;
  std::unordered_map<std::string, CachedTexture> asset_textures;
  std::unordered_map<std::string, CachedTexture> svg_textures;
  std::unordered_map<std::string, SvgIntrinsicCacheEntry> svg_intrinsic_sizes;
  std::vector<GlyphAtlasPage> glyph_pages;
  std::unordered_map<GlyphAtlasKey, GlyphAtlasPlacement, GlyphAtlasKeyHash>
      glyph_placements;
  std::uint64_t resource_frame = 0u;
  std::uint64_t resource_generation = 1u;
  bool shutdown_complete = false;

  Impl(assets::AssetRegistry& asset_registry,
       rendering::GraphicsDevice* graphics_device,
       TextEngine& engine,
       PresentationResourceConfig resource_config)
      : assets(asset_registry),
        graphics(graphics_device),
        text_engine(engine),
        config(std::move(resource_config)),
        observed_asset_version(asset_registry.version()),
        svg_rasterizer({
            .cache_budget_bytes = config.svg_raster_budget_bytes,
            .max_raster_bytes = std::min<std::size_t>(
                config.svg_raster_budget_bytes == 0u
                    ? 64u * 1024u * 1024u
                    : config.svg_raster_budget_bytes,
                256u * 1024u * 1024u),
            .max_entries = 1024u,
            .max_dimension = 16384u,
        }) {}

  void incrementGeneration() noexcept {
    ++resource_generation;
    if (resource_generation == 0u) ++resource_generation;
  }

  [[nodiscard]] bool active() const noexcept { return !shutdown_complete; }

  bool synchronizeAssetVersion() {
    const std::uint64_t current_version = assets.version();
    if (current_version == observed_asset_version) return false;
    observed_asset_version = current_version;
    pruneCachedTextures();
    return true;
  }

  void clearGlyphAtlases() {
    incrementGeneration();
    if (graphics != nullptr) {
      for (GlyphAtlasPage& page : glyph_pages) {
        if (page.texture != rendering::kInvalidTexture) {
          graphics->destroyTexture(page.texture);
        }
      }
    }
    glyph_pages.clear();
    glyph_placements.clear();
  }

  bool ensureNodeFonts(const runtime_dom::Node& node) {
    bool changed = false;
    for (const runtime_dom::NodeFontSource& source : node.font_sources) {
      const assets::FontAsset* asset = assets.findFontAsset(source.asset_key);
      if (asset == nullptr) {
        if (const auto registered = registered_fonts.find(source.registration_key);
            registered != registered_fonts.end()) {
          if (text_engine.findFont(source.registration_key) ==
              registered->second.font) {
            text_engine.unregisterFont(source.registration_key);
          }
          registered_fonts.erase(registered);
          clearGlyphAtlases();
          changed = true;
        }
        continue;
      }
      const std::string hash =
          asset->content_hash.empty()
              ? assets::hashBytes(asset->bytes.data(), asset->bytes.size())
              : asset->content_hash;
      const auto registered =
          registered_fonts.find(source.registration_key);
      if (registered != registered_fonts.end() &&
          registered->second.content_hash == hash &&
          text_engine.findFont(source.registration_key) ==
              registered->second.font) {
        continue;
      }
      std::string error;
      if (const auto font = text_engine.registerFont(
              source.registration_key, *asset, source.face_index, &error)) {
        registered_fonts[source.registration_key] = {
            .content_hash = hash,
            .font = *font,
        };
        clearGlyphAtlases();
        changed = true;
      }
    }
    return changed;
  }

  bool ensureTreeFonts(const runtime_dom::Node& node) {
    bool changed = ensureNodeFonts(node);
    runtime_dom::forRuntimeChildren(
        node, [&](const runtime_dom::Node& child, const Value::Object*) {
          if (child.present) changed = ensureTreeFonts(child) || changed;
        });
    return changed;
  }

  bool uploadGlyphPage(GlyphAtlasPage& page) {
    if (graphics == nullptr) return false;
    const rendering::TextureFormat format =
        page.format == GlyphPixelFormat::R8 ? rendering::TextureFormat::R8
                                             : rendering::TextureFormat::RGBA8;
    if (page.texture == rendering::kInvalidTexture) {
      page.texture = graphics->createTexture({.width = page.width,
                                              .height = page.height,
                                              .format = format,
                                              .srgb = false,
                                              .generate_mips = false,
                                              .mip_levels = 1u});
      if (page.texture == rendering::kInvalidTexture) return false;
    }
    const std::size_t bytes_per_pixel =
        format == rendering::TextureFormat::R8 ? 1u : 4u;
    const auto upload = makeTextureUpload(
        format, page.width, page.height,
        static_cast<std::size_t>(page.width) * bytes_per_pixel, page.pixels);
    return graphics->uploadTexture(page.texture, upload);
  }

  bool uploadGlyphRegion(GlyphAtlasPage& page,
                         int x,
                         int y,
                         int width,
                         int height) {
    if (graphics == nullptr || page.texture == rendering::kInvalidTexture) {
      return false;
    }
    const rendering::TextureFormat format =
        page.format == GlyphPixelFormat::R8 ? rendering::TextureFormat::R8
                                             : rendering::TextureFormat::RGBA8;
    const std::size_t bytes_per_pixel =
        format == rendering::TextureFormat::R8 ? 1u : 4u;
    const std::size_t row_stride =
        static_cast<std::size_t>(width) * bytes_per_pixel;
    rendering::TextureRegionUploadData upload{
        .format = format,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .row_stride = row_stride,
        .bytes = std::vector<std::uint8_t>(
            row_stride * static_cast<std::size_t>(height)),
    };
    for (int row = 0; row < height; ++row) {
      const std::uint8_t* source =
          page.pixels.data() +
          (static_cast<std::size_t>(y + row) *
               static_cast<std::size_t>(page.width) +
           static_cast<std::size_t>(x)) *
              bytes_per_pixel;
      std::memcpy(upload.bytes.data() +
                      static_cast<std::size_t>(row) * row_stride,
                  source, row_stride);
    }
    return graphics->updateTextureRegion(page.texture, upload);
  }

  GlyphTexturePlacement texturePlacement(
      const GlyphAtlasPlacement& placement) const {
    GlyphTexturePlacement result{.x = placement.x,
                                 .y = placement.y,
                                 .width = placement.width,
                                 .height = placement.height,
                                 .bearing_x = placement.bearing_x,
                                 .bearing_y = placement.bearing_y,
                                 .format = placement.format};
    if (placement.page < glyph_pages.size()) {
      const GlyphAtlasPage& page = glyph_pages[placement.page];
      result.texture =
          static_cast<rendering::UITextureHandle>(page.texture);
      result.atlas_width = page.width;
      result.atlas_height = page.height;
    }
    return result;
  }

  void eraseGlyphPlacementsForPage(std::size_t page_index) {
    for (auto placement = glyph_placements.begin();
         placement != glyph_placements.end();) {
      if (placement->second.page == page_index) {
        placement = glyph_placements.erase(placement);
      } else {
        ++placement;
      }
    }
  }

  void clearGlyphPage(std::size_t page_index, bool retain_storage) {
    if (page_index >= glyph_pages.size()) return;
    GlyphAtlasPage& page = glyph_pages[page_index];
    const GlyphPixelFormat format = page.format;
    const int width = page.width;
    const int height = page.height;
    if (page.texture != rendering::kInvalidTexture && graphics != nullptr) {
      graphics->destroyTexture(page.texture);
      incrementGeneration();
    }
    eraseGlyphPlacementsForPage(page_index);
    page = GlyphAtlasPage{};
    if (retain_storage && width > 0 && height > 0) {
      const std::size_t bytes_per_pixel =
          format == GlyphPixelFormat::R8 ? 1u : 4u;
      page.format = format;
      page.width = width;
      page.height = height;
      page.pixels.assign(static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height) *
                             bytes_per_pixel,
                         0u);
      page.last_use_frame = resource_frame;
    }
  }

  bool initializeGlyphPage(std::size_t page_index,
                           GlyphPixelFormat format,
                           int page_width,
                           int page_height,
                           std::size_t page_bytes,
                           int glyph_width,
                           int glyph_height,
                           int& glyph_x,
                           int& glyph_y) {
    GlyphAtlasPage page;
    page.format = format;
    page.width = page_width;
    page.height = page_height;
    page.pixels.assign(page_bytes, 0u);
    page.last_use_frame = resource_frame;
    if (!reserveGlyphRect(page, glyph_width, glyph_height, glyph_x, glyph_y)) {
      return false;
    }
    if (page_index < glyph_pages.size()) {
      glyph_pages[page_index] = std::move(page);
    } else {
      glyph_pages.push_back(std::move(page));
    }
    return true;
  }

  std::optional<GlyphTexturePlacement> ensureGlyphPlacement(
      const ShapedGlyph& glyph,
      float physical_pixel_size) {
    if (graphics == nullptr) return std::nullopt;
    const float normalized_size = std::max(1.0f, physical_pixel_size);
    const GlyphAtlasKey key{
        .font = glyph.font,
        .glyph = glyph.glyph,
        .pixel_size_bits = std::bit_cast<std::uint32_t>(normalized_size),
        .tofu = glyph.tofu,
    };
    if (const auto existing = glyph_placements.find(key);
        existing != glyph_placements.end()) {
      if (existing->second.page < glyph_pages.size()) {
        glyph_pages[existing->second.page].last_use_frame = resource_frame;
      }
      return texturePlacement(existing->second);
    }

    std::string error;
    auto bitmap = text_engine.rasterize(glyph, normalized_size, &error);
    if (!bitmap.has_value() || bitmap->width == 0u ||
        bitmap->height == 0u) {
      return std::nullopt;
    }
    const int bitmap_width = static_cast<int>(bitmap->width);
    const int bitmap_height = static_cast<int>(bitmap->height);
    const int page_width = config.glyph_atlas_page_width;
    const int page_height = config.glyph_atlas_page_height;
    if (bitmap_width + 2 > page_width || bitmap_height + 2 > page_height) {
      return std::nullopt;
    }

    std::size_t selected_page = glyph_pages.size();
    int glyph_x = 0;
    int glyph_y = 0;
    for (std::size_t index = 0u; index < glyph_pages.size(); ++index) {
      GlyphAtlasPage& page = glyph_pages[index];
      if (page.format == bitmap->format &&
          reserveGlyphRect(page, bitmap_width, bitmap_height, glyph_x,
                           glyph_y)) {
        selected_page = index;
        break;
      }
    }
    if (selected_page == glyph_pages.size()) {
      const std::size_t bytes_per_pixel =
          bitmap->format == GlyphPixelFormat::R8 ? 1u : 4u;
      const std::size_t page_bytes =
          static_cast<std::size_t>(page_width) *
          static_cast<std::size_t>(page_height) * bytes_per_pixel;
      const std::size_t current_bytes = [&]() {
        std::size_t total = 0u;
        for (const GlyphAtlasPage& page : glyph_pages) {
          total += page.pixels.size();
        }
        return total;
      }();
      if (page_bytes > config.glyph_atlas_budget_bytes) return std::nullopt;
      const bool fits_without_eviction =
          current_bytes <= config.glyph_atlas_budget_bytes - page_bytes;
      if (!fits_without_eviction) {
        std::vector<std::size_t> oldest_pages;
        oldest_pages.reserve(glyph_pages.size());
        for (std::size_t index = 0u; index < glyph_pages.size(); ++index) {
          const GlyphAtlasPage& page = glyph_pages[index];
          if (!page.pixels.empty() &&
              page.last_use_frame != resource_frame) {
            oldest_pages.push_back(index);
          }
        }
        std::sort(oldest_pages.begin(), oldest_pages.end(),
                  [&](std::size_t left, std::size_t right) {
                    if (glyph_pages[left].last_use_frame !=
                        glyph_pages[right].last_use_frame) {
                      return glyph_pages[left].last_use_frame <
                             glyph_pages[right].last_use_frame;
                    }
                    return left < right;
                  });
        std::size_t freed_bytes = 0u;
        std::size_t eviction_count = 0u;
        while (eviction_count < oldest_pages.size() &&
               current_bytes - freed_bytes >
                   config.glyph_atlas_budget_bytes - page_bytes) {
          freed_bytes +=
              glyph_pages[oldest_pages[eviction_count]].pixels.size();
          ++eviction_count;
        }
        if (current_bytes - freed_bytes >
            config.glyph_atlas_budget_bytes - page_bytes) {
          return std::nullopt;
        }
        selected_page = oldest_pages.front();
        for (std::size_t index = 0u; index < eviction_count; ++index) {
          clearGlyphPage(oldest_pages[index], false);
        }
      } else {
        const auto empty_page = std::find_if(
            glyph_pages.begin(), glyph_pages.end(),
            [](const GlyphAtlasPage& page) {
              return page.pixels.empty() &&
                     page.texture == rendering::kInvalidTexture;
            });
        if (empty_page != glyph_pages.end()) {
          selected_page = static_cast<std::size_t>(
              std::distance(glyph_pages.begin(), empty_page));
        }
      }
      if (!initializeGlyphPage(selected_page, bitmap->format, page_width,
                               page_height, page_bytes, bitmap_width,
                               bitmap_height, glyph_x, glyph_y)) {
        return std::nullopt;
      }
    }

    GlyphAtlasPage& page = glyph_pages[selected_page];
    page.last_use_frame = resource_frame;
    const std::size_t bytes_per_pixel =
        page.format == GlyphPixelFormat::R8 ? 1u : 4u;
    for (int row = 0; row < bitmap_height; ++row) {
      const std::uint8_t* source =
          bitmap->pixels.data() + static_cast<std::size_t>(row) * bitmap->stride;
      std::uint8_t* destination =
          page.pixels.data() +
          (static_cast<std::size_t>(glyph_y + row) *
               static_cast<std::size_t>(page.width) +
           static_cast<std::size_t>(glyph_x)) *
              bytes_per_pixel;
      std::memcpy(destination, source,
                  static_cast<std::size_t>(bitmap_width) * bytes_per_pixel);
    }
    const bool uploaded =
        page.texture == rendering::kInvalidTexture
            ? uploadGlyphPage(page)
            : uploadGlyphRegion(page, glyph_x, glyph_y, bitmap_width,
                                bitmap_height) ||
                  uploadGlyphPage(page);
    if (!uploaded) {
      clearGlyphPage(selected_page, true);
      return std::nullopt;
    }
    const GlyphAtlasPlacement placement{
        .page = selected_page,
        .x = glyph_x,
        .y = glyph_y,
        .width = bitmap_width,
        .height = bitmap_height,
        .bearing_x = bitmap->bearing_x,
        .bearing_y = bitmap->bearing_y,
        .format = bitmap->format,
    };
    glyph_placements.emplace(key, placement);
    return texturePlacement(placement);
  }

  std::optional<std::pair<float, float>> cachedSvgIntrinsicDimensions(
      std::string_view asset_key,
      const assets::SvgAsset& asset) {
    const std::string content_hash =
        asset.content_hash.empty() ? assets::hashString(asset.source_utf8)
                                   : asset.content_hash;
    const auto cached = svg_intrinsic_sizes.find(std::string(asset_key));
    if (cached != svg_intrinsic_sizes.end() &&
        cached->second.content_hash == content_hash) {
      return cached->second.dimensions;
    }
    const auto dimensions = svgIntrinsicDimensions(asset.source_utf8);
    auto& stored = svg_intrinsic_sizes[std::string(asset_key)];
    stored.content_hash = content_hash;
    stored.dimensions = dimensions;
    return dimensions;
  }

  rendering::UITextureHandle resolveTexture(const ImageSource& source,
                                             float logical_width,
                                             float logical_height,
                                             math::Color tint_color,
                                             float scale_x,
                                             float scale_y) {
    if (!source) return 0u;
    if (source.kind == ImageSource::Kind::Dynamic) {
      const DynamicImageHandle handle = source.dynamic_image;
      if (!handle.valid() || handle.index >= images.size()) return 0u;
      const DynamicImageSlot& slot = images[handle.index];
      return slot.generation == handle.generation
                 ? static_cast<rendering::UITextureHandle>(slot.texture)
                 : 0u;
    }
    if (source.kind == ImageSource::Kind::RenderTarget) {
      return graphics == nullptr
                 ? 0u
                 : static_cast<rendering::UITextureHandle>(
                       graphics->getRenderTargetTextureId(source.render_target));
    }
    if (source.kind != ImageSource::Kind::Asset || graphics == nullptr) {
      return 0u;
    }
    if (const assets::SvgAsset* svg = assets.findSvgAsset(source.asset_key);
        svg != nullptr) {
      const std::uint32_t width = static_cast<std::uint32_t>(std::clamp(
          std::round(std::max(0.0f, logical_width) * scale_x), 1.0f,
          16384.0f));
      const std::uint32_t height = static_cast<std::uint32_t>(std::clamp(
          std::round(std::max(0.0f, logical_height) * scale_y), 1.0f,
          16384.0f));
      const auto byte = [](float value) {
        return static_cast<std::uint8_t>(
            std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
      };
      const SvgTint tint{.red = byte(tint_color.r),
                         .green = byte(tint_color.g),
                         .blue = byte(tint_color.b),
                         .alpha = byte(tint_color.a)};
      std::string cache_key =
          source.asset_key + "#" + std::to_string(width) + "x" +
          std::to_string(height) + "@" +
          std::to_string(std::bit_cast<std::uint32_t>(scale_x)) + "," +
          std::to_string(std::bit_cast<std::uint32_t>(scale_y)) + ":" +
          std::to_string(static_cast<unsigned>(tint.red)) + "," +
          std::to_string(static_cast<unsigned>(tint.green)) + "," +
          std::to_string(static_cast<unsigned>(tint.blue)) + "," +
          std::to_string(static_cast<unsigned>(tint.alpha));
      const std::string content_hash =
          svg->content_hash.empty() ? assets::hashString(svg->source_utf8)
                                    : svg->content_hash;
      if (auto cached = svg_textures.find(cache_key);
          cached != svg_textures.end() &&
          cached->second.content_hash == content_hash) {
        cached->second.last_use_frame = resource_frame;
        return static_cast<rendering::UITextureHandle>(cached->second.texture);
      }
      std::string error;
      const auto raster = svg_rasterizer.rasterize(
          source.asset_key, assets.version(), *svg,
          {.physical_width = width,
           .physical_height = height,
           .dpi_scale_x = scale_x,
           .dpi_scale_y = scale_y,
           .tint = tint},
          &error);
      if (!raster) return 0u;
      const rendering::TextureId id = graphics->createTexture({
          .width = static_cast<int>(raster->width),
          .height = static_cast<int>(raster->height),
          .format = rendering::TextureFormat::RGBA8,
          .srgb = false,
          .generate_mips = false,
          .mip_levels = 1u,
      });
      const auto upload = makeTextureUpload(
          rendering::TextureFormat::RGBA8, static_cast<int>(raster->width),
          static_cast<int>(raster->height), raster->stride, raster->pixels);
      if (id == rendering::kInvalidTexture ||
          !graphics->uploadTexture(id, upload)) {
        if (id != rendering::kInvalidTexture) graphics->destroyTexture(id);
        return 0u;
      }
      if (auto previous = svg_textures.find(cache_key);
          previous != svg_textures.end() &&
          previous->second.texture != rendering::kInvalidTexture) {
        graphics->destroyTexture(previous->second.texture);
        incrementGeneration();
      }
      auto& stored = svg_textures[std::move(cache_key)];
      stored.asset_key = source.asset_key;
      stored.content_hash = content_hash;
      stored.texture = id;
      stored.bytes = raster->pixels.size();
      stored.last_use_frame = resource_frame;
      return static_cast<rendering::UITextureHandle>(id);
    }

    const assets::TextureAsset* texture =
        assets.findTextureAsset(source.asset_key);
    if (texture == nullptr) return 0u;
    if (auto cached = asset_textures.find(source.asset_key);
        cached != asset_textures.end() &&
        cached->second.content_hash == texture->content_hash) {
      cached->second.last_use_frame = resource_frame;
      return static_cast<rendering::UITextureHandle>(cached->second.texture);
    }
    auto prepared = assets::prepareTextureUpload(*texture);
    if (!prepared.has_value()) return 0u;
    const rendering::TextureId id = graphics->createTexture(prepared->desc);
    if (id == rendering::kInvalidTexture ||
        !graphics->uploadTexture(id, prepared->upload)) {
      if (id != rendering::kInvalidTexture) graphics->destroyTexture(id);
      return 0u;
    }
    if (auto previous = asset_textures.find(source.asset_key);
        previous != asset_textures.end() &&
        previous->second.texture != rendering::kInvalidTexture) {
      graphics->destroyTexture(previous->second.texture);
      incrementGeneration();
    }
    auto& stored = asset_textures[source.asset_key];
    stored.asset_key = source.asset_key;
    stored.content_hash = texture->content_hash;
    stored.texture = id;
    stored.bytes = prepared->upload.bytes.size();
    stored.last_use_frame = resource_frame;
    return static_cast<rendering::UITextureHandle>(id);
  }

  std::optional<std::pair<float, float>> intrinsicSize(
      const ImageSource& source) {
    if (source.kind == ImageSource::Kind::Asset) {
      if (const assets::TextureAsset* texture =
              assets.findTextureAsset(source.asset_key)) {
        return std::pair{static_cast<float>(texture->desc.width),
                         static_cast<float>(texture->desc.height)};
      }
      if (const assets::SvgAsset* svg =
              assets.findSvgAsset(source.asset_key)) {
        return cachedSvgIntrinsicDimensions(source.asset_key, *svg);
      }
    } else if (source.kind == ImageSource::Kind::Dynamic &&
               source.dynamic_image.valid() &&
               source.dynamic_image.index < images.size()) {
      const DynamicImageSlot& image = images[source.dynamic_image.index];
      if (image.generation == source.dynamic_image.generation) {
        return std::pair{static_cast<float>(image.desc.width),
                         static_cast<float>(image.desc.height)};
      }
    } else if (source.kind == ImageSource::Kind::RenderTarget &&
               graphics != nullptr) {
      if (const auto desc =
              graphics->getRenderTargetDesc(source.render_target)) {
        return std::pair{static_cast<float>(desc->width),
                         static_cast<float>(desc->height)};
      }
    }
    return std::nullopt;
  }

  DynamicImageHandle createImage(
      const rendering::TextureDesc& desc,
      const rendering::TextureUploadData& upload) {
    if (graphics == nullptr ||
        !rendering::validateTextureUpload(desc, upload)) {
      return {};
    }
    const rendering::TextureId texture = graphics->createTexture(desc);
    if (texture == rendering::kInvalidTexture ||
        !graphics->uploadTexture(texture, upload)) {
      if (texture != rendering::kInvalidTexture) {
        graphics->destroyTexture(texture);
      }
      return {};
    }
    std::uint32_t index = 0u;
    if (!free_images.empty()) {
      index = free_images.back();
      free_images.pop_back();
    } else {
      index = static_cast<std::uint32_t>(images.size());
      images.emplace_back();
    }
    DynamicImageSlot& slot = images[index];
    if (slot.generation == 0u) {
      slot.generation = nextDynamicImageGeneration();
    }
    slot.texture = texture;
    slot.desc = desc;
    return {.index = index, .generation = slot.generation};
  }

  bool updateImage(DynamicImageHandle image,
                   const rendering::TextureUploadData& upload) {
    if (graphics == nullptr || !image.valid() || image.index >= images.size()) {
      return false;
    }
    DynamicImageSlot& slot = images[image.index];
    return slot.generation == image.generation &&
           slot.texture != rendering::kInvalidTexture &&
           rendering::validateTextureUpload(slot.desc, upload) &&
           graphics->uploadTexture(slot.texture, upload);
  }

  bool destroyImage(DynamicImageHandle image) {
    if (graphics == nullptr || !image.valid() || image.index >= images.size()) {
      return false;
    }
    DynamicImageSlot& slot = images[image.index];
    if (slot.generation != image.generation ||
        slot.texture == rendering::kInvalidTexture) {
      return false;
    }
    graphics->destroyTexture(slot.texture);
    incrementGeneration();
    slot.texture = rendering::kInvalidTexture;
    slot.desc = {};
    slot.generation = nextDynamicImageGeneration();
    free_images.push_back(image.index);
    return true;
  }

  void pruneCachedTextures() {
    if (graphics == nullptr) return;
    const auto destroy = [&](auto& map, auto iterator) {
      if (iterator->second.texture != rendering::kInvalidTexture) {
        graphics->destroyTexture(iterator->second.texture);
        incrementGeneration();
      }
      return map.erase(iterator);
    };
    for (auto iterator = asset_textures.begin();
         iterator != asset_textures.end();) {
      const assets::TextureAsset* asset =
          assets.findTextureAsset(iterator->second.asset_key);
      if (asset == nullptr ||
          asset->content_hash != iterator->second.content_hash) {
        iterator = destroy(asset_textures, iterator);
      } else {
        ++iterator;
      }
    }
    std::size_t svg_bytes = 0u;
    for (auto iterator = svg_textures.begin();
         iterator != svg_textures.end();) {
      const assets::SvgAsset* asset =
          assets.findSvgAsset(iterator->second.asset_key);
      const bool stale_asset =
          asset == nullptr ||
          (!asset->content_hash.empty() &&
           asset->content_hash != iterator->second.content_hash);
      if (stale_asset) {
        iterator = destroy(svg_textures, iterator);
      } else {
        svg_bytes += iterator->second.bytes;
        ++iterator;
      }
    }
    while (!svg_textures.empty() &&
           svg_bytes > config.svg_raster_budget_bytes) {
      const auto oldest = std::min_element(
          svg_textures.begin(), svg_textures.end(),
          [](const auto& left, const auto& right) {
            return left.second.last_use_frame < right.second.last_use_frame;
          });
      if (oldest == svg_textures.end()) break;
      svg_bytes -= oldest->second.bytes;
      destroy(svg_textures, oldest);
    }
  }

  void shutdown() {
    if (shutdown_complete) return;
    shutdown_complete = true;
    clearGlyphAtlases();
    if (graphics != nullptr) {
      for (DynamicImageSlot& image : images) {
        if (image.texture != rendering::kInvalidTexture) {
          graphics->destroyTexture(image.texture);
          image.texture = rendering::kInvalidTexture;
        }
      }
      for (auto& [key, texture] : asset_textures) {
        if (texture.texture != rendering::kInvalidTexture) {
          graphics->destroyTexture(texture.texture);
        }
      }
      for (auto& [key, texture] : svg_textures) {
        if (texture.texture != rendering::kInvalidTexture) {
          graphics->destroyTexture(texture.texture);
        }
      }
    }
    asset_textures.clear();
    svg_textures.clear();
    svg_intrinsic_sizes.clear();
    for (const auto& [key, font] : registered_fonts) {
      if (text_engine.findFont(key) == font.font) {
        text_engine.unregisterFont(key);
      }
    }
    registered_fonts.clear();
    svg_rasterizer.clear();
    images.clear();
    free_images.clear();
  }
};

PresentationResources::PresentationResources(
    assets::AssetRegistry& assets,
    rendering::GraphicsDevice* graphics,
    TextEngine& text_engine,
    PresentationResourceConfig config)
    : impl_(std::make_unique<Impl>(assets, graphics, text_engine,
                                   std::move(config))) {}

PresentationResources::~PresentationResources() {
  if (impl_) impl_->shutdown();
}

bool PresentationResources::ensureTreeFonts(
    const runtime_dom::Node& root) {
  if (impl_ == nullptr || !impl_->active()) return false;
  impl_->synchronizeAssetVersion();
  return impl_->ensureTreeFonts(root);
}

std::optional<GlyphTexturePlacement>
PresentationResources::ensureGlyphPlacement(const ShapedGlyph& glyph,
                                            float physical_pixel_size) {
  if (impl_ == nullptr || !impl_->active()) return std::nullopt;
  impl_->synchronizeAssetVersion();
  return impl_->ensureGlyphPlacement(glyph, physical_pixel_size);
}

rendering::UITextureHandle PresentationResources::resolveTexture(
    const ImageSource& source,
    float logical_width,
    float logical_height,
    math::Color tint,
    float scale_x,
    float scale_y) {
  if (impl_ == nullptr || !impl_->active()) return 0u;
  impl_->synchronizeAssetVersion();
  return impl_->resolveTexture(source, logical_width, logical_height, tint,
                               scale_x, scale_y);
}

std::optional<std::pair<float, float>> PresentationResources::intrinsicSize(
    const ImageSource& source) {
  if (impl_ == nullptr || !impl_->active()) return std::nullopt;
  impl_->synchronizeAssetVersion();
  return impl_->intrinsicSize(source);
}

DynamicImageHandle PresentationResources::createImage(
    const rendering::TextureDesc& desc,
    const rendering::TextureUploadData& upload) {
  if (impl_ == nullptr || !impl_->active()) return {};
  impl_->synchronizeAssetVersion();
  return impl_->createImage(desc, upload);
}

bool PresentationResources::updateImage(
    DynamicImageHandle image,
    const rendering::TextureUploadData& upload) {
  if (impl_ == nullptr || !impl_->active()) return false;
  impl_->synchronizeAssetVersion();
  return impl_->updateImage(image, upload);
}

bool PresentationResources::destroyImage(DynamicImageHandle image) {
  if (impl_ == nullptr || !impl_->active()) return false;
  impl_->synchronizeAssetVersion();
  return impl_->destroyImage(image);
}

void PresentationResources::advanceFrame() {
  if (impl_ == nullptr || !impl_->active()) return;
  ++impl_->resource_frame;
  if (impl_->resource_frame == 0u) ++impl_->resource_frame;
  const bool pruned_for_assets = impl_->synchronizeAssetVersion();
  if (!pruned_for_assets &&
      (impl_->resource_frame == 1u || impl_->resource_frame % 60u == 0u)) {
    impl_->pruneCachedTextures();
  }
}

void PresentationResources::pruneCachedTextures() {
  if (impl_ == nullptr || !impl_->active()) return;
  if (!impl_->synchronizeAssetVersion()) impl_->pruneCachedTextures();
}

std::uint64_t PresentationResources::frame() const noexcept {
  return impl_ == nullptr ? 0u : impl_->resource_frame;
}

std::uint64_t PresentationResources::resourceGeneration() const noexcept {
  return impl_ == nullptr ? 0u : impl_->resource_generation;
}

void PresentationResources::shutdown() {
  if (impl_ != nullptr) impl_->shutdown();
}

}  // namespace karma::ui::native
