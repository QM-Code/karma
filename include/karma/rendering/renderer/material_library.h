#pragma once

#include <string>
#include <unordered_map>

#include "karma/rendering/renderer/material.h"

namespace karma::renderer {

class MaterialLibrary {
 public:
  void registerMaterial(const std::string& key, MaterialResourceDesc desc) {
    desc.material_key = key;
    materials_[key] = std::move(desc);
    version_ += 1;
  }

  void registerFromMeshTint(const std::string& key,
                            const std::string& mesh_key,
                            Color tint) {
    registerMaterial(key, MaterialResourceDesc::fromMeshTint(mesh_key, tint));
  }

  void unregisterMaterial(const std::string& key) {
    if (materials_.erase(key) > 0) {
      version_ += 1;
    }
  }

  void clear() {
    if (!materials_.empty()) {
      materials_.clear();
      version_ += 1;
    }
  }

  const MaterialResourceDesc* find(const std::string& key) const {
    auto it = materials_.find(key);
    if (it == materials_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  uint64_t version() const {
    return version_;
  }

 private:
  std::unordered_map<std::string, MaterialResourceDesc> materials_;
  uint64_t version_ = 0;
};

}  // namespace karma::renderer
