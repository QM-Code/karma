#include "features/ui/native/listener_registry.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace karma::ui::native {
namespace {

enum class ListenerKind : std::uint8_t { Action, Element };

struct ListenerSlot {
  std::uint32_t generation = 1u;
  bool occupied = false;
  ListenerKind kind = ListenerKind::Action;
  DocumentHandle document{};
  ElementHandle element{};
  std::string action;
  EventType event_type = EventType::Click;
  bool capture = false;
  ActionCallback action_callback;
  EventCallback event_callback;
};

std::size_t combine(std::size_t hash, std::size_t value) {
  return hash ^ (value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u));
}

struct ActionKey {
  DocumentHandle document{};
  std::string action;
  bool operator==(const ActionKey&) const = default;
};

struct ActionKeyHash {
  std::size_t operator()(const ActionKey& key) const noexcept {
    std::size_t hash = key.document.index;
    hash = combine(hash, key.document.generation);
    return combine(hash, std::hash<std::string>{}(key.action));
  }
};

struct ElementKey {
  ElementHandle element{};
  EventType type = EventType::Click;
  bool capture = false;
  bool operator==(const ElementKey&) const = default;
};

struct ElementKeyHash {
  std::size_t operator()(const ElementKey& key) const noexcept {
    std::size_t hash = key.element.index;
    hash = combine(hash, key.element.generation);
    hash = combine(hash, static_cast<std::size_t>(key.type));
    return combine(hash, key.capture ? 1u : 0u);
  }
};

}  // namespace

struct ListenerRegistry::Impl {
  std::vector<ListenerSlot> slots;
  std::vector<std::uint32_t> free_slots;
  std::unordered_map<ActionKey, std::vector<ListenerHandle>, ActionKeyHash>
      action_index;
  std::unordered_map<ElementKey, std::vector<ListenerHandle>, ElementKeyHash>
      element_index;
  std::unordered_map<std::uint64_t, std::size_t> element_listener_counts;
  std::size_t active_count = 0u;
  std::size_t visited_slot_count = 0u;

  static std::uint64_t elementKey(ElementHandle element) {
    return (static_cast<std::uint64_t>(element.generation) << 32u) |
           element.index;
  }

  ListenerHandle allocate() {
    std::uint32_t index = 0u;
    if (!free_slots.empty()) {
      index = free_slots.back();
      free_slots.pop_back();
    } else {
      index = static_cast<std::uint32_t>(slots.size());
      slots.emplace_back();
    }
    ListenerSlot& slot = slots[index];
    slot.occupied = true;
    ++active_count;
    return {.index = index, .generation = slot.generation};
  }

  ListenerSlot* find(ListenerHandle handle) {
    if (!handle.valid() || handle.index >= slots.size()) return nullptr;
    ListenerSlot& slot = slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
  }

  const ListenerSlot* find(ListenerHandle handle) const {
    if (!handle.valid() || handle.index >= slots.size()) return nullptr;
    const ListenerSlot& slot = slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
  }

  bool release(ListenerHandle handle) {
    ListenerSlot* slot = find(handle);
    if (slot == nullptr) return false;
    auto erase_handle = [&](auto& index, const auto& key) {
      const auto found = index.find(key);
      if (found == index.end()) return;
      auto& handles = found->second;
      handles.erase(std::remove(handles.begin(), handles.end(), handle),
                    handles.end());
      if (handles.empty()) index.erase(found);
    };
    if (slot->kind == ListenerKind::Action) {
      erase_handle(action_index,
                   ActionKey{.document = slot->document,
                             .action = slot->action});
    } else {
      erase_handle(element_index,
                   ElementKey{.element = slot->element,
                              .type = slot->event_type,
                              .capture = slot->capture});
      const std::uint64_t key = elementKey(slot->element);
      if (auto count = element_listener_counts.find(key);
          count != element_listener_counts.end()) {
        if (count->second <= 1u) element_listener_counts.erase(count);
        else --count->second;
      }
    }
    const std::uint32_t next_generation = slot->generation + 1u;
    *slot = ListenerSlot{};
    slot->generation = next_generation == 0u ? 1u : next_generation;
    free_slots.push_back(handle.index);
    --active_count;
    return true;
  }

  template <typename Key, typename Hash, typename Predicate>
  void compact(std::unordered_map<Key, std::vector<ListenerHandle>, Hash>& index,
               const Key& key,
               Predicate&& valid) {
    auto found = index.find(key);
    if (found == index.end()) return;
    std::vector<ListenerHandle>& handles = found->second;
    handles.erase(std::remove_if(handles.begin(), handles.end(),
                                 [&](ListenerHandle handle) {
                                   return !valid(handle);
                                 }),
                  handles.end());
    if (handles.empty()) index.erase(found);
  }
};

ListenerRegistry::ListenerRegistry() : impl_(std::make_unique<Impl>()) {}
ListenerRegistry::~ListenerRegistry() = default;
ListenerRegistry::ListenerRegistry(ListenerRegistry&&) noexcept = default;
ListenerRegistry& ListenerRegistry::operator=(ListenerRegistry&&) noexcept = default;

ListenerHandle ListenerRegistry::addAction(DocumentHandle document,
                                           std::string_view action,
                                           ActionCallback callback) {
  if (!document.valid() || action.empty() || !callback) return {};
  const ListenerHandle handle = impl_->allocate();
  ListenerSlot& slot = impl_->slots[handle.index];
  slot.kind = ListenerKind::Action;
  slot.document = document;
  slot.action = std::string(action);
  slot.action_callback = std::move(callback);
  impl_->action_index[{.document = document, .action = slot.action}].push_back(handle);
  return handle;
}

ListenerHandle ListenerRegistry::addElement(DocumentHandle document,
                                            ElementHandle element,
                                            EventType type,
                                            bool capture,
                                            EventCallback callback) {
  if (!document.valid() || !element.valid() || !callback) return {};
  const ListenerHandle handle = impl_->allocate();
  ListenerSlot& slot = impl_->slots[handle.index];
  slot.kind = ListenerKind::Element;
  slot.document = document;
  slot.element = element;
  slot.event_type = type;
  slot.capture = capture;
  slot.event_callback = std::move(callback);
  impl_->element_index[{.element = element, .type = type, .capture = capture}]
      .push_back(handle);
  ++impl_->element_listener_counts[Impl::elementKey(element)];
  return handle;
}

bool ListenerRegistry::remove(ListenerHandle listener) {
  return impl_->release(listener);
}

void ListenerRegistry::removeDocument(DocumentHandle document) {
  for (std::uint32_t index = 0u; index < impl_->slots.size(); ++index) {
    const ListenerSlot& slot = impl_->slots[index];
    if (slot.occupied && slot.document == document) {
      impl_->release({.index = index, .generation = slot.generation});
    }
  }
}

void ListenerRegistry::removeElement(ElementHandle element) {
  for (std::uint32_t index = 0u; index < impl_->slots.size(); ++index) {
    const ListenerSlot& slot = impl_->slots[index];
    if (slot.occupied && slot.kind == ListenerKind::Element &&
        slot.element == element) {
      impl_->release({.index = index, .generation = slot.generation});
    }
  }
}

void ListenerRegistry::clear() {
  impl_->slots.clear();
  impl_->free_slots.clear();
  impl_->action_index.clear();
  impl_->element_index.clear();
  impl_->element_listener_counts.clear();
  impl_->active_count = 0u;
  impl_->visited_slot_count = 0u;
}

void ListenerRegistry::dispatchAction(const ActionEvent& event) {
  const ActionKey key{.document = event.document, .action = event.action};
  const auto found = impl_->action_index.find(key);
  if (found == impl_->action_index.end()) return;
  const std::vector<ListenerHandle> callbacks = found->second;
  std::size_t stale = 0u;
  for (const ListenerHandle handle : callbacks) {
    ++impl_->visited_slot_count;
    ListenerSlot* slot = impl_->find(handle);
    if (slot == nullptr || slot->kind != ListenerKind::Action ||
        slot->document != event.document || slot->action != event.action) {
      ++stale;
      continue;
    }
    ActionCallback callback = slot->action_callback;
    if (callback) callback(event);
  }
  if (stale > 0u) {
    impl_->compact(impl_->action_index, key, [&](ListenerHandle handle) {
      const ListenerSlot* slot = impl_->find(handle);
      return slot != nullptr && slot->kind == ListenerKind::Action &&
             slot->document == key.document && slot->action == key.action;
    });
  }
}

void ListenerRegistry::dispatchElement(ElementHandle element,
                                       EventType type,
                                       bool capture,
                                       Event& event) {
  const ElementKey key{.element = element, .type = type, .capture = capture};
  const auto found = impl_->element_index.find(key);
  if (found == impl_->element_index.end()) return;
  const std::vector<ListenerHandle> callbacks = found->second;
  std::size_t stale = 0u;
  for (const ListenerHandle handle : callbacks) {
    ++impl_->visited_slot_count;
    ListenerSlot* slot = impl_->find(handle);
    if (slot == nullptr || slot->kind != ListenerKind::Element ||
        slot->element != element || slot->event_type != type ||
        slot->capture != capture) {
      ++stale;
      continue;
    }
    EventCallback callback = slot->event_callback;
    if (callback) callback(event);
  }
  if (stale > 0u) {
    impl_->compact(impl_->element_index, key, [&](ListenerHandle handle) {
      const ListenerSlot* slot = impl_->find(handle);
      return slot != nullptr && slot->kind == ListenerKind::Element &&
             slot->element == key.element && slot->event_type == key.type &&
             slot->capture == key.capture;
    });
  }
}

bool ListenerRegistry::hasElementListeners(ElementHandle element) const {
  return element.valid() &&
         impl_->element_listener_counts.contains(Impl::elementKey(element));
}

std::size_t ListenerRegistry::activeCount() const {
  return impl_->active_count;
}

std::size_t ListenerRegistry::visitedSlotCount() const {
  return impl_->visited_slot_count;
}

void ListenerRegistry::resetVisitedSlotCount() {
  impl_->visited_slot_count = 0u;
}

}  // namespace karma::ui::native
