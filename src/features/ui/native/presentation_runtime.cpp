#include "features/ui/native/presentation_runtime.h"

#include "features/ui/native/runtime_dom.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace karma::ui::native::presentation {
namespace {

bool empty(const rendering::UIDrawData& draw_data) {
  return draw_data.vertices.empty() && draw_data.indices.empty() &&
         draw_data.commands.empty();
}

bool countFits(std::size_t current,
               std::size_t added,
               std::size_t maximum) {
  return current <= maximum && added <= maximum - current;
}

std::size_t saturatedProduct(std::size_t count, std::size_t element_size) {
  constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
  return element_size != 0u && count > maximum / element_size
             ? maximum
             : count * element_size;
}

std::size_t saturatedAdd(std::size_t left, std::size_t right) {
  constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
  return right > maximum - left ? maximum : left + right;
}

}  // namespace

bool compatibleCommandState(const rendering::UIDrawCmd& left,
                            const rendering::UIDrawCmd& right) noexcept {
  return left.texture == right.texture &&
         left.blend_mode == right.blend_mode &&
         left.sampler_mode == right.sampler_mode &&
         left.texture_mode == right.texture_mode &&
         left.scissor_enabled == right.scissor_enabled &&
         (!left.scissor_enabled ||
          (left.scissor_x == right.scissor_x &&
           left.scissor_y == right.scissor_y &&
           left.scissor_w == right.scissor_w &&
           left.scissor_h == right.scissor_h));
}

bool appendRebased(rendering::UIDrawData& destination,
                   const rendering::UIDrawData& fragment) {
  if (&destination == &fragment) return false;
  if (empty(fragment)) return true;
  if (!rendering::validateUIDrawData(fragment)) return false;

  const std::size_t vertex_base = destination.vertices.size();
  const std::size_t index_base = destination.indices.size();
  if (!countFits(vertex_base, fragment.vertices.size(),
                 rendering::kMaxUIVertices) ||
      !countFits(index_base, fragment.indices.size(),
                 rendering::kMaxUIIndices)) {
    return false;
  }

  const std::uint64_t maximum_u32 =
      std::numeric_limits<std::uint32_t>::max();
  for (const std::uint32_t index : fragment.indices) {
    if (static_cast<std::uint64_t>(vertex_base) + index > maximum_u32) {
      return false;
    }
  }
  for (const rendering::UIDrawCmd& command : fragment.commands) {
    if (static_cast<std::uint64_t>(index_base) + command.index_offset >
        maximum_u32) {
      return false;
    }
  }

  rendering::UIDrawCmd first = fragment.commands.front();
  first.index_offset += static_cast<std::uint32_t>(index_base);
  bool merge_boundary = false;
  if (!destination.commands.empty() &&
      compatibleCommandState(destination.commands.back(), first)) {
    const rendering::UIDrawCmd& previous = destination.commands.back();
    const std::uint64_t previous_end =
        static_cast<std::uint64_t>(previous.index_offset) +
        previous.index_count;
    merge_boundary = previous_end == first.index_offset &&
                     static_cast<std::uint64_t>(previous.index_count) +
                             first.index_count <=
                         maximum_u32;
  }
  const std::size_t added_commands =
      fragment.commands.size() - (merge_boundary ? 1u : 0u);
  if (!countFits(destination.commands.size(), added_commands,
                 rendering::kMaxUIDrawCommands)) {
    return false;
  }

  destination.vertices.reserve(vertex_base + fragment.vertices.size());
  destination.indices.reserve(index_base + fragment.indices.size());
  destination.commands.reserve(destination.commands.size() + added_commands);
  destination.vertices.insert(destination.vertices.end(),
                              fragment.vertices.begin(),
                              fragment.vertices.end());
  for (const std::uint32_t index : fragment.indices) {
    destination.indices.push_back(
        static_cast<std::uint32_t>(vertex_base + index));
  }

  std::size_t first_command = 0u;
  if (merge_boundary) {
    destination.commands.back().index_count += first.index_count;
    first_command = 1u;
  }
  for (std::size_t index = first_command;
       index < fragment.commands.size(); ++index) {
    rendering::UIDrawCmd command = fragment.commands[index];
    command.index_offset += static_cast<std::uint32_t>(index_base);
    destination.commands.push_back(command);
  }
  return true;
}

std::size_t retainedByteCost(
    const rendering::UIDrawData& draw_data) noexcept {
  std::size_t bytes = saturatedProduct(draw_data.vertices.capacity(),
                                       sizeof(rendering::UIVertex));
  bytes = saturatedAdd(
      bytes, saturatedProduct(draw_data.indices.capacity(),
                              sizeof(std::uint32_t)));
  return saturatedAdd(
      bytes, saturatedProduct(draw_data.commands.capacity(),
                              sizeof(rendering::UIDrawCmd)));
}

bool canRetainFragment(
    const rendering::UIDrawData& fragment,
    std::size_t budget_bytes,
    std::uint64_t build_resource_generation,
    std::uint64_t current_resource_generation) noexcept {
  return budget_bytes > 0u &&
         build_resource_generation == current_resource_generation &&
         retainedByteCost(fragment) <= budget_bytes;
}

std::vector<std::size_t> selectRetainedEvictions(
    std::span<const RetainedFragmentUsage> fragments,
    std::size_t budget_bytes) {
  std::vector<std::size_t> oldest_first(fragments.size());
  for (std::size_t index = 0u; index < fragments.size(); ++index) {
    oldest_first[index] = index;
  }
  std::sort(oldest_first.begin(), oldest_first.end(),
            [&](std::size_t left_index, std::size_t right_index) {
              const RetainedFragmentUsage& left = fragments[left_index];
              const RetainedFragmentUsage& right = fragments[right_index];
              if (left.last_use_frame != right.last_use_frame) {
                return left.last_use_frame < right.last_use_frame;
              }
              if (left.tree_depth != right.tree_depth) {
                return left.tree_depth > right.tree_depth;
              }
              if (left.bytes != right.bytes) return left.bytes > right.bytes;
              return left_index < right_index;
            });

  // Keep the newest suffix that fits. This is equivalent to repeatedly
  // evicting the oldest candidate until the total fits, but it cannot overflow
  // while summing fragment sizes.
  std::size_t retained_bytes = 0u;
  std::size_t first_retained = oldest_first.size();
  while (first_retained > 0u) {
    const std::size_t candidate = oldest_first[first_retained - 1u];
    const std::size_t bytes = fragments[candidate].bytes;
    if (bytes > budget_bytes - retained_bytes) break;
    retained_bytes += bytes;
    --first_retained;
  }
  oldest_first.resize(first_retained);
  return oldest_first;
}

std::size_t enforceRetainedPaintBudget(
    std::span<runtime_dom::Node*> roots,
    std::size_t budget_bytes) {
  struct Candidate {
    runtime_dom::Node* node = nullptr;
    RetainedFragmentUsage usage{};
  };
  std::vector<Candidate> candidates;
  auto collect = [&](auto&& self,
                     runtime_dom::Node& node,
                     std::size_t depth) -> void {
    if (node.retained_fragment) {
      candidates.push_back({
          .node = &node,
          .usage = {
              .bytes = retainedByteCost(*node.retained_fragment),
              .last_use_frame = node.retained_last_use_frame,
              .tree_depth = depth,
          },
      });
    }
    runtime_dom::forRuntimeChildren(
        node, [&](runtime_dom::Node& child, const Value::Object*) {
          self(self, child, depth + 1u);
        });
  };
  for (runtime_dom::Node* root : roots) {
    if (root != nullptr) collect(collect, *root, 0u);
  }

  std::vector<RetainedFragmentUsage> usage;
  usage.reserve(candidates.size());
  for (const Candidate& candidate : candidates) {
    usage.push_back(candidate.usage);
  }
  const std::vector<std::size_t> evictions =
      selectRetainedEvictions(usage, budget_bytes);
  for (const std::size_t index : evictions) {
    candidates[index].node->retained_fragment.reset();
    candidates[index].node->retained_paint_revision = 0u;
  }
  return evictions.size();
}

std::optional<FramebufferScissor> framebufferScissor(
    const LogicalClip& logical_clip,
    float scale_x,
    float scale_y,
    int framebuffer_width,
    int framebuffer_height) noexcept {
  if (framebuffer_width <= 0 || framebuffer_height <= 0 ||
      !std::isfinite(scale_x) || !std::isfinite(scale_y) ||
      scale_x <= 0.0f || scale_y <= 0.0f ||
      !std::isfinite(logical_clip.x) ||
      !std::isfinite(logical_clip.y) ||
      !std::isfinite(logical_clip.width) ||
      !std::isfinite(logical_clip.height) || logical_clip.width <= 0.0f ||
      logical_clip.height <= 0.0f) {
    return std::nullopt;
  }

  const double left = std::floor(static_cast<double>(logical_clip.x) *
                                 static_cast<double>(scale_x));
  const double top = std::floor(static_cast<double>(logical_clip.y) *
                                static_cast<double>(scale_y));
  const double right = std::ceil(
      (static_cast<double>(logical_clip.x) + logical_clip.width) *
      static_cast<double>(scale_x));
  const double bottom = std::ceil(
      (static_cast<double>(logical_clip.y) + logical_clip.height) *
      static_cast<double>(scale_y));

  const auto clamp_pixel = [](double value, int extent) {
    return static_cast<int>(
        std::clamp(value, 0.0, static_cast<double>(extent)));
  };
  const int clamped_left = clamp_pixel(left, framebuffer_width);
  const int clamped_top = clamp_pixel(top, framebuffer_height);
  const int clamped_right = clamp_pixel(right, framebuffer_width);
  const int clamped_bottom = clamp_pixel(bottom, framebuffer_height);
  if (clamped_right <= clamped_left || clamped_bottom <= clamped_top) {
    return std::nullopt;
  }
  return FramebufferScissor{.x = clamped_left,
                            .y = clamped_top,
                            .width = clamped_right - clamped_left,
                            .height = clamped_bottom - clamped_top};
}

}  // namespace karma::ui::native::presentation
