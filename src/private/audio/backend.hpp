#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace karma::audio {
struct AudioPlaybackOptions;
}

namespace karma::audio::backend {

/// \ingroup karma_media
/// Backend clip creation options.
struct ClipOptions {
  int max_instances = 5;
};

/// \ingroup karma_media
/// Backend audio clip interface.
class Clip {
 public:
  virtual ~Clip() = default;
  virtual uint64_t play(const AudioPlaybackOptions& options) = 0;
  virtual bool isPlaying(uint64_t voice_id) const = 0;
  virtual bool stop(uint64_t voice_id) = 0;
  virtual bool setPosition(uint64_t voice_id, const glm::vec3& position) = 0;
};

/// \ingroup karma_media
/// Audio backend interface implemented by miniaudio or SDL adapters.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual std::shared_ptr<Clip> loadClip(const std::string& filepath,
                                         const ClipOptions& options) = 0;
  virtual void setListenerPosition(const glm::vec3& position) = 0;
  virtual void setListenerRotation(const glm::quat& rotation) = 0;
};

/// Creates the configured audio backend.
std::unique_ptr<Backend> createAudioBackend();

}  // namespace karma::audio::backend
