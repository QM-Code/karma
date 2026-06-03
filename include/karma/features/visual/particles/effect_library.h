#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "karma/world/components/particle_emitter.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::particles {

/// \ingroup karma_particles
/// Resolved particle effect template and optional texture alias.
struct ParticleEffectDesc {
  components::ParticleEmitterComponent emitter{};
  std::string texture_key;
};

/// \ingroup karma_particles
/// Texture alias registration entry.
struct ParticleTextureAliasRegistration {
  std::string_view key;
  renderer::TextureId texture = renderer::kInvalidTexture;
};

/// \ingroup karma_particles
/// File-backed particle effect registration entry.
struct ParticleEffectFileRegistration {
  std::string_view key;
  std::filesystem::path path;
};

/// \ingroup karma_particles
/// Registry and hot-reload owner for particle effect templates.
///
/// `.kpeffect` files are parsed into emitter templates. `ParticleSystem`
/// observes `version()` to reapply effect bindings after reloads.
class ParticleLibrary {
 public:
  ParticleLibrary() = default;

  /// Registers an in-memory effect template.
  void registerEffect(const std::string& key, ParticleEffectDesc desc);
  /// Registers an emitter template without a texture alias.
  void registerEmitterTemplate(const std::string& key,
                               components::ParticleEmitterComponent emitter);
  /// Registers and parses a file-backed effect.
  bool registerEffectFile(const std::string& key, const std::filesystem::path& path);
  /// Removes an effect registration.
  void unregisterEffect(const std::string& key);
  /// Clears effects and file records.
  void clear();

  /// Registers a texture alias used by `.kpeffect` files.
  void registerTextureAlias(const std::string& key, renderer::TextureId texture);
  /// Registers several texture aliases.
  void registerTextureAliases(
      std::initializer_list<ParticleTextureAliasRegistration> aliases);
  /// Removes a texture alias.
  void unregisterTextureAlias(const std::string& key);
  /// Clears texture aliases.
  void clearTextureAliases();
  /// Resolves a texture alias to a renderer texture handle.
  renderer::TextureId resolveTextureAlias(const std::string& key) const;

  /// Registers several file-backed effects.
  bool registerEffectFiles(std::initializer_list<ParticleEffectFileRegistration> effects);

  /// Polls file-backed effects for hot reload.
  void update();

  /// Finds a resolved effect by key.
  const ParticleEffectDesc* find(const std::string& key) const;
  /// Finds an emitter template by key.
  const components::ParticleEmitterComponent* findEmitterTemplate(
      const std::string& key) const;
  /// Instantiates an emitter template into `out_emitter`.
  bool instantiateEmitter(const std::string& key,
                          components::ParticleEmitterComponent& out_emitter) const;
  /// Instantiates and returns an emitter template.
  std::optional<components::ParticleEmitterComponent> instantiateEmitter(
      const std::string& key) const;

  /// Monotonic version incremented when registrations or reloads change.
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
