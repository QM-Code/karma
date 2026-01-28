#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "karma/audio/backend.hpp"

namespace karma::audio {

class AudioClip {
 public:
  AudioClip() = delete;
  AudioClip(const AudioClip&) = default;
  AudioClip(AudioClip&&) noexcept = default;
  AudioClip& operator=(const AudioClip&) = default;
  AudioClip& operator=(AudioClip&&) noexcept = default;
  ~AudioClip() = default;

  void play(const glm::vec3& position, float volume = 1.0f) const;
  void playSpatial(const glm::vec3& position, float volume,
                   float min_distance, float max_distance) const;
  void setSpatialDefaults(bool spatialized, float min_distance, float max_distance);

 private:
  friend class Audio;
  explicit AudioClip(std::shared_ptr<audio_backend::Clip> data);

  std::shared_ptr<audio_backend::Clip> data_;
  bool spatialized_ = true;
  float min_distance_ = 1.0f;
  float max_distance_ = 20.0f;
};

class Audio {
 public:
  Audio();
  ~Audio();

  AudioClip loadClip(const std::string& filepath, int maxInstances = 5);
  void setListenerPosition(const glm::vec3& position);
  void setListenerRotation(const glm::quat& rotation);

 private:
  std::shared_ptr<audio_backend::Clip> createClip(const std::string& filepath,
                                                  int maxInstances);

  std::unique_ptr<audio_backend::Backend> backend_;
  std::unordered_map<std::string, std::weak_ptr<audio_backend::Clip>> clip_cache_;
};

}  // namespace karma::audio
