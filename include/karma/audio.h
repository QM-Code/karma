#pragma once

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/world.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace karma::audio {

inline constexpr int kMaxAudioClipInstances = 256;
inline constexpr float kMaxAudioGain = 16.0f;
inline constexpr float kMinAudioPitch = 0.125f;
inline constexpr float kMaxAudioPitch = 8.0f;

/// Complete settings captured when a new playback voice starts.
struct AudioPlaybackOptions {
  glm::vec3 position{0.0f};
  float gain = 1.0f;
  float pitch = 1.0f;
  float min_distance = 1.0f;
  float max_distance = 20.0f;
  bool looping = false;
  bool spatialized = true;

  /// Returns whether all values are finite and within supported ranges.
  bool valid() const noexcept;
};

class AudioVoice;

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

  /// Starts a voice using an atomic snapshot of all playback settings.
  /// Returns an invalid handle when validation fails or the pool is exhausted.
  AudioVoice play(const AudioPlaybackOptions& options = {}) const;

 private:
  friend class Audio;
  friend class AudioVoice;
  struct Impl;
  explicit AudioClip(std::shared_ptr<Impl> data);

  std::shared_ptr<Impl> data_;
};

/// \ingroup karma_media
/// Handle for one active playback voice.
///
/// Handles are copyable and do not stop playback when destroyed. They retain
/// the clip backend so voices remain safe when their creating `Audio` facade is
/// destroyed first.
class AudioVoice {
 public:
  AudioVoice() = default;

  /// Returns whether this handle identifies a voice, active or completed.
  explicit operator bool() const noexcept;
  /// Returns true while the voice is actively playing.
  bool isPlaying() const;
  /// Stops this voice. Returns false if it had already completed.
  bool stop() const;
  /// Updates the world position used for spatial playback.
  bool setPosition(const glm::vec3& position) const;

 private:
  friend class AudioClip;
  AudioVoice(std::shared_ptr<AudioClip::Impl> data, uint64_t voice_id);

  std::shared_ptr<AudioClip::Impl> data_;
  uint64_t voice_id_ = 0;
};

/// \ingroup karma_media
/// Audio facade owned by `EngineApp`.
class Audio {
 public:
  Audio();
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;
  Audio(Audio&&) noexcept;
  Audio& operator=(Audio&&) noexcept;

  /// Returns whether this build has an initialized playback backend.
  bool isAvailable() const noexcept;

  /// Loads or returns a cached clip. Invalid paths or pool sizes throw.
  AudioClip loadClip(const std::string& filepath, int max_instances = 5);
  /// Updates listener world position, rejecting non-finite values.
  bool setListenerPosition(const glm::vec3& position);
  /// Updates listener rotation after normalization.
  bool setListenerRotation(const glm::quat& rotation);

 private:
  std::shared_ptr<AudioClip::Impl> createClip(const std::string& filepath,
                                              int max_instances);

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
  ~AudioSystem() override;

  std::string_view name() const override { return "AudioSystem"; }
  void update(world::World& world, float dt) override;

 private:
  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  AudioClip& getClip(const std::string& key, int max_instances);

  struct ActiveVoice {
    AudioVoice voice;
    bool looping = false;
  };

  Audio& audio_;
  const assets::AssetRegistry* assets_ = nullptr;
  std::unordered_map<std::string, AudioClip> clip_cache_;
  std::unordered_map<uint64_t, bool> played_on_start_;
  std::unordered_map<uint64_t, std::vector<ActiveVoice>> active_voices_;
  bool warned_multiple_listeners_ = false;
  bool warned_no_listener_ = false;
};

}  // namespace karma::audio
