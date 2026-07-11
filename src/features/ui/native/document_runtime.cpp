#include "features/ui/native/document_runtime.h"

#include "features/ui/native/listener_registry.h"
#include "features/ui/native/runtime_dom.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace karma::ui::native {
namespace {

using runtime_dom::DocumentInstance;
using runtime_dom::Node;
using runtime_dom::forRuntimeChildren;

struct DocumentSlot {
  std::uint32_t generation = 1u;
  std::unique_ptr<DocumentInstance> value;
};

struct ElementSlot {
  std::uint32_t generation = 1u;
  DocumentHandle document{};
  Node* node = nullptr;
};

}  // namespace

struct DocumentRuntime::Impl {
  std::vector<DocumentSlot> documents;
  std::vector<std::uint32_t> free_documents;
  std::vector<ElementSlot> elements;
  std::vector<std::uint32_t> free_elements;
  ListenerRegistry listeners;
  std::uint64_t next_opening_order = 1u;

  bool all_documents_dirty = true;
  bool document_order_dirty = true;
  std::shared_ptr<const DocumentOrder> retained_all_documents;
  std::shared_ptr<const DocumentOrder> retained_document_paint_order;
  std::shared_ptr<const DocumentOrder> retained_document_hit_order;

  void invalidateStorage() noexcept {
    all_documents_dirty = true;
    document_order_dirty = true;
  }

  DocumentInstance* find(DocumentHandle handle) {
    if (!handle.valid() || handle.index >= documents.size()) return nullptr;
    DocumentSlot& slot = documents[handle.index];
    return slot.generation == handle.generation ? slot.value.get() : nullptr;
  }

  const DocumentInstance* find(DocumentHandle handle) const {
    if (!handle.valid() || handle.index >= documents.size()) return nullptr;
    const DocumentSlot& slot = documents[handle.index];
    return slot.generation == handle.generation ? slot.value.get() : nullptr;
  }

  Node* findElement(ElementHandle handle,
                    DocumentHandle* out_document = nullptr) {
    if (!handle.valid() || handle.index >= elements.size()) return nullptr;
    ElementSlot& slot = elements[handle.index];
    if (slot.generation != handle.generation || slot.node == nullptr) {
      return nullptr;
    }
    if (out_document != nullptr) *out_document = slot.document;
    return slot.node;
  }

  const Node* findElement(ElementHandle handle,
                          DocumentHandle* out_document = nullptr) const {
    if (!handle.valid() || handle.index >= elements.size()) return nullptr;
    const ElementSlot& slot = elements[handle.index];
    if (slot.generation != handle.generation || slot.node == nullptr) {
      return nullptr;
    }
    if (out_document != nullptr) *out_document = slot.document;
    return slot.node;
  }

  ElementHandle allocateElement(DocumentInstance& document, Node& node) {
    if (node.handle.valid()) return node.handle;
    std::uint32_t index = 0u;
    if (!free_elements.empty()) {
      index = free_elements.back();
      free_elements.pop_back();
    } else {
      index = static_cast<std::uint32_t>(elements.size());
      elements.emplace_back();
    }
    ElementSlot& slot = elements[index];
    if (slot.generation == 0u) slot.generation = 1u;
    slot.document = document.handle;
    slot.node = &node;
    node.handle = {.index = index, .generation = slot.generation};
    if (!node.id.empty() && !document.ids.contains(node.id)) {
      document.ids[node.id] = node.handle;
    }
    return node.handle;
  }

  void releaseElement(DocumentInstance& document, Node& node) {
    if (!node.handle.valid() || node.handle.index >= elements.size()) return;
    ElementSlot& slot = elements[node.handle.index];
    if (slot.generation != node.handle.generation || slot.node != &node ||
        slot.document != document.handle) {
      return;
    }

    const ElementHandle released = node.handle;
    if (!node.id.empty()) {
      const auto found = document.ids.find(node.id);
      if (found != document.ids.end() && found->second == released) {
        document.ids.erase(found);
      }
    }
    if (document.focused == released) document.focused = {};
    if (document.hovered == released) document.hovered = {};
    if (document.pointer_capture == released) {
      document.pointer_capture = {};
      document.pointer_down = false;
    }
    node.focused = false;
    node.hovered = false;
    node.active = false;
    document.active_transition_nodes.erase(&node);
    document.active_animation_nodes.erase(&node);
    listeners.removeElement(released);

    slot.node = nullptr;
    slot.document = {};
    ++slot.generation;
    if (slot.generation == 0u) ++slot.generation;
    free_elements.push_back(released.index);
    node.handle = {};
  }

  void allocateTree(DocumentInstance& document, Node& root) {
    allocateElement(document, root);
    forRuntimeChildren(root, [&](Node& child, const Value::Object*) {
      allocateTree(document, child);
    });
  }

  void releaseTree(DocumentInstance& document, Node& root) {
    forRuntimeChildren(root, [&](Node& child, const Value::Object*) {
      releaseTree(document, child);
    });
    releaseElement(document, root);
  }
};

DocumentRuntime::DocumentRuntime() : impl_(std::make_unique<Impl>()) {}
DocumentRuntime::~DocumentRuntime() = default;
DocumentRuntime::DocumentRuntime(DocumentRuntime&&) noexcept = default;
DocumentRuntime& DocumentRuntime::operator=(DocumentRuntime&&) noexcept =
    default;

DocumentHandle DocumentRuntime::adopt(
    std::unique_ptr<DocumentInstance> document) {
  if (!document) return {};
  std::uint32_t index = 0u;
  if (!impl_->free_documents.empty()) {
    index = impl_->free_documents.back();
    impl_->free_documents.pop_back();
  } else {
    index = static_cast<std::uint32_t>(impl_->documents.size());
    impl_->documents.emplace_back();
  }
  DocumentSlot& slot = impl_->documents[index];
  if (slot.generation == 0u) slot.generation = 1u;
  document->handle = {.index = index, .generation = slot.generation};
  document->opening_order = impl_->next_opening_order++;
  slot.value = std::move(document);
  if (slot.value->body) impl_->allocateTree(*slot.value, *slot.value->body);
  impl_->invalidateStorage();
  return slot.value->handle;
}

bool DocumentRuntime::destroy(DocumentHandle document) {
  DocumentInstance* instance = impl_->find(document);
  if (instance == nullptr) return false;
  if (instance->body) impl_->releaseTree(*instance, *instance->body);
  impl_->listeners.removeDocument(document);
  DocumentSlot& slot = impl_->documents[document.index];
  slot.value.reset();
  ++slot.generation;
  if (slot.generation == 0u) ++slot.generation;
  impl_->free_documents.push_back(document.index);
  impl_->invalidateStorage();
  return true;
}

void DocumentRuntime::clear() {
  std::vector<DocumentHandle> live_documents;
  live_documents.reserve(impl_->documents.size());
  for (const DocumentSlot& slot : impl_->documents) {
    if (slot.value) live_documents.push_back(slot.value->handle);
  }
  for (DocumentHandle handle : live_documents) (void)destroy(handle);
  impl_->retained_all_documents.reset();
  impl_->retained_document_paint_order.reset();
  impl_->retained_document_hit_order.reset();
  impl_->all_documents_dirty = true;
  impl_->document_order_dirty = true;
}

DocumentInstance* DocumentRuntime::document(DocumentHandle handle) {
  return impl_->find(handle);
}

const DocumentInstance* DocumentRuntime::document(DocumentHandle handle) const {
  return impl_->find(handle);
}

Node* DocumentRuntime::element(ElementHandle handle,
                               DocumentHandle* out_document) {
  return impl_->findElement(handle, out_document);
}

const Node* DocumentRuntime::element(ElementHandle handle,
                                     DocumentHandle* out_document) const {
  return impl_->findElement(handle, out_document);
}

DocumentRuntime::ResolvedElement DocumentRuntime::resolve(
    ElementHandle handle) {
  DocumentHandle owner;
  Node* node = impl_->findElement(handle, &owner);
  return {.node = node, .document = node == nullptr ? nullptr : impl_->find(owner)};
}

void DocumentRuntime::allocateTree(DocumentInstance& document, Node& root) {
  if (impl_->find(document.handle) != &document) return;
  impl_->allocateTree(document, root);
}

void DocumentRuntime::releaseTree(DocumentInstance& document, Node& root) {
  if (impl_->find(document.handle) != &document) return;
  impl_->releaseTree(document, root);
}

void DocumentRuntime::invalidateOrder() noexcept {
  impl_->document_order_dirty = true;
}

bool DocumentRuntime::bringToFront(DocumentHandle document) {
  DocumentInstance* instance = impl_->find(document);
  if (instance == nullptr) return false;
  instance->opening_order = impl_->next_opening_order++;
  invalidateOrder();
  return true;
}

std::shared_ptr<const DocumentRuntime::DocumentOrder>
DocumentRuntime::allDocuments() {
  if (!impl_->all_documents_dirty && impl_->retained_all_documents) {
    return impl_->retained_all_documents;
  }
  auto documents = std::make_shared<DocumentOrder>();
  documents->reserve(impl_->documents.size());
  for (DocumentSlot& slot : impl_->documents) {
    if (slot.value) documents->push_back(slot.value.get());
  }
  impl_->retained_all_documents = std::move(documents);
  impl_->all_documents_dirty = false;
  return impl_->retained_all_documents;
}

std::shared_ptr<const DocumentRuntime::DocumentOrder>
DocumentRuntime::documentsInPaintOrder() {
  if (!impl_->document_order_dirty &&
      impl_->retained_document_paint_order &&
      impl_->retained_document_hit_order) {
    return impl_->retained_document_paint_order;
  }
  auto paint_order = std::make_shared<DocumentOrder>();
  paint_order->reserve(impl_->documents.size());
  for (DocumentSlot& slot : impl_->documents) {
    if (slot.value && slot.value->options.visible && slot.value->body) {
      paint_order->push_back(slot.value.get());
    }
  }
  std::sort(paint_order->begin(), paint_order->end(),
            [](const DocumentInstance* left, const DocumentInstance* right) {
              if (left->options.layer != right->options.layer) {
                return left->options.layer < right->options.layer;
              }
              return left->opening_order < right->opening_order;
            });
  auto hit_order = std::make_shared<DocumentOrder>(paint_order->rbegin(),
                                                    paint_order->rend());
  impl_->retained_document_paint_order = std::move(paint_order);
  impl_->retained_document_hit_order = std::move(hit_order);
  impl_->document_order_dirty = false;
  return impl_->retained_document_paint_order;
}

std::shared_ptr<const DocumentRuntime::DocumentOrder>
DocumentRuntime::documentsInHitOrder() {
  (void)documentsInPaintOrder();
  return impl_->retained_document_hit_order;
}

ListenerHandle DocumentRuntime::addActionListener(
    DocumentHandle document,
    std::string_view action,
    ActionCallback callback) {
  if (impl_->find(document) == nullptr) return {};
  return impl_->listeners.addAction(document, action, std::move(callback));
}

ListenerHandle DocumentRuntime::addElementListener(
    ElementHandle element,
    EventType type,
    bool capture,
    EventCallback callback) {
  DocumentHandle document;
  if (impl_->findElement(element, &document) == nullptr) return {};
  return impl_->listeners.addElement(document, element, type, capture,
                                     std::move(callback));
}

bool DocumentRuntime::removeListener(ListenerHandle listener) {
  return impl_->listeners.remove(listener);
}

void DocumentRuntime::dispatchAction(const ActionEvent& event) {
  impl_->listeners.dispatchAction(event);
}

void DocumentRuntime::dispatchElement(ElementHandle element,
                                      EventType type,
                                      bool capture,
                                      Event& event) {
  impl_->listeners.dispatchElement(element, type, capture, event);
}

bool DocumentRuntime::hasElementListeners(ElementHandle element) const {
  return impl_->listeners.hasElementListeners(element);
}

}  // namespace karma::ui::native
