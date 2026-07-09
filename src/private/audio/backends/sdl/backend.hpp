#pragma once

#include "private/audio/backend.hpp"
#include "private/audio/spatial.hpp"

#include <SDL3/SDL_audio.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace karma::audio::backend {

class SdlAudioClip;

/// \ingroup karma_media
/// SDL audio implementation of the audio backend interface.
class SdlAudioBackend final : public Backend {
 public:
  SdlAudioBackend();
  ~SdlAudioBackend() override;

  std::shared_ptr<Clip> loadClip(const std::string& filepath,
                                 const ClipOptions& options) override;
  void setListenerPosition(const glm::vec3& position) override;
  void setListenerRotation(const glm::quat& rotation) override;

 private:
  static constexpr int kMixBufferFrames = 4096;
  static constexpr int kOutputChannels = 2;

  static void SDLCALL AudioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                          int additional_amount, int total_amount);
  void mixAudio(float* output, int frames);

  SDL_AudioStream* stream_ = nullptr;
  SDL_AudioSpec device_spec_{};
  std::mutex mutex_;
  std::vector<std::weak_ptr<SdlAudioClip>> clips_;
  ListenerState listener_{};
  std::array<float, kMixBufferFrames * kOutputChannels> mix_buffer_{};
  std::atomic<uint64_t> stream_write_failures_{0};
};

}  // namespace karma::audio::backend
