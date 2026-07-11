#include "features/ui/native/listener_registry.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using namespace karma::ui;
  native::ListenerRegistry listeners;
  const DocumentHandle document{.index = 2u, .generation = 4u};
  const ElementHandle element{.index = 8u, .generation = 3u};

  for (int index = 0; index < 1000; ++index) {
    assert(listeners.addAction(
        document, "unrelated-" + std::to_string(index),
        [](const ActionEvent&) {}));
  }
  int actions = 0;
  ListenerHandle self_removing{};
  self_removing = listeners.addAction(
      document, "accept", [&](const ActionEvent&) {
        ++actions;
        assert(listeners.remove(self_removing));
      });
  assert(self_removing);
  listeners.resetVisitedSlotCount();
  listeners.dispatchAction({.document = document,
                            .target = element,
                            .action = "accept"});
  assert(actions == 1);
  assert(listeners.visitedSlotCount() == 1u);
  listeners.dispatchAction({.document = document,
                            .target = element,
                            .action = "accept"});
  assert(actions == 1);

  int capture_events = 0;
  int bubble_events = 0;
  assert(listeners.addElement(document, element, EventType::Click, true,
                              [&](Event&) { ++capture_events; }));
  assert(listeners.addElement(document, element, EventType::Click, false,
                              [&](Event&) { ++bubble_events; }));
  assert(listeners.hasElementListeners(element));
  Event event{.type = EventType::Click,
              .document = document,
              .target = element};
  listeners.dispatchElement(element, EventType::Click, true, event);
  listeners.dispatchElement(element, EventType::Click, false, event);
  assert(capture_events == 1 && bubble_events == 1);

  const std::size_t before_element_removal = listeners.activeCount();
  listeners.removeElement(element);
  assert(listeners.activeCount() + 2u == before_element_removal);
  assert(!listeners.hasElementListeners(element));
  listeners.dispatchElement(element, EventType::Click, true, event);
  assert(capture_events == 1);

  listeners.removeDocument(document);
  assert(listeners.activeCount() == 0u);
  listeners.clear();
  std::cout << "native UI listener registry tests passed\n";
  return 0;
}
