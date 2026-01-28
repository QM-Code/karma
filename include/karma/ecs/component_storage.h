#pragma once

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "karma/ecs/entity.h"

namespace karma::ecs {

template <typename T>
class ComponentStorage {
 public:
  bool has(Entity entity) const {
    return entity.index < sparse_.size() &&
           sparse_[entity.index] != kInvalidIndex;
  }

  T& get(Entity entity) { return components_[sparse_[entity.index]]; }

  const T& get(Entity entity) const { return components_[sparse_[entity.index]]; }

  void add(Entity entity, T component) {
    if (has(entity)) {
      components_[sparse_[entity.index]] = std::move(component);
      return;
    }
    ensureSparse(entity.index);
    dense_.push_back(entity);
    components_.push_back(std::move(component));
    sparse_[entity.index] = dense_.size() - 1;
  }

  void remove(Entity entity) {
    if (!has(entity)) {
      return;
    }
    const size_t dense_index = sparse_[entity.index];
    const size_t last_index = dense_.size() - 1;
    if (dense_index != last_index) {
      const Entity last_entity = dense_[last_index];
      dense_[dense_index] = last_entity;
      components_[dense_index] = std::move(components_[last_index]);
      sparse_[last_entity.index] = dense_index;
    }
    dense_.pop_back();
    components_.pop_back();
    sparse_[entity.index] = kInvalidIndex;
  }

  const std::vector<Entity>& denseEntities() const { return dense_; }

 private:
  static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

  void ensureSparse(uint32_t index) {
    if (index >= sparse_.size()) {
      sparse_.resize(index + 1, kInvalidIndex);
    }
  }

  std::vector<Entity> dense_;
  std::vector<size_t> sparse_;
  std::vector<T> components_;
};

}  // namespace karma::ecs
