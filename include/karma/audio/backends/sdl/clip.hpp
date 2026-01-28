#pragma once

#include "karma/audio/backend.hpp"

#include <mutex>
#include <vector>

namespace karma::audio_backend {

class SdlAudioClip final : public Clip {
 public:
  SdlAudioClip(std::vector<float> samples, int channels, int max_instances);
  ~SdlAudioClip() override = default;

  void play(const glm::vec3& position, float volume) override;
  void setSpatialization(bool enabled) override;
  void setDistanceRange(float min_distance, float max_distance) override;
  void mix(float* output, int frames, int channels);

 private:
  struct Instance {
    int frame_offset = 0;
    float volume = 1.0f;
  };

  std::mutex mutex_;
  std::vector<float> samples_;
  std::vector<Instance> instances_;
  int channels_ = 2;
  int max_instances_ = 1;
};

}  // namespace karma::audio_backend
