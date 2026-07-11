#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {
class BindingEngine;
namespace runtime_dom {
struct Node;
}
}  // namespace karma::ui::native

namespace karma::ui::native::reconciler {

/// One compiled model dependency and the runtime node that owns it.
///
/// `owner` is a non-owning opaque token. This module never dereferences it;
/// callers must keep the pointed-to object alive while the dependency index is
/// queried.
struct BindingDependency {
  std::string path;
  void* owner = nullptr;
  bool repeat_owner = false;
};

/// Unique owners affected by a group of model-path changes.
struct AffectedOwners {
  std::vector<void*> owners;
  bool repeat_owner_affected = false;
};

/// Model paths are related when either is the other path's field/array prefix.
/// Prefix boundaries must be a dot or an opening bracket, so `user` is related
/// to `user.name` and `items[2]`, while `use` is unrelated to `user`.
[[nodiscard]] bool relatedModelPaths(std::string_view left,
                                     std::string_view right) noexcept;

/// Selects affected owners in dependency-index order, collapsing duplicate
/// owner tokens. Null owner tokens and unrelated dependencies are ignored.
/// Repeat ownership is reported independently from owner de-duplication.
[[nodiscard]] AffectedOwners selectAffectedOwners(
    std::span<const BindingDependency> dependencies,
    std::span<const std::string> changed_paths);

/// Rebuilds the compiled model-dependency index for one authored DOM tree.
/// Repeat collection nodes own dependencies in their local descendants; local
/// loop variables are excluded because they are not model paths. Existing
/// output capacity is reused and duplicate owner/path entries are collapsed.
void rebuildBindingDependencies(
    runtime_dom::Node& root,
    BindingEngine& bindings,
    std::vector<BindingDependency>& output);

}  // namespace karma::ui::native::reconciler
