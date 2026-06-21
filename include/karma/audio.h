#pragma once

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/world.h"



#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace karma::audio {

/// \ingroup karma_media
/// Shared audio clip handle.
///
/// Clips are cached by `Audio` and can play overlapping instances up to their
/// configured backend limit.
class AudioClip {
 public:
  AudioClip() = delete;
  AudioClip(const AudioClip&) = default;
  AudioClip(AudioClip&&) noexcept = default;
  AudioClip& operator=(const AudioClip&) = default;
  AudioClip& operator=(AudioClip&&) noexcept = default;
  ~AudioClip() = default;

  /// Plays the clip at a world position using current spatial defaults.
  void play(const glm::vec3& position, float volume = 1.0f) const;
  /// Plays the clip with explicit spatial distance settings.
  void playSpatial(const glm::vec3& position, float volume,
                   float min_distance, float max_distance) const;
  /// Sets default spatialization for future `play()` calls.
  void setSpatialDefaults(bool spatialized, float min_distance, float max_distance);

 private:
  friend class Audio;
  struct Impl;
  explicit AudioClip(std::shared_ptr<Impl> data);

  std::shared_ptr<Impl> data_;
  bool spatialized_ = true;
  float min_distance_ = 1.0f;
  float max_distance_ = 20.0f;
};

/// \ingroup karma_media
/// Audio facade owned by `EngineApp`.
class Audio {
 public:
  Audio();
  ~Audio();

  /// Loads or returns a cached clip.
  AudioClip loadClip(const std::string& filepath, int maxInstances = 5);
  /// Updates listener world position.
  void setListenerPosition(const glm::vec3& position);
  /// Updates listener world rotation.
  void setListenerRotation(const glm::quat& rotation);

 private:
  std::shared_ptr<AudioClip::Impl> createClip(const std::string& filepath,
                                              int maxInstances);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::audio


#include <string_view>
#include <unordered_map>


namespace karma::assets {
class AssetRegistry;
}  // namespace karma::assets

namespace karma::audio {

/// \ingroup karma_media
/// ECS audio source/listener system.
///
/// Consumes `AudioSourceComponent`, `AudioListenerComponent`, and transforms to
/// drive backend clip playback.
class AudioSystem final : public world::ISystem {
 public:
  explicit AudioSystem(Audio& audio, const assets::AssetRegistry* assets = nullptr)
      : audio_(audio), assets_(assets) {}

  std::string_view name() const override { return "AudioSystem"; }
  void update(world::World& world, float dt) override;

 private:
  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  AudioClip& getClip(const std::string& key, int max_instances);

  Audio& audio_;
  const assets::AssetRegistry* assets_ = nullptr;
  std::unordered_map<std::string, AudioClip> clip_cache_;
  std::unordered_map<uint64_t, bool> played_on_start_;
  bool warned_multiple_listeners_ = false;
  bool warned_no_listener_ = false;
};

}  // namespace karma::audio
