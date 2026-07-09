#pragma once

#include "private/audio/backend.hpp"

#include <miniaudio.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace karma::audio::backend {

/// \ingroup karma_media
/// miniaudio clip with a reusable instance pool.
class MiniaudioClip final : public Clip {
 public:
  explicit MiniaudioClip(std::vector<ma_sound*> instances);
  ~MiniaudioClip() override;

  uint64_t play(const AudioPlaybackOptions& options) override;
  bool isPlaying(uint64_t voice_id) const override;
  bool stop(uint64_t voice_id) override;
  bool setPosition(uint64_t voice_id, const glm::vec3& position) override;

 private:
  struct Instance {
    ma_sound* sound = nullptr;
    uint64_t voice_id = 0;
  };

  void release();
  uint64_t nextVoiceId();

  mutable std::mutex mutex_;
  std::vector<Instance> instances_;
  uint64_t next_voice_id_ = 1;
  bool released_ = false;
};

}  // namespace karma::audio::backend
