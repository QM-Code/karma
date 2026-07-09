#include "private/audio/backends/miniaudio/clip.hpp"

#include "karma/audio.h"

#include <algorithm>
#include <cmath>

namespace {

bool finiteVec3(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

}  // namespace

namespace karma::audio::backend {

MiniaudioClip::MiniaudioClip(std::vector<ma_sound*> instances) {
  instances_.reserve(instances.size());
  for (ma_sound* sound : instances) {
    instances_.push_back(Instance{.sound = sound});
  }
}

MiniaudioClip::~MiniaudioClip() {
  release();
}

uint64_t MiniaudioClip::nextVoiceId() {
  for (;;) {
    const uint64_t candidate = next_voice_id_++;
    if (next_voice_id_ == 0) {
      next_voice_id_ = 1;
    }
    if (candidate == 0) {
      continue;
    }
    const bool in_use = std::any_of(
        instances_.begin(), instances_.end(), [candidate](const Instance& instance) {
          return instance.voice_id == candidate;
        });
    if (!in_use) {
      return candidate;
    }
  }
}

uint64_t MiniaudioClip::play(const AudioPlaybackOptions& options) {
  if (!options.valid()) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (released_) {
    return 0;
  }

  const auto it = std::find_if(
      instances_.begin(), instances_.end(), [](const Instance& instance) {
        return instance.sound != nullptr && !ma_sound_is_playing(instance.sound);
      });
  if (it == instances_.end()) {
    return 0;
  }

  ma_sound_stop(it->sound);
  if (ma_sound_seek_to_pcm_frame(it->sound, 0) != MA_SUCCESS) {
    return 0;
  }

  ma_sound_set_spatialization_enabled(it->sound,
                                      options.spatialized ? MA_TRUE : MA_FALSE);
  ma_sound_set_attenuation_model(
      it->sound,
      options.spatialized ? ma_attenuation_model_linear
                          : ma_attenuation_model_none);
  if (options.spatialized) {
    ma_sound_set_min_distance(it->sound, options.min_distance);
    ma_sound_set_max_distance(it->sound, options.max_distance);
  }
  ma_sound_set_position(it->sound,
                        options.position.x,
                        options.position.y,
                        options.position.z);
  ma_sound_set_volume(it->sound, options.gain);
  ma_sound_set_pitch(it->sound, options.pitch);
  ma_sound_set_looping(it->sound, options.looping ? MA_TRUE : MA_FALSE);

  const uint64_t voice_id = nextVoiceId();
  it->voice_id = voice_id;
  if (ma_sound_start(it->sound) != MA_SUCCESS) {
    it->voice_id = 0;
    return 0;
  }
  return voice_id;
}

bool MiniaudioClip::isPlaying(uint64_t voice_id) const {
  if (voice_id == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(
      instances_.begin(), instances_.end(), [voice_id](const Instance& instance) {
        return instance.voice_id == voice_id;
      });
  return it != instances_.end() && it->sound != nullptr &&
         ma_sound_is_playing(it->sound);
}

bool MiniaudioClip::stop(uint64_t voice_id) {
  if (voice_id == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(
      instances_.begin(), instances_.end(), [voice_id](const Instance& instance) {
        return instance.voice_id == voice_id;
      });
  if (it == instances_.end() || it->sound == nullptr) {
    return false;
  }
  const bool stopped = ma_sound_stop(it->sound) == MA_SUCCESS;
  if (stopped) {
    it->voice_id = 0;
  }
  return stopped;
}

bool MiniaudioClip::setPosition(uint64_t voice_id,
                                const glm::vec3& position) {
  if (voice_id == 0 || !finiteVec3(position)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(
      instances_.begin(), instances_.end(), [voice_id](const Instance& instance) {
        return instance.voice_id == voice_id;
      });
  if (it == instances_.end() || it->sound == nullptr ||
      !ma_sound_is_playing(it->sound)) {
    return false;
  }
  ma_sound_set_position(it->sound, position.x, position.y, position.z);
  return true;
}

void MiniaudioClip::release() {
  if (released_) {
    return;
  }

  for (const Instance& instance : instances_) {
    if (instance.sound != nullptr) {
      ma_sound_uninit(instance.sound);
      delete instance.sound;
    }
  }
  instances_.clear();
  released_ = true;
}

}  // namespace karma::audio::backend
