#pragma once

#include "private/audio/backend.hpp"
#include "private/audio/spatial.hpp"

#include <cstdint>
#include <mutex>
#include <vector>

namespace karma::audio::backend {

/// \ingroup karma_media
/// SDL audio clip mixed into the backend stream.
class SdlAudioClip final : public Clip {
 public:
  SdlAudioClip(std::vector<float> samples, int channels, int max_instances);
  ~SdlAudioClip() override = default;

  uint64_t play(const AudioPlaybackOptions& options) override;
  bool isPlaying(uint64_t voice_id) const override;
  bool stop(uint64_t voice_id) override;
  bool setPosition(uint64_t voice_id, const glm::vec3& position) override;
  void mix(float* output, int frames, int channels,
           const ListenerState& listener);

 private:
  struct Instance {
    AudioPlaybackOptions options;
    double frame_cursor = 0.0;
    uint64_t voice_id = 0;
  };

  uint64_t nextVoiceId();

  mutable std::mutex mutex_;
  std::vector<float> samples_;
  std::vector<Instance> instances_;
  int channels_ = 2;
  int max_instances_ = 1;
  uint64_t next_voice_id_ = 1;
};

}  // namespace karma::audio::backend
