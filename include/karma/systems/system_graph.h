#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "karma/systems/system.h"

namespace karma::systems {

using SystemId = uint32_t;

class SystemGraph {
 public:
  SystemId addSystem(std::unique_ptr<ISystem> system) {
    const SystemId id = next_id_++;
    nodes_[id] = Node{.id = id, .system = std::move(system), .depends_on = {}};
    insertion_order_.push_back(id);
    return id;
  }

  void addDependency(SystemId system, SystemId depends_on) {
    nodes_[system].depends_on.push_back(depends_on);
  }

  void update(ecs::World& world, float dt) {
    const auto order = buildOrder();
    for (SystemId id : order) {
      if (nodes_[id].system) {
        nodes_[id].system->update(world, dt);
      }
    }
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
};

}  // namespace karma::systems
