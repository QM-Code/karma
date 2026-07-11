#include "features/ui/native/transient_runtime.h"

#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace karma::ui::native::transient_runtime {
namespace {

using runtime_dom::attributeBoolean;
using runtime_dom::contains;
using runtime_dom::forRuntimeChildren;
using runtime_dom::invalidatePaint;
using runtime_dom::invalidatePaintTree;
using runtime_dom::isVisibleForInteraction;
using runtime_dom::isWithin;
using runtime_dom::Rect;
using runtime_dom::styleFloat;
using runtime_dom::translateSubtree;
using runtime_dom::visitRuntimeTree;

using string_utils::parseFiniteDouble;

bool sameRect(const Rect& left, const Rect& right) noexcept {
  return left.x == right.x && left.y == right.y &&
         left.width == right.width && left.height == right.height;
}

}  // namespace

bool isOverlayTransientRoot(const Node& node) noexcept {
  return node.tag == "popup" || node.tag == "menu" ||
         node.tag == "tooltip";
}

Node* resolveAnchor(DocumentInstance& document,
                    Node& transient,
                    const NodeLookup& lookup) {
  if (transient.tag == "select") return &transient;
  const auto anchor = transient.attributes.find("anchor");
  if (anchor == transient.attributes.end()) return transient.parent;
  return lookup.find_by_id == nullptr
             ? nullptr
             : lookup.find_by_id(lookup.context, document, anchor->second);
}

const std::vector<Node*>& overlayRootsInPaintOrder(
    DocumentInstance& document) {
  if (!document.overlay_order_revision) {
    return document.retained_overlay_order;
  }

  document.retained_overlay_order.clear();
  if (document.body && document.has_transients) {
    visitRuntimeTree(*document.body, [&](Node& node) {
      if (!isVisibleForInteraction(node)) return;
      bool inside_overlay = false;
      for (const Node* parent = node.parent; parent != nullptr;
           parent = parent->parent) {
        inside_overlay = inside_overlay || isOverlayTransientRoot(*parent);
      }
      if (inside_overlay) return;
      if ((isOverlayTransientRoot(node) && !node.collapsed_hidden) ||
          (node.tag == "select" && attributeBoolean(node, "open"))) {
        document.retained_overlay_order.push_back(&node);
      }
    });
    std::stable_sort(document.retained_overlay_order.begin(),
                     document.retained_overlay_order.end(),
                     [](const Node* left, const Node* right) {
                       return styleFloat(*left, "z-index", 0.0f) <
                              styleFloat(*right, "z-index", 0.0f);
                     });
  }
  document.overlay_order_revision.complete();
  return document.retained_overlay_order;
}

Node* topOpenTransient(DocumentInstance& document) {
  const std::vector<Node*>& overlays = overlayRootsInPaintOrder(document);
  const auto found = std::find_if(
      overlays.rbegin(), overlays.rend(), [](const Node* node) {
        return (node->tag == "select" || node->tag == "popup" ||
                node->tag == "menu") &&
               attributeBoolean(*node, "open") &&
               isVisibleForInteraction(*node);
      });
  return found == overlays.rend() ? nullptr : *found;
}

bool pointInsideTransient(DocumentInstance& document,
                          Node& transient,
                          double x,
                          double y,
                          const NodeLookup& lookup) {
  if (contains(transient.layout, x, y)) return true;
  if (transient.tag == "select") {
    bool inside_option = false;
    forRuntimeChildren(transient, [&](Node& child, const Value::Object*) {
      if (child.tag == "option" && !child.collapsed_hidden &&
          contains(child.layout, x, y)) {
        inside_option = true;
      }
    });
    if (inside_option) return true;
  }
  Node* anchor = resolveAnchor(document, transient, lookup);
  return anchor != nullptr && contains(anchor->layout, x, y);
}

std::size_t updateTimedTooltips(DocumentInstance& document,
                                Node* hovered,
                                double clock_seconds,
                                const NodeLookup& lookup) {
  if (!document.body) return 0u;
  std::size_t visibility_changes = 0u;
  visitRuntimeTree(*document.body, [&](Node& node) {
    if (node.tag != "tooltip") return;
    Node* anchor = resolveAnchor(document, node, lookup);
    const bool over_anchor = anchor != nullptr && isWithin(hovered, anchor);
    if (!over_anchor) {
      node.tooltip_hover_started = -1.0;
    } else if (node.tooltip_hover_started < 0.0) {
      node.tooltip_hover_started = clock_seconds;
    }
    double delay_ms = 400.0;
    if (const auto delay = node.attributes.find("delay_ms");
        delay != node.attributes.end()) {
      delay_ms = parseFiniteDouble(delay->second).value_or(delay_ms);
    }
    delay_ms = std::clamp(delay_ms, 0.0, 60000.0);
    const bool visible =
        over_anchor &&
        (clock_seconds - node.tooltip_hover_started) * 1000.0 >= delay_ms;
    if (node.collapsed_hidden == visible) {
      node.collapsed_hidden = !visible;
      document.layout_revision.invalidate();
      document.placement_revision.invalidate();
      document.accessibility_revision.invalidate();
      document.overlay_order_revision.invalidate();
      invalidatePaint(&node);
      ++visibility_changes;
    }
  });
  return visibility_changes;
}

std::size_t placeTransientWidgets(DocumentInstance& document,
                                  const NodeLookup& lookup) {
  if (!document.body) return 0u;
  const Rect viewport = document.body->clip;
  auto anchored_position = [&](const Node& anchor,
                               float width,
                               float height,
                               std::string_view placement) {
    const float below = anchor.layout.y + anchor.layout.height;
    const float above = anchor.layout.y - height;
    const float below_space = viewport.y + viewport.height - below;
    const float above_space = anchor.layout.y - viewport.y;
    const bool use_above =
        placement == "top" ||
        (placement != "bottom" && height > below_space &&
         above_space > below_space);
    const float x = std::clamp(
        anchor.layout.x, viewport.x,
        std::max(viewport.x, viewport.x + viewport.width - width));
    const float y = std::clamp(
        use_above ? above : below, viewport.y,
        std::max(viewport.y, viewport.y + viewport.height - height));
    return std::pair{x, y};
  };

  std::size_t placed_nodes = 0u;
  visitRuntimeTree(*document.body, [&](Node& node) {
    if (node.tag == "select" && attributeBoolean(node, "open")) {
      std::vector<Node*> options;
      float height = 0.0f;
      forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
        if (child.tag != "option" || child.collapsed_hidden) return;
        child.layout.height = std::max(24.0f, child.layout.height);
        height += child.layout.height;
        options.push_back(&child);
      });
      const auto [x, y] = anchored_position(
          node, node.layout.width, height,
          node.attributes.contains("placement")
              ? std::string_view(node.attributes.at("placement"))
              : std::string_view("auto"));
      float cursor = y;
      bool geometry_changed = false;
      for (Node* option : options) {
        const Rect previous_layout = option->layout;
        const Rect previous_clip = option->clip;
        option->layout.x = x;
        option->layout.y = cursor;
        option->layout.width = node.layout.width;
        option->clip = viewport;
        geometry_changed = geometry_changed ||
                           !sameRect(previous_layout, option->layout) ||
                           !sameRect(previous_clip, option->clip);
        cursor += option->layout.height;
      }
      if (geometry_changed) {
        invalidatePaintTree(node);
        invalidatePaint(node.parent);
      }
      placed_nodes += options.size();
      return;
    }
    if ((node.tag != "popup" && node.tag != "menu" &&
         node.tag != "tooltip") ||
        node.collapsed_hidden) {
      return;
    }
    Node* anchor = resolveAnchor(document, node, lookup);
    if (anchor == nullptr) return;
    const float width =
        node.tag == "tooltip"
            ? node.layout.width
            : std::max(node.layout.width, anchor->layout.width);
    const float height = node.layout.height;
    const auto [x, y] = anchored_position(
        *anchor, width, height,
        node.attributes.contains("placement")
            ? std::string_view(node.attributes.at("placement"))
            : std::string_view("auto"));
    const float delta_x = x - node.layout.x;
    const float delta_y = y - node.layout.y;
    const Rect previous_layout = node.layout;
    const Rect previous_clip = node.clip;
    node.layout.width = width;
    translateSubtree(node, delta_x, delta_y, viewport);
    if (!sameRect(previous_layout, node.layout) ||
        !sameRect(previous_clip, node.clip)) {
      invalidatePaintTree(node);
      invalidatePaint(node.parent);
    }
    ++placed_nodes;
  });
  return placed_nodes;
}

}  // namespace karma::ui::native::transient_runtime
