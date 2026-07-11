#include "features/ui/native/svg_rasterizer.h"

#include "content/assets/asset_ui_source_import.h"

#include <lunasvg.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace karma::ui::native {
namespace {

void setError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

float normalizedDpi(float value) {
  return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}

struct CacheKey {
  std::string asset_key;
  std::string content_hash;
  std::uint64_t generation = 0u;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::uint32_t dpi_x_bits = 0u;
  std::uint32_t dpi_y_bits = 0u;
  std::uint32_t tint = 0u;
  bool has_tint = false;

  bool operator==(const CacheKey&) const = default;
};

struct CacheKeyHash {
  std::size_t operator()(const CacheKey& key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.asset_key);
    auto combine = [&hash](std::size_t value) {
      hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    };
    combine(std::hash<std::string>{}(key.content_hash));
    combine(std::hash<std::uint64_t>{}(key.generation));
    combine(key.width);
    combine(key.height);
    combine(key.dpi_x_bits);
    combine(key.dpi_y_bits);
    combine(key.tint);
    combine(key.has_tint);
    return hash;
  }
};

std::uint32_t packTint(const SvgTint& tint) {
  return static_cast<std::uint32_t>(tint.red) |
         (static_cast<std::uint32_t>(tint.green) << 8u) |
         (static_cast<std::uint32_t>(tint.blue) << 16u) |
         (static_cast<std::uint32_t>(tint.alpha) << 24u);
}

}  // namespace

struct SvgRasterizer::Impl {
  struct Entry {
    std::shared_ptr<const SvgRaster> raster;
    std::size_t bytes = 0u;
    std::uint64_t last_use = 0u;
  };

  explicit Impl(SvgRasterizerConfig requested_config) : config(requested_config) {}

  SvgRasterizerConfig config;
  std::unordered_map<CacheKey, Entry, CacheKeyHash> cache;
  std::size_t cache_bytes = 0u;
  std::uint64_t use_clock = 0u;
  mutable std::mutex mutex;

  void trim() {
    while ((!cache.empty() && cache_bytes > config.cache_budget_bytes) ||
           cache.size() > config.max_entries) {
      const auto oldest = std::min_element(
          cache.begin(), cache.end(), [](const auto& left, const auto& right) {
            return left.second.last_use < right.second.last_use;
          });
      if (oldest == cache.end()) {
        break;
      }
      cache_bytes -= oldest->second.bytes;
      cache.erase(oldest);
    }
  }
};

SvgRasterizer::SvgRasterizer(SvgRasterizerConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

SvgRasterizer::~SvgRasterizer() = default;
SvgRasterizer::SvgRasterizer(SvgRasterizer&&) noexcept = default;
SvgRasterizer& SvgRasterizer::operator=(SvgRasterizer&&) noexcept = default;

std::shared_ptr<const SvgRaster> SvgRasterizer::rasterize(
    std::string_view asset_key,
    std::uint64_t asset_generation,
    const assets::SvgAsset& asset,
    const SvgRasterRequest& request,
    std::string* error) {
  std::scoped_lock lock(impl_->mutex);
  if (error != nullptr) {
    error->clear();
  }
  if (asset_key.empty()) {
    setError(error, "SVG asset key must not be empty");
    return {};
  }
  if (request.physical_width == 0u || request.physical_height == 0u) {
    setError(error, "SVG raster dimensions must be non-zero");
    return {};
  }
  if (request.physical_width > impl_->config.max_dimension ||
      request.physical_height > impl_->config.max_dimension) {
    setError(error, "SVG raster dimensions exceed the configured safety limit");
    return {};
  }
  const std::size_t width = request.physical_width;
  const std::size_t height = request.physical_height;
  if (width > std::numeric_limits<std::size_t>::max() / 4u ||
      height > std::numeric_limits<std::size_t>::max() / (width * 4u)) {
    setError(error, "SVG raster dimensions overflow addressable memory");
    return {};
  }
  const std::size_t raster_bytes = width * height * 4u;
  if (impl_->config.max_raster_bytes == 0u ||
      raster_bytes > impl_->config.max_raster_bytes) {
    setError(error, "SVG raster exceeds the configured per-image byte limit");
    return {};
  }
  if (!assets::detail::validateSvgSource(asset.source_utf8, error)) {
    return {};
  }

  const float dpi_x = normalizedDpi(request.dpi_scale_x);
  const float dpi_y = normalizedDpi(request.dpi_scale_y);
  const CacheKey key{.asset_key = std::string(asset_key),
                     .content_hash = asset.content_hash.empty()
                                         ? assets::hashString(asset.source_utf8)
                                         : asset.content_hash,
                     .generation = asset_generation,
                     .width = request.physical_width,
                     .height = request.physical_height,
                     .dpi_x_bits = std::bit_cast<std::uint32_t>(dpi_x),
                     .dpi_y_bits = std::bit_cast<std::uint32_t>(dpi_y),
                     .tint = request.tint.has_value() ? packTint(*request.tint) : 0u,
                     .has_tint = request.tint.has_value()};
  if (const auto cached = impl_->cache.find(key); cached != impl_->cache.end()) {
    cached->second.last_use = ++impl_->use_clock;
    return cached->second.raster;
  }

  std::unique_ptr<lunasvg::Document> document =
      lunasvg::Document::loadFromData(asset.source_utf8.data(), asset.source_utf8.size());
  if (!document) {
    setError(error, "LunaSVG could not parse the validated SVG asset");
    return {};
  }
  lunasvg::Bitmap bitmap = document->renderToBitmap(
      static_cast<int>(request.physical_width),
      static_cast<int>(request.physical_height), 0x00000000u);
  if (!bitmap.valid() || bitmap.data() == nullptr || bitmap.width() <= 0 ||
      bitmap.height() <= 0) {
    setError(error, "LunaSVG could not rasterize the SVG at the requested size");
    return {};
  }
  bitmap.convertToRGBA();
  if (bitmap.stride() < bitmap.width() * 4) {
    setError(error, "LunaSVG returned an invalid bitmap row stride");
    return {};
  }

  auto raster = std::make_shared<SvgRaster>();
  raster->width = static_cast<std::uint32_t>(bitmap.width());
  raster->height = static_cast<std::uint32_t>(bitmap.height());
  raster->stride = raster->width * 4u;
  raster->pixels.resize(static_cast<std::size_t>(raster->stride) * raster->height);
  const std::size_t source_stride = static_cast<std::size_t>(bitmap.stride());
  for (std::uint32_t y = 0u; y < raster->height; ++y) {
    const std::uint8_t* source = bitmap.data() + static_cast<std::size_t>(y) * source_stride;
    std::uint8_t* destination =
        raster->pixels.data() + static_cast<std::size_t>(y) * raster->stride;
    std::copy_n(source, raster->stride, destination);
  }

  if (request.tint.has_value()) {
    const SvgTint tint = *request.tint;
    for (std::size_t offset = 0u; offset < raster->pixels.size(); offset += 4u) {
      raster->pixels[offset + 0u] = static_cast<std::uint8_t>(
          static_cast<unsigned>(raster->pixels[offset + 0u]) * tint.red / 255u);
      raster->pixels[offset + 1u] = static_cast<std::uint8_t>(
          static_cast<unsigned>(raster->pixels[offset + 1u]) * tint.green / 255u);
      raster->pixels[offset + 2u] = static_cast<std::uint8_t>(
          static_cast<unsigned>(raster->pixels[offset + 2u]) * tint.blue / 255u);
      raster->pixels[offset + 3u] = static_cast<std::uint8_t>(
          static_cast<unsigned>(raster->pixels[offset + 3u]) * tint.alpha / 255u);
    }
  }

  if (impl_->config.cache_budget_bytes > 0u && impl_->config.max_entries > 0u &&
      raster->pixels.size() <= impl_->config.cache_budget_bytes) {
    impl_->cache_bytes += raster->pixels.size();
    impl_->cache.emplace(key, Impl::Entry{raster, raster->pixels.size(), ++impl_->use_clock});
    impl_->trim();
  }
  return raster;
}

void SvgRasterizer::invalidate(std::string_view asset_key) {
  std::scoped_lock lock(impl_->mutex);
  for (auto iterator = impl_->cache.begin(); iterator != impl_->cache.end();) {
    if (iterator->first.asset_key == asset_key) {
      impl_->cache_bytes -= iterator->second.bytes;
      iterator = impl_->cache.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void SvgRasterizer::clear() {
  std::scoped_lock lock(impl_->mutex);
  impl_->cache.clear();
  impl_->cache_bytes = 0u;
}

std::size_t SvgRasterizer::cacheBytes() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->cache_bytes;
}

std::size_t SvgRasterizer::cacheSize() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->cache.size();
}

}  // namespace karma::ui::native
