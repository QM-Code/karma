#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

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
  virtual void play(const glm::vec3& position, float volume) = 0;
  virtual void setSpatialization(bool enabled) = 0;
  virtual void setDistanceRange(float min_distance, float max_distance) = 0;
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
std::unique_ptr<Backend> CreateAudioBackend();

}  // namespace karma::audio::backend
