#pragma once

#include "karma/assets.h"
#include "karma/math.h"
#include "karma/rendering.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace karma::scene_authoring {

/// Configuration for one fixed-size, single-image terrain authoring canvas.
///
/// Heights are stored as normalized samples. `height_scale` and
/// `height_offset` map them to the runtime terrain's local Y axis. Height
/// resolutions must be power-of-two-plus-one and no larger than 4097.
struct TerrainCanvasDesc {
  uint32_t resolution = 513u;
  /// Zero uses `resolution`; otherwise this is the square splat-map extent.
  uint32_t control_resolution = 0u;
  float terrain_size = 1000.0f;
  float height_scale = 120.0f;
  float height_offset = 0.0f;
};

/// Distance weighting used by terrain sculpt and splat brushes.
enum class TerrainBrushFalloff : uint8_t {
  Constant = 0,
  Linear = 1,
  Smooth = 2,
};

/// Shared terrain brush settings.
///
/// Radius is measured in terrain-local XZ units. Strength is normalized and
/// clamped to [0, 1] for each application.
struct TerrainBrush {
  float radius = 10.0f;
  float strength = 0.25f;
  TerrainBrushFalloff falloff = TerrainBrushFalloff::Smooth;
};

/// Height operation applied by `TerrainCanvas::applySculpt`.
enum class TerrainSculptMode : uint8_t {
  Raise = 0,
  Lower = 1,
  Smooth = 2,
  Flatten = 3,
  SetHeight = 4,
};

/// Closest intersection returned by `TerrainCanvas::raycast`.
struct TerrainRaycastHit {
  math::Vec3 position{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};
  float distance = 0.0f;
  float normalized_height = 0.0f;
};

/// Mutable, CPU-side authoring surface for one single-image terrain.
class TerrainCanvas {
 public:
  TerrainCanvas() = default;

  /// Creates a flat canvas. Returns no value when the descriptor is invalid.
  static std::optional<TerrainCanvas> create(
      const TerrainCanvasDesc& desc,
      float initial_normalized_height = 0.0f);

  /// Imports and bilinearly resamples a supported scalar image.
  static std::optional<TerrainCanvas> import(
      const TerrainCanvasDesc& desc,
      const assets::ScalarImage& image);

  [[nodiscard]] bool valid() const;
  [[nodiscard]] const TerrainCanvasDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t resolution() const { return desc_.resolution; }
  [[nodiscard]] uint32_t controlResolution() const {
    return desc_.control_resolution;
  }

  /// Direct sample access intended for compact regional undo snapshots.
  /// Callers must restore normalized finite height values and splat pixels
  /// whose four channels sum to 255 before previewing or saving.
  [[nodiscard]] std::span<const float> heights() const { return heights_; }
  [[nodiscard]] std::span<float> mutableHeights() { return heights_; }
  [[nodiscard]] std::span<const uint8_t> controlRgba8() const {
    return control_rgba8_;
  }
  [[nodiscard]] std::span<uint8_t> mutableControlRgba8() {
    return control_rgba8_;
  }

  /// Bilinearly samples normalized height at terrain-local XZ coordinates.
  [[nodiscard]] std::optional<float> sampleNormalizedHeight(
      float local_x,
      float local_z) const;

  /// Samples world Y for an axis-aligned terrain placed at `terrain_origin`.
  [[nodiscard]] std::optional<float> sampleWorldHeight(
      float world_x,
      float world_z,
      const math::Vec3& terrain_origin = {}) const;

  /// Intersects a world-space ray with the authored height surface.
  [[nodiscard]] std::optional<TerrainRaycastHit> raycast(
      const math::Vec3& ray_origin,
      const math::Vec3& ray_direction,
      const math::Vec3& terrain_origin = {},
      float max_distance = std::numeric_limits<float>::infinity()) const;

  /// Applies one sculpt dab centered at terrain-local XZ coordinates.
  /// `target_normalized_height` is used by Flatten and SetHeight.
  bool applySculpt(float local_x,
                   float local_z,
                   TerrainSculptMode mode,
                   const TerrainBrush& brush,
                   float target_normalized_height = 0.0f);

  /// Paints one of four normalized splat layers. Increasing the selected
  /// channel proportionally reduces the other three channels.
  bool paintLayer(float local_x,
                  float local_z,
                  uint32_t layer,
                  const TerrainBrush& brush);

  /// Builds a renderer-ready in-memory tile for live preview.
  [[nodiscard]] rendering::TerrainTileData buildTileData(
      rendering::TerrainTileCoord coord = {}) const;

  /// Writes normalized IEEE-754 float samples in little-endian row order.
  bool saveHeightR32(const std::filesystem::path& path,
                     std::string* diagnostic = nullptr) const;

  /// Writes the four splat weights as a lossless, uncompressed RGBA TGA.
  bool saveControlTga(const std::filesystem::path& path,
                      std::string* diagnostic = nullptr) const;

 private:
  TerrainCanvasDesc desc_{};
  std::vector<float> heights_;
  std::vector<uint8_t> control_rgba8_;
};

}  // namespace karma::scene_authoring
