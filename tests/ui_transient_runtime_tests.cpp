#include "features/ui/native/transient_runtime.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

using karma::ui::native::runtime_dom::DocumentInstance;
using karma::ui::native::runtime_dom::Node;
using karma::ui::native::runtime_dom::Rect;
using karma::ui::native::transient_runtime::NodeLookup;
using karma::ui::native::transient_runtime::overlayRootsInPaintOrder;
using karma::ui::native::transient_runtime::placeTransientWidgets;
using karma::ui::native::transient_runtime::pointInsideTransient;
using karma::ui::native::transient_runtime::resolveAnchor;
using karma::ui::native::transient_runtime::topOpenTransient;
using karma::ui::native::transient_runtime::updateTimedTooltips;

struct LookupState {
  std::unordered_map<std::string, Node*> nodes;
};

Node* findNode(void* context,
               DocumentInstance&,
               std::string_view id) {
  auto& state = *static_cast<LookupState*>(context);
  const auto found = state.nodes.find(std::string(id));
  return found == state.nodes.end() ? nullptr : found->second;
}

NodeLookup lookupFor(LookupState& state) {
  return NodeLookup{.context = &state, .find_by_id = findNode};
}

Node& append(Node& parent, std::string tag, std::string id = {}) {
  auto child = std::make_unique<Node>();
  child->tag = std::move(tag);
  child->id = std::move(id);
  child->parent = &parent;
  Node& result = *child;
  parent.children.push_back(std::move(child));
  return result;
}

bool near(float left, float right) {
  return std::abs(left - right) < 0.001f;
}

DocumentInstance makeDocument() {
  DocumentInstance document;
  document.body = std::make_unique<Node>();
  document.body->tag = "body";
  document.body->layout = {.width = 300.0f, .height = 200.0f};
  document.body->clip = document.body->layout;
  document.has_transients = true;
  return document;
}

void testOverlayOrderAndCache() {
  DocumentInstance document = makeDocument();
  Node& popup = append(*document.body, "popup");
  popup.attributes["open"] = "true";
  popup.style["z-index"] = "2";
  Node& nested = append(popup, "menu");
  nested.attributes["open"] = "true";
  nested.style["z-index"] = "100";
  Node& select = append(*document.body, "select");
  select.attributes["open"] = "true";
  select.style["z-index"] = "1";
  Node& tooltip = append(*document.body, "tooltip");
  tooltip.style["z-index"] = "4";
  Node& hidden = append(*document.body, "popup");
  hidden.attributes["open"] = "true";
  hidden.collapsed_hidden = true;

  const auto& first = overlayRootsInPaintOrder(document);
  assert((first == std::vector<Node*>{&select, &popup, &tooltip}));
  assert(topOpenTransient(document) == &popup);

  Node& later = append(*document.body, "menu");
  later.attributes["open"] = "true";
  later.style["z-index"] = "3";
  assert(overlayRootsInPaintOrder(document).size() == 3u);
  document.overlay_order_revision.invalidate();
  const auto& rebuilt = overlayRootsInPaintOrder(document);
  assert((rebuilt == std::vector<Node*>{&select, &popup, &later, &tooltip}));
  assert(topOpenTransient(document) == &later);
}

void testAnchorResolutionAndContainment() {
  DocumentInstance document = makeDocument();
  LookupState state;
  Node& anchor = append(*document.body, "button", "anchor");
  anchor.layout = {.x = 10.0f, .y = 20.0f, .width = 30.0f, .height = 20.0f};
  state.nodes.emplace(anchor.id, &anchor);
  Node& popup = append(*document.body, "popup");
  popup.attributes["anchor"] = "anchor";
  popup.layout = {.x = 40.0f, .y = 40.0f, .width = 60.0f, .height = 50.0f};
  const NodeLookup lookup = lookupFor(state);

  assert(resolveAnchor(document, popup, lookup) == &anchor);
  assert(pointInsideTransient(document, popup, 50.0, 50.0, lookup));
  assert(pointInsideTransient(document, popup, 15.0, 25.0, lookup));
  assert(!pointInsideTransient(document, popup, 150.0, 150.0, lookup));

  Node& unanchored = append(anchor, "tooltip");
  assert(resolveAnchor(document, unanchored, lookup) == &anchor);
  Node& select = append(*document.body, "select");
  assert(resolveAnchor(document, select, lookup) == &select);
  select.layout = {.x = 100.0f, .y = 20.0f, .width = 60.0f, .height = 20.0f};
  Node& option = append(select, "option");
  option.layout = {.x = 100.0f, .y = 50.0f, .width = 60.0f, .height = 24.0f};
  assert(pointInsideTransient(document, select, 110.0, 60.0, lookup));
}

void testTooltipTiming() {
  DocumentInstance document = makeDocument();
  LookupState state;
  Node& anchor = append(*document.body, "button", "help");
  state.nodes.emplace(anchor.id, &anchor);
  Node& child = append(anchor, "span");
  Node& tooltip = append(*document.body, "tooltip");
  tooltip.attributes["anchor"] = "help";
  tooltip.attributes["delay_ms"] = "400";
  tooltip.collapsed_hidden = true;
  tooltip.retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>();
  document.body->retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>();
  document.binding_revision.complete();
  document.style_revision.complete();
  document.layout_revision.complete();
  document.placement_revision.complete();
  document.accessibility_revision.complete();
  document.overlay_order_revision.complete();
  const NodeLookup lookup = lookupFor(state);

  assert(updateTimedTooltips(document, &child, 1.0, lookup) == 0u);
  assert(tooltip.collapsed_hidden);
  assert(!document.binding_revision.pending());
  assert(!document.style_revision.pending());
  assert(!document.layout_revision.pending());
  assert(near(static_cast<float>(tooltip.tooltip_hover_started), 1.0f));
  assert(updateTimedTooltips(document, &child, 1.399, lookup) == 0u);
  assert(updateTimedTooltips(document, &child, 1.401, lookup) == 1u);
  assert(!tooltip.collapsed_hidden);
  assert(!document.binding_revision.pending());
  assert(!document.style_revision.pending());
  assert(document.layout_revision.pending());
  assert(document.placement_revision.pending());
  assert(document.accessibility_revision.pending());
  assert(document.overlay_order_revision.pending());
  assert(tooltip.retained_fragment == nullptr);
  assert(document.body->retained_fragment == nullptr);
  assert(updateTimedTooltips(document, nullptr, 1.5, lookup) == 1u);
  assert(tooltip.collapsed_hidden);
  assert(tooltip.tooltip_hover_started < 0.0);
}

void testPlacement() {
  DocumentInstance document = makeDocument();
  LookupState state;
  Node& anchor = append(*document.body, "button", "panel-anchor");
  anchor.layout = {.x = 250.0f,
                   .y = 160.0f,
                   .width = 60.0f,
                   .height = 20.0f};
  state.nodes.emplace(anchor.id, &anchor);
  Node& popup = append(*document.body, "popup");
  popup.attributes["anchor"] = "panel-anchor";
  popup.layout = {.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 80.0f};
  Node& popup_child = append(popup, "span");
  popup_child.layout = {.x = 15.0f,
                        .y = 20.0f,
                        .width = 10.0f,
                        .height = 10.0f};
  Node& scroller = append(popup, "scroll");
  scroller.layout = {.x = 20.0f,
                     .y = 30.0f,
                     .width = 20.0f,
                     .height = 20.0f};
  scroller.scroll_viewport = scroller.layout;
  scroller.vertical_scroll_track = {
      .x = 35.0f, .y = 30.0f, .width = 5.0f, .height = 20.0f};
  scroller.vertical_scroll_thumb = {
      .x = 35.0f, .y = 32.0f, .width = 5.0f, .height = 8.0f};
  Node& window = append(popup, "window");
  window.layout = {.x = 20.0f,
                   .y = 55.0f,
                   .width = 20.0f,
                   .height = 20.0f};
  window.window_titlebar = {
      .x = 20.0f, .y = 55.0f, .width = 20.0f, .height = 6.0f};
  popup.retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>();
  document.body->retained_fragment =
      std::make_unique<karma::rendering::UIDrawData>();

  Node& select = append(*document.body, "select");
  select.attributes["open"] = "true";
  select.layout = {.x = 10.0f,
                   .y = 160.0f,
                   .width = 100.0f,
                   .height = 20.0f};
  Node& first = append(select, "option");
  first.layout.height = 10.0f;
  Node& second = append(select, "option");
  second.layout.height = 30.0f;

  const std::size_t placed =
      placeTransientWidgets(document, lookupFor(state));
  assert(placed == 3u);
  assert(near(popup.layout.x, 240.0f));
  assert(near(popup.layout.y, 80.0f));
  assert(near(popup.layout.width, 60.0f));
  assert(near(popup_child.layout.x, 245.0f));
  assert(near(popup_child.layout.y, 90.0f));
  assert(near(popup_child.clip.x, 0.0f));
  assert(near(popup_child.clip.width, 300.0f));
  assert(near(scroller.scroll_viewport.x, 250.0f));
  assert(near(scroller.scroll_viewport.y, 100.0f));
  assert(near(scroller.vertical_scroll_thumb.x, 265.0f));
  assert(near(scroller.vertical_scroll_thumb.y, 102.0f));
  assert(near(window.window_titlebar.x, 250.0f));
  assert(near(window.window_titlebar.y, 125.0f));
  assert(popup.retained_fragment == nullptr);
  assert(document.body->retained_fragment == nullptr);
  assert(near(first.layout.x, 10.0f));
  assert(near(first.layout.y, 106.0f));
  assert(near(first.layout.height, 24.0f));
  assert(near(second.layout.y, 130.0f));
  assert(near(second.layout.height, 30.0f));
}

}  // namespace

int main() {
  testOverlayOrderAndCache();
  testAnchorResolutionAndContainment();
  testTooltipTiming();
  testPlacement();
  std::cout << "ui transient runtime tests passed\n";
  return 0;
}
