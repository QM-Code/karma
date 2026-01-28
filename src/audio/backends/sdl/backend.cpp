#include "karma/audio/backends/sdl/backend.hpp"

#include "karma/audio/backends/sdl/clip.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

namespace {
constexpr int kDefaultFrequency = 48000;
constexpr int kDefaultChannels = 2;
}

namespace karma::audio_backend {

SdlAudioBackend::SdlAudioBackend() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    throw std::runtime_error("Audio: SDL audio subsystem failed to initialize");
  }

  device_spec_.format = SDL_AUDIO_F32;
  device_spec_.channels = kDefaultChannels;
  device_spec_.freq = kDefaultFrequency;

  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                      &device_spec_,
                                      AudioStreamCallback,
                                      this);
  if (!stream_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    throw std::runtime_error("Audio: Failed to open SDL audio device");
  }

  SDL_ResumeAudioStreamDevice(stream_);
}

SdlAudioBackend::~SdlAudioBackend() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::shared_ptr<Clip> SdlAudioBackend::loadClip(const std::string& filepath,
                                                const ClipOptions& options) {
  SDL_AudioSpec src_spec{};
  Uint8* src_buffer = nullptr;
  Uint32 src_length = 0;
  if (!SDL_LoadWAV(filepath.c_str(), &src_spec, &src_buffer, &src_length)) {
    spdlog::error("Audio: Failed to load WAV '{}': {}", filepath, SDL_GetError());
    throw std::runtime_error("Audio: Failed to load WAV");
  }

  Uint8* dst_buffer = nullptr;
  int dst_length = 0;
  if (!SDL_ConvertAudioSamples(&src_spec,
                               src_buffer,
                               static_cast<int>(src_length),
                               &device_spec_,
                               &dst_buffer,
                               &dst_length)) {
    SDL_free(src_buffer);
    spdlog::error("Audio: Failed to convert WAV '{}': {}", filepath, SDL_GetError());
    throw std::runtime_error("Audio: Failed to convert WAV");
  }

  SDL_free(src_buffer);

  const size_t sample_count = static_cast<size_t>(dst_length) / sizeof(float);
  std::vector<float> samples(sample_count);
  std::memcpy(samples.data(), dst_buffer, dst_length);
  SDL_free(dst_buffer);

  auto clip = std::make_shared<SdlAudioClip>(std::move(samples),
                                             device_spec_.channels,
                                             std::max(1, options.max_instances));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clips_.push_back(clip);
  }
  return clip;
}

void SdlAudioBackend::setListenerPosition(const glm::vec3&) {}

void SdlAudioBackend::setListenerRotation(const glm::quat&) {}

void SDLCALL SdlAudioBackend::AudioStreamCallback(void* userdata,
                                                  SDL_AudioStream* stream,
                                                  int additional_amount,
                                                  int) {
  if (additional_amount <= 0 || !userdata) {
    return;
  }

  auto* backend = static_cast<SdlAudioBackend*>(userdata);
  const int frames = additional_amount /
                     (static_cast<int>(sizeof(float)) * backend->device_spec_.channels);
  if (frames <= 0) {
    return;
  }

  std::vector<float> buffer(static_cast<size_t>(frames * backend->device_spec_.channels),
                            0.0f);
  backend->mixAudio(buffer.data(), frames);
  SDL_PutAudioStreamData(stream, buffer.data(), additional_amount);
}

void SdlAudioBackend::mixAudio(float* output, int frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = clips_.begin(); it != clips_.end();) {
    if (auto clip = it->lock()) {
      clip->mix(output, frames, device_spec_.channels);
      ++it;
    } else {
      it = clips_.erase(it);
    }
  }
}

}  // namespace karma::audio_backend
