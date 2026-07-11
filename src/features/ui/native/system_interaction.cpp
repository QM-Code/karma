#include "features/ui/native/system_impl.h"

#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "features/ui/native/style_runtime.h"
#include "features/ui/native/transient_runtime.h"
#include "features/ui/native/widget_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace karma::ui {
namespace {

using native::runtime_dom::ancestorWithTag;
using native::runtime_dom::attributeBoolean;
using native::runtime_dom::clipForOverflow;
using native::runtime_dom::clipsOverflow;
using native::runtime_dom::DocumentInstance;
using native::runtime_dom::forRuntimeChildren;
using native::runtime_dom::invalidatePaint;
using native::runtime_dom::invalidatePaintTree;
using native::runtime_dom::isVisibleForInteraction;
using native::runtime_dom::isWithin;
using native::runtime_dom::Node;
using native::runtime_dom::Rect;
using native::runtime_dom::styleString;
using native::runtime_dom::translateSubtree;
using native::runtime_dom::visitRuntimeTree;
using native::string_utils::parseFiniteDouble;
using native::widget_runtime::isVerticalSlider;
using native::widget_runtime::updateScrollbarGeometry;

}  // namespace

Node* System::Impl::nodeById(DocumentInstance& doc, std::string_view id) {
  const auto found = doc.ids.find(std::string(id));
  return found == doc.ids.end() ? nullptr
                                : document_runtime.element(found->second);
}

native::transient_runtime::NodeLookup System::Impl::transientNodeLookup() {
  return {
      .context = this,
      .find_by_id = [](void* context,
                       DocumentInstance& document,
                       std::string_view id) {
        return static_cast<Impl*>(context)->nodeById(document, id);
      },
  };
}

Value System::Impl::choiceValue(const Node& node) {
  if (const auto value = node.attributes.find("value");
      value != node.attributes.end()) {
    return Value(value->second);
  }
  if (!node.id.empty()) return Value(node.id);
  return Value(node.text);
}

void System::Impl::setOpenState(DocumentInstance& doc, Node& node, bool open) {
  node.attributes["open"] = open ? "true" : "false";
  if (const auto binding = node.attributes.find("bind-open");
      binding != node.attributes.end()) {
    setModelFromWidget(doc, binding->second, Value(open));
  }
  if (node.tag == "select") {
    forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
      if (child.tag == "option") child.collapsed_hidden = !open;
    });
  } else if (node.tag == "popup" || node.tag == "menu") {
    node.collapsed_hidden = !open;
  }
  doc.overlay_order_revision = true;
  if (!open) {
    Node* focused = document_runtime.element(doc.focused);
    if (focused != nullptr && isWithin(focused, &node)) {
      Node* replacement =
          node.tag == "select" ? &node : transientAnchor(doc, node);
      setFocus(doc, replacement != nullptr &&
                            isVisibleForInteraction(*replacement)
                        ? replacement
                        : nullptr);
    }
  }
  markDirty(doc, doc.binding_revision);
}

Node* System::Impl::transientAnchor(DocumentInstance& doc, Node& transient) {
  return native::transient_runtime::resolveAnchor(
      doc, transient, transientNodeLookup());
}

Node* System::Impl::topOpenTransient(DocumentInstance& doc) {
  return native::transient_runtime::topOpenTransient(doc);
}

bool System::Impl::pointInsideTransient(DocumentInstance& doc,
                          Node& transient,
                          double x,
                          double y) {
  return native::transient_runtime::pointInsideTransient(
      doc, transient, x, y, transientNodeLookup());
}

bool System::Impl::dismissTransient(DocumentInstance& doc,
                      Node& transient,
                      bool emit_cancel) {
  ++dispatch_depth;
  Event cancel{.type = EventType::Cancel,
               .document = doc.handle,
               .target = transient.handle};
  if (emit_cancel) {
    dispatchEvent(doc, transient, cancel);
    if (cancel.defaultPrevented()) {
      --dispatch_depth;
      if (dispatch_depth == 0) flushDeferredCloses();
      return false;
    }
    if (const auto action = transient.attributes.find("on-cancel");
        action != transient.attributes.end()) {
      fireAction(doc, transient, action->second);
    }
  }
  Node* anchor = transientAnchor(doc, transient);
  setOpenState(doc, transient, false);
  if (anchor != nullptr && anchor != &transient && anchor->handle.valid()) {
    setFocus(doc, anchor);
  } else if (transient.tag == "select" && transient.handle.valid()) {
    setFocus(doc, &transient);
  }
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
  return true;
}

bool System::Impl::dismissTransientOutside(DocumentInstance& doc, double x, double y) {
  Node* transient = topOpenTransient(doc);
  return transient != nullptr && !pointInsideTransient(doc, *transient, x, y) &&
         dismissTransient(doc, *transient, true);
}

bool System::Impl::cancelTopTransient(DocumentInstance& doc) {
  Node* transient = topOpenTransient(doc);
  return transient != nullptr && dismissTransient(doc, *transient, true);
}

void System::Impl::closeOtherTransients(DocumentInstance& doc, const Node* except) {
  if (!doc.body) return;
  std::vector<Node*> open;
  visitRuntimeTree(*doc.body, [&](Node& node) {
    if (&node != except &&
        (node.tag == "select" || node.tag == "popup" || node.tag == "menu") &&
        attributeBoolean(node, "open")) {
      open.push_back(&node);
    }
  });
  for (Node* node : open) setOpenState(doc, *node, false);
}

bool System::Impl::toggleAnchoredTransient(DocumentInstance& doc, Node& anchor) {
  if (anchor.id.empty() || !doc.body) return false;
  Node* matched = nullptr;
  visitRuntimeTree(*doc.body, [&](Node& node) {
    if (matched != nullptr || (node.tag != "popup" && node.tag != "menu")) {
      return;
    }
    const auto configured = node.attributes.find("anchor");
    if (configured != node.attributes.end() && configured->second == anchor.id) {
      matched = &node;
    }
  });
  if (matched == nullptr) return false;
  const bool open = !attributeBoolean(*matched, "open");
  closeOtherTransients(doc, open ? matched : nullptr);
  setOpenState(doc, *matched, open);
  if (open && matched->tag == "menu") {
    Node* first = nullptr;
    forRuntimeChildren(*matched, [&](Node& child, const Value::Object*) {
      if (first == nullptr && child.tag == "menu-item" && !child.disabled) {
        first = &child;
      }
    });
    if (first != nullptr) setFocus(doc, first);
  }
  return true;
}

bool System::Impl::selectOwnedItem(DocumentInstance& doc,
                     Node& item,
                     std::string_view owner_tag) {
  Node* owner = ancestorWithTag(item.parent, owner_tag);
  if (owner == nullptr) return false;
  const Value value = choiceValue(item);
  const bool changed = owner->control_value != value;
  owner->control_value = value;
  if (const auto binding = owner->attributes.find("bind-value");
      binding != owner->attributes.end()) {
    setModelFromWidget(doc, binding->second, value);
  }
  std::function<void(Node&)> update = [&](Node& current) {
    forRuntimeChildren(current, [&](Node& child, const Value::Object*) {
      if ((owner_tag == "tabs" && child.tag == "tab") ||
          (owner_tag == "tree" && child.tag == "tree-item")) {
        child.attributes["selected"] = &child == &item ? "true" : "false";
      }
      if (owner_tag == "tree") update(child);
    });
  };
  update(*owner);
  if (changed) {
    Event change{.type = EventType::Change,
                 .document = doc.handle,
                 .target = owner->handle,
                 .value = value};
    dispatchEvent(doc, *owner, change);
    if (const auto action = owner->attributes.find("on-change");
        action != owner->attributes.end()) {
      fireAction(doc, *owner, action->second, value);
    }
  }
  if (const auto action = item.attributes.find("on-select");
      action != item.attributes.end()) {
    fireAction(doc, item, action->second, value);
  }
  markDirty(doc, doc.binding_revision);
  return true;
}

bool System::Impl::moveTabSelection(DocumentInstance& doc,
                      Node& current,
                      platform::Key key) {
  Node* tabs = ancestorWithTag(current.parent, "tabs");
  if (tabs == nullptr) return false;
  std::vector<Node*> choices;
  forRuntimeChildren(*tabs, [&](Node& child, const Value::Object*) {
    if (child.tag == "tab" && !child.disabled) choices.push_back(&child);
  });
  if (choices.empty()) return false;
  const auto found = std::find(choices.begin(), choices.end(), &current);
  std::ptrdiff_t index = found == choices.end() ? 0 : found - choices.begin();
  if (key == platform::Key::Home) index = 0;
  else if (key == platform::Key::End) index = choices.size() - 1u;
  else if (key == platform::Key::Left) {
    index = index == 0 ? static_cast<std::ptrdiff_t>(choices.size()) - 1
                       : index - 1;
  } else if (key == platform::Key::Right) {
    index = (index + 1) % static_cast<std::ptrdiff_t>(choices.size());
  } else {
    return false;
  }
  ++dispatch_depth;
  Node* selected = choices[static_cast<std::size_t>(index)];
  setFocus(doc, selected);
  const bool handled = selectOwnedItem(doc, *selected, "tabs");
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
  return handled;
}

bool System::Impl::moveMenuFocus(DocumentInstance& doc,
                   Node& current,
                   platform::Key key) {
  Node* menu = ancestorWithTag(current.parent, "menu");
  if (menu == nullptr) return false;
  std::vector<Node*> items;
  forRuntimeChildren(*menu, [&](Node& child, const Value::Object*) {
    if (child.tag == "menu-item" && !child.disabled) items.push_back(&child);
  });
  if (items.empty()) return false;
  const auto found = std::find(items.begin(), items.end(), &current);
  std::ptrdiff_t index = found == items.end() ? 0 : found - items.begin();
  if (key == platform::Key::Home) index = 0;
  else if (key == platform::Key::End) index = items.size() - 1u;
  else if (key == platform::Key::Up) {
    index = index == 0 ? static_cast<std::ptrdiff_t>(items.size()) - 1
                       : index - 1;
  } else if (key == platform::Key::Down) {
    index = (index + 1) % static_cast<std::ptrdiff_t>(items.size());
  } else {
    return false;
  }
  setFocus(doc, items[static_cast<std::size_t>(index)]);
  return true;
}

bool System::Impl::openSelectListbox(DocumentInstance& doc, Node& select) {
  if (select.tag != "select" || select.disabled) return false;
  closeOtherTransients(doc, &select);
  setOpenState(doc, select, true);
  Node* selected = nullptr;
  Node* first = nullptr;
  forRuntimeChildren(select, [&](Node& child, const Value::Object*) {
    if (child.tag != "option" || child.disabled) return;
    if (first == nullptr) first = &child;
    if (child.checked) selected = &child;
  });
  if (selected == nullptr) selected = first;
  if (selected != nullptr) setFocus(doc, selected);
  return true;
}

bool System::Impl::moveOptionFocus(DocumentInstance& doc,
                     Node& current,
                     platform::Key key) {
  Node* select = ancestorWithTag(current.parent, "select");
  if (select == nullptr || !attributeBoolean(*select, "open")) return false;
  std::vector<Node*> options;
  forRuntimeChildren(*select, [&](Node& child, const Value::Object*) {
    if (child.tag == "option" && !child.disabled) options.push_back(&child);
  });
  if (options.empty()) return false;
  const auto found = std::find(options.begin(), options.end(), &current);
  std::ptrdiff_t index = found == options.end() ? 0 : found - options.begin();
  if (key == platform::Key::Home) index = 0;
  else if (key == platform::Key::End) index = options.size() - 1u;
  else if (key == platform::Key::Up) {
    index = index == 0 ? static_cast<std::ptrdiff_t>(options.size()) - 1
                       : index - 1;
  } else if (key == platform::Key::Down) {
    index = (index + 1) % static_cast<std::ptrdiff_t>(options.size());
  } else {
    return false;
  }
  setFocus(doc, options[static_cast<std::size_t>(index)]);
  return true;
}

bool System::Impl::handleTreeNavigation(DocumentInstance& doc,
                          Node& item,
                          platform::Key key) {
  if (item.tag != "tree-item") return false;
  std::vector<Node*> children;
  forRuntimeChildren(item, [&](Node& child, const Value::Object*) {
    if (child.tag == "tree-item" && !child.disabled) children.push_back(&child);
  });
  const bool expanded = attributeBoolean(item, "expanded");
  bool next_expanded = expanded;
  Node* next_focus = nullptr;
  if (key == platform::Key::Right) {
    if (!children.empty() && !expanded) next_expanded = true;
    else if (!children.empty()) next_focus = children.front();
    else return false;
  } else if (key == platform::Key::Left) {
    if (expanded) next_expanded = false;
    else next_focus = ancestorWithTag(item.parent, "tree-item");
    if (!expanded && next_focus == nullptr) return false;
  } else {
    return false;
  }
  if (next_focus != nullptr) {
    setFocus(doc, next_focus);
    return true;
  }
  ++dispatch_depth;
  item.attributes["expanded"] = next_expanded ? "true" : "false";
  if (const auto binding = item.attributes.find("bind-expanded");
      binding != item.attributes.end()) {
    setModelFromWidget(doc, binding->second, Value(next_expanded));
  }
  for (auto& child : item.children) {
    if (child->tag == "tree-item") {
      child->collapsed_hidden = !next_expanded;
    }
  }
  if (const auto action = item.attributes.find("on-toggle");
      action != item.attributes.end()) {
    fireAction(doc, item, action->second, Value(next_expanded));
  }
  markDirty(doc, doc.binding_revision);
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
  return true;
}

void System::Impl::updateTimedTooltips(DocumentInstance& doc) {
  (void)native::transient_runtime::updateTimedTooltips(
      doc, document_runtime.element(doc.hovered), clock_seconds,
      transientNodeLookup());
}

void System::Impl::placeTransientWidgets(DocumentInstance& doc) {
  pending_frame_diagnostics.placed_nodes +=
      native::transient_runtime::placeTransientWidgets(
          doc, transientNodeLookup());
}
void System::Impl::applyScrollPlacement(DocumentInstance& doc,
                                        Node& scroller,
                                        float previous_x,
                                        float previous_y) {
  const float delta_x = previous_x - scroller.scroll_x;
  const float delta_y = previous_y - scroller.scroll_y;
  if (delta_x == 0.0f && delta_y == 0.0f) return;
  Rect child_clip = scroller.clip;
  if (clipsOverflow(scroller)) {
    child_clip = clipForOverflow(scroller, child_clip,
                                 scroller.scroll_viewport);
  }
  forRuntimeChildren(scroller, [&](Node& child, const Value::Object*) {
    translateSubtree(child, delta_x, delta_y, child_clip);
  });
  updateScrollbarGeometry(scroller, scroller.scroll_content_width,
                          scroller.scroll_content_height);
  doc.virtual_range_revision = true;
  doc.placement_revision = true;
  doc.accessibility_revision = true;
  // Retained fragments currently store framebuffer-space geometry. Placement
  // updates therefore invalidate the translated subtree as well as its
  // ancestors; otherwise the scrollbar thumb moves while cached descendants
  // remain at their pre-scroll coordinates.
  invalidatePaintTree(scroller);
  invalidatePaint(scroller.parent);
}
void System::Impl::setFocus(DocumentInstance& doc, Node* node) {
  if (node != nullptr &&
      (node->disabled || !node->handle.valid() ||
       !isVisibleForInteraction(*node))) {
    return;
  }
  Node* previous = document_runtime.element(doc.focused);
  if (previous == node) return;
  ++dispatch_depth;
  doc.focused = {};
  if (previous != nullptr) {
    previous->focused = false;
    Event blur{.type = EventType::Blur, .document = doc.handle, .target = previous->handle};
    dispatchEvent(doc, *previous, blur);
  }
  if (doc.focused.valid()) {
    recordStyleResult(native::style_runtime::restyleNode(
        doc, previous, styleInputs(doc)));
    if (previous != nullptr) invalidatePaint(previous);
    --dispatch_depth;
    if (dispatch_depth == 0) flushDeferredCloses();
    return;
  }
  doc.focused = node == nullptr ? ElementHandle{} : node->handle;
  if (node != nullptr) {
    node->focused = true;
    Event focus_event{.type = EventType::Focus,
                      .document = doc.handle,
                      .target = node->handle};
    dispatchEvent(doc, *node, focus_event);
  }
  recordStyleResult(native::style_runtime::restyleNode(
      doc, previous, styleInputs(doc)));
  recordStyleResult(native::style_runtime::restyleNode(
      doc, node, styleInputs(doc)));
  if (previous != nullptr) invalidatePaint(previous);
  if (node != nullptr) invalidatePaint(node);
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
}

void System::Impl::dispatchEvent(DocumentInstance& doc, Node& target, Event& event) {
  ++dispatch_depth;
  const EventType dispatched_type = event.type;
  std::vector<Node*> path;
  for (Node* current = &target; current != nullptr; current = current->parent) {
    path.push_back(current);
  }
  auto invoke = [&](Node* current, EventPhase phase, bool capture_listeners) {
    if (!current->handle.valid()) return;
    event.phase = phase;
    event.current_target = current->handle;
    document_runtime.dispatchElement(current->handle, dispatched_type,
                                     capture_listeners, event);
  };
  for (std::size_t index = path.size(); index > 1u; --index) {
    invoke(path[index - 1u], EventPhase::Capture, true);
    if (event.propagationStopped()) break;
  }
  if (!event.propagationStopped() && !path.empty()) {
    invoke(path.front(), EventPhase::Target, true);
    invoke(path.front(), EventPhase::Target, false);
  }
  if (!event.propagationStopped()) {
    for (std::size_t index = 1; index < path.size(); ++index) {
      invoke(path[index], EventPhase::Bubble, false);
      if (event.propagationStopped()) break;
    }
  }
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
}

void System::Impl::fireAction(DocumentInstance& doc,
                              Node& target,
                              std::string_view action,
                              Value value) {
  if (action.empty()) return;
  ++dispatch_depth;
  const ActionEvent event{.document = doc.handle,
                          .target = target.handle,
                          .action = std::string(action),
                          .value = std::move(value)};
  document_runtime.dispatchAction(event);
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
}

void System::Impl::activateDefault(DocumentInstance& doc,
                                   Node& target,
                                   double pointer_coordinate,
                                   bool emit_click_action) {
  if (target.disabled) return;
  // Keep the document and every generational element reference alive for the
  // complete default action. Action callbacks may request structural mutation
  // or document closure; those requests are applied after the widget finishes
  // updating all of its related state.
  ++dispatch_depth;
  Node* control = &target;
  if (target.tag == "option") {
    for (Node* parent = target.parent; parent != nullptr; parent = parent->parent) {
      if (parent->tag == "select") {
        control = parent;
        break;
      }
    }
  }
  Value new_value = control->control_value;
  bool changed = false;
  bool selection_handled = false;
  if (control->tag == "toggle") {
    control->checked = !control->checked;
    new_value = control->checked;
    changed = true;
  } else if (target.tag == "tab") {
    new_value = choiceValue(target);
    selection_handled = selectOwnedItem(doc, target, "tabs");
  } else if (target.tag == "tree-item") {
    new_value = choiceValue(target);
    selection_handled = selectOwnedItem(doc, target, "tree");
  } else if (control->tag == "disclosure") {
    const bool expanded = !attributeBoolean(*control, "expanded");
    control->attributes["expanded"] = expanded ? "true" : "false";
    for (std::size_t index = 0; index < control->children.size(); ++index) {
      control->children[index]->collapsed_hidden = !expanded && index > 0u;
    }
    new_value = expanded;
    changed = true;
    if (const auto binding = control->attributes.find("bind-expanded");
        binding != control->attributes.end()) {
      setModelFromWidget(doc, binding->second, new_value);
    }
    if (const auto action = control->attributes.find("on-toggle");
        action != control->attributes.end()) {
      fireAction(doc, *control, action->second, new_value);
    }
  } else if (control->tag == "slider") {
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.01;
    if (const auto found = control->attributes.find("min"); found != control->attributes.end()) {
      minimum = parseFiniteDouble(found->second).value_or(minimum);
    }
    if (const auto found = control->attributes.find("max"); found != control->attributes.end()) {
      maximum = parseFiniteDouble(found->second).value_or(maximum);
    }
    if (const auto found = control->attributes.find("step"); found != control->attributes.end()) {
      step = parseFiniteDouble(found->second).value_or(step);
    }
    const bool vertical = isVerticalSlider(*control);
    const double extent = vertical ? control->layout.height
                                   : control->layout.width;
    double ratio = extent > 0.0
                       ? std::clamp(
                             vertical
                                 ? (control->layout.y + control->layout.height -
                                    pointer_coordinate) /
                                       extent
                                 : (pointer_coordinate - control->layout.x) /
                                       extent,
                             0.0, 1.0)
                       : 0.0;
    if (!vertical && styleString(*control, "direction", "ltr") == "rtl") {
      ratio = 1.0 - ratio;
    }
    double numeric = minimum + (maximum - minimum) * ratio;
    if (step > 0.0) numeric = minimum + std::round((numeric - minimum) / step) * step;
    numeric = std::clamp(numeric, std::min(minimum, maximum),
                         std::max(minimum, maximum));
    new_value = numeric;
    changed = true;
  } else if (control->tag == "select") {
    std::vector<Node*> options;
    forRuntimeChildren(*control, [&](Node& option, const Value::Object*) {
      if (option.present && option.tag == "option" && !option.disabled) {
        options.push_back(&option);
      }
    });
    if (target.tag == "option") {
      new_value = choiceValue(target);
      changed = new_value != control->control_value;
      for (Node* option : options) option->checked = option == &target;
      setOpenState(doc, *control, false);
      setFocus(doc, control);
    } else if (attributeBoolean(*control, "open")) {
      setOpenState(doc, *control, false);
      setFocus(doc, control);
    } else {
      openSelectListbox(doc, *control);
    }
  }
  if (changed) {
    control->control_value = new_value;
    if (const auto binding = control->attributes.find("bind-value");
        binding != control->attributes.end()) {
      setModelFromWidget(doc, binding->second, new_value);
    }
    Event change{.type = EventType::Change,
                 .document = doc.handle,
                 .target = control->handle,
                 .value = new_value};
    dispatchEvent(doc, *control, change);
    if (const auto action = control->attributes.find("on-change");
        action != control->attributes.end()) {
      fireAction(doc, *control, action->second, new_value);
    }
  }

  if (target.tag == "menu-item") {
    if (Node* menu = ancestorWithTag(target.parent, "menu"); menu != nullptr) {
      Node* anchor = transientAnchor(doc, *menu);
      setOpenState(doc, *menu, false);
      if (anchor != nullptr && anchor->handle.valid()) setFocus(doc, anchor);
    }
    new_value = choiceValue(target);
  } else if (target.tag == "button") {
    toggleAnchoredTransient(doc, target);
  }

  if (emit_click_action) {
    const auto action = target.attributes.find("on-click");
    if (action != target.attributes.end()) {
      fireAction(doc, target, action->second, new_value);
    }
  }
  const bool paint_only_control = changed && control->tag == "slider";
  if (paint_only_control) {
    invalidatePaint(control);
    doc.accessibility_revision = true;
  } else {
    const bool is_bound = control->attributes.contains("bind-value") ||
                          control->attributes.contains("bind-expanded") ||
                          control->attributes.contains("bind-open");
    markDirty(doc,
              (changed && is_bound) || selection_handled || doc.binding_revision);
  }
  --dispatch_depth;
  if (dispatch_depth == 0) flushDeferredCloses();
}

bool System::Impl::closeNow(DocumentHandle handle) {
  if (!document_runtime.destroy(handle)) return false;
  accessibility_dirty = true;
  return true;
}

void System::Impl::flushDeferredCloses() {
  std::vector<DocumentHandle> closes = std::move(deferred_closes);
  deferred_closes.clear();
  for (DocumentHandle handle : closes) closeNow(handle);
  if (reconciling_models) return;
  const auto all_documents = document_runtime.allDocuments();
  for (DocumentInstance* document : *all_documents) {
    if (!document->binding_revision || !document->body) {
      continue;
    }
    DocumentInstance& doc = *document;
    if (!doc.pending_model_paths.empty()) {
      std::vector<std::string> pending = std::move(doc.pending_model_paths);
      doc.pending_model_paths.clear();
      const auto reconcile_start = std::chrono::steady_clock::now();
      reconcileModelPaths(doc, pending);
      pending_frame_diagnostics.reconcile_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - reconcile_start)
              .count();
      continue;
    }
    reconciling_models = true;
    refreshBindingsFully(doc);
    recordStyleResult(
        native::style_runtime::styleDocument(doc, styleInputs(doc)));
    doc.layout_revision = true;
    doc.accessibility_revision = true;
    doc.placement_revision = true;
    doc.virtual_range_revision = true;
    invalidatePaintTree(*doc.body);
    reconciling_models = false;
  }
}

}  // namespace karma::ui

