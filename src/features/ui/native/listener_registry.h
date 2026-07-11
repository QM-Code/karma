#pragma once

#include "karma/ui.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace karma::ui::native {

/// Generational listener storage with indexed action and element dispatch.
/// Structural removal remains linear, while the input hot path visits only
/// listeners registered for the exact document/action or element/event key.
class ListenerRegistry {
 public:
  ListenerRegistry();
  ~ListenerRegistry();
  ListenerRegistry(const ListenerRegistry&) = delete;
  ListenerRegistry& operator=(const ListenerRegistry&) = delete;
  ListenerRegistry(ListenerRegistry&&) noexcept;
  ListenerRegistry& operator=(ListenerRegistry&&) noexcept;

  ListenerHandle addAction(DocumentHandle document,
                           std::string_view action,
                           ActionCallback callback);
  ListenerHandle addElement(DocumentHandle document,
                            ElementHandle element,
                            EventType type,
                            bool capture,
                            EventCallback callback);
  bool remove(ListenerHandle listener);
  void removeDocument(DocumentHandle document);
  void removeElement(ElementHandle element);
  void clear();

  void dispatchAction(const ActionEvent& event);
  void dispatchElement(ElementHandle element,
                       EventType type,
                       bool capture,
                       Event& event);
  [[nodiscard]] bool hasElementListeners(ElementHandle element) const;

  [[nodiscard]] std::size_t activeCount() const;
  [[nodiscard]] std::size_t visitedSlotCount() const;
  void resetVisitedSlotCount();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
