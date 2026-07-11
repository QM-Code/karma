#pragma once

#include "features/ui/native/authoring.h"
#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/document_reconciler.h"
#include "karma/ui.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace karma::ui::native::runtime_dom {

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

[[nodiscard]] bool contains(const Rect& rect, double x, double y) noexcept;
[[nodiscard]] bool nonEmpty(const Rect& rect) noexcept;
[[nodiscard]] Rect intersectRects(const Rect& left,
                                  const Rect& right) noexcept;

struct NodeFontSource {
  std::string registration_key;
  std::string asset_key;
  std::uint32_t face_index = 0u;
};

struct Node;

struct TemplateInstance {
  std::string key;
  Value::Object locals;
  std::vector<std::unique_ptr<Node>> children;
};

struct AnimationState {
  std::string name;
  std::string signature;
  AnimationSpec spec{};
  double start_seconds = 0.0;
  std::unordered_map<std::string, std::optional<std::string>> underlay;
  bool completed = false;
};

enum class ScrollbarPart : std::uint8_t {
  None,
  HorizontalTrack,
  HorizontalThumb,
  VerticalTrack,
  VerticalThumb,
  Corner,
};

enum class DragInteraction : std::uint8_t {
  None,
  WindowMove,
  WindowResizeLeft,
  WindowResizeRight,
  WindowResizeTop,
  WindowResizeBottom,
  WindowResizeTopLeft,
  WindowResizeTopRight,
  WindowResizeBottomLeft,
  WindowResizeBottomRight,
  WindowClose,
  WindowCollapse,
  Splitter,
};

struct Node {
  std::string tag;
  std::string id;
  std::unordered_set<std::string> classes;
  std::vector<std::string> style_names;
  std::unordered_map<std::string, std::string> attributes;
  Declarations inline_style;
  std::string source_text;
  std::string text;
  std::string title;
  bool programmatic_text = false;
  ImageSource image{};
  Value control_value{};
  Node* parent = nullptr;
  std::vector<std::unique_ptr<Node>> children;
  std::vector<TemplateInstance> instances;
  ElementHandle handle{};
  Rect layout{};
  Rect clip{};
  std::unordered_map<std::string, std::string> style;
  std::vector<std::string> font_keys;
  std::vector<NodeFontSource> font_sources;
  std::unordered_map<std::string, std::unique_ptr<TransitionTrack>> transitions;
  std::optional<AnimationState> animation;
  std::uint64_t motion_advance_frame = 0u;
  bool present = true;
  bool hovered = false;
  bool active = false;
  bool focused = false;
  bool disabled = false;
  bool checked = false;
  bool collapsed_hidden = false;
  bool template_node = false;
  float scroll_x = 0.0f;
  float scroll_y = 0.0f;
  float scroll_max_x = 0.0f;
  float scroll_max_y = 0.0f;
  float scroll_content_width = 0.0f;
  float scroll_content_height = 0.0f;
  Rect scroll_viewport{};
  Rect horizontal_scroll_track{};
  Rect horizontal_scroll_thumb{};
  Rect vertical_scroll_track{};
  Rect vertical_scroll_thumb{};
  Rect scrollbar_corner{};
  ScrollbarPart hovered_scrollbar_part = ScrollbarPart::None;
  ScrollbarPart active_scrollbar_part = ScrollbarPart::None;
  double scrollbar_drag_pointer = 0.0;
  float scrollbar_drag_offset = 0.0f;
  DragInteraction drag_interaction = DragInteraction::None;
  double drag_start_x = 0.0;
  double drag_start_y = 0.0;
  Rect drag_start_rect{};
  Node* drag_resize_target = nullptr;
  Rect window_titlebar{};
  Rect window_close_button{};
  Rect window_collapse_button{};
  double tooltip_hover_started = -1.0;
  std::size_t virtual_total_count = 0u;
  std::size_t virtual_first_index = 0u;
  std::size_t virtual_last_index = 0u;
  float virtual_item_extent = 0.0f;
  std::string identity;
  std::optional<AnchorSpec> anchor;

  // Retained presentation cache. Geometry is stored in framebuffer space for
  // the scale at which it was built; mutations invalidate this node and its
  // ancestors while leaving unaffected sibling fragments reusable.
  std::uint64_t paint_revision = 1u;
  mutable std::uint64_t retained_paint_revision = 0u;
  mutable std::uint64_t retained_resource_generation = 0u;
  mutable std::uint64_t retained_last_use_frame = 0u;
  mutable float retained_scale_x = 0.0f;
  mutable float retained_scale_y = 0.0f;
  mutable float retained_inherited_opacity = 0.0f;
  mutable bool retained_overlay_subtree = false;
  mutable std::unique_ptr<rendering::UIDrawData> retained_fragment;
  mutable bool reported_nine_slice_cell_limit = false;

  // Stable author order, sorted by the current computed z-index. Both paint
  // and reverse hit testing consume this cache so they cannot disagree about
  // stacking order. Runtime repeat changes and child z-index restyles bump the
  // owning node's revision.
  std::uint64_t child_order_revision = 1u;
  mutable std::uint64_t retained_child_order_revision = 0u;
  mutable std::vector<const Node*> retained_child_order;
};

/// Normalized computed-style lookup shared by layout, input, paint, and
/// accessibility. Authored values are trimmed and lower-cased; the fallback is
/// returned unchanged when the property is absent.
[[nodiscard]] std::string styleString(const Node& node,
                                      std::string_view property,
                                      std::string fallback = {});
[[nodiscard]] float styleFloat(const Node& node,
                               std::string_view property,
                               float fallback);
[[nodiscard]] std::string overflowForAxis(const Node& node,
                                          std::string_view axis);
[[nodiscard]] bool scrollableOverflow(std::string_view value) noexcept;
[[nodiscard]] bool clipsOverflow(const Node& node);
[[nodiscard]] Rect clipForOverflow(const Node& node,
                                   Rect inherited,
                                   Rect bounds);
[[nodiscard]] bool isScrollContainer(const Node& node);
[[nodiscard]] bool isVisibleForInteraction(const Node& node);
[[nodiscard]] bool isFocusableTag(std::string_view tag) noexcept;
[[nodiscard]] bool attributeBoolean(const Node& node,
                                    std::string_view name);
[[nodiscard]] std::unique_ptr<Node> cloneNode(const Node& source,
                                              Node* parent,
                                              std::string key_prefix);

template <typename Callback>
void forRuntimeChildren(Node& node, Callback&& callback) {
  for (auto& child : node.children) {
    if (child->template_node) {
      for (TemplateInstance& instance : child->instances) {
        for (auto& repeated : instance.children) {
          callback(*repeated, &instance.locals);
        }
      }
    } else {
      callback(*child, nullptr);
    }
  }
}

template <typename Callback>
void forRuntimeChildren(const Node& node, Callback&& callback) {
  for (const auto& child : node.children) {
    if (child->template_node) {
      for (const TemplateInstance& instance : child->instances) {
        for (const auto& repeated : instance.children) {
          callback(*repeated, &instance.locals);
        }
      }
    } else {
      callback(*child, nullptr);
    }
  }
}

template <typename Callback>
void visitRuntimeTree(Node& node, Callback&& callback) {
  callback(node);
  forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
    visitRuntimeTree(child, callback);
  });
}

template <typename Callback>
void visitRuntimeTree(const Node& node, Callback&& callback) {
  callback(node);
  forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
    visitRuntimeTree(child, callback);
  });
}

/// Returns present, non-collapsed runtime children whose computed display is
/// not `none`, including live repeat instances in authored order.
[[nodiscard]] std::vector<Node*> visibleRuntimeChildren(Node& node);

[[nodiscard]] bool isWithin(const Node* node,
                            const Node* ancestor) noexcept;
[[nodiscard]] Node* ancestorWithTag(Node* node,
                                    std::string_view tag) noexcept;
[[nodiscard]] const Node* ancestorWithTag(const Node* node,
                                          std::string_view tag) noexcept;

/// Translates layout and derived interaction geometry for one runtime subtree,
/// then recomputes descendant clips from authored overflow policy.
void translateSubtree(Node& node,
                      float delta_x,
                      float delta_y,
                      Rect inherited_clip);

void invalidateRuntimeChildOrder(Node& node) noexcept;
[[nodiscard]] const std::vector<const Node*>& runtimeChildrenInPaintOrder(
    const Node& node);
void invalidatePaint(Node* node) noexcept;
void invalidatePaintTree(Node& node) noexcept;

// A work domain advances once when it transitions from settled to pending.
// Multiple mutations before the corresponding stage runs collapse into one
// generation, while requested/completed remain inspectable when diagnosing
// invalidation routing.
struct WorkRevision {
  std::uint64_t requested = 1u;
  std::uint64_t completed = 0u;

  [[nodiscard]] bool pending() const noexcept {
    return requested != completed;
  }

  operator bool() const noexcept { return pending(); }

  WorkRevision& operator=(bool pending_work) noexcept {
    if (pending_work) {
      invalidate();
    } else {
      complete();
    }
    return *this;
  }

  void invalidate() noexcept {
    if (pending()) return;
    ++requested;
    if (requested == 0u) {
      requested = 1u;
      completed = 0u;
    }
  }

  void complete() noexcept { completed = requested; }
};

struct DocumentInstance {
  DocumentHandle handle{};
  std::string asset_key;
  std::string source_hash;
  std::string style_hash;
  std::filesystem::path development_path;
  std::filesystem::path development_root;
  std::vector<std::filesystem::path> development_dependencies;
  std::vector<std::string> stylesheet_keys;
  std::vector<std::string> reload_stylesheet_keys;
  OpenDocumentOptions options{};
  std::uint64_t opening_order = 0;
  Value model = Value::Object{};
  std::unique_ptr<Node> body;
  CanvasSpec canvas_spec;
  CanvasLayout canvas_layout;
  std::vector<StyleRule> rules;
  StyleRuleCandidateIndex rule_candidates;
  std::vector<FontFaceDefinition> font_faces;
  std::vector<Keyframes> keyframes;
  std::vector<Diagnostic> diagnostics;
  std::string reload_requested_fingerprint;
  std::uint64_t reload_request_sequence = 0u;
  bool reload_pending = false;
  std::unordered_map<std::string, ElementHandle> ids;
  ElementHandle focused{};
  ElementHandle hovered{};
  ElementHandle pointer_capture{};
  bool pointer_down = false;
  WorkRevision binding_revision;
  WorkRevision style_revision;
  WorkRevision measure_revision;
  WorkRevision layout_revision;
  WorkRevision accessibility_revision;
  WorkRevision font_revision;
  WorkRevision placement_revision;
  WorkRevision virtual_range_revision;
  WorkRevision dependency_index_revision;
  std::vector<reconciler::BindingDependency> binding_dependencies;
  std::vector<std::string> pending_model_paths;

  // Nodes enter these sets when styling starts a transition or configures a
  // running keyframe animation. Frame advancement visits only these nodes.
  std::unordered_set<Node*> active_transition_nodes;
  std::unordered_set<Node*> active_animation_nodes;

  // Top-level transient roots are painted after ordinary document content and
  // hit-tested in the exact reverse order. Keep that order until structure,
  // visibility, open state, or a relevant computed style changes.
  WorkRevision overlay_order_revision;
  std::vector<Node*> retained_overlay_order;
  bool has_tooltips = false;
  bool has_virtual_lists = false;
  bool has_transients = false;
  bool has_motion = false;
};

}  // namespace karma::ui::native::runtime_dom
