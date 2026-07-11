#include "features/ui/native/focus_runtime.h"

#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

namespace karma::ui::native::focus_runtime {

using runtime_dom::Node;
using runtime_dom::forRuntimeChildren;
using runtime_dom::isFocusableTag;
using runtime_dom::styleString;

namespace {

using string_utils::parseFiniteDouble;
using string_utils::trim;

}  // namespace

void collectFocusable(Node& node, std::vector<Node*>& output) {
  if (!node.present || node.collapsed_hidden ||
      styleString(node, "display", "block") == "none") return;
  const auto tabindex = node.attributes.find("tab-index");
  const bool explicitly_disabled = tabindex != node.attributes.end() && trim(tabindex->second) == "-1";
  if (!node.disabled && !explicitly_disabled &&
      (isFocusableTag(node.tag) || tabindex != node.attributes.end())) {
    output.push_back(&node);
  }
  forRuntimeChildren(node, [&](Node& child, const Value::Object*) { collectFocusable(child, output); });
}

void sortFocusableForTab(std::vector<Node*>& nodes) {
  auto order = [](const Node* node) -> std::optional<int> {
    const auto found = node->attributes.find("tab-index");
    if (found == node->attributes.end()) return std::nullopt;
    const std::optional<double> value = parseFiniteDouble(found->second);
    if (!value.has_value() || *value <= 0.0 ||
        *value > static_cast<double>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    return static_cast<int>(*value);
  };
  std::stable_sort(nodes.begin(), nodes.end(), [&](const Node* left, const Node* right) {
    const auto left_order = order(left);
    const auto right_order = order(right);
    if (left_order.has_value() != right_order.has_value()) {
      return left_order.has_value();
    }
    return left_order.has_value() && *left_order < *right_order;
  });
}

Node* spatialCandidate(const std::vector<Node*>& candidates,
                       const Node* current,
                       float direction_x,
                       float direction_y) {
  if (candidates.empty()) return nullptr;
  if (current == nullptr) return candidates.front();
  const float current_x = current->layout.x + current->layout.width * 0.5f;
  const float current_y = current->layout.y + current->layout.height * 0.5f;
  Node* best = nullptr;
  float best_score = std::numeric_limits<float>::max();
  for (Node* candidate : candidates) {
    if (candidate == current) continue;
    const float delta_x = candidate->layout.x + candidate->layout.width * 0.5f - current_x;
    const float delta_y = candidate->layout.y + candidate->layout.height * 0.5f - current_y;
    const float forward = delta_x * direction_x + delta_y * direction_y;
    if (forward <= 0.01f) continue;
    const float sideways = std::abs(delta_x * direction_y - delta_y * direction_x);
    const float score = forward + sideways * 2.5f;
    if (score < best_score) {
      best_score = score;
      best = candidate;
    }
  }
  return best;
}

}  // namespace karma::ui::native::focus_runtime
