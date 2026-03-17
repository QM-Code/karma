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
      Entity entity{index, generations_[index]};
      alive_.push_back(entity);
      ++version_;
      return entity;
    }
    const uint32_t index = static_cast<uint32_t>(generations_.size());
    generations_.push_back(0);
    Entity entity{index, 0};
    alive_.push_back(entity);
    ++version_;
    return entity;
  }

  void destroy(Entity entity) {
    if (!isAlive(entity)) {
      return;
    }
    generations_[entity.index]++;
    free_list_.push_back(entity.index);
    for (size_t i = 0; i < alive_.size(); ++i) {
      if (alive_[i] == entity) {
        alive_[i] = alive_.back();
        alive_.pop_back();
        break;
      }
    }
    ++version_;
  }

  bool isAlive(Entity entity) const {
    return entity.index < generations_.size() &&
           generations_[entity.index] == entity.generation;
  }

  const std::vector<Entity>& entities() const { return alive_; }
  uint64_t version() const { return version_; }

 private:
  std::vector<uint32_t> generations_;
  std::vector<uint32_t> free_list_;
  std::vector<Entity> alive_;
  uint64_t version_ = 0;
};

}  // namespace karma::ecs
