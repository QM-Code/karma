#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "karma/world/systems/system.h"

namespace karma::systems {

/// \ingroup karma_systems
/// Opaque id assigned to systems registered in a `SystemGraph`.
using SystemId = uint32_t;

/// \ingroup karma_systems
/// Runtime-owned collection of ECS systems with dependency ordering.
///
/// Dependencies are topologically sorted before update. If a cycle is detected,
/// insertion order is used as a safe fallback rather than failing at runtime.
class SystemGraph {
 public:
  /// Introspection record for debug UI and diagnostics.
  struct SystemInfo {
    SystemId id = 0;
    std::string name;
    std::vector<SystemId> depends_on;
  };

  /// Adds a system and returns its graph id.
  SystemId addSystem(std::unique_ptr<ISystem> system) {
    const SystemId id = next_id_++;
    nodes_[id] = Node{.id = id, .system = std::move(system), .depends_on = {}};
    insertion_order_.push_back(id);
    order_dirty_ = true;
    return id;
  }

  /// Declares that `system` must run after `depends_on`.
  void addDependency(SystemId system, SystemId depends_on) {
    nodes_[system].depends_on.push_back(depends_on);
    order_dirty_ = true;
  }

  /// Updates all registered systems in dependency order.
  void update(ecs::World& world, float dt) {
    if (order_dirty_) {
      cached_order_ = buildOrder();
      order_dirty_ = false;
    }
    const auto& order = cached_order_;
    for (SystemId id : order) {
      if (nodes_[id].system) {
        nodes_[id].system->update(world, dt);
      }
    }
  }

  /// Returns system metadata for tooling and debug UI.
  std::vector<SystemInfo> systems() const {
    std::vector<SystemInfo> out;
    out.reserve(nodes_.size());
    for (const auto& [id, node] : nodes_) {
      SystemInfo info{};
      info.id = id;
      if (node.system) {
        info.name = std::string(node.system->name());
      }
      info.depends_on = node.depends_on;
      out.push_back(std::move(info));
    }
    return out;
  }

  /// Finds the first registered system with runtime type `T`.
  template <typename T>
  T* findSystem() {
    for (auto& [id, node] : nodes_) {
      (void)id;
      if (auto* system = dynamic_cast<T*>(node.system.get())) {
        return system;
      }
    }
    return nullptr;
  }

  /// Finds the first registered system with runtime type `T`.
  template <typename T>
  const T* findSystem() const {
    for (const auto& [id, node] : nodes_) {
      (void)id;
      if (const auto* system = dynamic_cast<const T*>(node.system.get())) {
        return system;
      }
    }
    return nullptr;
  }

 private:
  struct Node {
    SystemId id = 0;
    std::unique_ptr<ISystem> system;
    std::vector<SystemId> depends_on;
  };

  std::vector<SystemId> buildOrder() const {
    std::unordered_map<SystemId, uint32_t> indegree;
    for (const auto& [id, node] : nodes_) {
      indegree[id] = 0;
    }
    for (const auto& [id, node] : nodes_) {
      for (SystemId dep : node.depends_on) {
        if (nodes_.find(dep) != nodes_.end()) {
          indegree[id]++;
        }
      }
    }

    std::queue<SystemId> ready;
    for (const auto& [id, count] : indegree) {
      if (count == 0) {
        ready.push(id);
      }
    }

    std::vector<SystemId> order;
    order.reserve(nodes_.size());
    while (!ready.empty()) {
      const SystemId id = ready.front();
      ready.pop();
      order.push_back(id);
      for (SystemId dependent : dependentsOf(id)) {
        if (--indegree[dependent] == 0) {
          ready.push(dependent);
        }
      }
    }

    if (order.size() != nodes_.size()) {
      return insertion_order_;
    }
    return order;
  }

  std::vector<SystemId> dependentsOf(SystemId id) const {
    std::vector<SystemId> dependents;
    for (const auto& [node_id, node] : nodes_) {
      for (SystemId dep : node.depends_on) {
        if (dep == id) {
          dependents.push_back(node_id);
        }
      }
    }
    return dependents;
  }

  SystemId next_id_ = 1;
  std::unordered_map<SystemId, Node> nodes_;
  std::vector<SystemId> insertion_order_;
  mutable std::vector<SystemId> cached_order_;
  mutable bool order_dirty_ = true;
};

}  // namespace karma::systems
