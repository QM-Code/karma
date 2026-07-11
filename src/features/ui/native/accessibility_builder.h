#pragma once

#include "karma/ui.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace karma::ui::native {

namespace runtime_dom {
struct DocumentInstance;
}

/// Incrementally assembles one accessibility snapshot from a DOM traversal.
/// Parents must be appended before their children. The previous tree is
/// accepted by value so its vector allocations can be reused by the rebuild.
class AccessibilityTreeBuilder {
 public:
  explicit AccessibilityTreeBuilder(AccessibilityTree previous = {});

  /// Appends one semantic node and records its tree relationship. Authored
  /// child indexes on `node` are discarded; relationships are owned by the
  /// builder so every output index is guaranteed to refer to this snapshot.
  [[nodiscard]] std::size_t append(
      AccessibilityNode node,
      std::optional<std::size_t> parent = std::nullopt);

  /// Completes the snapshot and advances its generation exactly once.
  [[nodiscard]] AccessibilityTree finish() &&;

 private:
  AccessibilityTree tree_;
};

/// Derives and assembles the public semantic snapshot for the supplied
/// documents in paint order. Runtime documents remain borrowed for the
/// duration of the call; the returned tree owns all exported semantic data.
[[nodiscard]] AccessibilityTree buildAccessibilityTree(
    AccessibilityTree previous,
    const std::vector<runtime_dom::DocumentInstance*>& documents);

}  // namespace karma::ui::native
