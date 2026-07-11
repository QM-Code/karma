#pragma once

#include "features/ui/native/runtime_dom.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace karma::ui::native::transient_runtime {

using runtime_dom::DocumentInstance;
using runtime_dom::Node;

/// Non-owning bridge from transient policy to the generational element table
/// owned by System. Keeping the lookup explicit lets this module operate on a
/// document without reaching back into System::Impl.
struct NodeLookup {
  void* context = nullptr;
  Node* (*find_by_id)(void* context,
                      DocumentInstance& document,
                      std::string_view id) = nullptr;
};

[[nodiscard]] bool isOverlayTransientRoot(const Node& node) noexcept;

[[nodiscard]] Node* resolveAnchor(DocumentInstance& document,
                                  Node& transient,
                                  const NodeLookup& lookup);

/// Cached top-level transient order shared by painting and reverse hit tests.
[[nodiscard]] const std::vector<Node*>& overlayRootsInPaintOrder(
    DocumentInstance& document);

[[nodiscard]] Node* topOpenTransient(DocumentInstance& document);

[[nodiscard]] bool pointInsideTransient(DocumentInstance& document,
                                        Node& transient,
                                        double x,
                                        double y,
                                        const NodeLookup& lookup);

/// Advances tooltip delay/visibility state. Returns the number of visibility
/// changes so orchestration can invalidate the document once.
[[nodiscard]] std::size_t updateTimedTooltips(DocumentInstance& document,
                                              Node* hovered,
                                              double clock_seconds,
                                              const NodeLookup& lookup);

/// Places open selects, popups, menus, and tooltips in document space. Returns
/// the number of nodes counted by frame diagnostics.
[[nodiscard]] std::size_t placeTransientWidgets(
    DocumentInstance& document,
    const NodeLookup& lookup);

}  // namespace karma::ui::native::transient_runtime
