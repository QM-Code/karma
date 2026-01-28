#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "karma/core/id.h"

namespace karma::renderer {

class MaterialOverrideTable {
 public:
  void setOverride(const std::string& camera_key, core::EntityId entity,
                   const std::string& material_key) {
    table_[makeKey(camera_key, entity)] = material_key;
  }

  void clearOverride(const std::string& camera_key, core::EntityId entity) {
    table_.erase(makeKey(camera_key, entity));
  }

  const std::string* findOverride(const std::string& camera_key,
                                  core::EntityId entity) const {
    const uint64_t key = makeKey(camera_key, entity);
    auto it = table_.find(key);
    if (it == table_.end()) {
      return nullptr;
    }
    return &it->second;
  }

 private:
  static uint64_t makeKey(const std::string& camera_key, core::EntityId entity) {
    const uint64_t hash = std::hash<std::string>{}(camera_key);
    const uint64_t entity_bits = (static_cast<uint64_t>(entity.index) << 32) |
                                static_cast<uint64_t>(entity.generation);
    return hash ^ (entity_bits + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2));
  }

  std::unordered_map<uint64_t, std::string> table_;
};

}  // namespace karma::renderer
