#include "karma/media/audio/backend.hpp"

#if defined(KARMA_AUDIO_BACKEND_MINIAUDIO)
#include "karma/media/audio/backends/miniaudio/backend.hpp"
#endif

#if defined(KARMA_AUDIO_BACKEND_SDL)
#include "karma/media/audio/backends/sdl/backend.hpp"
#endif

namespace karma::audio_backend {

std::unique_ptr<Backend> CreateAudioBackend() {
#if defined(KARMA_AUDIO_BACKEND_MINIAUDIO)
  return std::make_unique<MiniaudioBackend>();
#elif defined(KARMA_AUDIO_BACKEND_SDL)
  return std::make_unique<SdlAudioBackend>();
#else
  return nullptr;
#endif
}

}  // namespace karma::audio_backend
