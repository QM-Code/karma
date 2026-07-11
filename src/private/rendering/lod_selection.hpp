#pragma once

#include <cstddef>

namespace karma::rendering::detail {

/// Selects the base bucket (zero) or the last renderable LOD bucket whose
/// threshold has been crossed. LOD bucket indices retain their authored
/// position so callers can address per-level renderer state directly.
template <class LodRange, class IsRenderable>
std::size_t selectRenderableLodBucket(float distance,
                                      const LodRange& lods,
                                      IsRenderable&& is_renderable) {
  std::size_t selected_bucket = 0u;
  std::size_t lod_index = 0u;
  for (const auto& lod : lods) {
    if (distance < lod.start_distance) break;
    if (is_renderable(lod)) selected_bucket = lod_index + 1u;
    ++lod_index;
  }
  return selected_bucket;
}

template <class LodRange, class IsRenderable>
bool hasUnrenderableLod(const LodRange& lods,
                        IsRenderable&& is_renderable) {
  for (const auto& lod : lods) {
    if (!is_renderable(lod)) return true;
  }
  return false;
}

}  // namespace karma::rendering::detail
