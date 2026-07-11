#include "features/ui/native/document_layout_runtime.h"

#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/text_engine.h"
#include "karma/assets.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

namespace {

using karma::ui::native::runtime_dom::DocumentInstance;
using karma::ui::native::runtime_dom::Node;
using karma::ui::native::runtime_dom::Rect;

bool near(float left, float right, float epsilon = 0.001f) {
  return std::abs(left - right) <= epsilon;
}

void expectRect(Rect actual,
                float x,
                float y,
                float width,
                float height) {
  assert(near(actual.x, x));
  assert(near(actual.y, y));
  assert(near(actual.width, width));
  assert(near(actual.height, height));
}

std::unique_ptr<Node> node(std::string tag) {
  auto result = std::make_unique<Node>();
  result->tag = std::move(tag);
  return result;
}

void testInvalidViewportDoesNoWork(
    karma::ui::native::TextEngine& text_engine,
    karma::ui::native::PresentationResources& resources) {
  DocumentInstance document;
  document.body = node("body");
  document.body->layout = {3.0f, 4.0f, 5.0f, 6.0f};

  const auto result =
      karma::ui::native::document_layout_runtime::layoutDocument(
          document, {.logical_width = 0, .logical_height = 100},
          text_engine, resources);
  assert(!result.performed);
  assert(result.laid_out_nodes == 0u);
  expectRect(document.body->layout, 3.0f, 4.0f, 5.0f, 6.0f);
  assert(document.layout_revision);
  assert(document.measure_revision);
}

void testAnchorsCountersAndRevisions(
    karma::ui::native::TextEngine& text_engine,
    karma::ui::native::PresentationResources& resources) {
  DocumentInstance document;
  document.asset_key = "ui:test/layout-runtime";
  document.canvas_layout.layout_rect = {10.0f, 20.0f, 300.0f, 200.0f};
  document.canvas_layout.layout_clip = document.canvas_layout.layout_rect;
  document.body = node("body");
  document.body->style = {{"display", "block"},
                          {"width", "100%"},
                          {"height", "100%"},
                          {"padding", "10px"}};

  auto anchored = node("div");
  anchored->parent = document.body.get();
  anchored->style = {{"width", "50px"}, {"height", "20px"}};
  anchored->anchor = karma::ui::native::AnchorSpec{
      .minimum = {1.0f, 1.0f},
      .maximum = {1.0f, 1.0f},
      .pivot = {1.0f, 1.0f},
  };
  Node* anchored_node = anchored.get();
  document.body->children.push_back(std::move(anchored));

  auto collapsed = node("div");
  collapsed->parent = document.body.get();
  collapsed->collapsed_hidden = true;
  document.body->children.push_back(std::move(collapsed));

  const std::uint64_t previous_paint_revision =
      document.body->paint_revision;
  const auto result =
      karma::ui::native::document_layout_runtime::layoutDocument(
          document,
          {.logical_width = 320, .logical_height = 240, .locale = "en"},
          text_engine, resources);
  assert(result.performed);
  // Work diagnostics count present runtime nodes even when a collapsed node is
  // omitted from the layout adapter.
  assert(result.laid_out_nodes == 3u);
  expectRect(document.body->layout, 10.0f, 20.0f, 300.0f, 200.0f);
  assert(near(anchored_node->layout.x, 250.0f));
  assert(near(anchored_node->layout.y, 190.0f));
  assert(near(anchored_node->layout.width, 50.0f));
  assert(near(anchored_node->layout.height, 20.0f));
  expectRect(anchored_node->clip, 10.0f, 20.0f, 300.0f, 200.0f);
  assert(!document.layout_revision);
  assert(!document.measure_revision);
  assert(document.accessibility_revision);
  assert(document.placement_revision);
  assert(document.virtual_range_revision);
  assert(document.body->paint_revision > previous_paint_revision);
}

void testScrollGeometry(
    karma::ui::native::TextEngine& text_engine,
    karma::ui::native::PresentationResources& resources) {
  DocumentInstance document;
  document.canvas_layout.layout_rect = {0.0f, 0.0f, 200.0f, 120.0f};
  document.canvas_layout.layout_clip = document.canvas_layout.layout_rect;
  document.body = node("body");
  document.body->style = {{"display", "block"},
                          {"width", "100%"},
                          {"height", "100%"}};

  auto scroll = node("scroll");
  scroll->parent = document.body.get();
  scroll->style = {{"width", "100px"},
                   {"height", "80px"},
                   {"overflow", "auto"}};
  Node* scroll_node = scroll.get();
  auto content = node("div");
  content->parent = scroll.get();
  content->style = {{"position", "absolute"},
                    {"width", "240px"},
                    {"height", "180px"}};
  scroll->children.push_back(std::move(content));
  document.body->children.push_back(std::move(scroll));

  const auto result =
      karma::ui::native::document_layout_runtime::layoutDocument(
          document, {.logical_width = 200, .logical_height = 120},
          text_engine, resources);
  assert(result.performed);
  assert(result.laid_out_nodes == 3u);
  assert(scroll_node->scroll_content_width >= 240.0f);
  assert(scroll_node->scroll_content_height >= 180.0f);
  assert(scroll_node->scroll_max_x > 0.0f);
  assert(scroll_node->scroll_max_y > 0.0f);
  assert(scroll_node->horizontal_scroll_track.width > 0.0f);
  assert(scroll_node->vertical_scroll_track.height > 0.0f);
  assert(scroll_node->scroll_viewport.width < scroll_node->layout.width);
  assert(scroll_node->scroll_viewport.height < scroll_node->layout.height);
}

}  // namespace

int main() {
  karma::assets::AssetRegistry assets;
  karma::ui::native::TextEngine text_engine;
  karma::ui::native::PresentationResources resources(assets, nullptr,
                                                       text_engine);
  testInvalidViewportDoesNoWork(text_engine, resources);
  testAnchorsCountersAndRevisions(text_engine, resources);
  testScrollGeometry(text_engine, resources);
  resources.shutdown();
  std::cout << "ui document layout runtime tests passed\n";
  return 0;
}
