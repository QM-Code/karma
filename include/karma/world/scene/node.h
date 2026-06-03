#pragma once

#include <cstdint>
#include <vector>

#include "karma/core/id.h"

namespace karma::scene {

/// \ingroup karma_scene
/// Index into `Scene`'s node array.
using NodeId = uint32_t;

/// \ingroup karma_scene
/// Scene hierarchy node optionally bound to an ECS entity.
struct Node {
  static constexpr NodeId kInvalidId = 0xFFFFFFFFu;

  NodeId id = kInvalidId;
  NodeId parent = kInvalidId;
  std::vector<NodeId> children;
  core::EntityId entity;
};

}  // namespace karma::scene
