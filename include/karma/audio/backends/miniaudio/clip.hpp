#pragma once

#include "karma/audio/backend.hpp"

#include <miniaudio.h>
#include <vector>

namespace karma::audio_backend {

class MiniaudioClip final : public Clip {
 public:
  MiniaudioClip(ma_sound* stem, std::vector<ma_sound*> instances);
  ~MiniaudioClip() override;

  void play(const glm::vec3& position, float volume) override;
  void setSpatialization(bool enabled) override;
  void setDistanceRange(float min_distance, float max_distance) override;

 private:
  void release();

  bool spatialized_ = true;
  float min_distance_ = 1.0f;
  float max_distance_ = 20.0f;
  ma_sound* stem_ = nullptr;
  std::vector<ma_sound*> instances_;
  bool released_ = false;
};

}  // namespace karma::audio_backend
