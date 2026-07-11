#include "features/ui/native/document_runtime.h"
#include "features/ui/native/runtime_dom.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace {

using karma::ui::ActionEvent;
using karma::ui::DocumentHandle;
using karma::ui::ElementHandle;
using karma::ui::Event;
using karma::ui::EventType;
using karma::ui::ListenerHandle;
using karma::ui::native::DocumentRuntime;
using karma::ui::native::runtime_dom::DocumentInstance;
using karma::ui::native::runtime_dom::Node;

std::unique_ptr<Node> makeNode(std::string tag, std::string id = {}) {
  auto node = std::make_unique<Node>();
  node->tag = std::move(tag);
  node->id = std::move(id);
  return node;
}

std::unique_ptr<DocumentInstance> makeDocument(int layer,
                                               bool visible = true) {
  auto document = std::make_unique<DocumentInstance>();
  document->options.layer = layer;
  document->options.visible = visible;
  document->body = makeNode("body", "root");
  return document;
}

void testLifecycleAndElementCleanup() {
  DocumentRuntime runtime;
  auto document = makeDocument(0);
  auto first = makeNode("button", "duplicate");
  Node* first_node = first.get();
  first->parent = document->body.get();
  document->body->children.push_back(std::move(first));
  auto second = makeNode("button", "duplicate");
  Node* second_node = second.get();
  second->parent = document->body.get();
  document->body->children.push_back(std::move(second));

  const DocumentHandle document_handle = runtime.adopt(std::move(document));
  assert(document_handle);
  DocumentInstance* adopted = runtime.document(document_handle);
  assert(adopted != nullptr);
  const ElementHandle first_handle = first_node->handle;
  const ElementHandle second_handle = second_node->handle;
  assert(first_handle && second_handle && first_handle != second_handle);
  assert(adopted->ids.at("duplicate") == first_handle);
  const DocumentRuntime::ResolvedElement resolved =
      runtime.resolve(first_handle);
  assert(resolved.node == first_node && resolved.document == adopted);

  adopted->focused = first_handle;
  adopted->hovered = first_handle;
  adopted->pointer_capture = first_handle;
  adopted->pointer_down = true;
  first_node->focused = true;
  first_node->hovered = true;
  first_node->active = true;
  adopted->active_transition_nodes.insert(first_node);
  adopted->active_animation_nodes.insert(first_node);

  int element_events = 0;
  const ListenerHandle element_listener = runtime.addElementListener(
      first_handle, EventType::Click, false,
      [&](Event&) { ++element_events; });
  assert(element_listener && runtime.hasElementListeners(first_handle));

  Event click{.type = EventType::Click,
              .document = document_handle,
              .target = first_handle};
  runtime.dispatchElement(first_handle, EventType::Click, false, click);
  assert(element_events == 1);

  runtime.releaseTree(*adopted, *first_node);
  assert(runtime.element(first_handle) == nullptr);
  assert(!runtime.hasElementListeners(first_handle));
  assert(!adopted->focused && !adopted->hovered &&
         !adopted->pointer_capture && !adopted->pointer_down);
  assert(!first_node->focused && !first_node->hovered && !first_node->active);
  assert(!adopted->active_transition_nodes.contains(first_node));
  assert(!adopted->active_animation_nodes.contains(first_node));

  runtime.allocateTree(*adopted, *first_node);
  const ElementHandle replacement = first_node->handle;
  assert(replacement.index == first_handle.index);
  assert(replacement.generation != first_handle.generation);
  assert(runtime.element(first_handle) == nullptr);
  assert(runtime.element(replacement) == first_node);

  int actions = 0;
  const ListenerHandle action_listener = runtime.addActionListener(
      document_handle, "accept", [&](const ActionEvent&) { ++actions; });
  assert(action_listener);
  runtime.dispatchAction({.document = document_handle,
                          .target = replacement,
                          .action = "accept"});
  assert(actions == 1);

  assert(runtime.destroy(document_handle));
  assert(runtime.document(document_handle) == nullptr);
  assert(runtime.element(replacement) == nullptr);
  runtime.dispatchAction({.document = document_handle,
                          .target = replacement,
                          .action = "accept"});
  assert(actions == 1);
  assert(!runtime.removeListener(action_listener));
  assert(!runtime.removeListener(element_listener));

  const DocumentHandle reused = runtime.adopt(makeDocument(0));
  assert(reused.index == document_handle.index);
  assert(reused.generation != document_handle.generation);
  assert(runtime.document(document_handle) == nullptr);
}

void testRetainedOrdersAndHiddenStorage() {
  DocumentRuntime runtime;
  const DocumentHandle first = runtime.adopt(makeDocument(0));
  const DocumentHandle second = runtime.adopt(makeDocument(0));
  const DocumentHandle high = runtime.adopt(makeDocument(5));
  const DocumentHandle hidden = runtime.adopt(makeDocument(-1, false));

  const auto all = runtime.allDocuments();
  assert(all->size() == 4u);
  assert((*all)[0]->handle == first && (*all)[1]->handle == second &&
         (*all)[2]->handle == high && (*all)[3]->handle == hidden);

  const auto paint = runtime.documentsInPaintOrder();
  assert(paint->size() == 3u);
  assert((*paint)[0]->handle == first && (*paint)[1]->handle == second &&
         (*paint)[2]->handle == high);
  assert(runtime.documentsInPaintOrder().get() == paint.get());
  const auto hit = runtime.documentsInHitOrder();
  assert((*hit)[0]->handle == high && (*hit)[1]->handle == second &&
         (*hit)[2]->handle == first);

  assert(runtime.bringToFront(first));
  const auto promoted = runtime.documentsInPaintOrder();
  assert((*promoted)[0]->handle == second &&
         (*promoted)[1]->handle == first &&
         (*promoted)[2]->handle == high);
  assert(runtime.allDocuments().get() == all.get());

  runtime.document(second)->options.visible = false;
  runtime.invalidateOrder();
  const auto without_second = runtime.documentsInPaintOrder();
  assert(without_second->size() == 2u);
  assert((*without_second)[0]->handle == first &&
         (*without_second)[1]->handle == high);
  assert(runtime.allDocuments()->size() == 4u);

  assert(runtime.destroy(hidden));
  assert(runtime.allDocuments()->size() == 3u);
}

void testClearPreservesGenerations() {
  DocumentRuntime runtime;
  auto document = makeDocument(0);
  auto child = makeNode("button", "button");
  Node* child_node = child.get();
  child->parent = document->body.get();
  document->body->children.push_back(std::move(child));
  const DocumentHandle old_document = runtime.adopt(std::move(document));
  const ElementHandle old_element = child_node->handle;
  const ListenerHandle old_listener = runtime.addActionListener(
      old_document, "old", [](const ActionEvent&) {});
  assert(old_listener);

  runtime.clear();
  assert(runtime.allDocuments()->empty());
  assert(runtime.document(old_document) == nullptr);
  assert(runtime.element(old_element) == nullptr);

  auto replacement = makeDocument(0);
  auto replacement_child = makeNode("button", "button");
  Node* replacement_node = replacement_child.get();
  replacement_child->parent = replacement->body.get();
  replacement->body->children.push_back(std::move(replacement_child));
  const DocumentHandle new_document = runtime.adopt(std::move(replacement));
  assert(new_document.index == old_document.index);
  assert(new_document.generation != old_document.generation);
  assert(replacement_node->handle.index == old_element.index);
  assert(replacement_node->handle.generation != old_element.generation);
  assert(runtime.document(old_document) == nullptr);
  assert(runtime.element(old_element) == nullptr);

  const ListenerHandle new_listener = runtime.addActionListener(
      new_document, "new", [](const ActionEvent&) {});
  assert(new_listener.index == old_listener.index);
  assert(new_listener.generation != old_listener.generation);
  assert(!runtime.removeListener(old_listener));
}

}  // namespace

int main() {
  testLifecycleAndElementCleanup();
  testRetainedOrdersAndHiddenStorage();
  testClearPreservesGenerations();
  std::cout << "native UI document runtime tests passed\n";
  return 0;
}
