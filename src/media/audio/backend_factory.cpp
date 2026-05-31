#include "karma/media/audio/backend.hpp"

#if defined(KARMA_AUDIO_BACKEND_MINIAUDIO)
#include "karma/media/audio/backends/miniaudio/backend.hpp"
#elif defined(KARMA_AUDIO_BACKEND_SDL)
#include "karma/media/audio/backends/sdl/backend.hpp"
#else
#error "Karma audio backend not set. Define KARMA_AUDIO_BACKEND_MINIAUDIO or KARMA_AUDIO_BACKEND_SDL."
#endif

namespace karma::audio_backend {

std::unique_ptr<Backend> CreateAudioBackend() {
#if defined(KARMA_AUDIO_BACKEND_MINIAUDIO)
  return std::make_unique<MiniaudioBackend>();
#elif defined(KARMA_AUDIO_BACKEND_SDL)
  return std::make_unique<SdlAudioBackend>();
#endif
}

}  // namespace karma::audio_backend
