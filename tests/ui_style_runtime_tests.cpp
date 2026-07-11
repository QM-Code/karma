#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/motion_engine.h"
#include "features/ui/native/style_runtime.h"
#include "karma/assets.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace {

using karma::ui::native::StyleRule;
using karma::ui::native::runtime_dom::DocumentInstance;
using karma::ui::native::runtime_dom::Node;
using karma::ui::native::style_runtime::StyleInputs;

std::unique_ptr<Node> node(std::string tag, Node* parent = nullptr) {
  auto result = std::make_unique<Node>();
  result->tag = std::move(tag);
  result->parent = parent;
  return result;
}

StyleInputs inputs(const karma::assets::AssetRegistry& assets,
                   double now_seconds = 0.0,
                   float motion_scale = 1.0f) {
  return {.assets = assets,
          .viewport_width = 800.0f,
          .viewport_height = 600.0f,
          .now_seconds = now_seconds,
          .motion_scale = motion_scale};
}

double number(const Node& node, std::string_view property) {
  const auto found = node.style.find(std::string(property));
  assert(found != node.style.end());
  return std::stod(found->second);
}

karma::ui::native::KeyframeDeclaration declaration(std::string value) {
  return {.source_value = value,
          .motion_value = karma::ui::native::parseMotionValue(value)};
}

void assertWhite(const Node& node) {
  const auto color = karma::ui::native::parseMotionColor(
      node.style.at("color"));
  assert(color.has_value());
  assert(std::abs(color->r - 1.0f) < 0.001f);
  assert(std::abs(color->g - 1.0f) < 0.001f);
  assert(std::abs(color->b - 1.0f) < 0.001f);
}

void testCascadeAndTargetedRestyle(
    const karma::assets::AssetRegistry& assets) {
  DocumentInstance document;
  document.body = node("div");
  document.body->classes.insert("panel");
  auto button = node("button", document.body.get());
  button->classes.insert("primary");
  Node* button_node = button.get();
  document.body->children.push_back(std::move(button));

  document.rules = {
      StyleRule{.selector = "button",
                .declarations = {{"color", "#111111"},
                                 {"width", "100px"}},
                .specificity = 1,
                .order = 0},
      StyleRule{.selector = ".primary",
                .declarations = {{"color", "#222222"},
                                 {"width", "120px"}},
                .specificity = 10,
                .order = 1},
      StyleRule{.selector = "div > button.primary:hover",
                .declarations = {{"color", "#333333"}},
                .specificity = 22,
                .order = 2},
  };
  karma::ui::native::style_runtime::rebuildDocumentStyleMetadata(document);

  const auto initial = karma::ui::native::style_runtime::styleDocument(
      document, inputs(assets));
  assert(initial.restyled_nodes == 2u);
  assert(initial.layout_changed);
  assert(button_node->style.at("color") == "#222222");
  assert(button_node->style.at("width") == "120px");
  assert(!document.style_revision);
  assert(document.font_revision);

  document.font_revision = false;
  document.accessibility_revision = false;
  button_node->hovered = true;
  const auto targeted = karma::ui::native::style_runtime::restyleNode(
      document, button_node, inputs(assets));
  assert(targeted.restyled_nodes == 1u);
  assert(!targeted.layout_changed);
  const auto hovered_color = karma::ui::native::parseMotionColor(
      button_node->style.at("color"));
  assert(hovered_color.has_value());
  assert(std::abs(hovered_color->r - 0.2f) < 0.001f);
  assert(document.font_revision);
  assert(document.accessibility_revision);

  karma::ui::native::style_runtime::setInlineStyleProperty(
      *button_node, "width", "144px");
  assert(button_node->inline_style.at("width") == "144px");
  assert(button_node->attributes.at("style").find("width:144px") !=
         std::string::npos);
  const auto inline_result = karma::ui::native::style_runtime::restyleNode(
      document, button_node, inputs(assets));
  assert(inline_result.restyled_nodes == 1u);
  assert(inline_result.layout_changed);
  assert(button_node->style.at("width") == "144px");
}

void testActivePaintMotion(const karma::assets::AssetRegistry& assets) {
  DocumentInstance document;
  document.body = node("div");
  Node& animated = *document.body;
  karma::ui::native::style_runtime::setInlineStyleProperty(
      animated, "opacity", "0");
  karma::ui::native::style_runtime::setInlineStyleProperty(
      animated, "transition", "opacity 1s linear");
  (void)karma::ui::native::style_runtime::styleDocument(
      document, inputs(assets));

  karma::ui::native::style_runtime::setInlineStyleProperty(
      animated, "opacity", "1");
  const auto retargeted = karma::ui::native::style_runtime::styleDocument(
      document, inputs(assets));
  assert(retargeted.restyled_nodes == 1u);
  assert(!animated.transitions.empty());
  assert(document.active_transition_nodes.contains(&animated));

  const auto halfway = karma::ui::native::style_runtime::advanceActiveMotion(
      document, 0.5, 1.0f, 41u);
  assert(halfway.advanced_nodes == 1u);
  assert(!halfway.layout_changed);
  assert(std::abs(number(animated, "opacity") - 0.5) < 0.001);

  const auto same_frame =
      karma::ui::native::style_runtime::advanceActiveMotion(
          document, 0.5, 1.0f, 41u);
  assert(same_frame.advanced_nodes == 0u);

  const auto finished = karma::ui::native::style_runtime::finishActiveMotion(
      document, 1.0);
  assert(!finished.layout_changed);
  assert(document.active_transition_nodes.empty());
  assert(animated.transitions.empty());
  assert(std::abs(number(animated, "opacity") - 1.0) < 0.001);
}

void testReducedMotionPropagatesTargetedInheritance(
    const karma::assets::AssetRegistry& assets) {
  {
    DocumentInstance document;
    document.body = node("div");
    auto child = node("text", document.body.get());
    Node* child_node = child.get();
    document.body->children.push_back(std::move(child));
    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "color", "#000000");
    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "font-size", "10px");
    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "transition",
        "color 1s linear, font-size 1s linear");
    (void)karma::ui::native::style_runtime::styleDocument(
        document, inputs(assets));
    document.layout_revision = false;
    const std::uint64_t child_paint_revision = child_node->paint_revision;

    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "color", "#ffffff");
    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "font-size", "20px");
    const auto reduced = karma::ui::native::style_runtime::restyleNode(
        document, document.body.get(), inputs(assets, 0.0, 0.0f));
    assert(reduced.restyled_nodes == 1u);
    assert(reduced.layout_changed);
    assert(document.layout_revision);
    assert(document.active_transition_nodes.empty());
    assert(document.body->transitions.empty());
    assertWhite(*child_node);
    assert(std::abs(number(*child_node, "font-size") - 20.0) < 0.001);
    assert(child_node->paint_revision > child_paint_revision);
  }

  {
    DocumentInstance document;
    document.style_hash = "inherit-animation-v1";
    document.body = node("div");
    auto child = node("text", document.body.get());
    Node* child_node = child.get();
    document.body->children.push_back(std::move(child));
    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "color", "#000000");
    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "font-size", "10px");
    document.keyframes = {
        {.name = "inherit-finish",
         .frames = {
             {.offset = 0.0,
              .declarations = {
                  {"color", declaration("#000000")},
                  {"font-size", declaration("10px")}}},
             {.offset = 1.0,
              .declarations = {
                  {"color", declaration("#ffffff")},
                  {"font-size", declaration("20px")}}},
         }},
    };
    (void)karma::ui::native::style_runtime::styleDocument(
        document, inputs(assets));
    document.layout_revision = false;

    karma::ui::native::style_runtime::setInlineStyleProperty(
        *document.body, "animation", "inherit-finish 1s linear");
    const auto reduced = karma::ui::native::style_runtime::restyleNode(
        document, document.body.get(), inputs(assets, 0.0, 0.0f));
    assert(reduced.restyled_nodes == 1u);
    assert(reduced.layout_changed);
    assert(document.layout_revision);
    assert(document.active_animation_nodes.empty());
    assert(document.body->animation.has_value());
    assert(document.body->animation->completed);
    assertWhite(*child_node);
    assert(std::abs(number(*child_node, "font-size") - 20.0) < 0.001);
  }
}

void testMotionMetadata(const karma::assets::AssetRegistry& assets) {
  DocumentInstance document;
  document.body = node("div");
  assert(!document.has_motion);
  karma::ui::native::style_runtime::setInlineStyleProperty(
      *document.body, "animation-name", "pulse");
  karma::ui::native::style_runtime::rebuildDocumentStyleMetadata(document);
  assert(document.has_motion);

  const auto absent = karma::ui::native::style_runtime::restyleNode(
      document, nullptr, inputs(assets));
  assert(absent.restyled_nodes == 0u);
  assert(!absent.layout_changed);
}

}  // namespace

int main() {
  karma::assets::AssetRegistry assets;
  testCascadeAndTargetedRestyle(assets);
  testActivePaintMotion(assets);
  testReducedMotionPropagatesTargetedInheritance(assets);
  testMotionMetadata(assets);
  std::cout << "ui style runtime tests passed\n";
  return 0;
}
