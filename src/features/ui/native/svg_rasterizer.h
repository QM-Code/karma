#pragma once

#include "karma/assets.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {

struct SvgRasterizerConfig {
  std::size_t cache_budget_bytes = 64u * 1024u * 1024u;
  std::size_t max_raster_bytes = 256u * 1024u * 1024u;
  std::size_t max_entries = 1024u;
  std::uint32_t max_dimension = 16384u;
};

struct SvgTint {
  std::uint8_t red = 255u;
  std::uint8_t green = 255u;
  std::uint8_t blue = 255u;
  std::uint8_t alpha = 255u;

  bool operator==(const SvgTint&) const = default;
};

struct SvgRasterRequest {
  std::uint32_t physical_width = 0u;
  std::uint32_t physical_height = 0u;
  float dpi_scale_x = 1.0f;
  float dpi_scale_y = 1.0f;
  std::optional<SvgTint> tint;
};

struct SvgRaster {
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::uint32_t stride = 0u;
  /// Straight-alpha RGBA8 pixels.
  std::vector<std::uint8_t> pixels;
};

/// Sandboxed LunaSVG rasterization with a generation/size/DPI/tint LRU cache.
class SvgRasterizer {
 public:
  explicit SvgRasterizer(SvgRasterizerConfig config = {});
  ~SvgRasterizer();
  SvgRasterizer(SvgRasterizer&&) noexcept;
  SvgRasterizer& operator=(SvgRasterizer&&) noexcept;
  SvgRasterizer(const SvgRasterizer&) = delete;
  SvgRasterizer& operator=(const SvgRasterizer&) = delete;

  std::shared_ptr<const SvgRaster> rasterize(std::string_view asset_key,
                                             std::uint64_t asset_generation,
                                             const assets::SvgAsset& asset,
                                             const SvgRasterRequest& request,
                                             std::string* error = nullptr);

  void invalidate(std::string_view asset_key);
  void clear();
  std::size_t cacheBytes() const;
  std::size_t cacheSize() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
