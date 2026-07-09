#include "private/audio/backends/sdl/backend.hpp"

#include "private/audio/backends/sdl/clip.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

namespace {
constexpr int kDefaultFrequency = 48000;
constexpr int kDefaultChannels = 2;
}

namespace karma::audio::backend {

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

  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    throw std::runtime_error("Audio: Failed to resume SDL audio device");
  }
}

SdlAudioBackend::~SdlAudioBackend() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  const uint64_t write_failures =
      stream_write_failures_.load(std::memory_order_relaxed);
  if (write_failures != 0) {
    spdlog::warn("Audio: SDL stream rejected {} callback writes", write_failures);
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

  if (src_length > static_cast<Uint32>(std::numeric_limits<int>::max())) {
    SDL_free(src_buffer);
    throw std::runtime_error("Audio: WAV is too large to convert");
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

  if (dst_length <= 0 ||
      dst_length % static_cast<int>(sizeof(float) * device_spec_.channels) != 0) {
    SDL_free(dst_buffer);
    throw std::runtime_error("Audio: converted WAV has invalid sample data");
  }

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

void SdlAudioBackend::setListenerPosition(const glm::vec3& position) {
  std::lock_guard<std::mutex> lock(mutex_);
  listener_.position = position;
}

void SdlAudioBackend::setListenerRotation(const glm::quat& rotation) {
  const double length_squared =
      static_cast<double>(rotation.w) * rotation.w +
      static_cast<double>(rotation.x) * rotation.x +
      static_cast<double>(rotation.y) * rotation.y +
      static_cast<double>(rotation.z) * rotation.z;
  if (!std::isfinite(length_squared) || length_squared <= 1.0e-12) {
    return;
  }
  const float inverse_length =
      static_cast<float>(1.0 / std::sqrt(length_squared));

  std::lock_guard<std::mutex> lock(mutex_);
  listener_.rotation = glm::quat{
      rotation.w * inverse_length,
      rotation.x * inverse_length,
      rotation.y * inverse_length,
      rotation.z * inverse_length,
  };
}

void SDLCALL SdlAudioBackend::AudioStreamCallback(void* userdata,
                                                  SDL_AudioStream* stream,
                                                  int additional_amount,
                                                  int) {
  if (additional_amount <= 0 || !userdata) {
    return;
  }

  auto* backend = static_cast<SdlAudioBackend*>(userdata);
  const int bytes_per_frame =
      static_cast<int>(sizeof(float)) * backend->device_spec_.channels;
  int remaining_frames = additional_amount / bytes_per_frame;
  if (remaining_frames <= 0) {
    return;
  }

  while (remaining_frames > 0) {
    const int chunk_frames = std::min(remaining_frames, kMixBufferFrames);
    const std::size_t sample_count = static_cast<std::size_t>(
        chunk_frames * backend->device_spec_.channels);
    std::fill_n(backend->mix_buffer_.data(), sample_count, 0.0f);
    backend->mixAudio(backend->mix_buffer_.data(), chunk_frames);
    if (!SDL_PutAudioStreamData(stream,
                                backend->mix_buffer_.data(),
                                chunk_frames * bytes_per_frame)) {
      backend->stream_write_failures_.fetch_add(1, std::memory_order_relaxed);
    }
    remaining_frames -= chunk_frames;
  }
}

void SdlAudioBackend::mixAudio(float* output, int frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = clips_.begin(); it != clips_.end();) {
    if (auto clip = it->lock()) {
      clip->mix(output, frames, device_spec_.channels, listener_);
      ++it;
    } else {
      it = clips_.erase(it);
    }
  }
  const std::size_t sample_count =
      static_cast<std::size_t>(frames * device_spec_.channels);
  for (std::size_t i = 0; i < sample_count; ++i) {
    output[i] = std::isfinite(output[i])
                    ? std::clamp(output[i], -1.0f, 1.0f)
                    : 0.0f;
  }
}

}  // namespace karma::audio::backend
