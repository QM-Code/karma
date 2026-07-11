#ifdef NDEBUG
#undef NDEBUG
#endif

#include "scene_editor_placement.h"

#include <cassert>
#include <cmath>

namespace {

bool near(float value, float expected, float epsilon = 1.0e-4f) {
  return std::abs(value - expected) <= epsilon;
}

void testPlacementGridSnapping() {
  namespace editor = karma::tools::scene_editor;
  const karma::math::Vec3 point{2.4f, 7.25f, -3.6f};
  const auto snapped = editor::snapPrefabPlacementPoint(point, true, 1.0f);
  assert(near(snapped.x, 2.0f));
  assert(near(snapped.y, 7.25f));
  assert(near(snapped.z, -4.0f));
  const auto unchanged = editor::snapPrefabPlacementPoint(point, false, 1.0f);
  assert(near(unchanged.x, point.x));
  assert(near(unchanged.z, point.z));
}

karma::scenes::SceneDocument groupDocument() {
  karma::scenes::SceneDocument document{};
  document.entities.push_back({
      .id = "root",
      .name = "Root",
      .transform = {.position = {10.0f, 0.0f, 0.0f},
                    .scale = {2.0f, 2.0f, 2.0f}},
  });
  document.entities.push_back({
      .id = "group",
      .name = "Group",
      .parent_id = "root",
      .transform = {.position = {1.0f, 0.0f, 0.0f}},
  });
  return document;
}

void testEditableGroupSelection() {
  namespace editor = karma::tools::scene_editor;
  auto document = groupDocument();
  assert(editor::selectedEditableGroupId(
             document, {editor::SelectionKind::Entity, "group"}) == "group");
  document.entities[1].components["MeshComponent"] = nlohmann::json::object();
  assert(editor::selectedEditableGroupId(
             document, {editor::SelectionKind::Entity, "group"})
             .empty());
  document.entities[1].components = nlohmann::json::object();
  document.lights.push_back({.id = "light", .entity_id = "group"});
  assert(editor::selectedEditableGroupId(
             document, {editor::SelectionKind::Entity, "group"})
             .empty());
}

void testParentRelativePlacement() {
  namespace editor = karma::tools::scene_editor;
  const auto document = groupDocument();
  const karma::scenes::SceneTransform desired{
      .position = {16.0f, 4.0f, 2.0f},
      .scale = {2.0f, 2.0f, 2.0f},
  };
  std::string diagnostic;
  const auto local = editor::linkedPrefabLocalTransform(
      document, "group", desired, &diagnostic);
  assert(local.has_value());
  assert(diagnostic.empty());
  // Root world position is 10 and its scale is 2. Group world position is 12.
  assert(near(local->position.x, 2.0f));
  assert(near(local->position.y, 2.0f));
  assert(near(local->position.z, 1.0f));
  assert(near(local->scale.x, 1.0f));

  const auto root_local = editor::linkedPrefabLocalTransform(
      document, {}, desired, &diagnostic);
  assert(root_local.has_value());
  assert(near(root_local->position.x, desired.position.x));
  assert(!editor::linkedPrefabLocalTransform(
      document, "missing", desired, &diagnostic));
  assert(!diagnostic.empty());
}

}  // namespace

int main() {
  testPlacementGridSnapping();
  testEditableGroupSelection();
  testParentRelativePlacement();
  return 0;
}
