#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace karma::assets {
class AssetRegistry;
}

namespace karma::ui::native {

struct StyleRule;

namespace runtime_dom {
struct DocumentInstance;
struct Node;
}

/// Parsed selector data retained with a theme rule. Selectors are compiled
/// while a theme is staged so restyling only performs structural matches.
struct SelectorCompound {
  std::string tag;
  std::string id;
  std::vector<std::string> classes;
  std::vector<std::string> pseudos;
};

struct CompiledSelector {
  std::vector<SelectorCompound> compounds;
  /// One entry between each pair of compounds: '>' for a direct child and
  /// ' ' for a descendant.
  std::vector<char> combinators;
  bool valid = false;
};

/// Candidate buckets use one required part of a selector's rightmost
/// compound. Full selector matching remains authoritative.
struct StyleRuleCandidateIndex {
  std::vector<std::size_t> universal;
  std::unordered_map<std::string, std::vector<std::size_t>> by_tag;
  std::unordered_map<std::string, std::vector<std::size_t>> by_id;
  std::unordered_map<std::string, std::vector<std::size_t>> by_class;

  void clear();
};

CompiledSelector compileSelector(std::string_view selector);
void rebuildStyleRuleCandidates(std::vector<StyleRule>& rules,
                                StyleRuleCandidateIndex& index);

namespace style_runtime {

/// External values used by one deterministic style pass. Runtime style and
/// motion state remains retained by the document and its nodes.
struct StyleInputs {
  const assets::AssetRegistry& assets;
  float viewport_width = 0.0f;
  float viewport_height = 0.0f;
  double now_seconds = 0.0;
  float motion_scale = 1.0f;
};

struct StyleResult {
  std::size_t restyled_nodes = 0u;
  bool layout_changed = false;
};

struct MotionResult {
  std::size_t advanced_nodes = 0u;
  bool layout_changed = false;
  bool stacking_changed = false;
};

/// Mutates an authored inline declaration and keeps its serialized attribute
/// representation synchronized. Cascading remains an explicit later step.
void setInlineStyleProperty(runtime_dom::Node& node,
                            std::string property,
                            std::string value);

/// Rebuilds compiled rule candidates and the authored-motion feature hint.
/// Non-style structural feature flags remain reconciliation concerns.
void rebuildDocumentStyleMetadata(runtime_dom::DocumentInstance& document);

/// Recomputes one present runtime subtree. Node paint/order invalidation and
/// active-motion membership are applied immediately; broad style, font, and
/// layout pass revisions remain under the caller's control.
[[nodiscard]] StyleResult styleSubtree(
    runtime_dom::DocumentInstance& document,
    runtime_dom::Node& root,
    const StyleInputs& inputs);

/// Recomputes the complete document and settles its broad style revision,
/// propagating resulting layout and font work to later frame stages.
[[nodiscard]] StyleResult styleDocument(
    runtime_dom::DocumentInstance& document,
    const StyleInputs& inputs);

/// Targeted pseudo/state restyle preserving the existing layout, font, and
/// accessibility invalidation contract without traversing siblings. Changed
/// inherited values propagate through descendants that still follow the old
/// value.
[[nodiscard]] StyleResult restyleNode(
    runtime_dom::DocumentInstance& document,
    runtime_dom::Node* node,
    const StyleInputs& inputs);

/// Advances only directly tracked transition/animation nodes. A node present
/// in both sets contributes once to advanced_nodes for `motion_frame`.
[[nodiscard]] MotionResult advanceActiveMotion(
    runtime_dom::DocumentInstance& document,
    double now_seconds,
    float motion_scale,
    std::uint64_t motion_frame);

/// Completes transitions and samples active animations at zero motion scale,
/// then clears both active sets.
[[nodiscard]] MotionResult finishActiveMotion(
    runtime_dom::DocumentInstance& document,
    double now_seconds);

}  // namespace style_runtime

}  // namespace karma::ui::native
