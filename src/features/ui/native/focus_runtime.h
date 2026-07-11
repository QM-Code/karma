#pragma once

#include <vector>

namespace karma::ui::native::runtime_dom {
struct Node;
}

namespace karma::ui::native::focus_runtime {

void collectFocusable(runtime_dom::Node& node,
                      std::vector<runtime_dom::Node*>& output);

void sortFocusableForTab(std::vector<runtime_dom::Node*>& nodes);

runtime_dom::Node* spatialCandidate(
    const std::vector<runtime_dom::Node*>& candidates,
    const runtime_dom::Node* current,
    float direction_x,
    float direction_y);

}  // namespace karma::ui::native::focus_runtime
