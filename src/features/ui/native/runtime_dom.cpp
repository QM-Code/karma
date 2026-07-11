#include "features/ui/native/runtime_dom.h"

#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace karma::ui::native::runtime_dom {
namespace {

using string_utils::lower;
using string_utils::trim;

float zIndex(const Node& node) {
  const auto found = node.style.find("z-index");
  if (found == node.style.end()) return 0.0f;

  const std::string& source = found->second;
  const char* begin = source.c_str();
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  while (end != nullptr && *end != '\0' &&
         std::isspace(static_cast<unsigned char>(*end)) != 0) {
    ++end;
  }
  return end != begin && end != nullptr && *end == '\0' &&
                 std::isfinite(value)
             ? static_cast<float>(value)
             : 0.0f;
}

}  // namespace

bool contains(const Rect& rect, double x, double y) noexcept {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
         y < rect.y + rect.height;
}

bool nonEmpty(const Rect& rect) noexcept {
  return rect.width > 0.0f && rect.height > 0.0f;
}

Rect intersectRects(const Rect& left, const Rect& right) noexcept {
  const float x1 = std::max(left.x, right.x);
  const float y1 = std::max(left.y, right.y);
  const float x2 = std::min(left.x + left.width, right.x + right.width);
  const float y2 = std::min(left.y + left.height, right.y + right.height);
  return Rect{.x = x1,
              .y = y1,
              .width = std::max(0.0f, x2 - x1),
              .height = std::max(0.0f, y2 - y1)};
}

std::string styleString(const Node& node,
                        std::string_view property,
                        std::string fallback) {
  const auto found = node.style.find(std::string(property));
  return found == node.style.end() ? std::move(fallback)
                                   : lower(trim(found->second));
}

float styleFloat(const Node& node,
                 std::string_view property,
                 float fallback) {
  const auto found = node.style.find(std::string(property));
  if (found == node.style.end()) return fallback;
  const std::string source = trim(found->second);
  if (source.empty()) return fallback;
  char* end = nullptr;
  const double value = std::strtod(source.c_str(), &end);
  return end == source.c_str() + source.size() && std::isfinite(value)
             ? static_cast<float>(value)
             : fallback;
}

std::string overflowForAxis(const Node& node, std::string_view axis) {
  const std::string property = axis == "x" ? "overflow-x" : "overflow-y";
  if (const auto found = node.style.find(property); found != node.style.end()) {
    return lower(trim(found->second));
  }
  return styleString(node, "overflow",
                     node.tag == "scroll" || node.tag == "list" ? "auto"
                                                                   : "visible");
}

bool scrollableOverflow(std::string_view value) noexcept {
  return value == "auto" || value == "scroll";
}

bool clipsOverflow(const Node& node) {
  return overflowForAxis(node, "x") != "visible" ||
         overflowForAxis(node, "y") != "visible";
}

Rect clipForOverflow(const Node& node, Rect inherited, Rect bounds) {
  if (overflowForAxis(node, "x") != "visible") {
    const float left = std::max(inherited.x, bounds.x);
    const float right =
        std::min(inherited.x + inherited.width, bounds.x + bounds.width);
    inherited.x = left;
    inherited.width = std::max(0.0f, right - left);
  }
  if (overflowForAxis(node, "y") != "visible") {
    const float top = std::max(inherited.y, bounds.y);
    const float bottom =
        std::min(inherited.y + inherited.height, bounds.y + bounds.height);
    inherited.y = top;
    inherited.height = std::max(0.0f, bottom - top);
  }
  return inherited;
}

bool isScrollContainer(const Node& node) {
  return scrollableOverflow(overflowForAxis(node, "x")) ||
         scrollableOverflow(overflowForAxis(node, "y"));
}

std::vector<Node*> visibleRuntimeChildren(Node& node) {
  std::vector<Node*> children;
  forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
    if (child.present && !child.collapsed_hidden &&
        styleString(child, "display", "block") != "none") {
      children.push_back(&child);
    }
  });
  return children;
}

bool isVisibleForInteraction(const Node& node) {
  for (const Node* current = &node; current != nullptr;
       current = current->parent) {
    if (!current->present || current->collapsed_hidden ||
        styleString(*current, "display", "block") == "none") {
      return false;
    }
  }
  return true;
}

bool isFocusableTag(std::string_view tag) noexcept {
  return tag == "button" || tag == "toggle" || tag == "slider" ||
         tag == "select" || tag == "option" || tag == "scroll" ||
         tag == "list" || tag == "window" || tag == "tab" ||
         tag == "disclosure" || tag == "tree-item" ||
         tag == "menu-item" || tag == "splitter";
}

bool attributeBoolean(const Node& node, std::string_view name) {
  const auto found = node.attributes.find(std::string(name));
  if (found == node.attributes.end()) return false;
  const std::string value = lower(trim(found->second));
  return value.empty() || value == "true" || value == "1" || value == name;
}

std::unique_ptr<Node> cloneNode(const Node& source,
                                Node* parent,
                                std::string key_prefix) {
  auto result = std::make_unique<Node>();
  result->tag = source.tag;
  result->id = source.id;
  result->classes = source.classes;
  result->style_names = source.style_names;
  result->attributes = source.attributes;
  result->inline_style = source.inline_style;
  result->source_text = source.source_text;
  result->text = source.source_text;
  result->title = source.title;
  result->programmatic_text = source.programmatic_text;
  result->image = source.image;
  result->parent = parent;
  result->template_node = source.template_node;
  result->identity = std::move(key_prefix) + "/" + source.identity;
  result->anchor = source.anchor;
  for (const auto& child : source.children) {
    result->children.push_back(
        cloneNode(*child, result.get(), result->identity));
  }
  return result;
}

bool isWithin(const Node* node, const Node* ancestor) noexcept {
  for (const Node* current = node; current != nullptr;
       current = current->parent) {
    if (current == ancestor) return true;
  }
  return false;
}

Node* ancestorWithTag(Node* node, std::string_view tag) noexcept {
  for (Node* current = node; current != nullptr; current = current->parent) {
    if (current->tag == tag) return current;
  }
  return nullptr;
}

const Node* ancestorWithTag(const Node* node, std::string_view tag) noexcept {
  for (const Node* current = node; current != nullptr;
       current = current->parent) {
    if (current->tag == tag) return current;
  }
  return nullptr;
}

void translateSubtree(Node& node,
                      float delta_x,
                      float delta_y,
                      Rect inherited_clip) {
  const auto translate_rect = [&](Rect& rect) {
    if (!nonEmpty(rect)) return;
    rect.x += delta_x;
    rect.y += delta_y;
  };
  node.layout.x += delta_x;
  node.layout.y += delta_y;
  node.clip = inherited_clip;
  translate_rect(node.scroll_viewport);
  translate_rect(node.horizontal_scroll_track);
  translate_rect(node.horizontal_scroll_thumb);
  translate_rect(node.vertical_scroll_track);
  translate_rect(node.vertical_scroll_thumb);
  translate_rect(node.scrollbar_corner);
  translate_rect(node.window_titlebar);
  translate_rect(node.window_close_button);
  translate_rect(node.window_collapse_button);

  Rect child_clip = inherited_clip;
  if (clipsOverflow(node)) {
    child_clip = clipForOverflow(
        node, child_clip,
        isScrollContainer(node) ? node.scroll_viewport : node.layout);
  }
  forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
    translateSubtree(child, delta_x, delta_y, child_clip);
  });
}

void invalidateRuntimeChildOrder(Node& node) noexcept {
  ++node.child_order_revision;
  if (node.child_order_revision == 0u) ++node.child_order_revision;
  node.retained_child_order.clear();
  node.retained_child_order_revision = 0u;
}

const std::vector<const Node*>& runtimeChildrenInPaintOrder(const Node& node) {
  if (node.retained_child_order_revision == node.child_order_revision) {
    return node.retained_child_order;
  }
  node.retained_child_order.clear();
  forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
    node.retained_child_order.push_back(&child);
  });
  std::stable_sort(node.retained_child_order.begin(),
                   node.retained_child_order.end(),
                   [](const Node* left, const Node* right) {
                     return zIndex(*left) < zIndex(*right);
                   });
  node.retained_child_order_revision = node.child_order_revision;
  return node.retained_child_order;
}

void invalidatePaint(Node* node) noexcept {
  for (Node* current = node; current != nullptr; current = current->parent) {
    ++current->paint_revision;
    if (current->paint_revision == 0u) ++current->paint_revision;
    current->retained_fragment.reset();
    current->retained_paint_revision = 0u;
  }
}

void invalidatePaintTree(Node& node) noexcept {
  ++node.paint_revision;
  if (node.paint_revision == 0u) ++node.paint_revision;
  node.retained_fragment.reset();
  node.retained_paint_revision = 0u;
  forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
    invalidatePaintTree(child);
  });
}

}  // namespace karma::ui::native::runtime_dom
