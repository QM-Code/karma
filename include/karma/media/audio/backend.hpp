#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace karma::audio_backend {

struct ClipOptions {
  int max_instances = 5;
};

class Clip {
 public:
  virtual ~Clip() = default;
  virtual void play(const glm::vec3& position, float volume) = 0;
  virtual void setSpatialization(bool enabled) = 0;
  virtual void setDistanceRange(float min_distance, float max_distance) = 0;
};

class Backend {
 public:
  virtual ~Backend() = default;
  virtual std::shared_ptr<Clip> loadClip(const std::string& filepath,
                                         const ClipOptions& options) = 0;
  virtual void setListenerPosition(const glm::vec3& position) = 0;
  virtual void setListenerRotation(const glm::quat& rotation) = 0;
};

std::unique_ptr<Backend> CreateAudioBackend();

}  // namespace karma::audio_backend
