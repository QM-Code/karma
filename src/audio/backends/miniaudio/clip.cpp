#include "karma/audio/backends/miniaudio/clip.hpp"

#include <spdlog/spdlog.h>

namespace karma::audio_backend {

MiniaudioClip::MiniaudioClip(ma_sound* stem, std::vector<ma_sound*> instances)
    : stem_(stem), instances_(std::move(instances)) {}

MiniaudioClip::~MiniaudioClip() {
  release();
}

void MiniaudioClip::play(const glm::vec3& position, float volume) {
  if (released_) {
    spdlog::warn("AudioClip: Attempted to play a released clip");
    return;
  }

  ma_sound* sound_to_play = nullptr;
  for (auto* sound : instances_) {
    if (!ma_sound_is_playing(sound)) {
      sound_to_play = sound;
      break;
    }
  }

  if (sound_to_play == nullptr) {
    spdlog::warn("AudioClip: No available sound instances");
    return;
  }

  ma_sound_set_spatialization_enabled(sound_to_play, spatialized_ ? MA_TRUE : MA_FALSE);
  if (spatialized_) {
    ma_sound_set_attenuation_model(sound_to_play, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(sound_to_play, min_distance_);
    ma_sound_set_max_distance(sound_to_play, max_distance_);
  } else {
    ma_sound_set_attenuation_model(sound_to_play, ma_attenuation_model_none);
  }
  ma_sound_stop(sound_to_play);
  ma_sound_seek_to_pcm_frame(sound_to_play, 0);
  ma_sound_set_position(sound_to_play, position.x, position.y, position.z);
  ma_sound_set_volume(sound_to_play, volume);
  ma_sound_start(sound_to_play);
}

void MiniaudioClip::release() {
  if (released_) {
    return;
  }

  for (auto* sound : instances_) {
    ma_sound_uninit(sound);
    delete sound;
  }
  instances_.clear();

  if (stem_ != nullptr) {
    ma_sound_uninit(stem_);
    delete stem_;
    stem_ = nullptr;
  }

  released_ = true;
}

void MiniaudioClip::setSpatialization(bool enabled) {
  spatialized_ = enabled;
}

void MiniaudioClip::setDistanceRange(float min_distance, float max_distance) {
  min_distance_ = min_distance;
  max_distance_ = max_distance;
}

}  // namespace karma::audio_backend
