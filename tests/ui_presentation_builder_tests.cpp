#include "features/ui/native/presentation_builder.h"

#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/text_engine.h"
#include "karma/assets.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using karma::ui::native::runtime_dom::DocumentInstance;
using karma::ui::native::runtime_dom::Node;
using karma::ui::native::runtime_dom::Rect;

std::unique_ptr<Node> solidNode(Rect bounds, std::string color) {
  auto node = std::make_unique<Node>();
  node->tag = "div";
  node->layout = bounds;
  node->clip = {0.0f, 0.0f, 100.0f, 40.0f};
  node->style["background-color"] = std::move(color);
  return node;
}

void testProviderFreeRetainedAssemblyAndBudget() {
  karma::assets::AssetRegistry assets;
  karma::ui::native::TextEngine text_engine;
  karma::ui::native::PresentationResources resources(assets, nullptr,
                                                       text_engine);

  DocumentInstance document;
  document.canvas_layout.layout_rect = {0.0f, 0.0f, 100.0f, 40.0f};
  document.canvas_layout.layout_clip = document.canvas_layout.layout_rect;
  document.canvas_layout.scale_x = 1.0f;
  document.canvas_layout.scale_y = 1.0f;
  document.body = std::make_unique<Node>();
  document.body->tag = "body";
  document.body->layout = {0.0f, 0.0f, 100.0f, 40.0f};
  document.body->clip = document.body->layout;

  auto left = solidNode({0.0f, 0.0f, 50.0f, 40.0f}, "#ff0000");
  auto right = solidNode({50.0f, 0.0f, 50.0f, 40.0f}, "#00ff00");
  Node* left_node = left.get();
  Node* right_node = right.get();
  left->parent = document.body.get();
  right->parent = document.body.get();
  document.body->children.push_back(std::move(left));
  document.body->children.push_back(std::move(right));

  std::vector<DocumentInstance*> documents{&document};
  constexpr std::size_t retained_budget = 1024u * 1024u;
  const karma::ui::native::presentation_builder::FrameInputs frame{
      .framebuffer_width = 150,
      .framebuffer_height = 60,
      .framebuffer_scale_x = 1.5f,
      .framebuffer_scale_y = 1.5f,
      .locale = "en",
      .retained_paint_budget_bytes = retained_budget,
      .graphics_available = false,
  };

  karma::rendering::UIDrawData first;
  const auto first_result = karma::ui::native::presentation_builder::build(
      documents, frame, text_engine, resources, first);
  assert(first_result.generation_stable);
  assert(first_result.rebuilt_fragments == 3u);
  assert(first.vertices.size() == 8u);
  assert(first.indices.size() == 12u);
  assert(first.commands.size() == 1u);
  assert(first.commands.front().scissor_x == 0);
  assert(first.commands.front().scissor_y == 0);
  assert(first.commands.front().scissor_w == 150);
  assert(first.commands.front().scissor_h == 60);
  assert(first.vertices[1u].x == 75.0f);
  assert(first.vertices.back().x == 75.0f);
  assert(karma::rendering::validateUIDrawData(first));
  assert(document.body->retained_fragment != nullptr);
  assert(left_node->retained_fragment != nullptr);
  assert(right_node->retained_fragment != nullptr);

  karma::rendering::UIDrawData idle;
  const auto idle_result = karma::ui::native::presentation_builder::build(
      documents, frame, text_engine, resources, idle);
  assert(idle_result.generation_stable);
  assert(idle_result.rebuilt_fragments == 0u);
  assert(idle.vertices.size() == first.vertices.size());
  for (std::size_t index = 0u; index < first.vertices.size(); ++index) {
    assert(idle.vertices[index].x == first.vertices[index].x);
    assert(idle.vertices[index].y == first.vertices[index].y);
    assert(idle.vertices[index].u == first.vertices[index].u);
    assert(idle.vertices[index].v == first.vertices[index].v);
    assert(idle.vertices[index].rgba == first.vertices[index].rgba);
  }
  assert(idle.indices == first.indices);
  assert(idle.commands.size() == first.commands.size());
  assert(karma::rendering::validateUIDrawData(idle));

  auto zero_budget = frame;
  zero_budget.retained_paint_budget_bytes = 0u;
  karma::rendering::UIDrawData final_frame;
  const auto final_result = karma::ui::native::presentation_builder::build(
      documents, zero_budget, text_engine, resources, final_frame);
  assert(final_result.generation_stable);
  assert(final_result.evicted_fragments == 3u);
  assert(karma::rendering::validateUIDrawData(final_frame));
  assert(document.body->retained_fragment == nullptr);
  assert(left_node->retained_fragment == nullptr);
  assert(right_node->retained_fragment == nullptr);

  resources.shutdown();
}

}  // namespace

int main() {
  testProviderFreeRetainedAssemblyAndBudget();
  std::cout << "ui presentation builder tests passed\n";
  return 0;
}
