#include "features/ui/native/widget_runtime.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

using karma::ui::native::runtime_dom::DragInteraction;
using karma::ui::native::runtime_dom::Node;
using karma::ui::native::runtime_dom::ScrollbarPart;
using karma::ui::native::widget_runtime::cursorForNode;
using karma::ui::native::widget_runtime::isVerticalSlider;
using karma::ui::native::widget_runtime::scrollbarPartAt;
using karma::ui::native::widget_runtime::sliderStepPointerCoordinate;
using karma::ui::native::widget_runtime::updateScrollbarGeometry;
using karma::ui::native::widget_runtime::updateWindowGeometry;
using karma::ui::native::widget_runtime::windowInteractionAt;

bool nearlyEqual(float left, float right) {
  return std::abs(left - right) < 0.001f;
}

bool nearlyEqual(double left, double right) {
  return std::abs(left - right) < 1.0e-6;
}

void testWindowGeometryInteractionAndCursor() {
  Node window;
  window.tag = "window";
  window.layout = {.x = 10.0f, .y = 20.0f, .width = 120.0f, .height = 90.0f};
  window.style["window-titlebar-height"] = "24";
  window.style["window-resize-grip"] = "6";
  window.attributes["closable"] = "true";
  window.attributes["collapsible"] = "true";
  updateWindowGeometry(window);

  assert(nearlyEqual(window.window_titlebar.height, 24.0f));
  assert(nearlyEqual(window.window_close_button.x, 106.0f));
  assert(nearlyEqual(window.window_collapse_button.x, 82.0f));
  assert(windowInteractionAt(window, 118.0, 30.0) ==
         DragInteraction::WindowClose);
  assert(windowInteractionAt(window, 90.0, 30.0) ==
         DragInteraction::WindowCollapse);
  assert(windowInteractionAt(window, 10.0, 60.0) ==
         DragInteraction::WindowResizeLeft);
  assert(windowInteractionAt(window, 60.0, 30.0) ==
         DragInteraction::WindowMove);
  assert(cursorForNode(window, 10.0, 60.0) ==
         karma::platform::CursorShape::ResizeHorizontal);

  Node authored;
  authored.tag = "panel";
  authored.layout = {.width = 20.0f, .height = 20.0f};
  authored.style["cursor"] = "crosshair";
  assert(cursorForNode(authored, 5.0, 5.0) ==
         karma::platform::CursorShape::Crosshair);
}

void testScrollbarGeometryAndHitParts() {
  Node scroll;
  scroll.tag = "scroll";
  scroll.layout = {.x = 4.0f, .y = 8.0f, .width = 100.0f, .height = 80.0f};
  scroll.style["overflow-x"] = "auto";
  scroll.style["overflow-y"] = "auto";
  updateScrollbarGeometry(scroll, 220.0f, 180.0f);

  assert(nearlyEqual(scroll.scroll_viewport.width, 88.0f));
  assert(nearlyEqual(scroll.scroll_viewport.height, 68.0f));
  assert(scroll.scroll_max_x > 100.0f);
  assert(scroll.scroll_max_y > 100.0f);
  assert(scroll.horizontal_scroll_thumb.width > 0.0f);
  assert(scroll.vertical_scroll_thumb.height > 0.0f);
  assert(scrollbarPartAt(scroll,
                         scroll.horizontal_scroll_thumb.x + 1.0,
                         scroll.horizontal_scroll_thumb.y + 1.0) ==
         ScrollbarPart::HorizontalThumb);
  assert(scrollbarPartAt(scroll,
                         scroll.vertical_scroll_thumb.x + 1.0,
                         scroll.vertical_scroll_thumb.y + 1.0) ==
         ScrollbarPart::VerticalThumb);

  scroll.style["direction"] = "rtl";
  updateScrollbarGeometry(scroll, 220.0f, 180.0f);
  assert(nearlyEqual(scroll.vertical_scroll_track.x, scroll.layout.x));
  assert(nearlyEqual(scroll.scroll_viewport.x,
                     scroll.layout.x + scroll.vertical_scroll_track.width));

  scroll.style["scrollbar-visibility"] = "hidden";
  updateScrollbarGeometry(scroll, 220.0f, 180.0f);
  assert(nearlyEqual(scroll.horizontal_scroll_track.width, 0.0f));
  assert(nearlyEqual(scroll.vertical_scroll_track.height, 0.0f));
  assert(scroll.scroll_max_x > 0.0f && scroll.scroll_max_y > 0.0f);
}

void testSliderCoordinates() {
  Node slider;
  slider.layout = {.x = 10.0f,
                   .y = 20.0f,
                   .width = 200.0f,
                   .height = 100.0f};
  slider.attributes["min"] = "0";
  slider.attributes["max"] = "1";
  slider.attributes["step"] = "0.25";
  slider.control_value = karma::ui::Value(0.5);
  slider.style["direction"] = "rtl";

  assert(!isVerticalSlider(slider));
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, 1), 60.0));
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, -1), 160.0));

  slider.attributes["orientation"] = "VeRtIcAl";
  assert(isVerticalSlider(slider));
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, 1), 45.0));
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, -1), 95.0));

  slider.attributes["orientation"] = " vertical ";
  assert(!isVerticalSlider(slider));
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, 1), 60.0));

  slider.attributes["orientation"] = "vertical";
  slider.attributes["min"] = "5";
  slider.attributes["max"] = "5";
  slider.attributes["step"] = "2";
  slider.control_value = karma::ui::Value(5.0);
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, 1), 120.0));

  slider.attributes.erase("orientation");
  slider.attributes["min"] = "10";
  slider.attributes["max"] = "0";
  slider.attributes["step"] = "2";
  slider.control_value = karma::ui::Value(6.0);
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, 1), 170.0));

  slider.attributes["min"] = "0";
  slider.attributes["max"] = "invalid";
  slider.attributes["step"] = "0.25";
  slider.control_value = karma::ui::Value(0.5);
  assert(nearlyEqual(sliderStepPointerCoordinate(slider, 1), 60.0));
}

}  // namespace

int main() {
  testWindowGeometryInteractionAndCursor();
  testScrollbarGeometryAndHitParts();
  testSliderCoordinates();
  std::cout << "UI widget runtime tests passed\n";
  return 0;
}
