#include "karma/audio/backends/sdl/clip.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace karma::audio_backend {

SdlAudioClip::SdlAudioClip(std::vector<float> samples, int channels, int max_instances)
    : samples_(std::move(samples)),
      channels_(channels),
      max_instances_(max_instances) {}

void SdlAudioClip::play(const glm::vec3&, float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (static_cast<int>(instances_.size()) >= max_instances_) {
    spdlog::warn("AudioClip: No available sound instances");
    return;
  }

  Instance instance;
  instance.frame_offset = 0;
  instance.volume = volume;
  instances_.push_back(instance);
}

void SdlAudioClip::mix(float* output, int frames, int channels) {
  if (channels_ != channels) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (instances_.empty() || samples_.empty()) {
    return;
  }

  const int total_frames = static_cast<int>(samples_.size() / channels_);
  for (auto it = instances_.begin(); it != instances_.end();) {
    int offset = it->frame_offset;
    const float volume = it->volume;
    if (offset >= total_frames) {
      it = instances_.erase(it);
      continue;
    }

    const int frames_to_mix = std::min(frames, total_frames - offset);
    const int start_index = offset * channels_;
    for (int frame = 0; frame < frames_to_mix; ++frame) {
      const int sample_index = start_index + frame * channels_;
      const int out_index = frame * channels_;
      for (int ch = 0; ch < channels_; ++ch) {
        output[out_index + ch] += samples_[sample_index + ch] * volume;
      }
    }

    it->frame_offset += frames_to_mix;
    if (it->frame_offset >= total_frames) {
      it = instances_.erase(it);
    } else {
      ++it;
    }
  }
}

void SdlAudioClip::setSpatialization(bool) {}

void SdlAudioClip::setDistanceRange(float, float) {}

}  // namespace karma::audio_backend
