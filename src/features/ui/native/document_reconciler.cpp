#include "features/ui/native/document_reconciler.h"

#include "features/ui/native/binding_engine.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace karma::ui::native::reconciler {
namespace {

using string_utils::trim;

bool pathPrefix(std::string_view prefix, std::string_view path) noexcept {
  if (!path.starts_with(prefix)) return false;
  return path.size() == prefix.size() ||
         (path.size() > prefix.size() &&
          (path[prefix.size()] == '.' || path[prefix.size()] == '['));
}

}  // namespace

bool relatedModelPaths(std::string_view left,
                       std::string_view right) noexcept {
  return pathPrefix(left, right) || pathPrefix(right, left);
}

AffectedOwners selectAffectedOwners(
    std::span<const BindingDependency> dependencies,
    std::span<const std::string> changed_paths) {
  AffectedOwners result;
  for (const BindingDependency& dependency : dependencies) {
    if (dependency.owner == nullptr ||
        !std::any_of(changed_paths.begin(), changed_paths.end(),
                     [&](const std::string& changed_path) {
                       return relatedModelPaths(changed_path, dependency.path);
                     })) {
      continue;
    }
    result.repeat_owner_affected |= dependency.repeat_owner;
    if (std::find(result.owners.begin(), result.owners.end(),
                  dependency.owner) == result.owners.end()) {
      result.owners.push_back(dependency.owner);
    }
  }
  return result;
}

void rebuildBindingDependencies(
    runtime_dom::Node& root,
    BindingEngine& bindings,
    std::vector<BindingDependency>& output) {
  output.clear();
  const auto first_segment = [](std::string_view path) {
    const std::size_t end = path.find_first_of(".[");
    return std::string(path.substr(0u, end));
  };
  const auto append_expression =
      [&](std::string_view expression,
          runtime_dom::Node* owner,
          bool repeat_owner,
          const std::unordered_set<std::string>& locals) {
        for (std::string dependency : bindings.dependencies(expression)) {
          if (dependency.empty() ||
              locals.contains(first_segment(dependency))) {
            continue;
          }
          const bool duplicate =
              std::any_of(output.begin(), output.end(),
                          [&](const BindingDependency& existing) {
                            return existing.owner == owner &&
                                   existing.path == dependency &&
                                   existing.repeat_owner == repeat_owner;
                          });
          if (!duplicate) {
            output.push_back({.path = std::move(dependency),
                              .owner = owner,
                              .repeat_owner = repeat_owner});
          }
        }
      };
  const auto scan_interpolation =
      [&](std::string_view text,
          runtime_dom::Node* owner,
          bool repeat_owner,
          const std::unordered_set<std::string>& locals) {
        std::size_t cursor = 0u;
        while (cursor < text.size()) {
          const std::size_t open = text.find("{{", cursor);
          if (open == text.npos) break;
          const std::size_t close = text.find("}}", open + 2u);
          if (close == text.npos) break;
          append_expression(text.substr(open + 2u, close - open - 2u), owner,
                            repeat_owner, locals);
          cursor = close + 2u;
        }
      };

  const auto scan = [&](auto&& self,
                        runtime_dom::Node& node,
                        std::unordered_set<std::string> locals,
                        runtime_dom::Node* repeat_owner) -> void {
    if (const auto loop = node.attributes.find("k-for");
        loop != node.attributes.end()) {
      const std::string expression = trim(loop->second);
      const std::size_t in = expression.find(" in ");
      if (in != expression.npos) {
        const std::string variable =
            trim(std::string_view(expression).substr(0u, in));
        const std::string collection =
            trim(std::string_view(expression).substr(in + 4u));
        append_expression(collection, &node, true, locals);
        if (!variable.empty()) {
          locals.insert(variable);
          locals.insert(variable + "_index");
        }
      }
      repeat_owner = &node;
    }

    runtime_dom::Node* dependency_owner =
        repeat_owner != nullptr ? repeat_owner : &node;
    const bool owns_repeat = repeat_owner != nullptr;
    for (const auto& [name, expression] : node.attributes) {
      if (name == "k-for" || name == "k-key") continue;
      if (name == "k-if" || name.starts_with("bind-") ||
          name.starts_with("loc-arg-")) {
        append_expression(expression, dependency_owner, owns_repeat, locals);
      }
    }
    if (!node.source_text.empty()) {
      scan_interpolation(node.source_text, dependency_owner, owns_repeat,
                         locals);
    }
    for (auto& child : node.children) {
      self(self, *child, locals, repeat_owner);
    }
  };
  scan(scan, root, {}, nullptr);
}

}  // namespace karma::ui::native::reconciler
