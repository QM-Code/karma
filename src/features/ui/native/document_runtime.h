#pragma once

#include "karma/ui.h"

#include <memory>
#include <string_view>
#include <vector>

namespace karma::ui::native {

namespace runtime_dom {
struct DocumentInstance;
struct Node;
}  // namespace runtime_dom

/// Owns native-UI document, element, and listener lifetimes.
///
/// Dispatch transactions and the work triggered after a document is removed
/// intentionally remain System responsibilities. Callers must not destroy a
/// document while a retained order snapshot is actively being traversed.
class DocumentRuntime {
 public:
  using DocumentOrder = std::vector<runtime_dom::DocumentInstance*>;

  struct ResolvedElement {
    runtime_dom::Node* node = nullptr;
    runtime_dom::DocumentInstance* document = nullptr;

    [[nodiscard]] explicit operator bool() const noexcept {
      return node != nullptr && document != nullptr;
    }
  };

  DocumentRuntime();
  ~DocumentRuntime();
  DocumentRuntime(const DocumentRuntime&) = delete;
  DocumentRuntime& operator=(const DocumentRuntime&) = delete;
  DocumentRuntime(DocumentRuntime&&) noexcept;
  DocumentRuntime& operator=(DocumentRuntime&&) noexcept;

  /// Adopts a fully staged document, assigns its generational handle/opening
  /// order, and registers every node in its current runtime tree.
  [[nodiscard]] DocumentHandle adopt(
      std::unique_ptr<runtime_dom::DocumentInstance> document);

  /// Immediately destroys a document. System defers calls to this operation
  /// while an event or action callback is active.
  bool destroy(DocumentHandle document);
  void clear();

  [[nodiscard]] runtime_dom::DocumentInstance* document(
      DocumentHandle handle);
  [[nodiscard]] const runtime_dom::DocumentInstance* document(
      DocumentHandle handle) const;
  [[nodiscard]] runtime_dom::Node* element(
      ElementHandle handle,
      DocumentHandle* out_document = nullptr);
  [[nodiscard]] const runtime_dom::Node* element(
      ElementHandle handle,
      DocumentHandle* out_document = nullptr) const;
  [[nodiscard]] ResolvedElement resolve(ElementHandle handle);

  /// Registers or releases a runtime subtree belonging to an adopted
  /// document. Used by repeated-template reconciliation and hot reload.
  void allocateTree(runtime_dom::DocumentInstance& document,
                    runtime_dom::Node& root);
  void releaseTree(runtime_dom::DocumentInstance& document,
                   runtime_dom::Node& root);

  void invalidateOrder() noexcept;
  bool bringToFront(DocumentHandle document);
  [[nodiscard]] std::shared_ptr<const DocumentOrder> allDocuments();
  [[nodiscard]] std::shared_ptr<const DocumentOrder> documentsInPaintOrder();
  [[nodiscard]] std::shared_ptr<const DocumentOrder> documentsInHitOrder();

  [[nodiscard]] ListenerHandle addActionListener(
      DocumentHandle document,
      std::string_view action,
      ActionCallback callback);
  [[nodiscard]] ListenerHandle addElementListener(
      ElementHandle element,
      EventType type,
      bool capture,
      EventCallback callback);
  bool removeListener(ListenerHandle listener);
  void dispatchAction(const ActionEvent& event);
  void dispatchElement(ElementHandle element,
                       EventType type,
                       bool capture,
                       Event& event);
  [[nodiscard]] bool hasElementListeners(ElementHandle element) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
