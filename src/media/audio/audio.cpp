#include "karma/audio.h"

#include "private/audio/backend.hpp"

#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

namespace {

bool finiteVec3(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool finiteQuat(const glm::quat& value) {
  return std::isfinite(value.w) && std::isfinite(value.x) &&
         std::isfinite(value.y) && std::isfinite(value.z);
}

std::string buildCacheKey(const std::string& filepath, int max_instances) {
  return filepath + "#" + std::to_string(max_instances);
}

}  // namespace

namespace karma::audio {

bool AudioPlaybackOptions::valid() const noexcept {
  return finiteVec3(position) && std::isfinite(gain) && gain >= 0.0f &&
         gain <= kMaxAudioGain &&
         std::isfinite(pitch) && pitch >= kMinAudioPitch &&
         pitch <= kMaxAudioPitch && std::isfinite(min_distance) &&
         min_distance >= 0.0f && std::isfinite(max_distance) &&
         max_distance >= min_distance;
}

struct AudioClip::Impl {
  Impl(std::shared_ptr<audio::backend::Backend> backend,
       std::shared_ptr<audio::backend::Clip> clip)
      : backend(std::move(backend)), clip(std::move(clip)) {}

  // Declaration order keeps the backend alive until after its clip is released.
  std::shared_ptr<audio::backend::Backend> backend;
  std::shared_ptr<audio::backend::Clip> clip;
};

struct Audio::Impl {
  std::shared_ptr<audio::backend::Backend> backend;
  std::unordered_map<std::string, std::weak_ptr<AudioClip::Impl>> clip_cache;
  std::mutex clip_cache_mutex;
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {
  impl_->backend = audio::backend::createAudioBackend();
}

Audio::~Audio() = default;
Audio::Audio(Audio&&) noexcept = default;
Audio& Audio::operator=(Audio&&) noexcept = default;

bool Audio::isAvailable() const noexcept {
  return impl_ != nullptr && impl_->backend != nullptr;
}

std::shared_ptr<AudioClip::Impl> Audio::createClip(const std::string& filepath,
                                                   int max_instances) {
  if (!isAvailable()) {
    throw std::runtime_error("Audio: playback backend is not available");
  }

  audio::backend::ClipOptions options;
  options.max_instances = max_instances;
  auto clip = impl_->backend->loadClip(filepath, options);
  if (!clip) {
    throw std::runtime_error("Audio: backend returned an empty clip");
  }
  return std::make_shared<AudioClip::Impl>(impl_->backend, std::move(clip));
}

AudioClip Audio::loadClip(const std::string& filepath, int max_instances) {
  if (filepath.empty()) {
    throw std::invalid_argument("Audio: clip path must not be empty");
  }
  if (max_instances <= 0 || max_instances > kMaxAudioClipInstances) {
    throw std::invalid_argument("Audio: max_instances must be in [1, 256]");
  }
  if (!impl_) {
    throw std::runtime_error("Audio: facade was moved from");
  }

  const std::string cache_key = buildCacheKey(filepath, max_instances);
  std::lock_guard<std::mutex> lock(impl_->clip_cache_mutex);
  if (auto it = impl_->clip_cache.find(cache_key); it != impl_->clip_cache.end()) {
    if (auto cached = it->second.lock()) {
      return AudioClip(std::move(cached));
    }
    impl_->clip_cache.erase(it);
  }

  auto clip_data = createClip(filepath, max_instances);
  impl_->clip_cache.emplace(cache_key, clip_data);
  return AudioClip(std::move(clip_data));
}

bool Audio::setListenerPosition(const glm::vec3& position) {
  if (!isAvailable() || !finiteVec3(position)) {
    return false;
  }
  impl_->backend->setListenerPosition(position);
  return true;
}

bool Audio::setListenerRotation(const glm::quat& rotation) {
  if (!isAvailable() || !finiteQuat(rotation)) {
    return false;
  }

  const double length_squared =
      static_cast<double>(rotation.w) * rotation.w +
      static_cast<double>(rotation.x) * rotation.x +
      static_cast<double>(rotation.y) * rotation.y +
      static_cast<double>(rotation.z) * rotation.z;
  if (!std::isfinite(length_squared) || length_squared <= 1.0e-12) {
    return false;
  }

  const float inverse_length =
      static_cast<float>(1.0 / std::sqrt(length_squared));
  impl_->backend->setListenerRotation(glm::quat{
      rotation.w * inverse_length,
      rotation.x * inverse_length,
      rotation.y * inverse_length,
      rotation.z * inverse_length,
  });
  return true;
}

AudioClip::AudioClip(std::shared_ptr<Impl> data) : data_(std::move(data)) {}

AudioVoice AudioClip::play(const AudioPlaybackOptions& options) const {
  if (!data_ || !data_->clip) {
    return {};
  }
  if (!options.valid()) {
    spdlog::warn("AudioClip: rejected invalid playback options");
    return {};
  }

  const uint64_t voice_id = data_->clip->play(options);
  return voice_id != 0 ? AudioVoice(data_, voice_id) : AudioVoice{};
}

AudioVoice::AudioVoice(std::shared_ptr<AudioClip::Impl> data, uint64_t voice_id)
    : data_(std::move(data)), voice_id_(voice_id) {}

AudioVoice::operator bool() const noexcept {
  return data_ != nullptr && data_->clip != nullptr && voice_id_ != 0;
}

bool AudioVoice::isPlaying() const {
  return static_cast<bool>(*this) && data_->clip->isPlaying(voice_id_);
}

bool AudioVoice::stop() const {
  return static_cast<bool>(*this) && data_->clip->stop(voice_id_);
}

bool AudioVoice::setPosition(const glm::vec3& position) const {
  return static_cast<bool>(*this) && finiteVec3(position) &&
         data_->clip->setPosition(voice_id_, position);
}

}  // namespace karma::audio
