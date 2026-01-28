#pragma once

#include "karma/audio/backend.hpp"

#include <SDL3/SDL_audio.h>
#include <mutex>
#include <vector>

namespace karma::audio_backend {

class SdlAudioClip;

class SdlAudioBackend final : public Backend {
 public:
  SdlAudioBackend();
  ~SdlAudioBackend() override;

  std::shared_ptr<Clip> loadClip(const std::string& filepath,
                                 const ClipOptions& options) override;
  void setListenerPosition(const glm::vec3& position) override;
  void setListenerRotation(const glm::quat& rotation) override;

 private:
  static void SDLCALL AudioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                          int additional_amount, int total_amount);
  void mixAudio(float* output, int frames);

  SDL_AudioStream* stream_ = nullptr;
  SDL_AudioSpec device_spec_{};
  std::mutex mutex_;
  std::vector<std::weak_ptr<SdlAudioClip>> clips_;
};

}  // namespace karma::audio_backend
