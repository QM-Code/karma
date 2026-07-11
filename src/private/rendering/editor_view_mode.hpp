#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace karma::rendering::detail {

inline constexpr uint32_t kRenderedEditorViewMode = 0u;
inline constexpr uint32_t kWireEditorViewMode = 3u;
inline constexpr uint32_t kMaxEditorViewMode = kWireEditorViewMode;

/// Decodes the numeric camera parameter used by the scene editor. Clamp the
/// floating-point value before rounding so extreme finite inputs cannot
/// overflow the integral rounding operation.
inline uint32_t decodeEditorViewMode(float value) noexcept {
  if (!std::isfinite(value)) {
    return kRenderedEditorViewMode;
  }
  const float clamped = std::clamp(
      value,
      static_cast<float>(kRenderedEditorViewMode),
      static_cast<float>(kMaxEditorViewMode));
  return static_cast<uint32_t>(std::lround(clamped));
}

/// Resolves a decoded request against graphics-device capabilities. Invalid
/// values and unsupported wireframe requests deliberately use the normal
/// rendered view instead of a wire-colored solid-fill approximation.
inline uint32_t effectiveEditorViewMode(uint32_t requested,
                                        bool wireframe_fill_supported) noexcept {
  if (requested > kMaxEditorViewMode ||
      (requested == kWireEditorViewMode && !wireframe_fill_supported)) {
    return kRenderedEditorViewMode;
  }
  return requested;
}

}  // namespace karma::rendering::detail
