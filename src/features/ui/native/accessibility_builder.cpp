#include "features/ui/native/accessibility_builder.h"

#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace karma::ui::native {
namespace {

using runtime_dom::DocumentInstance;
using runtime_dom::attributeBoolean;
using runtime_dom::forRuntimeChildren;
using runtime_dom::isFocusableTag;
using runtime_dom::isScrollContainer;
using runtime_dom::Node;
using runtime_dom::styleString;

using string_utils::lower;
using string_utils::parseFiniteDouble;
using string_utils::trim;

AccessibilityRole roleForTag(std::string_view tag) {
  if (tag == "body") return AccessibilityRole::Document;
  if (tag == "text") return AccessibilityRole::Text;
  if (tag == "img" || tag == "svg") return AccessibilityRole::Image;
  if (tag == "button") return AccessibilityRole::Button;
  if (tag == "toggle") return AccessibilityRole::Toggle;
  if (tag == "slider") return AccessibilityRole::Slider;
  if (tag == "select") return AccessibilityRole::Select;
  if (tag == "option") return AccessibilityRole::Option;
  if (tag == "progress") return AccessibilityRole::Progress;
  if (tag == "scroll" || tag == "list") return AccessibilityRole::Scroll;
  if (tag == "window") return AccessibilityRole::Window;
  if (tag == "tabs") return AccessibilityRole::TabList;
  if (tag == "tab") return AccessibilityRole::Tab;
  if (tag == "disclosure") return AccessibilityRole::Disclosure;
  if (tag == "tree") return AccessibilityRole::Tree;
  if (tag == "tree-item") return AccessibilityRole::TreeItem;
  if (tag == "splitter" || tag == "separator") {
    return AccessibilityRole::Separator;
  }
  if (tag == "menu") return AccessibilityRole::Menu;
  if (tag == "menu-item") return AccessibilityRole::MenuItem;
  if (tag == "tooltip") return AccessibilityRole::Tooltip;
  return AccessibilityRole::Group;
}

std::optional<AccessibilityRole> authoredRole(std::string_view name) {
  const std::string role = lower(trim(name));
  if (role == "document") return AccessibilityRole::Document;
  if (role == "group") return AccessibilityRole::Group;
  if (role == "text") return AccessibilityRole::Text;
  if (role == "image") return AccessibilityRole::Image;
  if (role == "button") return AccessibilityRole::Button;
  if (role == "toggle") return AccessibilityRole::Toggle;
  if (role == "slider") return AccessibilityRole::Slider;
  if (role == "select") return AccessibilityRole::Select;
  if (role == "option") return AccessibilityRole::Option;
  if (role == "progress") return AccessibilityRole::Progress;
  if (role == "scroll") return AccessibilityRole::Scroll;
  if (role == "window") return AccessibilityRole::Window;
  if (role == "tab-list") return AccessibilityRole::TabList;
  if (role == "tab") return AccessibilityRole::Tab;
  if (role == "disclosure") return AccessibilityRole::Disclosure;
  if (role == "tree") return AccessibilityRole::Tree;
  if (role == "tree-item") return AccessibilityRole::TreeItem;
  if (role == "separator") return AccessibilityRole::Separator;
  if (role == "menu") return AccessibilityRole::Menu;
  if (role == "menu-item") return AccessibilityRole::MenuItem;
  if (role == "tooltip") return AccessibilityRole::Tooltip;
  return std::nullopt;
}

bool visibleInSemanticTree(const Node& node) {
  return node.present && !node.collapsed_hidden &&
         styleString(node, "display", "block") != "none";
}

void collectDescendantText(const Node& node, std::string& output) {
  forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
    if (!visibleInSemanticTree(child)) return;
    if (!child.text.empty()) {
      if (!output.empty()) output.push_back(' ');
      output += child.text;
    }
    collectDescendantText(child, output);
  });
}

AccessibilityNode deriveNode(const Node& node,
                             const CanvasLayout& canvas,
                             int& next_focus_order) {
  AccessibilityNode item;
  item.element = node.handle;
  item.role = node.parent == nullptr ? AccessibilityRole::Document
                                     : roleForTag(node.tag);
  if (const auto role = node.attributes.find("aria-role");
      role != node.attributes.end()) {
    item.role = authoredRole(role->second).value_or(item.role);
  }
  if (const auto label = node.attributes.find("aria-label");
      label != node.attributes.end()) {
    item.name = label->second;
  } else if (const auto alt = node.attributes.find("alt");
             alt != node.attributes.end()) {
    item.name = alt->second;
  } else {
    item.name = node.text;
  }
  if (item.name.empty()) collectDescendantText(node, item.name);
  if (const auto description = node.attributes.find("aria-description");
      description != node.attributes.end()) {
    item.description = description->second;
  }

  const CanvasRect window_bounds = canvas.layoutToWindow(
      {.x = node.layout.x,
       .y = node.layout.y,
       .width = node.layout.width,
       .height = node.layout.height});
  item.bounds = AccessibilityBounds{window_bounds.x, window_bounds.y,
                                    window_bounds.width, window_bounds.height};

  const auto tab_index = node.attributes.find("tab-index");
  item.focusable = !node.disabled &&
                   (isFocusableTag(node.tag) || tab_index != node.attributes.end());
  if (item.focusable) {
    item.focus_order = next_focus_order++;
    if (tab_index != node.attributes.end()) {
      const std::optional<double> explicit_order =
          parseFiniteDouble(tab_index->second);
      if (explicit_order.has_value() && *explicit_order < 0.0) {
        item.focus_order = -1;
      } else if (explicit_order.has_value() && *explicit_order >= 0.0 &&
                 *explicit_order <=
                     static_cast<double>(std::numeric_limits<int>::max())) {
        item.focus_order = static_cast<int>(*explicit_order);
      }
    }
  }

  item.focused = node.focused;
  item.disabled = node.disabled;
  item.checked = node.checked;
  item.expanded = attributeBoolean(node, "expanded");
  item.selected = attributeBoolean(node, "selected") || node.checked;
  item.open = node.attributes.contains("open") && attributeBoolean(node, "open");
  item.value = node.control_value.asNumber();
  if (const auto minimum = node.attributes.find("min");
      minimum != node.attributes.end()) {
    item.minimum = parseFiniteDouble(minimum->second);
  }
  if (const auto maximum = node.attributes.find("max");
      maximum != node.attributes.end()) {
    item.maximum = parseFiniteDouble(maximum->second);
  }

  if (item.focusable) item.actions.push_back(AccessibilityAction::Focus);
  if (node.tag == "button" || node.tag == "option" ||
      node.tag == "menu-item") {
    item.actions.push_back(AccessibilityAction::Press);
  } else if (node.tag == "toggle") {
    item.actions.push_back(AccessibilityAction::Toggle);
  } else if (node.tag == "slider") {
    item.actions.push_back(AccessibilityAction::Increment);
    item.actions.push_back(AccessibilityAction::Decrement);
    item.actions.push_back(AccessibilityAction::SetValue);
  } else if (isScrollContainer(node)) {
    item.actions.push_back(AccessibilityAction::Scroll);
    item.scroll_x = node.scroll_x;
    item.scroll_y = node.scroll_y;
    item.scroll_max_x = node.scroll_max_x;
    item.scroll_max_y = node.scroll_max_y;
  } else if (node.tag == "disclosure") {
    item.actions.push_back(item.expanded ? AccessibilityAction::Collapse
                                        : AccessibilityAction::Expand);
  } else if (node.tag == "window") {
    item.actions.push_back(AccessibilityAction::Dismiss);
  }
  if (node.tag == "select") {
    item.actions.push_back(item.open ? AccessibilityAction::Collapse
                                    : AccessibilityAction::Expand);
  }
  if (node.tag == "tree-item") {
    bool has_tree_children = false;
    forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
      has_tree_children = has_tree_children || child.tag == "tree-item";
    });
    if (has_tree_children) {
      item.actions.push_back(item.expanded ? AccessibilityAction::Collapse
                                          : AccessibilityAction::Expand);
    }
    item.actions.push_back(AccessibilityAction::Select);
  } else if (node.tag == "tab" || node.tag == "option" ||
             node.tag == "menu-item") {
    item.actions.push_back(AccessibilityAction::Select);
  }
  if ((node.tag == "popup" || node.tag == "menu") && item.open) {
    item.actions.push_back(AccessibilityAction::Dismiss);
  }
  return item;
}

}  // namespace

AccessibilityTreeBuilder::AccessibilityTreeBuilder(
    AccessibilityTree previous)
    : tree_(std::move(previous)) {
  tree_.nodes.clear();
  tree_.roots.clear();
}

std::size_t AccessibilityTreeBuilder::append(
    AccessibilityNode node,
    std::optional<std::size_t> parent) {
  const std::size_t index = tree_.nodes.size();
  assert(!parent.has_value() || *parent < index);
  node.children.clear();
  tree_.nodes.push_back(std::move(node));
  if (parent.has_value()) {
    tree_.nodes[*parent].children.push_back(index);
  } else {
    tree_.roots.push_back(index);
  }
  return index;
}

AccessibilityTree AccessibilityTreeBuilder::finish() && {
  ++tree_.generation;
  return std::move(tree_);
}

AccessibilityTree buildAccessibilityTree(
    AccessibilityTree previous,
    const std::vector<DocumentInstance*>& documents) {
  AccessibilityTreeBuilder builder(std::move(previous));
  int next_focus_order = 0;
  auto append = [&](auto&& self,
                    const Node& node,
                    const CanvasLayout& canvas,
                    std::optional<std::size_t> parent) -> std::size_t {
    const std::size_t index = builder.append(
        deriveNode(node, canvas, next_focus_order), parent);
    forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
      if (visibleInSemanticTree(child)) self(self, child, canvas, index);
    });
    return index;
  };

  for (const DocumentInstance* document : documents) {
    if (document == nullptr || document->body == nullptr) continue;
    append(append, *document->body, document->canvas_layout, std::nullopt);
  }
  return std::move(builder).finish();
}

}  // namespace karma::ui::native
