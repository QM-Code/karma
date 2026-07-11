#include "features/ui/native/document_reconciler.h"

#include "features/ui/native/binding_engine.h"
#include "features/ui/native/runtime_dom.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using karma::ui::native::reconciler::AffectedOwners;
using karma::ui::native::reconciler::BindingDependency;
using karma::ui::native::reconciler::relatedModelPaths;
using karma::ui::native::reconciler::rebuildBindingDependencies;
using karma::ui::native::reconciler::selectAffectedOwners;

void testPathPrefixDirectionsAndBoundaries() {
  assert(relatedModelPaths("player", "player.health.current"));
  assert(relatedModelPaths("player.health.current", "player"));
  assert(relatedModelPaths("items", "items[12].name"));
  assert(relatedModelPaths("items[12].name", "items"));
  assert(relatedModelPaths("settings.volume", "settings.volume"));

  assert(!relatedModelPaths("play", "player"));
  assert(!relatedModelPaths("items[1]", "items[10]"));
  assert(!relatedModelPaths("settings.volume", "settings.volumes"));
}

void testAffectedOwnerSelection() {
  int player_owner = 0;
  int inventory_owner = 0;
  int settings_owner = 0;
  int unrelated_owner = 0;
  const std::vector<BindingDependency> dependencies{
      {.path = "player", .owner = &player_owner},
      // Multiple matching dependencies must still return one owner.
      {.path = "player.health", .owner = &player_owner},
      {.path = "inventory.selected", .owner = &inventory_owner},
      {.path = "inventory", .owner = &inventory_owner, .repeat_owner = true},
      {.path = "settings.audio.volume", .owner = &settings_owner},
      {.path = "profiled", .owner = &unrelated_owner},
      {.path = "player", .owner = nullptr, .repeat_owner = true},
  };

  const std::vector<std::string> descendant_change{"player.health.current"};
  AffectedOwners affected =
      selectAffectedOwners(dependencies, descendant_change);
  assert(affected.owners == std::vector<void*>({&player_owner}));
  assert(!affected.repeat_owner_affected);

  // A changed parent path must select a more-specific compiled dependency.
  const std::vector<std::string> parent_change{"settings"};
  affected = selectAffectedOwners(dependencies, parent_change);
  assert(affected.owners == std::vector<void*>({&settings_owner}));
  assert(!affected.repeat_owner_affected);

  const std::vector<std::string> repeat_change{"inventory[3].label"};
  affected = selectAffectedOwners(dependencies, repeat_change);
  assert(affected.owners == std::vector<void*>({&inventory_owner}));
  assert(affected.repeat_owner_affected);

  // Similar text without a path boundary must remain unrelated.
  const std::vector<std::string> unrelated_change{"profile"};
  affected = selectAffectedOwners(dependencies, unrelated_change);
  assert(affected.owners.empty());
  assert(!affected.repeat_owner_affected);

  const std::vector<std::string> no_changes;
  affected = selectAffectedOwners(dependencies, no_changes);
  assert(affected.owners.empty());
  assert(!affected.repeat_owner_affected);
}

void testCompiledDependencyOwnership() {
  using karma::ui::native::BindingEngine;
  using karma::ui::native::runtime_dom::Node;

  Node root;
  root.tag = "body";
  root.source_text = "{{ player.name }} / {{ player.name }}";

  auto repeated = std::make_unique<Node>();
  repeated->tag = "template";
  repeated->parent = &root;
  repeated->attributes["k-for"] = "item in inventory.items";

  auto child = std::make_unique<Node>();
  child->tag = "text";
  child->parent = repeated.get();
  child->attributes["bind-value"] = "item.value";
  child->attributes["k-if"] = "settings.inventory_enabled";
  child->source_text = "{{ item.label }} {{ profile.rank }}";
  repeated->children.push_back(std::move(child));
  Node* repeat_owner = repeated.get();
  root.children.push_back(std::move(repeated));

  BindingEngine bindings;
  std::vector<BindingDependency> dependencies;
  rebuildBindingDependencies(root, bindings, dependencies);

  const auto has = [&](std::string_view path, void* owner, bool is_repeat) {
    return std::any_of(
        dependencies.begin(), dependencies.end(),
        [&](const BindingDependency& dependency) {
          return dependency.path == path && dependency.owner == owner &&
                 dependency.repeat_owner == is_repeat;
        });
  };
  assert(has("player.name", &root, false));
  assert(has("inventory.items", repeat_owner, true));
  assert(has("settings.inventory_enabled", repeat_owner, true));
  assert(has("profile.rank", repeat_owner, true));
  assert(!std::any_of(dependencies.begin(), dependencies.end(),
                      [](const BindingDependency& dependency) {
                        return dependency.path.starts_with("item");
                      }));
  assert(std::count_if(dependencies.begin(), dependencies.end(),
                       [](const BindingDependency& dependency) {
                         return dependency.path == "player.name";
                       }) == 1);
}

}  // namespace

int main() {
  testPathPrefixDirectionsAndBoundaries();
  testAffectedOwnerSelection();
  testCompiledDependencyOwnership();
  std::cout << "Karma UI document reconciler tests passed\n";
  return 0;
}
