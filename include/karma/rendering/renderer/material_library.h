#pragma once

#include <string>
#include <unordered_map>

#include "karma/rendering/renderer/material.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// Keyed registry of material resource descriptors.
///
/// `RenderSystem` watches `version()` and rebuilds shared material variants
/// when registrations change.
class MaterialLibrary {
 public:
  /// Registers or replaces a material descriptor.
  void registerMaterial(const std::string& key, MaterialResourceDesc desc) {
    desc.material_key = key;
    materials_[key] = std::move(desc);
    version_ += 1;
  }

  /// Registers a mesh-tint material descriptor.
  void registerFromMeshTint(const std::string& key,
                            const std::string& mesh_key,
                            Color tint) {
    registerMaterial(key, MaterialResourceDesc::fromMeshTint(mesh_key, tint));
  }

  /// Removes a material descriptor.
  void unregisterMaterial(const std::string& key) {
    if (materials_.erase(key) > 0) {
      version_ += 1;
    }
  }

  /// Removes all material descriptors.
  void clear() {
    if (!materials_.empty()) {
      materials_.clear();
      version_ += 1;
    }
  }

  /// Finds a material descriptor by key.
  const MaterialResourceDesc* find(const std::string& key) const {
    auto it = materials_.find(key);
    if (it == materials_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  /// Monotonic registry version used for cache invalidation.
  uint64_t version() const {
    return version_;
  }

 private:
  std::unordered_map<std::string, MaterialResourceDesc> materials_;
  uint64_t version_ = 0;
};

}  // namespace karma::renderer
