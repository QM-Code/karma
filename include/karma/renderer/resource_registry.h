#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "karma/renderer/material.h"
#include "karma/renderer/render_target.h"
#include "karma/renderer/shader.h"
#include "karma/renderer/skybox.h"
#include "karma/renderer/texture.h"

namespace karma::renderer {

using ResourceId = uint32_t;

class ResourceRegistry {
 public:
  ResourceId registerTexture(const std::string& key, TextureDesc desc) {
    const ResourceId id = next_id_++;
    textures_[id] = Entry<TextureDesc>{key, std::move(desc)};
    return id;
  }

  ResourceId registerMaterial(const std::string& key, MaterialResourceDesc desc) {
    const ResourceId id = next_id_++;
    materials_[id] = Entry<MaterialResourceDesc>{key, std::move(desc)};
    return id;
  }

  ResourceId registerRenderTarget(const std::string& key, RenderTargetDesc desc) {
    const ResourceId id = next_id_++;
    render_targets_[id] = Entry<RenderTargetDesc>{key, std::move(desc)};
    return id;
  }

  ResourceId registerShader(const std::string& key, ShaderDesc desc) {
    const ResourceId id = next_id_++;
    shaders_[id] = Entry<ShaderDesc>{key, std::move(desc)};
    return id;
  }

  ResourceId registerSkybox(const std::string& key, SkyboxDesc desc) {
    const ResourceId id = next_id_++;
    skyboxes_[id] = Entry<SkyboxDesc>{key, std::move(desc)};
    return id;
  }

  const TextureDesc* findTexture(const std::string& key) const {
    return findByKey(textures_, key);
  }

  const MaterialResourceDesc* findMaterial(const std::string& key) const {
    return findByKey(materials_, key);
  }

  const RenderTargetDesc* findRenderTarget(const std::string& key) const {
    return findByKey(render_targets_, key);
  }

  const ShaderDesc* findShader(const std::string& key) const {
    return findByKey(shaders_, key);
  }

  const SkyboxDesc* findSkybox(const std::string& key) const {
    return findByKey(skyboxes_, key);
  }

 private:
  template <typename T>
  struct Entry {
    std::string key;
    T desc;
  };

  template <typename T>
  const T* findByKey(const std::unordered_map<ResourceId, Entry<T>>& map,
                     const std::string& key) const {
    for (const auto& [id, entry] : map) {
      if (entry.key == key) {
        return &entry.desc;
      }
    }
    return nullptr;
  }

  ResourceId next_id_ = 1;
  std::unordered_map<ResourceId, Entry<TextureDesc>> textures_;
  std::unordered_map<ResourceId, Entry<MaterialResourceDesc>> materials_;
  std::unordered_map<ResourceId, Entry<RenderTargetDesc>> render_targets_;
  std::unordered_map<ResourceId, Entry<ShaderDesc>> shaders_;
  std::unordered_map<ResourceId, Entry<SkyboxDesc>> skyboxes_;
};

}  // namespace karma::renderer
