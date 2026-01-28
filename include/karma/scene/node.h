#pragma once

#include <cstdint>
#include <vector>

#include "karma/core/id.h"

namespace karma::scene {

using NodeId = uint32_t;

struct Node {
  static constexpr NodeId kInvalidId = 0xFFFFFFFFu;

  NodeId id = kInvalidId;
  NodeId parent = kInvalidId;
  std::vector<NodeId> children;
  core::EntityId entity;
};

}  // namespace karma::scene
