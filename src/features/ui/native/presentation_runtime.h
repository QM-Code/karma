#pragma once

#include "karma/rendering.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace karma::ui::native::runtime_dom {
struct Node;
}

namespace karma::ui::native::presentation {

/// Logical clip rectangle before framebuffer scaling.
struct LogicalClip {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

/// Positive, framebuffer-clamped scissor rectangle in physical pixels.
struct FramebufferScissor {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  friend bool operator==(const FramebufferScissor&,
                         const FramebufferScissor&) = default;
};

/// Budget-relevant metadata for one retained paint fragment.
struct RetainedFragmentUsage {
  std::size_t bytes = 0u;
  std::uint64_t last_use_frame = 0u;
  std::size_t tree_depth = 0u;
};

/// Returns whether two adjacent commands can be represented by one draw.
[[nodiscard]] bool compatibleCommandState(
    const rendering::UIDrawCmd& left,
    const rendering::UIDrawCmd& right) noexcept;

/// Appends one retained draw fragment, rebasing its indices and command spans.
/// The destination's final command and the fragment's first command coalesce
/// when their state and index ranges are compatible. Empty fragments succeed;
/// malformed fragments and count overflows fail without changing destination.
[[nodiscard]] bool appendRebased(
    rendering::UIDrawData& destination,
    const rendering::UIDrawData& fragment);

/// Allocation bytes retained by a draw fragment's three backing vectors.
/// Vector object and allocator bookkeeping overhead are intentionally omitted.
[[nodiscard]] std::size_t retainedByteCost(
    const rendering::UIDrawData& draw_data) noexcept;

/// Returns whether a fragment built under `build_resource_generation` can be
/// retained under the current resource generation and byte budget. A resource
/// change during construction makes the fragment ineligible even when its
/// final texture handles happen to remain valid.
[[nodiscard]] bool canRetainFragment(
    const rendering::UIDrawData& fragment,
    std::size_t budget_bytes,
    std::uint64_t build_resource_generation,
    std::uint64_t current_resource_generation) noexcept;

/// Selects fragment indexes to evict so the remaining byte cost fits budget.
/// Oldest fragments are evicted first. Equal-age fragments prefer evicting
/// deeper (duplicated subtree) fragments, then larger fragments. The returned
/// indexes are in eviction order and selection is deterministic.
[[nodiscard]] std::vector<std::size_t> selectRetainedEvictions(
    std::span<const RetainedFragmentUsage> fragments,
    std::size_t budget_bytes);

/// Collects retained fragments below each root and evicts candidates until the
/// configured budget is met. Traversal follows runtime children, including
/// live repeat instances, in root order. Returns the number of evicted
/// fragments; retained paint revisions are cleared with their fragments.
[[nodiscard]] std::size_t enforceRetainedPaintBudget(
    std::span<runtime_dom::Node*> roots,
    std::size_t budget_bytes);

/// Converts an authored logical clip to a conservative physical scissor.
/// Left/top use floor and right/bottom use ceil before framebuffer clamping.
/// Invalid inputs and clips with no framebuffer intersection return nullopt.
[[nodiscard]] std::optional<FramebufferScissor> framebufferScissor(
    const LogicalClip& logical_clip,
    float scale_x,
    float scale_y,
    int framebuffer_width,
    int framebuffer_height) noexcept;

}  // namespace karma::ui::native::presentation
