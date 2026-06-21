#include "karma/audio.h"

#include "private/audio/backend.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

namespace {
std::string buildCacheKey(const std::string& filepath, int max_instances) {
  return filepath + "#" + std::to_string(max_instances);
}
}

namespace karma::audio {

struct AudioClip::Impl {
  explicit Impl(std::shared_ptr<audio::backend::Clip> clip)
      : clip(std::move(clip)) {}

  std::shared_ptr<audio::backend::Clip> clip;
};

struct Audio::Impl {
  std::unique_ptr<audio::backend::Backend> backend;
  std::unordered_map<std::string, std::weak_ptr<AudioClip::Impl>> clip_cache;
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {
  impl_->backend = audio::backend::CreateAudioBackend();
}

Audio::~Audio() {
  if (impl_) {
    impl_->clip_cache.clear();
  }
}

std::shared_ptr<AudioClip::Impl> Audio::createClip(const std::string& filepath,
                                                   int maxInstances) {
  if (!impl_ || !impl_->backend) {
    throw std::runtime_error("Audio: Backend not initialized");
  }

  audio::backend::ClipOptions options;
  options.max_instances = maxInstances;
  return std::make_shared<AudioClip::Impl>(impl_->backend->loadClip(filepath, options));
}

AudioClip Audio::loadClip(const std::string& filepath, int maxInstances) {
  const std::string cache_key = buildCacheKey(filepath, maxInstances);

  if (auto it = impl_->clip_cache.find(cache_key); it != impl_->clip_cache.end()) {
    if (auto cached = it->second.lock()) {
      return AudioClip(std::move(cached));
    }
  }

  auto clip_data = createClip(filepath, maxInstances);
  impl_->clip_cache[cache_key] = clip_data;
  return AudioClip(std::move(clip_data));
}

void Audio::setListenerPosition(const glm::vec3& position) {
  if (impl_ && impl_->backend) {
    impl_->backend->setListenerPosition(position);
  }
}

void Audio::setListenerRotation(const glm::quat& rotation) {
  if (impl_ && impl_->backend) {
    impl_->backend->setListenerRotation(rotation);
  }
}

AudioClip::AudioClip(std::shared_ptr<Impl> data)
    : data_(std::move(data)) {}

void AudioClip::play(const glm::vec3& position, float volume) const {
  if (!data_ || !data_->clip) {
    spdlog::error("AudioClip: Attempted to play an uninitialized clip");
    return;
  }

  data_->clip->setSpatialization(false);
  data_->clip->play(position, volume);
}

void AudioClip::setSpatialDefaults(bool spatialized, float min_distance, float max_distance) {
  spatialized_ = spatialized;
  min_distance_ = min_distance;
  max_distance_ = max_distance;
}

void AudioClip::playSpatial(const glm::vec3& position, float volume,
                            float min_distance, float max_distance) const {
  if (!data_ || !data_->clip) {
    spdlog::error("AudioClip: Attempted to play an uninitialized clip");
    return;
  }
  if (!spatialized_) {
    data_->clip->play(position, volume);
    return;
  }
  data_->clip->setSpatialization(true);
  data_->clip->setDistanceRange(min_distance, max_distance);
  data_->clip->play(position, volume);
}

}  // namespace karma::audio
