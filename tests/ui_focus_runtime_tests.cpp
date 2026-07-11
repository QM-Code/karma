#include "features/ui/native/focus_runtime.h"
#include "features/ui/native/runtime_dom.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using karma::ui::native::focus_runtime::collectFocusable;
using karma::ui::native::focus_runtime::sortFocusableForTab;
using karma::ui::native::focus_runtime::spatialCandidate;
using karma::ui::native::runtime_dom::Node;
using karma::ui::native::runtime_dom::Rect;
using karma::ui::native::runtime_dom::TemplateInstance;

std::unique_ptr<Node> makeNode(std::string tag) {
  auto node = std::make_unique<Node>();
  node->tag = std::move(tag);
  return node;
}

void testFocusableCollection() {
  Node root;
  root.tag = "body";

  auto button = makeNode("button");
  Node* button_ptr = button.get();
  root.children.push_back(std::move(button));

  auto explicit_zero = makeNode("div");
  explicit_zero->attributes["tab-index"] = "0";
  Node* explicit_zero_ptr = explicit_zero.get();
  root.children.push_back(std::move(explicit_zero));

  auto disabled = makeNode("button");
  disabled->disabled = true;
  root.children.push_back(std::move(disabled));

  auto explicit_minus_one = makeNode("button");
  explicit_minus_one->attributes["tab-index"] = "  -1  ";
  root.children.push_back(std::move(explicit_minus_one));

  auto decimal_minus_one = makeNode("div");
  decimal_minus_one->attributes["tab-index"] = "-1.0";
  Node* decimal_minus_one_ptr = decimal_minus_one.get();
  root.children.push_back(std::move(decimal_minus_one));

  auto hidden_parent = makeNode("div");
  hidden_parent->style["display"] = " NoNe ";
  hidden_parent->children.push_back(makeNode("button"));
  root.children.push_back(std::move(hidden_parent));

  auto absent_parent = makeNode("div");
  absent_parent->present = false;
  absent_parent->children.push_back(makeNode("button"));
  root.children.push_back(std::move(absent_parent));

  auto collapsed = makeNode("button");
  collapsed->collapsed_hidden = true;
  root.children.push_back(std::move(collapsed));

  auto repeated_template = makeNode("template");
  repeated_template->template_node = true;
  TemplateInstance instance;
  auto repeated_button = makeNode("button");
  Node* repeated_button_ptr = repeated_button.get();
  instance.children.push_back(std::move(repeated_button));
  repeated_template->instances.push_back(std::move(instance));
  root.children.push_back(std::move(repeated_template));

  Node existing;
  std::vector<Node*> focusable{&existing};
  collectFocusable(root, focusable);

  const std::vector<Node*> expected{
      &existing, button_ptr, explicit_zero_ptr, decimal_minus_one_ptr,
      repeated_button_ptr};
  assert(focusable == expected);
}

void testTabOrdering() {
  Node implicit;
  Node positive_two;
  Node fractional_one;
  Node positive_one;
  Node zero;
  Node negative;
  Node malformed;

  positive_two.attributes["tab-index"] = "2";
  fractional_one.attributes["tab-index"] = " 1.9 ";
  positive_one.attributes["tab-index"] = "1";
  zero.attributes["tab-index"] = "0";
  negative.attributes["tab-index"] = "-2";
  malformed.attributes["tab-index"] = "1px";

  std::vector<Node*> nodes{&implicit,       &positive_two, &fractional_one,
                           &positive_one,   &zero,         &negative,
                           &malformed};
  sortFocusableForTab(nodes);

  const std::vector<Node*> expected{&fractional_one, &positive_one,
                                    &positive_two,   &implicit,
                                    &zero,           &negative,
                                    &malformed};
  assert(nodes == expected);
}

void setCenter(Node& node, float x, float y) {
  node.layout = Rect{.x = x, .y = y};
}

void testSpatialSelection() {
  Node current;
  Node straight;
  Node diagonal;
  Node behind;
  setCenter(current, 0.0f, 0.0f);
  setCenter(straight, 20.0f, 0.0f);
  setCenter(diagonal, 10.0f, 3.0f);
  setCenter(behind, -1.0f, 0.0f);

  std::vector<Node*> candidates{&current, &straight, &diagonal, &behind};
  assert(spatialCandidate(candidates, &current, 1.0f, 0.0f) == &diagonal);
  assert(spatialCandidate(candidates, nullptr, 1.0f, 0.0f) == &current);
  assert(spatialCandidate({}, &current, 1.0f, 0.0f) == nullptr);
  assert(spatialCandidate(candidates, &current, 0.0f, 0.0f) == nullptr);

  Node tie;
  setCenter(tie, 10.0f, 4.0f);
  std::vector<Node*> tie_first{&current, &tie, &straight};
  assert(spatialCandidate(tie_first, &current, 1.0f, 0.0f) == &tie);
  std::vector<Node*> straight_first{&current, &straight, &tie};
  assert(spatialCandidate(straight_first, &current, 1.0f, 0.0f) ==
         &straight);

  Node below_threshold;
  setCenter(below_threshold, 0.005f, 0.0f);
  assert(spatialCandidate({&current, &below_threshold}, &current, 1.0f,
                          0.0f) == nullptr);
}

}  // namespace

int main() {
  testFocusableCollection();
  testTabOrdering();
  testSpatialSelection();
  std::cout << "ui focus runtime tests passed\n";
  return 0;
}
