#include "features/ui/native/system_impl.h"

#include "features/ui/native/authoring.h"
#include "features/ui/native/focus_runtime.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "features/ui/native/style_runtime.h"
#include "features/ui/native/transient_runtime.h"
#include "features/ui/native/widget_runtime.h"
#include "karma/platform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace karma::ui {
namespace {

using native::focus_runtime::collectFocusable;
using native::focus_runtime::sortFocusableForTab;
using native::focus_runtime::spatialCandidate;
using native::jsonNumber;
using native::runtime_dom::attributeBoolean;
using native::runtime_dom::clipForOverflow;
using native::runtime_dom::clipsOverflow;
using native::runtime_dom::contains;
using native::runtime_dom::DocumentInstance;
using native::runtime_dom::DragInteraction;
using native::runtime_dom::forRuntimeChildren;
using native::runtime_dom::invalidatePaint;
using native::runtime_dom::invalidatePaintTree;
using native::runtime_dom::isFocusableTag;
using native::runtime_dom::isScrollContainer;
using native::runtime_dom::isVisibleForInteraction;
using native::runtime_dom::Node;
using native::runtime_dom::Rect;
using native::runtime_dom::runtimeChildrenInPaintOrder;
using native::runtime_dom::ScrollbarPart;
using native::runtime_dom::styleFloat;
using native::runtime_dom::styleString;
using native::runtime_dom::translateSubtree;
using native::string_utils::lower;
using native::style_runtime::setInlineStyleProperty;
using native::widget_runtime::cursorForNode;
using native::widget_runtime::isVerticalSlider;
using native::widget_runtime::scrollbarPartAt;
using native::widget_runtime::sliderStepPointerCoordinate;
using native::widget_runtime::updateWindowGeometry;
using native::widget_runtime::windowInteractionAt;

Node* hitTestNode(Node& node, double x, double y) {
  if (!node.present || node.collapsed_hidden ||
      styleString(node, "display", "block") == "none" ||
      styleString(node, "pointer-events", "auto") == "none" ||
      !contains(node.clip, x, y)) {
    return nullptr;
  }
  // Scrollbar parts are painted above content and therefore win hit testing.
  if (scrollbarPartAt(node, x, y) != ScrollbarPart::None) return &node;
  const std::vector<const Node*>& children = runtimeChildrenInPaintOrder(node);
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    if (Node* hit = hitTestNode(*const_cast<Node*>(*it), x, y)) return hit;
  }
  return contains(node.layout, x, y) ? &node : nullptr;
}
Value windowStateValue(const Node& node) {
  return Value::Object{
      {"open", !node.attributes.contains("open") || attributeBoolean(node, "open")},
      {"collapsed", attributeBoolean(node, "collapsed")},
      {"position", Value::Array{node.layout.x, node.layout.y}},
      {"size", Value::Array{node.layout.width, node.layout.height}},
      {"z", styleFloat(node, "z-index", 0.0f)},
  };
}

Rect inheritedClipFor(const Node& node) {
  if (node.parent == nullptr) return node.clip;
  const Node& parent = *node.parent;
  Rect inherited = parent.clip;
  if (clipsOverflow(parent)) {
    inherited = clipForOverflow(
        parent, inherited,
        isScrollContainer(parent) ? parent.scroll_viewport : parent.layout);
  }
  return inherited;
}

void moveWindowPlacement(DocumentInstance& document,
                         Node& window,
                         float x,
                         float y) {
  const float delta_x = x - window.layout.x;
  const float delta_y = y - window.layout.y;
  if (delta_x == 0.0f && delta_y == 0.0f) return;

  const std::string left = jsonNumber(x);
  const std::string top = jsonNumber(y);
  setInlineStyleProperty(window, "left", left);
  setInlineStyleProperty(window, "top", top);
  // Placement is authoritative during capture. Keep computed values aligned
  // with the authored inline state so an unrelated frame-bound layout cannot
  // snap the window back before its two-way binding commits on release.
  window.style["left"] = left;
  window.style["top"] = top;
  window.transitions.erase("left");
  window.transitions.erase("top");
  if (window.transitions.empty()) {
    document.active_transition_nodes.erase(&window);
  }

  translateSubtree(window, delta_x, delta_y, inheritedClipFor(window));
  document.placement_revision = true;
  document.accessibility_revision = true;
  // Retained fragments contain framebuffer-space geometry. Rebuild the moved
  // subtree and its assembly ancestors, while unchanged sibling fragments stay
  // reusable and style/layout revisions remain settled.
  invalidatePaintTree(window);
  invalidatePaint(window.parent);
}

}  // namespace

Node* System::Impl::hitTest(DocumentInstance& doc, double x, double y) {
  if (doc.body == nullptr) return nullptr;
  const native::CanvasPoint point = doc.canvas_layout.windowToLayout(
      static_cast<float>(x), static_cast<float>(y));
  if (doc.has_transients) {
    const std::vector<Node*>& overlays =
        native::transient_runtime::overlayRootsInPaintOrder(doc);
    auto ancestors_accept_pointer = [](const Node& node) {
      for (const Node* current = node.parent; current != nullptr;
           current = current->parent) {
        if (styleString(*current, "pointer-events", "auto") == "none") {
          return false;
        }
      }
      return true;
    };
    for (auto overlay = overlays.rbegin(); overlay != overlays.rend();
         ++overlay) {
      if (!ancestors_accept_pointer(**overlay)) continue;
      if ((*overlay)->tag == "select") {
        const std::vector<const Node*>& options =
            runtimeChildrenInPaintOrder(**overlay);
        for (auto option = options.rbegin(); option != options.rend();
             ++option) {
          if ((*option)->tag != "option" || !(*option)->present ||
              (*option)->collapsed_hidden) {
            continue;
          }
          if (Node* hit = hitTestNode(*const_cast<Node*>(*option), point.x,
                                     point.y)) {
            return hit;
          }
        }
      } else if (Node* hit = hitTestNode(**overlay, point.x, point.y)) {
        return hit;
      }
    }
  }
  return hitTestNode(*doc.body, point.x, point.y);
}

System::InputDisposition System::processEvent(const platform::Event& event) {
  if (!impl_ || !impl_->config.enabled) return InputDisposition::Ignored;
  ++impl_->dispatch_depth;
  auto finish_dispatch = [&]() {
    --impl_->dispatch_depth;
    if (impl_->dispatch_depth == 0) impl_->flushDeferredCloses();
  };
  struct DispatchGuard {
    decltype(finish_dispatch)& callback;
    ~DispatchGuard() { callback(); }
  } dispatch_guard{finish_dispatch};
  if (event.type == platform::EventType::WindowFocus && !event.focused) {
    const auto documents = impl_->document_runtime.allDocuments();
    for (DocumentInstance* document : *documents) {
      if (Node* captured =
              impl_->document_runtime.element(document->pointer_capture)) {
        captured->active = false;
      }
      document->pointer_capture = {};
      document->pointer_down = false;
      if (Node* hovered =
              impl_->document_runtime.element(document->hovered)) {
        hovered->hovered = false;
      }
      document->hovered = {};
      impl_->markDirty(*document);
    }
    return InputDisposition::Ignored;
  }

  const bool pointer_event = event.type == platform::EventType::MouseMove ||
                             event.type == platform::EventType::MouseButtonDown ||
                             event.type == platform::EventType::MouseButtonUp ||
                             event.type == platform::EventType::MouseScroll;
  const bool keyboard_event = event.type == platform::EventType::KeyDown ||
                              event.type == platform::EventType::KeyUp ||
                              event.type == platform::EventType::TextInput;
  const bool gamepad_event = event.type == platform::EventType::GamepadButtonDown ||
                             event.type == platform::EventType::GamepadButtonUp ||
                             event.type == platform::EventType::GamepadAxisMotion;
  // A wheel event does not move the pointer. Platform wheel callbacks carry
  // deltas but may not carry cursor coordinates, so retain the last pointer
  // position for hit testing instead of replacing it with the event defaults.
  if (event.type == platform::EventType::MouseMove ||
      event.type == platform::EventType::MouseButtonDown ||
      event.type == platform::EventType::MouseButtonUp) {
    impl_->pointer_x = event.x;
    impl_->pointer_y = event.y;
  }

  const std::shared_ptr<const native::DocumentRuntime::DocumentOrder>
      ordered_snapshot = impl_->document_runtime.documentsInHitOrder();
  const native::DocumentRuntime::DocumentOrder* ordered =
      ordered_snapshot.get();
  native::DocumentRuntime::DocumentOrder capture_order;
  if (pointer_event) {
    const auto captured = std::find_if(ordered->begin(), ordered->end(), [](const auto* doc) {
      return doc->pointer_capture.valid();
    });
    if (captured != ordered->end() && captured != ordered->begin()) {
      capture_order.assign(ordered->begin(), ordered->end());
      const auto mutable_captured = capture_order.begin() +
          std::distance(ordered->begin(), captured);
      std::rotate(capture_order.begin(), mutable_captured,
                  mutable_captured + 1);
      ordered = &capture_order;
    }
  }

  bool blocked_by_modal = false;
  auto resolve_pointer_target = [&](Node* hit,
                                    double pointer_x,
                                    double pointer_y) -> Node* {
    for (Node* current = hit; current != nullptr; current = current->parent) {
      if (windowInteractionAt(*current, pointer_x, pointer_y) !=
          DragInteraction::None) {
        return current;
      }
    }
    for (Node* current = hit; current != nullptr; current = current->parent) {
      const bool has_listener =
          impl_->document_runtime.hasElementListeners(current->handle);
      if (has_listener || isFocusableTag(current->tag) ||
          isScrollContainer(*current) ||
          current->attributes.contains("on-click") ||
          current->attributes.contains("on-change")) {
        return current;
      }
    }
    return nullptr;
  };

  // Hover follows the topmost clipped box, including ordinary styled elements.
  // Pointer move/down/up targeting intentionally retains the interactive-ancestor
  // resolution below so transparent, non-interactive document content does not
  // change input consumption behavior.
  if (event.type == platform::EventType::MouseMove) {
    DocumentInstance* hover_document = nullptr;
    Node* hover_target = nullptr;
    for (DocumentInstance* doc : *ordered) {
      Node* hit = impl_->document_runtime.element(doc->pointer_capture);
      if (hit == nullptr) {
        hit = impl_->hitTest(*doc, impl_->pointer_x, impl_->pointer_y);
      }
      if (hit != nullptr) {
        hover_document = doc;
        hover_target = hit;
        break;
      }
      if (doc->options.modal) break;
    }

    struct HoverChange {
      DocumentInstance* document = nullptr;
      Node* previous = nullptr;
      Node* next = nullptr;
    };
    std::vector<HoverChange> hover_changes;
    for (DocumentInstance* doc : *ordered) {
      Node* next = doc == hover_document ? hover_target : nullptr;
      Node* previous = impl_->document_runtime.element(doc->hovered);
      if (previous == next) continue;
      doc->hovered = {};
      if (previous != nullptr) {
        previous->hovered = false;
        previous->hovered_scrollbar_part = ScrollbarPart::None;
      }
      hover_changes.push_back({.document = doc, .previous = previous, .next = next});
    }

    // Clear and leave every old target before entering the new topmost target.
    // This keeps synchronous callbacks from observing hover in two documents.
    for (const HoverChange& change : hover_changes) {
      DocumentInstance* doc = change.document;
      Node* previous = change.previous;
      if (previous != nullptr) {
        const native::CanvasPoint point = doc->canvas_layout.windowToLayout(
            static_cast<float>(event.x), static_cast<float>(event.y));
        Event leave{.type = EventType::PointerLeave,
                    .document = doc->handle,
                    .target = previous->handle,
                    .x = point.x,
                    .y = point.y};
        impl_->dispatchEvent(*doc, *previous, leave);
      }
    }
    for (const HoverChange& change : hover_changes) {
      DocumentInstance* doc = change.document;
      Node* next = change.next;
      if (next != nullptr && doc->options.visible &&
          impl_->document_runtime.element(next->handle) == next) {
        const native::CanvasPoint point = doc->canvas_layout.windowToLayout(
            static_cast<float>(event.x), static_cast<float>(event.y));
        doc->hovered = next->handle;
        next->hovered = true;
        next->hovered_scrollbar_part =
            scrollbarPartAt(*next, point.x, point.y);
        Event enter{.type = EventType::PointerEnter,
                    .document = doc->handle,
                    .target = next->handle,
                    .x = point.x,
                    .y = point.y};
        impl_->dispatchEvent(*doc, *next, enter);
      }
      impl_->recordStyleResult(native::style_runtime::restyleNode(
          *doc, change.previous, impl_->styleInputs(*doc)));
      impl_->recordStyleResult(native::style_runtime::restyleNode(
          *doc, change.next, impl_->styleInputs(*doc)));
      if (change.previous != nullptr) invalidatePaint(change.previous);
      if (change.next != nullptr) invalidatePaint(change.next);
    }
    for (DocumentInstance* doc : *ordered) {
      if (doc->has_tooltips) impl_->updateTimedTooltips(*doc);
    }
  }

  for (DocumentInstance* doc : *ordered) {
    const native::CanvasPoint event_point = doc->canvas_layout.windowToLayout(
        static_cast<float>(event.x), static_cast<float>(event.y));
    const native::CanvasPoint pointer_point = doc->canvas_layout.windowToLayout(
        static_cast<float>(impl_->pointer_x), static_cast<float>(impl_->pointer_y));
    const double event_x = event_point.x;
    const double event_y = event_point.y;
    const double pointer_x = pointer_point.x;
    const double pointer_y = pointer_point.y;
    Node* target = impl_->document_runtime.element(doc->pointer_capture);
    if (target == nullptr && pointer_event) {
      target = resolve_pointer_target(
          impl_->hitTest(*doc, impl_->pointer_x, impl_->pointer_y),
          pointer_x, pointer_y);
    }

    if (event.type == platform::EventType::MouseButtonDown &&
        event.mouseButton == platform::MouseButton::Left &&
        impl_->dismissTransientOutside(*doc, event_x, event_y)) {
      return InputDisposition::Consumed;
    }

    if (event.type == platform::EventType::MouseMove) {
      if (target != nullptr) {
        Event move{.type = EventType::PointerMove,
                   .document = doc->handle,
                   .target = target->handle,
                   .pointer_button = event.mouseButton,
                   .modifiers = event.mods,
                   .x = event_x,
                   .y = event_y};
        impl_->dispatchEvent(*doc, *target, move);
        if (doc->pointer_down && !move.defaultPrevented() &&
            target->drag_interaction != DragInteraction::None) {
          const float delta_x = static_cast<float>(event_x - target->drag_start_x);
          const float delta_y = static_cast<float>(event_y - target->drag_start_y);
          Rect rect = target->drag_start_rect;
          const DragInteraction interaction = target->drag_interaction;
          bool requires_layout = false;
          if (interaction == DragInteraction::Splitter &&
              target->drag_resize_target != nullptr) {
            requires_layout = true;
            Node& resized = *target->drag_resize_target;
            const auto orientation_attribute =
                target->attributes.find("orientation");
            const std::string orientation =
                orientation_attribute == target->attributes.end()
                    ? styleString(*target, "orientation", "vertical")
                    : lower(orientation_attribute->second);
            const bool vertical = orientation != "horizontal";
            if (vertical) {
              const float width = std::max(
                  styleFloat(resized, "min-width", 32.0f),
                  target->drag_start_rect.width + delta_x);
              setInlineStyleProperty(resized, "width", jsonNumber(width));
              rect.width = width;
            } else {
              const float height = std::max(
                  styleFloat(resized, "min-height", 24.0f),
                  target->drag_start_rect.height + delta_y);
              setInlineStyleProperty(resized, "height", jsonNumber(height));
              rect.height = height;
            }
          } else if (interaction == DragInteraction::WindowClose ||
                     interaction == DragInteraction::WindowCollapse) {
            // Buttons commit on release inside their hit rectangle.
          } else if (interaction == DragInteraction::WindowMove) {
            rect.x += delta_x;
            rect.y += delta_y;
            const float visible_title = std::min(
                rect.width, std::max(32.0f,
                    styleFloat(*target, "window-titlebar-visible", 64.0f)));
            const native::CanvasRect& bounds = doc->canvas_layout.layout_clip;
            rect.x = std::clamp(rect.x, bounds.x - rect.width + visible_title,
                                bounds.x + bounds.width - visible_title);
            rect.y = std::clamp(rect.y, bounds.y,
                                std::max(bounds.y,
                                         bounds.y + bounds.height - 24.0f));
          } else {
            requires_layout = true;
            const bool resize_left =
                interaction == DragInteraction::WindowResizeLeft ||
                interaction == DragInteraction::WindowResizeTopLeft ||
                interaction == DragInteraction::WindowResizeBottomLeft;
            const bool resize_right =
                interaction == DragInteraction::WindowResizeRight ||
                interaction == DragInteraction::WindowResizeTopRight ||
                interaction == DragInteraction::WindowResizeBottomRight;
            const bool resize_top =
                interaction == DragInteraction::WindowResizeTop ||
                interaction == DragInteraction::WindowResizeTopLeft ||
                interaction == DragInteraction::WindowResizeTopRight;
            const bool resize_bottom =
                interaction == DragInteraction::WindowResizeBottom ||
                interaction == DragInteraction::WindowResizeBottomLeft ||
                interaction == DragInteraction::WindowResizeBottomRight;
            if (resize_left) {
              rect.x += delta_x;
              rect.width -= delta_x;
            }
            if (resize_right) rect.width += delta_x;
            if (resize_top) {
              rect.y += delta_y;
              rect.height -= delta_y;
            }
            if (resize_bottom) rect.height += delta_y;
            const float min_width = std::max(
                32.0f, styleFloat(*target, "window-min-width", 120.0f));
            const float min_height = std::max(
                24.0f, styleFloat(*target, "window-min-height", 64.0f));
            if (rect.width < min_width) {
              if (resize_left) rect.x -= min_width - rect.width;
              rect.width = min_width;
            }
            if (rect.height < min_height) {
              if (resize_top) rect.y -= min_height - rect.height;
              rect.height = min_height;
            }
          }
          if (interaction != DragInteraction::Splitter &&
              interaction != DragInteraction::WindowClose &&
              interaction != DragInteraction::WindowCollapse) {
            if (interaction == DragInteraction::WindowMove) {
              moveWindowPlacement(*doc, *target, rect.x, rect.y);
            } else {
              setInlineStyleProperty(*target, "left", jsonNumber(rect.x));
              setInlineStyleProperty(*target, "top", jsonNumber(rect.y));
              setInlineStyleProperty(*target, "width", jsonNumber(rect.width));
              setInlineStyleProperty(*target, "height", jsonNumber(rect.height));
              target->layout = rect;
              updateWindowGeometry(*target);
            }
          }
          Event input{.type = EventType::Input,
                      .document = doc->handle,
                      .target = target->handle,
                      .value = Value::Object{
                          {"x", rect.x}, {"y", rect.y},
                          {"width", rect.width}, {"height", rect.height}}};
          impl_->dispatchEvent(*doc, *target, input);
          if (requires_layout) impl_->markDirty(*doc);
        }
        if (target->tag == "slider" && doc->pointer_down && !move.defaultPrevented()) {
          impl_->activateDefault(*doc, *target,
                                 isVerticalSlider(*target) ? event_y : event_x,
                                 false);
        }
        if (doc->pointer_down && !move.defaultPrevented() &&
            target->active_scrollbar_part == ScrollbarPart::HorizontalThumb) {
          const float travel = std::max(
              0.0f, target->horizontal_scroll_track.width -
                        target->horizontal_scroll_thumb.width);
          if (travel > 0.0f && target->scroll_max_x > 0.0f) {
            const float delta = static_cast<float>(event_x -
                                                   target->scrollbar_drag_pointer);
            const float previous_x = target->scroll_x;
            const float previous_y = target->scroll_y;
            target->scroll_x = std::clamp(
                target->scrollbar_drag_offset +
                    delta * target->scroll_max_x / travel,
                0.0f, target->scroll_max_x);
            Event input{.type = EventType::Input,
                        .document = doc->handle,
                        .target = target->handle,
                        .value = Value::Object{{"x", target->scroll_x},
                                               {"y", target->scroll_y}}};
            impl_->dispatchEvent(*doc, *target, input);
            impl_->applyScrollPlacement(*doc, *target, previous_x,
                                        previous_y);
          }
        } else if (doc->pointer_down && !move.defaultPrevented() &&
                   target->active_scrollbar_part == ScrollbarPart::VerticalThumb) {
          const float travel = std::max(
              0.0f, target->vertical_scroll_track.height -
                        target->vertical_scroll_thumb.height);
          if (travel > 0.0f && target->scroll_max_y > 0.0f) {
            const float delta = static_cast<float>(event_y -
                                                   target->scrollbar_drag_pointer);
            const float previous_x = target->scroll_x;
            const float previous_y = target->scroll_y;
            target->scroll_y = std::clamp(
                target->scrollbar_drag_offset +
                    delta * target->scroll_max_y / travel,
                0.0f, target->scroll_max_y);
            Event input{.type = EventType::Input,
                        .document = doc->handle,
                        .target = target->handle,
                        .value = Value::Object{{"x", target->scroll_x},
                                               {"y", target->scroll_y}}};
            impl_->dispatchEvent(*doc, *target, input);
            impl_->applyScrollPlacement(*doc, *target, previous_x,
                                        previous_y);
          }
        }
        return doc->pointer_capture.valid() ? InputDisposition::CapturePointer
                                            : InputDisposition::Consumed;
      }
    } else if (event.type == platform::EventType::MouseButtonDown && target != nullptr) {
      Event down{.type = EventType::PointerDown,
                 .document = doc->handle,
                 .target = target->handle,
                 .pointer_button = event.mouseButton,
                 .modifiers = event.mods,
                 .x = event_x,
                 .y = event_y};
      impl_->dispatchEvent(*doc, *target, down);
      if (event.mouseButton == platform::MouseButton::Left && !down.defaultPrevented()) {
        const float previous_scroll_x = target->scroll_x;
        const float previous_scroll_y = target->scroll_y;
        const ScrollbarPart scrollbar_part =
            scrollbarPartAt(*target, event_x, event_y);
        target->active = true;
        doc->pointer_down = true;
        doc->pointer_capture = target->handle;
        if (isFocusableTag(target->tag)) impl_->setFocus(*doc, target);
        if (target->tag == "slider") {
          impl_->activateDefault(*doc, *target,
                                 isVerticalSlider(*target) ? event_y : event_x,
                                 false);
        }
        if (scrollbar_part != ScrollbarPart::None) {
          target->active_scrollbar_part = scrollbar_part;
          if (scrollbar_part == ScrollbarPart::HorizontalThumb) {
            target->scrollbar_drag_pointer = event_x;
            target->scrollbar_drag_offset = target->scroll_x;
          } else if (scrollbar_part == ScrollbarPart::VerticalThumb) {
            target->scrollbar_drag_pointer = event_y;
            target->scrollbar_drag_offset = target->scroll_y;
          } else if (scrollbar_part == ScrollbarPart::HorizontalTrack) {
            const float direction = event_x < target->horizontal_scroll_thumb.x
                                        ? -1.0f
                                        : 1.0f;
            target->scroll_x = std::clamp(
                target->scroll_x + direction * target->scroll_viewport.width * 0.9f,
                0.0f, target->scroll_max_x);
          } else if (scrollbar_part == ScrollbarPart::VerticalTrack) {
            const float direction = event_y < target->vertical_scroll_thumb.y
                                        ? -1.0f
                                        : 1.0f;
            target->scroll_y = std::clamp(
                target->scroll_y + direction * target->scroll_viewport.height * 0.9f,
                0.0f, target->scroll_max_y);
          }
        } else if (target->tag == "window") {
          target->drag_interaction = windowInteractionAt(*target, event_x, event_y);
          if (target->drag_interaction != DragInteraction::None) {
            target->drag_start_x = event_x;
            target->drag_start_y = event_y;
            target->drag_start_rect = target->layout;
            (void)bringToFront(target->handle);
          }
        } else if (target->tag == "splitter" && target->parent != nullptr) {
          std::vector<Node*> siblings =
              visibleRuntimeChildren(*target->parent);
          const auto found = std::find(siblings.begin(), siblings.end(), target);
          if (found != siblings.end() && found != siblings.begin()) {
            target->drag_interaction = DragInteraction::Splitter;
            target->drag_resize_target = *(found - 1);
            target->drag_start_x = event_x;
            target->drag_start_y = event_y;
            target->drag_start_rect = target->drag_resize_target->layout;
          }
        }
        if (target->scroll_x != previous_scroll_x ||
            target->scroll_y != previous_scroll_y) {
          impl_->applyScrollPlacement(*doc, *target, previous_scroll_x,
                                      previous_scroll_y);
        }
      }
      impl_->recordStyleResult(native::style_runtime::restyleNode(
          *doc, target, impl_->styleInputs(*doc)));
      invalidatePaint(target);
      return doc->pointer_capture.valid() ? InputDisposition::CapturePointer
                                          : InputDisposition::Consumed;
    } else if (event.type == platform::EventType::MouseButtonUp && target != nullptr) {
      const ScrollbarPart active_scrollbar = target->active_scrollbar_part;
      const DragInteraction active_drag = target->drag_interaction;
      const bool should_click = active_scrollbar == ScrollbarPart::None &&
                                active_drag == DragInteraction::None &&
                                event.mouseButton == platform::MouseButton::Left &&
                                target->active && contains(target->layout, event_x, event_y) &&
                                contains(target->clip, event_x, event_y);
      target->active = false;
      target->active_scrollbar_part = ScrollbarPart::None;
      target->drag_interaction = DragInteraction::None;
      target->drag_resize_target = nullptr;
      doc->pointer_down = false;
      doc->pointer_capture = {};
      Event up{.type = EventType::PointerUp,
               .document = doc->handle,
               .target = target->handle,
               .pointer_button = event.mouseButton,
               .modifiers = event.mods,
               .x = event_x,
               .y = event_y};
      impl_->dispatchEvent(*doc, *target, up);
      if (active_scrollbar != ScrollbarPart::None) {
        Event change{.type = EventType::Change,
                     .document = doc->handle,
                     .target = target->handle,
                     .value = Value::Object{{"x", target->scroll_x},
                                            {"y", target->scroll_y}}};
        impl_->dispatchEvent(*doc, *target, change);
      } else if (active_drag == DragInteraction::WindowClose &&
                 contains(target->window_close_button, event_x, event_y)) {
        target->attributes["open"] = "false";
        if (const auto binding = target->attributes.find("bind-open");
            binding != target->attributes.end()) {
          impl_->setModelFromWidget(*doc, binding->second, Value(false));
        }
        if (const auto binding = target->attributes.find("bind-window-state");
            binding != target->attributes.end()) {
          impl_->setModelFromWidget(*doc, binding->second,
                                    windowStateValue(*target));
        }
        if (const auto action = target->attributes.find("on-close");
            action != target->attributes.end()) {
          impl_->fireAction(*doc, *target, action->second, Value(false));
        }
        impl_->markDirty(*doc, true);
      } else if (active_drag == DragInteraction::WindowCollapse &&
                 contains(target->window_collapse_button, event_x, event_y)) {
        const bool collapsed = !attributeBoolean(*target, "collapsed");
        target->attributes["collapsed"] = collapsed ? "true" : "false";
        if (const auto binding = target->attributes.find("bind-collapsed");
            binding != target->attributes.end()) {
          impl_->setModelFromWidget(*doc, binding->second, Value(collapsed));
        }
        if (const auto binding = target->attributes.find("bind-window-state");
            binding != target->attributes.end()) {
          impl_->setModelFromWidget(*doc, binding->second,
                                    windowStateValue(*target));
        }
        if (const auto action = target->attributes.find("on-toggle");
            action != target->attributes.end()) {
          impl_->fireAction(*doc, *target, action->second, Value(collapsed));
        }
        impl_->markDirty(*doc, true);
      } else if (active_drag != DragInteraction::None &&
                 active_drag != DragInteraction::WindowClose &&
                 active_drag != DragInteraction::WindowCollapse) {
        if (const auto binding = target->attributes.find("bind-window-state");
            binding != target->attributes.end()) {
          impl_->setModelFromWidget(*doc, binding->second,
                                    windowStateValue(*target));
        }
        Event change{.type = EventType::Change,
                     .document = doc->handle,
                     .target = target->handle,
                     .value = Value::Object{
                         {"x", target->layout.x}, {"y", target->layout.y},
                         {"width", target->layout.width},
                         {"height", target->layout.height}}};
        impl_->dispatchEvent(*doc, *target, change);
      }
      if (should_click) {
        Event click{.type = EventType::Click,
                    .document = doc->handle,
                    .target = target->handle,
                    .pointer_button = event.mouseButton,
                    .modifiers = event.mods,
                    .x = event_x,
                    .y = event_y};
        impl_->dispatchEvent(*doc, *target, click);
        if (!click.defaultPrevented() && target->tag != "slider") {
          impl_->activateDefault(*doc, *target, event_x);
        }
      }
      impl_->recordStyleResult(native::style_runtime::restyleNode(
          *doc, target, impl_->styleInputs(*doc)));
      invalidatePaint(target);
      return InputDisposition::Consumed;
    } else if (event.type == platform::EventType::MouseScroll && target != nullptr) {
      Event scroll_event{.type = EventType::Scroll,
                         .document = doc->handle,
                         .target = target->handle,
                         .modifiers = event.mods,
                         .x = pointer_x,
                         .y = pointer_y,
                         .delta_x = event.scrollX,
                         .delta_y = event.scrollY};
      impl_->dispatchEvent(*doc, *target, scroll_event);
      if (!scroll_event.defaultPrevented()) {
        for (Node* scroll = target; scroll != nullptr; scroll = scroll->parent) {
          if (isScrollContainer(*scroll)) {
            double wheel_x = event.scrollX;
            double wheel_y = event.scrollY;
            const bool can_scroll_x = scroll->scroll_max_x > 0.0f;
            const bool can_scroll_y = scroll->scroll_max_y > 0.0f;
            if (event.mods.shift && wheel_y != 0.0) {
              if (wheel_x == 0.0) wheel_x = wheel_y;
              wheel_y = 0.0;
            } else if (!can_scroll_y && can_scroll_x && wheel_x == 0.0 &&
                       wheel_y != 0.0) {
              // Conventional wheel fallback for horizontal-only regions.
              // Re-evaluate this for every ancestor so an exhausted horizontal
              // child can still hand the original vertical delta to a vertical
              // parent.
              wheel_x = wheel_y;
              wheel_y = 0.0;
            }
            const float previous_x = scroll->scroll_x;
            const float previous_y = scroll->scroll_y;
            scroll->scroll_x = std::clamp(
                scroll->scroll_x - static_cast<float>(wheel_x * 32.0),
                0.0f, scroll->scroll_max_x);
            scroll->scroll_y = std::clamp(
                scroll->scroll_y - static_cast<float>(wheel_y * 32.0),
                0.0f, scroll->scroll_max_y);
            if (scroll->scroll_x != previous_x || scroll->scroll_y != previous_y) {
              impl_->applyScrollPlacement(*doc, *scroll, previous_x,
                                          previous_y);
              break;
            }
          }
        }
      }
      return InputDisposition::Consumed;
    } else if (keyboard_event) {
      Node* focused = impl_->document_runtime.element(doc->focused);
      if (event.type == platform::EventType::KeyDown &&
          event.key == platform::Key::Escape &&
          impl_->cancelTopTransient(*doc)) {
        return InputDisposition::Consumed;
      }
      if (event.type == platform::EventType::KeyDown && event.key == platform::Key::Tab) {
        if (focused != nullptr) {
          Event tab_event{.type = EventType::KeyDown,
                          .document = doc->handle,
                          .target = focused->handle,
                          .key = event.key,
                          .modifiers = event.mods};
          impl_->dispatchEvent(*doc, *focused, tab_event);
          if (tab_event.defaultPrevented()) return InputDisposition::Consumed;
        }
        std::vector<Node*> focusable;
        if (doc->body) collectFocusable(*doc->body, focusable);
        sortFocusableForTab(focusable);
        if (!focusable.empty()) {
          auto found = std::find(focusable.begin(), focusable.end(), focused);
          std::ptrdiff_t index = found == focusable.end() ? -1 : found - focusable.begin();
          index += event.mods.shift ? -1 : 1;
          if (index < 0) index = static_cast<std::ptrdiff_t>(focusable.size()) - 1;
          if (index >= static_cast<std::ptrdiff_t>(focusable.size())) index = 0;
          impl_->setFocus(*doc, focusable[static_cast<std::size_t>(index)]);
          return InputDisposition::Consumed;
        }
        if (focused != nullptr) return InputDisposition::Consumed;
      }
      if (focused != nullptr) {
        const EventType type = event.type == platform::EventType::KeyUp
                                   ? EventType::KeyUp
                                   : EventType::KeyDown;
        Event key_event{.type = type,
                        .document = doc->handle,
                        .target = focused->handle,
                        .key = event.key,
                        .modifiers = event.mods};
        impl_->dispatchEvent(*doc, *focused, key_event);
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "slider" &&
            (event.key == platform::Key::Left || event.key == platform::Key::Down ||
             event.key == platform::Key::Right || event.key == platform::Key::Up)) {
          const int direction =
              (event.key == platform::Key::Left || event.key == platform::Key::Down) ? -1 : 1;
          impl_->activateDefault(*doc, *focused,
                                 sliderStepPointerCoordinate(*focused, direction),
                                 false);
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "splitter" && focused->parent != nullptr &&
            (event.key == platform::Key::Left || event.key == platform::Key::Right ||
             event.key == platform::Key::Up || event.key == platform::Key::Down)) {
          std::vector<Node*> siblings =
              visibleRuntimeChildren(*focused->parent);
          const auto found = std::find(siblings.begin(), siblings.end(), focused);
          if (found != siblings.end() && found != siblings.begin()) {
            Node* resized = *(found - 1);
            const float step = std::max(1.0f, styleFloat(*focused, "splitter-step", 8.0f));
            const auto orientation = focused->attributes.find("orientation");
            const bool vertical = orientation == focused->attributes.end() ||
                                  lower(orientation->second) != "horizontal";
            if (vertical) {
              const float direction = event.key == platform::Key::Left ? -1.0f :
                                      event.key == platform::Key::Right ? 1.0f : 0.0f;
              if (direction != 0.0f) {
                setInlineStyleProperty(*resized, "width",
                    jsonNumber(std::max(32.0f, resized->layout.width + direction * step)));
              }
            } else {
              const float direction = event.key == platform::Key::Up ? -1.0f :
                                      event.key == platform::Key::Down ? 1.0f : 0.0f;
              if (direction != 0.0f) {
                setInlineStyleProperty(*resized, "height",
                    jsonNumber(std::max(24.0f, resized->layout.height + direction * step)));
              }
            }
            impl_->markDirty(*doc);
          }
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "tab" &&
            (event.key == platform::Key::Left ||
             event.key == platform::Key::Right ||
             event.key == platform::Key::Home ||
             event.key == platform::Key::End) &&
            impl_->moveTabSelection(*doc, *focused, event.key)) {
          return InputDisposition::Consumed;
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "tree-item" &&
            impl_->handleTreeNavigation(*doc, *focused, event.key)) {
          return InputDisposition::Consumed;
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "menu-item" &&
            impl_->moveMenuFocus(*doc, *focused, event.key)) {
          return InputDisposition::Consumed;
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "option" &&
            impl_->moveOptionFocus(*doc, *focused, event.key)) {
          return InputDisposition::Consumed;
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            focused->tag == "select" &&
            (event.key == platform::Key::Up || event.key == platform::Key::Down ||
             event.key == platform::Key::Enter || event.key == platform::Key::Space) &&
            impl_->openSelectListbox(*doc, *focused)) {
          return InputDisposition::Consumed;
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown) {
          Node* scroller = focused;
          while (scroller != nullptr && !isScrollContainer(*scroller)) {
            scroller = scroller->parent;
          }
          if (scroller != nullptr &&
              (event.key == platform::Key::PageUp ||
               event.key == platform::Key::PageDown ||
               event.key == platform::Key::Home ||
               event.key == platform::Key::End)) {
            float next = scroller->scroll_y;
            if (event.key == platform::Key::PageUp) {
              next -= scroller->scroll_viewport.height * 0.9f;
            } else if (event.key == platform::Key::PageDown) {
              next += scroller->scroll_viewport.height * 0.9f;
            } else if (event.key == platform::Key::Home) {
              next = 0.0f;
            } else if (event.key == platform::Key::End) {
              next = scroller->scroll_max_y;
            }
            scrollTo(scroller->handle, scroller->scroll_x, next);
          }
        }
        if (!key_event.defaultPrevented() && event.type == platform::EventType::KeyDown &&
            !event.repeat && (event.key == platform::Key::Enter ||
                              event.key == platform::Key::Space)) {
          Event click{.type = EventType::Click,
                      .document = doc->handle,
                      .target = focused->handle,
                      .key = event.key,
                      .modifiers = event.mods};
          impl_->dispatchEvent(*doc, *focused, click);
          if (!click.defaultPrevented()) {
            const double activation_coordinate =
                focused->tag == "slider" && isVerticalSlider(*focused)
                    ? focused->layout.y + focused->layout.height * 0.5
                    : focused->layout.x + focused->layout.width * 0.5;
            impl_->activateDefault(*doc, *focused, activation_coordinate);
          }
        }
        return InputDisposition::Consumed;
      }
    } else if (gamepad_event) {
      Node* focused = impl_->document_runtime.element(doc->focused);
      std::vector<Node*> focusable;
      if (doc->body) collectFocusable(*doc->body, focusable);
      const GamepadNavigationBindings& bindings = impl_->config.gamepad_navigation;
      const auto button_matches = [](platform::GamepadButton input,
                                     platform::GamepadButton binding) {
        return binding != platform::GamepadButton::Unknown && input == binding;
      };
      const auto axis_matches = [](platform::GamepadAxis input,
                                   platform::GamepadAxis binding) {
        return binding != platform::GamepadAxis::Unknown && input == binding;
      };
      if (event.type == platform::EventType::GamepadButtonDown &&
          button_matches(event.gamepadButton, bindings.cancel) &&
          impl_->cancelTopTransient(*doc)) {
        return InputDisposition::Consumed;
      }
      float direction_x = 0.0f;
      float direction_y = 0.0f;
      if (event.type == platform::EventType::GamepadButtonDown) {
        if (button_matches(event.gamepadButton, bindings.left)) direction_x = -1.0f;
        if (button_matches(event.gamepadButton, bindings.right)) direction_x = 1.0f;
        if (button_matches(event.gamepadButton, bindings.up)) direction_y = -1.0f;
        if (button_matches(event.gamepadButton, bindings.down)) direction_y = 1.0f;
      } else if (event.type == platform::EventType::GamepadAxisMotion &&
                 std::abs(event.gamepadValue) >= 0.55f) {
        if (axis_matches(event.gamepadAxis, bindings.horizontal_axis)) {
          direction_x = event.gamepadValue < 0.0f ? -1.0f : 1.0f;
        } else if (axis_matches(event.gamepadAxis, bindings.vertical_axis)) {
          direction_y = event.gamepadValue < 0.0f ? -1.0f : 1.0f;
        }
      }
      if (direction_x != 0.0f || direction_y != 0.0f) {
        if (focused != nullptr && focused->tag == "slider") {
          const int direction = direction_x < 0.0f || direction_y > 0.0f ? -1 : 1;
          impl_->activateDefault(*doc, *focused,
                                 sliderStepPointerCoordinate(*focused, direction),
                                 false);
          return InputDisposition::Consumed;
        }
        if (focused != nullptr && focused->tag == "tab" && direction_x != 0.0f) {
          impl_->moveTabSelection(*doc, *focused,
                                  direction_x < 0.0f ? platform::Key::Left
                                                     : platform::Key::Right);
          return InputDisposition::Consumed;
        }
        if (focused != nullptr && focused->tag == "tree-item" &&
            direction_x != 0.0f) {
          impl_->handleTreeNavigation(*doc, *focused,
                                      direction_x < 0.0f ? platform::Key::Left
                                                         : platform::Key::Right);
          return InputDisposition::Consumed;
        }
        if (focused != nullptr && focused->tag == "select" && direction_y != 0.0f) {
          impl_->openSelectListbox(*doc, *focused);
          return InputDisposition::Consumed;
        }
        if (focused != nullptr && focused->tag == "option" && direction_y != 0.0f) {
          impl_->moveOptionFocus(*doc, *focused,
                                 direction_y < 0.0f ? platform::Key::Up
                                                    : platform::Key::Down);
          return InputDisposition::Consumed;
        }
        if (focused != nullptr && focused->tag == "menu-item" && direction_y != 0.0f) {
          impl_->moveMenuFocus(*doc, *focused,
                               direction_y < 0.0f ? platform::Key::Up
                                                  : platform::Key::Down);
          return InputDisposition::Consumed;
        }
        if (Node* next = spatialCandidate(focusable, focused, direction_x, direction_y)) {
          impl_->setFocus(*doc, next);
        }
        return InputDisposition::Consumed;
      }
      if (focused != nullptr) {
        Event gamepad{.type = event.type == platform::EventType::GamepadButtonUp
                                  ? EventType::GamepadButtonUp
                                  : (event.type == platform::EventType::GamepadAxisMotion
                                         ? EventType::GamepadAxisMotion
                                         : EventType::GamepadButtonDown),
                      .document = doc->handle,
                      .target = focused->handle,
                      .gamepad = event.gamepad,
                      .gamepad_button = event.gamepadButton,
                      .gamepad_axis = event.gamepadAxis,
                      .gamepad_value = event.gamepadValue};
        impl_->dispatchEvent(*doc, *focused, gamepad);
        if (!gamepad.defaultPrevented() &&
            event.type == platform::EventType::GamepadButtonDown &&
            button_matches(event.gamepadButton, bindings.accept)) {
          Event click{.type = EventType::Click,
                      .document = doc->handle,
                      .target = focused->handle};
          impl_->dispatchEvent(*doc, *focused, click);
          if (!click.defaultPrevented()) {
            const double activation_coordinate =
                focused->tag == "slider" && isVerticalSlider(*focused)
                    ? focused->layout.y + focused->layout.height * 0.5
                    : focused->layout.x + focused->layout.width * 0.5;
            impl_->activateDefault(*doc, *focused, activation_coordinate);
          }
        } else if (!gamepad.defaultPrevented() &&
                   event.type == platform::EventType::GamepadButtonDown &&
                   button_matches(event.gamepadButton, bindings.cancel)) {
          Event cancel{.type = EventType::Cancel,
                       .document = doc->handle,
                       .target = focused->handle};
          impl_->dispatchEvent(*doc, *focused, cancel);
          if (const auto action = focused->attributes.find("on-cancel");
              !cancel.defaultPrevented() && action != focused->attributes.end()) {
            impl_->fireAction(*doc, *focused, action->second);
          }
        } else if (event.type == platform::EventType::GamepadButtonDown &&
                   (button_matches(event.gamepadButton, bindings.page_previous) ||
                    button_matches(event.gamepadButton, bindings.page_next))) {
          for (Node* scroll = focused; scroll != nullptr; scroll = scroll->parent) {
            if (isScrollContainer(*scroll)) {
              const float previous_x = scroll->scroll_x;
              const float previous_y = scroll->scroll_y;
              const float direction =
                  button_matches(event.gamepadButton, bindings.page_previous) ? -1.0f : 1.0f;
              scroll->scroll_y = std::clamp(
                  scroll->scroll_y + direction * scroll->scroll_viewport.height * 0.9f,
                  0.0f, scroll->scroll_max_y);
              impl_->applyScrollPlacement(*doc, *scroll, previous_x,
                                          previous_y);
              break;
            }
          }
        }
        return InputDisposition::Consumed;
      }
    }
    if (doc->options.modal) {
      blocked_by_modal = true;
      break;
    }
  }
  if (blocked_by_modal) return InputDisposition::CaptureAll;
  return InputDisposition::Ignored;
}

System::InputCapture System::inputCapture() const {
  InputCapture capture;
  if (!impl_ || !impl_->config.enabled) return capture;
  const std::shared_ptr<const native::DocumentRuntime::DocumentOrder> ordered =
      impl_->document_runtime.documentsInHitOrder();
  for (const DocumentInstance* doc : *ordered) {
    if (doc->options.modal) {
      capture.keyboard = true;
      capture.pointer = true;
      capture.gamepad = true;
      return capture;
    }
    capture.pointer = capture.pointer || doc->pointer_capture.valid();
  }
  return capture;
}

platform::CursorShape System::cursorShape() const {
  if (!impl_ || !impl_->config.enabled) {
    return platform::CursorShape::Default;
  }
  const std::shared_ptr<const native::DocumentRuntime::DocumentOrder> ordered =
      impl_->document_runtime.documentsInHitOrder();
  for (const DocumentInstance* doc : *ordered) {
    const ElementHandle target_handle = doc->pointer_capture.valid()
                                            ? doc->pointer_capture
                                            : doc->hovered;
    if (const Node* target = impl_->document_runtime.element(target_handle)) {
      const native::CanvasPoint point = doc->canvas_layout.windowToLayout(
          static_cast<float>(impl_->pointer_x),
          static_cast<float>(impl_->pointer_y));
      return cursorForNode(*target, point.x, point.y);
    }
    if (doc->options.modal) break;
  }
  return platform::CursorShape::Default;
}

}  // namespace karma::ui
