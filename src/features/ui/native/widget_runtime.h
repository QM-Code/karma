#pragma once

#include "features/ui/native/runtime_dom.h"
#include "karma/platform.h"

namespace karma::ui::native::widget_runtime {

[[nodiscard]] runtime_dom::ScrollbarPart scrollbarPartAt(
    const runtime_dom::Node& node,
    double x,
    double y);

[[nodiscard]] runtime_dom::DragInteraction windowInteractionAt(
    const runtime_dom::Node& node,
    double x,
    double y);

[[nodiscard]] platform::CursorShape cursorForNode(
    const runtime_dom::Node& node,
    double x,
    double y);

[[nodiscard]] bool isVerticalSlider(const runtime_dom::Node& slider);

[[nodiscard]] double sliderStepPointerCoordinate(
    const runtime_dom::Node& slider,
    int direction);

void updateWindowGeometry(runtime_dom::Node& node);
void updateScrollbarGeometry(runtime_dom::Node& node,
                             float content_width,
                             float content_height);

}  // namespace karma::ui::native::widget_runtime
