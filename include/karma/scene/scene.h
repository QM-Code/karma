#pragma once

#include <algorithm>
#include <vector>

#include "karma/scene/node.h"

namespace karma::scene {

class Scene {
 public:
  NodeId createNode(core::EntityId entity = {}) {
    if (!free_list_.empty()) {
      const NodeId id = free_list_.back();
      free_list_.pop_back();
      nodes_[id] = Node{.id = id, .parent = Node::kInvalidId, .children = {}, .entity = entity};
      return id;
    }
    const NodeId id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back(Node{.id = id, .parent = Node::kInvalidId, .children = {}, .entity = entity});
    return id;
  }

  void destroyNode(NodeId id) {
    if (!isAlive(id)) {
      return;
    }
    detachFromParent(id);
    for (NodeId child : nodes_[id].children) {
      if (isAlive(child)) {
        nodes_[child].parent = Node::kInvalidId;
      }
    }
    nodes_[id].children.clear();
    nodes_[id].id = Node::kInvalidId;
    free_list_.push_back(id);
  }

  void reparent(NodeId child, NodeId new_parent) {
    if (!isAlive(child)) {
      return;
    }
    detachFromParent(child);
    nodes_[child].parent = new_parent;
    if (isAlive(new_parent)) {
      nodes_[new_parent].children.push_back(child);
    }
  }

  bool isAlive(NodeId id) const {
    return id < nodes_.size() && nodes_[id].id != Node::kInvalidId;
  }

  Node& get(NodeId id) { return nodes_[id]; }

  const Node& get(NodeId id) const { return nodes_[id]; }

 private:
  void detachFromParent(NodeId id) {
    const NodeId parent = nodes_[id].parent;
    if (!isAlive(parent)) {
      nodes_[id].parent = Node::kInvalidId;
      return;
    }
    auto& siblings = nodes_[parent].children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    nodes_[id].parent = Node::kInvalidId;
  }

  std::vector<Node> nodes_;
  std::vector<NodeId> free_list_;
};

}  // namespace karma::scene
