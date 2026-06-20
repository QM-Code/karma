#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "karma/rendering/renderer/material.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// Internal keyed registry of shared material assets and reusable material variants.
///
/// `AssetRegistry` wraps this store and exposes the public version counter that
/// render systems watch for material changes.
class MaterialLibrary {
 public:
  /// Registers or replaces a shared material asset.
  void registerMaterialAsset(const std::string& key, MaterialAssetDesc desc) {
    desc.material_key = key;
    assets_[key] = std::move(desc);
    variants_.erase(key);
    version_ += 1;
  }

  /// Registers or replaces a material variant.
  void registerMaterialVariant(const std::string& key, MaterialVariantDesc desc) {
    desc.material_key = key;
    variants_[key] = std::move(desc);
    assets_.erase(key);
    version_ += 1;
  }

  /// Registers a material asset sourced from an imported asset material.
  void registerImportedAssetMaterial(const std::string& key,
                                     std::filesystem::path path,
                                     uint32_t material_index,
                                     MaterialDesc fallback = {},
                                     std::shared_ptr<const ImportedMaterialData> imported = {}) {
    MaterialAssetDesc asset{};
    asset.surface = std::move(fallback);
    asset.material_asset_path = std::move(path);
    asset.material_asset_index = material_index;
    asset.imported_material = std::move(imported);
    registerMaterialAsset(key, std::move(asset));
  }

  /// Removes a material asset or variant.
  bool unregisterMaterial(const std::string& key) {
    const bool removed = assets_.erase(key) > 0 || variants_.erase(key) > 0;
    if (removed) {
      version_ += 1;
    }
    return removed;
  }

  /// Removes all material assets and variants.
  void clear() {
    if (!assets_.empty() || !variants_.empty()) {
      assets_.clear();
      variants_.clear();
      version_ += 1;
    }
  }

  /// Finds a shared material asset by key.
  const MaterialAssetDesc* findAsset(const std::string& key) const {
    const auto it = assets_.find(key);
    return it != assets_.end() ? &it->second : nullptr;
  }

  /// Finds a material variant by key.
  const MaterialVariantDesc* findVariant(const std::string& key) const {
    const auto it = variants_.find(key);
    return it != variants_.end() ? &it->second : nullptr;
  }

  /// Resolves a material asset or variant to a flattened backend descriptor.
  std::optional<ResolvedMaterialDesc> resolve(const std::string& key) const {
    return resolveRecursive(key, 0);
  }

  /// Monotonic registry version used for cache invalidation.
  uint64_t version() const {
    return version_;
  }

 private:
  static constexpr uint32_t kMaxResolveDepth = 16;

  static const float* asFloat(const MaterialParameterValue& value) {
    if (const auto* f = std::get_if<float>(&value)) {
      return f;
    }
    return nullptr;
  }

  static const bool* asBool(const MaterialParameterValue& value) {
    if (const auto* b = std::get_if<bool>(&value)) {
      return b;
    }
    return nullptr;
  }

  static const Color* asColor(const MaterialParameterValue& value) {
    if (const auto* c = std::get_if<Color>(&value)) {
      return c;
    }
    return nullptr;
  }

  static const glm::vec3* asVec3(const MaterialParameterValue& value) {
    if (const auto* v = std::get_if<glm::vec3>(&value)) {
      return v;
    }
    return nullptr;
  }

  static const std::string* asString(const MaterialParameterValue& value) {
    if (const auto* s = std::get_if<std::string>(&value)) {
      return s;
    }
    return nullptr;
  }

  static void applyParameter(MaterialDesc& material,
                             const std::string& name,
                             const MaterialParameterValue& value) {
    if (name == "base_color") {
      if (const auto* color = asColor(value)) material.base_color = *color;
    } else if (name == "emissive_color") {
      if (const auto* color = asColor(value)) material.emissive_color = *color;
    } else if (name == "metallic") {
      if (const auto* f = asFloat(value)) material.metallic = *f;
    } else if (name == "roughness") {
      if (const auto* f = asFloat(value)) material.roughness = *f;
    } else if (name == "normal_scale") {
      if (const auto* f = asFloat(value)) material.normal_scale = *f;
    } else if (name == "occlusion_strength") {
      if (const auto* f = asFloat(value)) material.occlusion_strength = *f;
    } else if (name == "emissive_strength") {
      if (const auto* f = asFloat(value)) material.emissive_strength = *f;
    } else if (name == "clearcoat") {
      if (const auto* f = asFloat(value)) material.clearcoat = *f;
    } else if (name == "clearcoat_roughness") {
      if (const auto* f = asFloat(value)) material.clearcoat_roughness = *f;
    } else if (name == "sheen_color") {
      if (const auto* color = asColor(value)) material.sheen_color = *color;
    } else if (name == "sheen_roughness") {
      if (const auto* f = asFloat(value)) material.sheen_roughness = *f;
    } else if (name == "anisotropy") {
      if (const auto* f = asFloat(value)) material.anisotropy = *f;
    } else if (name == "transmission") {
      if (const auto* f = asFloat(value)) material.transmission = *f;
    } else if (name == "ior") {
      if (const auto* f = asFloat(value)) material.ior = *f;
    } else if (name == "thickness") {
      if (const auto* f = asFloat(value)) material.thickness = *f;
    } else if (name == "attenuation_distance") {
      if (const auto* f = asFloat(value)) material.attenuation_distance = *f;
    } else if (name == "attenuation_color") {
      if (const auto* color = asColor(value)) material.attenuation_color = *color;
    } else if (name == "unlit") {
      if (const auto* b = asBool(value)) material.unlit = *b;
    } else if (name == "alpha_cutoff") {
      if (const auto* f = asFloat(value)) material.alpha_cutoff = *f;
    } else if (name == "alpha_softness") {
      if (const auto* f = asFloat(value)) material.alpha_softness = *f;
    } else if (name == "alpha_dither") {
      if (const auto* b = asBool(value)) material.alpha_dither = *b;
    } else if (name == "alpha_to_coverage") {
      if (const auto* b = asBool(value)) material.alpha_to_coverage = *b;
    } else if (name == "alpha_mode") {
      if (const auto* s = asString(value)) {
        if (*s == "opaque") {
          material.alpha_mode = MaterialDesc::AlphaMode::Opaque;
          material.transparent = false;
        } else if (*s == "masked") {
          material.alpha_mode = MaterialDesc::AlphaMode::Masked;
          material.transparent = false;
        } else if (*s == "blend") {
          material.alpha_mode = MaterialDesc::AlphaMode::Blend;
          material.transparent = true;
        }
      }
    } else if (name == "transparent") {
      if (const auto* b = asBool(value)) {
        material.transparent = *b;
        if (*b && material.alpha_mode == MaterialDesc::AlphaMode::Opaque) {
          material.alpha_mode = MaterialDesc::AlphaMode::Blend;
        }
      }
    } else if (name == "depth_test") {
      if (const auto* b = asBool(value)) material.depth_test = *b;
    } else if (name == "depth_write") {
      if (const auto* b = asBool(value)) material.depth_write = *b;
    } else if (name == "wireframe") {
      if (const auto* b = asBool(value)) material.wireframe = *b;
    } else if (name == "double_sided") {
      if (const auto* b = asBool(value)) material.double_sided = *b;
    } else if (name == "blend_mode") {
      if (const auto* s = asString(value)) {
        if (*s == "additive") {
          material.blend_mode = MaterialDesc::BlendMode::Additive;
        } else if (*s == "alpha") {
          material.blend_mode = MaterialDesc::BlendMode::Alpha;
        }
      }
    } else if (name == "analytic_sphere_normals") {
      if (const auto* b = asBool(value)) material.analytic_sphere_normals = *b;
    }
  }

  static void applyParameters(MaterialDesc& material,
                              const std::unordered_map<std::string, MaterialParameterValue>& params) {
    for (const auto& [name, value] : params) {
      applyParameter(material, name, value);
    }
  }

  std::optional<ResolvedMaterialDesc> resolveRecursive(const std::string& key,
                                                       uint32_t depth) const {
    if (depth > kMaxResolveDepth) {
      return std::nullopt;
    }

    if (const auto asset_it = assets_.find(key); asset_it != assets_.end()) {
      ResolvedMaterialDesc resolved{};
      resolved.pipeline = asset_it->second.pipeline;
      resolved.surface = asset_it->second.surface;
      resolved.params = asset_it->second.params;
      resolved.textures = asset_it->second.textures;
      resolved.material_asset_path = asset_it->second.material_asset_path;
      resolved.material_asset_index = asset_it->second.material_asset_index;
      resolved.imported_material = asset_it->second.imported_material;
      applyParameters(resolved.surface, resolved.params);
      return resolved;
    }

    const auto variant_it = variants_.find(key);
    if (variant_it == variants_.end() ||
        variant_it->second.base_material_key.empty()) {
      return std::nullopt;
    }

    auto base = resolveRecursive(variant_it->second.base_material_key, depth + 1);
    if (!base.has_value()) {
      return std::nullopt;
    }
    for (const auto& [name, value] : variant_it->second.params) {
      base->params[name] = value;
    }
    for (const auto& [name, value] : variant_it->second.textures) {
      base->textures[name] = value;
    }
    applyParameters(base->surface, variant_it->second.params);
    return base;
  }

  std::unordered_map<std::string, MaterialAssetDesc> assets_;
  std::unordered_map<std::string, MaterialVariantDesc> variants_;
  uint64_t version_ = 0;
};

}  // namespace karma::renderer
