#include "private/audio/backends/sdl/clip.hpp"

#include <algorithm>
#include <cmath>

namespace {

bool finiteVec3(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

}  // namespace

namespace karma::audio::backend {

SdlAudioClip::SdlAudioClip(std::vector<float> samples,
                           int channels,
                           int max_instances)
    : samples_(std::move(samples)),
      channels_(std::max(1, channels)),
      max_instances_(std::clamp(max_instances, 1, kMaxAudioClipInstances)) {
  instances_.reserve(static_cast<std::size_t>(max_instances_));
}

uint64_t SdlAudioClip::nextVoiceId() {
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

uint64_t SdlAudioClip::play(const AudioPlaybackOptions& options) {
  if (!options.valid() || samples_.empty()) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (static_cast<int>(instances_.size()) >= max_instances_) {
    return 0;
  }

  const uint64_t voice_id = nextVoiceId();
  instances_.push_back(Instance{
      .options = options,
      .frame_cursor = 0.0,
      .voice_id = voice_id,
  });
  return voice_id;
}

bool SdlAudioClip::isPlaying(uint64_t voice_id) const {
  if (voice_id == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(
      instances_.begin(), instances_.end(), [voice_id](const Instance& instance) {
        return instance.voice_id == voice_id;
      });
}

bool SdlAudioClip::stop(uint64_t voice_id) {
  if (voice_id == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(
      instances_.begin(), instances_.end(), [voice_id](const Instance& instance) {
        return instance.voice_id == voice_id;
      });
  if (it == instances_.end()) {
    return false;
  }
  instances_.erase(it);
  return true;
}

bool SdlAudioClip::setPosition(uint64_t voice_id, const glm::vec3& position) {
  if (voice_id == 0 || !finiteVec3(position)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(
      instances_.begin(), instances_.end(), [voice_id](const Instance& instance) {
        return instance.voice_id == voice_id;
      });
  if (it == instances_.end()) {
    return false;
  }
  it->options.position = position;
  return true;
}

void SdlAudioClip::mix(float* output,
                       int frames,
                       int channels,
                       const ListenerState& listener) {
  if (output == nullptr || frames <= 0 || channels <= 0 ||
      channels_ != channels) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (instances_.empty() || samples_.empty()) {
    return;
  }

  const std::size_t total_frames = samples_.size() /
                                   static_cast<std::size_t>(channels_);
  if (total_frames == 0) {
    instances_.clear();
    return;
  }

  for (auto it = instances_.begin(); it != instances_.end();) {
    const StereoGains gains = stereoGains(it->options, listener);
    int output_frame = 0;
    while (output_frame < frames) {
      if (it->frame_cursor >= static_cast<double>(total_frames)) {
        if (!it->options.looping) {
          break;
        }
        it->frame_cursor = std::fmod(
            it->frame_cursor, static_cast<double>(total_frames));
      }

      const std::size_t source_frame =
          static_cast<std::size_t>(it->frame_cursor);
      const std::size_t next_frame = source_frame + 1u < total_frames
                                         ? source_frame + 1u
                                         : (it->options.looping ? 0u : source_frame);
      const float fraction = static_cast<float>(
          it->frame_cursor - static_cast<double>(source_frame));
      const std::size_t source_index =
          source_frame * static_cast<std::size_t>(channels_);
      const std::size_t next_index =
          next_frame * static_cast<std::size_t>(channels_);
      const std::size_t output_index =
          static_cast<std::size_t>(output_frame * channels_);

      if (channels_ == 2 && it->options.spatialized) {
        const float left = std::lerp(samples_[source_index],
                                     samples_[next_index],
                                     fraction);
        const float right = std::lerp(samples_[source_index + 1u],
                                      samples_[next_index + 1u],
                                      fraction);
        const float mono = (left + right) * 0.5f;
        output[output_index] += mono * gains.left;
        output[output_index + 1u] += mono * gains.right;
      } else {
        const float gain = channels_ == 2
                               ? 0.0f
                               : std::max(gains.left, gains.right);
        for (int channel = 0; channel < channels_; ++channel) {
          const std::size_t channel_index = static_cast<std::size_t>(channel);
          const float sample = std::lerp(samples_[source_index + channel_index],
                                         samples_[next_index + channel_index],
                                         fraction);
          const float channel_gain = channels_ == 2
                                         ? (channel == 0 ? gains.left : gains.right)
                                         : gain;
          output[output_index + channel_index] += sample * channel_gain;
        }
      }

      it->frame_cursor += static_cast<double>(it->options.pitch);
      ++output_frame;
    }

    if (!it->options.looping &&
        it->frame_cursor >= static_cast<double>(total_frames)) {
      it = instances_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace karma::audio::backend
