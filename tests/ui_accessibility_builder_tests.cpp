#include "features/ui/native/accessibility_builder.h"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

karma::ui::AccessibilityNode node(
    karma::ui::AccessibilityRole role,
    const char* name) {
  karma::ui::AccessibilityNode result;
  result.role = role;
  result.name = name;
  return result;
}

void testTreeAssemblyAndGeneration() {
  karma::ui::AccessibilityTree previous;
  previous.generation = 41u;
  previous.nodes.push_back(node(karma::ui::AccessibilityRole::Text, "stale"));
  previous.roots.push_back(0u);

  karma::ui::native::AccessibilityTreeBuilder builder(std::move(previous));
  karma::ui::AccessibilityNode document =
      node(karma::ui::AccessibilityRole::Document, "document");
  document.children = {999u};
  const std::size_t document_index = builder.append(std::move(document));
  const std::size_t button_index = builder.append(
      node(karma::ui::AccessibilityRole::Button, "save"), document_index);
  const std::size_t text_index = builder.append(
      node(karma::ui::AccessibilityRole::Text, "status"), document_index);
  const std::size_t overlay_index = builder.append(
      node(karma::ui::AccessibilityRole::Tooltip, "tip"));

  const karma::ui::AccessibilityTree tree = std::move(builder).finish();
  assert(tree.generation == 42u);
  assert(tree.nodes.size() == 4u);
  assert(tree.roots ==
         std::vector<std::size_t>({document_index, overlay_index}));
  assert(tree.nodes[document_index].children ==
         std::vector<std::size_t>({button_index, text_index}));
  assert(tree.nodes[button_index].children.empty());
  assert(tree.nodes[text_index].children.empty());
  assert(tree.nodes[overlay_index].children.empty());
  assert(tree.nodes[button_index].name == "save");
  assert(tree.nodes[overlay_index].role ==
         karma::ui::AccessibilityRole::Tooltip);
}

void testEmptySnapshotStillAdvancesGeneration() {
  karma::ui::AccessibilityTree previous;
  previous.generation = 7u;
  previous.nodes.push_back(node(karma::ui::AccessibilityRole::Text, "old"));
  previous.roots.push_back(0u);

  karma::ui::native::AccessibilityTreeBuilder builder(std::move(previous));
  const karma::ui::AccessibilityTree tree = std::move(builder).finish();
  assert(tree.generation == 8u);
  assert(tree.nodes.empty());
  assert(tree.roots.empty());
}

}  // namespace

int main() {
  testTreeAssemblyAndGeneration();
  testEmptySnapshotStillAdvancesGeneration();
  return 0;
}
