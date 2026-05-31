#include "karma/content/prefabs/prefab_entry_handler.h"

#include <unordered_map>

namespace karma::prefabs {

namespace {

using PrefabEntryHandlerMap = std::unordered_map<PrefabEntry::Type, PrefabEntryHandler>;

PrefabEntryHandlerMap& entryHandlers() {
  static PrefabEntryHandlerMap handlers;
  return handlers;
}

}  // namespace

bool registerPrefabEntryHandler(PrefabEntry::Type type, PrefabEntryHandler handler) {
  if (!handler) {
    return false;
  }
  entryHandlers()[type] = std::move(handler);
  return true;
}

void unregisterPrefabEntryHandler(PrefabEntry::Type type) {
  entryHandlers().erase(type);
}

void clearPrefabEntryHandlers() {
  entryHandlers().clear();
}

bool hasPrefabEntryHandler(PrefabEntry::Type type) {
  return entryHandlers().find(type) != entryHandlers().end();
}

ecs::Entity instantiatePrefabEntry(const PrefabEntryHandlerContext& context) {
  const auto it = entryHandlers().find(context.entry.type);
  if (it == entryHandlers().end()) {
    return {};
  }
  return it->second(context);
}

}  // namespace karma::prefabs
