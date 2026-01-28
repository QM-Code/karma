#pragma once

#include <cstdint>
#include <vector>

#include "karma/ecs/entity.h"

namespace karma::ecs {

class EntityRegistry {
 public:
  Entity create() {
    if (!free_list_.empty()) {
      const uint32_t index = free_list_.back();
      free_list_.pop_back();
      return Entity{index, generations_[index]};
    }
    const uint32_t index = static_cast<uint32_t>(generations_.size());
    generations_.push_back(0);
    return Entity{index, 0};
  }

  void destroy(Entity entity) {
    if (!isAlive(entity)) {
      return;
    }
    generations_[entity.index]++;
    free_list_.push_back(entity.index);
  }

  bool isAlive(Entity entity) const {
    return entity.index < generations_.size() &&
           generations_[entity.index] == entity.generation;
  }

 private:
  std::vector<uint32_t> generations_;
  std::vector<uint32_t> free_list_;
};

}  // namespace karma::ecs
