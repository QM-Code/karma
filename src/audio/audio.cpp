#include "karma/audio/audio.h"

#include <stdexcept>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace {
std::string buildCacheKey(const std::string& filepath, int max_instances) {
  return filepath + "#" + std::to_string(max_instances);
}
}

namespace karma::audio {

Audio::Audio() {
  backend_ = audio_backend::CreateAudioBackend();
}

Audio::~Audio() {
  clip_cache_.clear();
}

std::shared_ptr<audio_backend::Clip> Audio::createClip(const std::string& filepath,
                                                       int maxInstances) {
  if (!backend_) {
    throw std::runtime_error("Audio: Backend not initialized");
  }

  audio_backend::ClipOptions options;
  options.max_instances = maxInstances;
  return backend_->loadClip(filepath, options);
}

AudioClip Audio::loadClip(const std::string& filepath, int maxInstances) {
  const std::string cache_key = buildCacheKey(filepath, maxInstances);

  if (auto it = clip_cache_.find(cache_key); it != clip_cache_.end()) {
    if (auto cached = it->second.lock()) {
      return AudioClip(std::move(cached));
    }
  }

  auto clip_data = createClip(filepath, maxInstances);
  clip_cache_[cache_key] = clip_data;
  return AudioClip(std::move(clip_data));
}

void Audio::setListenerPosition(const glm::vec3& position) {
  if (backend_) {
    backend_->setListenerPosition(position);
  }
}

void Audio::setListenerRotation(const glm::quat& rotation) {
  if (backend_) {
    backend_->setListenerRotation(rotation);
  }
}

AudioClip::AudioClip(std::shared_ptr<audio_backend::Clip> data)
    : data_(std::move(data)) {}

void AudioClip::play(const glm::vec3& position, float volume) const {
  if (!data_) {
    spdlog::error("AudioClip: Attempted to play an uninitialized clip");
    return;
  }

  data_->setSpatialization(false);
  data_->play(position, volume);
}

void AudioClip::setSpatialDefaults(bool spatialized, float min_distance, float max_distance) {
  spatialized_ = spatialized;
  min_distance_ = min_distance;
  max_distance_ = max_distance;
}

void AudioClip::playSpatial(const glm::vec3& position, float volume,
                            float min_distance, float max_distance) const {
  if (!data_) {
    spdlog::error("AudioClip: Attempted to play an uninitialized clip");
    return;
  }
  if (!spatialized_) {
    data_->play(position, volume);
    return;
  }
  data_->setSpatialization(true);
  data_->setDistanceRange(min_distance, max_distance);
  data_->play(position, volume);
}

}  // namespace karma::audio
