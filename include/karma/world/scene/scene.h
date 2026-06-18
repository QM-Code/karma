#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "karma/world/scene/node.h"

namespace karma::scene {

/// \ingroup karma_scene
/// Lightweight scene graph that maps ECS entities to parent/child nodes.
///
/// The scene graph owns hierarchy relationships only. Authored local transforms
/// and final world transforms live in `TransformComponent` and are composed by
/// `updateWorldTransforms(...)`.
class Scene {
 public:
  /// Creates a node, reusing an existing node for `entity` when one exists.
  NodeId createNode(core::EntityId entity = {}) {
    if (entity.isValid()) {
      const NodeId existing = findNode(entity);
      if (isAlive(existing)) {
        return existing;
      }
    }

    if (!free_list_.empty()) {
      const NodeId id = free_list_.back();
      free_list_.pop_back();
      nodes_[id] = Node{.id = id, .parent = Node::kInvalidId, .children = {}, .entity = entity};
      registerEntityNode(entity, id);
      return id;
    }
    const NodeId id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back(Node{.id = id, .parent = Node::kInvalidId, .children = {}, .entity = entity});
    registerEntityNode(entity, id);
    return id;
  }

  /// Ensures there is a node for `entity`.
  NodeId ensureNode(core::EntityId entity) {
    return createNode(entity);
  }

  /// Finds the node bound to `entity`, or `Node::kInvalidId`.
  NodeId findNode(core::EntityId entity) const {
    if (!entity.isValid()) {
      return Node::kInvalidId;
    }
    const auto it = entity_to_node_.find(entityKey(entity));
    if (it == entity_to_node_.end() || !isAlive(it->second)) {
      return Node::kInvalidId;
    }
    return it->second;
  }

  /// Destroys a node and detaches its children.
  void destroyNode(NodeId id) {
    if (!isAlive(id)) {
      return;
    }
    unregisterEntityNode(nodes_[id].entity, id);
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

  /// Reparents `child` under `new_parent`.
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

  /// Returns true when a node id is alive.
  bool isAlive(NodeId id) const {
    return id < nodes_.size() && nodes_[id].id != Node::kInvalidId;
  }

  /// Returns mutable node data. Caller must ensure `id` is alive.
  Node& get(NodeId id) { return nodes_[id]; }

  /// Returns node data. Caller must ensure `id` is alive.
  const Node& get(NodeId id) const { return nodes_[id]; }

  /// Raw node array, including dead slots.
  const std::vector<Node>& nodes() const { return nodes_; }

 private:
  static uint64_t entityKey(core::EntityId entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void registerEntityNode(core::EntityId entity, NodeId id) {
    if (!entity.isValid()) {
      return;
    }
    entity_to_node_[entityKey(entity)] = id;
  }

  void unregisterEntityNode(core::EntityId entity, NodeId id) {
    if (!entity.isValid()) {
      return;
    }
    const auto it = entity_to_node_.find(entityKey(entity));
    if (it != entity_to_node_.end() && it->second == id) {
      entity_to_node_.erase(it);
    }
  }

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
  std::unordered_map<uint64_t, NodeId> entity_to_node_;
};

}  // namespace karma::scene
