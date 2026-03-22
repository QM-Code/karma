#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "karma/components/particle_emitter.h"
#include "karma/renderer/types.h"

namespace karma::particles {

struct ParticleEffectDesc {
  components::ParticleEmitterComponent emitter{};
  std::string texture_key;
};

struct ParticleTextureAliasRegistration {
  std::string_view key;
  renderer::TextureId texture = renderer::kInvalidTexture;
};

struct ParticleEffectFileRegistration {
  std::string_view key;
  std::filesystem::path path;
};

class ParticleLibrary {
 public:
  ParticleLibrary() = default;

  void registerEffect(const std::string& key, ParticleEffectDesc desc);
  void registerEmitterTemplate(const std::string& key,
                               components::ParticleEmitterComponent emitter);
  bool registerEffectFile(const std::string& key, const std::filesystem::path& path);
  void unregisterEffect(const std::string& key);
  void clear();

  void registerTextureAlias(const std::string& key, renderer::TextureId texture);
  void registerTextureAliases(
      std::initializer_list<ParticleTextureAliasRegistration> aliases);
  void unregisterTextureAlias(const std::string& key);
  void clearTextureAliases();
  renderer::TextureId resolveTextureAlias(const std::string& key) const;

  bool registerEffectFiles(std::initializer_list<ParticleEffectFileRegistration> effects);

  void update();

  const ParticleEffectDesc* find(const std::string& key) const;
  const components::ParticleEmitterComponent* findEmitterTemplate(
      const std::string& key) const;
  bool instantiateEmitter(const std::string& key,
                          components::ParticleEmitterComponent& out_emitter) const;
  std::optional<components::ParticleEmitterComponent> instantiateEmitter(
      const std::string& key) const;

  uint64_t version() const {
    return version_;
  }

 private:
  struct EffectFileRecord {
    std::filesystem::path path;
    std::filesystem::file_time_type last_write_time{};
  };

  bool reloadEffectFile(const std::string& key, EffectFileRecord& record);
  bool parseEffectFile(const std::filesystem::path& path, ParticleEffectDesc& out_desc) const;

  std::unordered_map<std::string, ParticleEffectDesc> effects_;
  std::unordered_map<std::string, EffectFileRecord> effect_files_;
  std::unordered_map<std::string, renderer::TextureId> texture_aliases_;
  uint64_t version_ = 0;
  std::chrono::steady_clock::time_point next_poll_time_{};
  std::chrono::milliseconds poll_interval_{250};
};

}  // namespace karma::particles
