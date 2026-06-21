#pragma once

#include "private/audio/backend.hpp"

struct ma_engine;

namespace karma::audio::backend {

/// \ingroup karma_media
/// miniaudio implementation of the audio backend interface.
class MiniaudioBackend final : public Backend {
 public:
  MiniaudioBackend();
  ~MiniaudioBackend() override;

  std::shared_ptr<Clip> loadClip(const std::string& filepath,
                                 const ClipOptions& options) override;
  void setListenerPosition(const glm::vec3& position) override;
  void setListenerRotation(const glm::quat& rotation) override;

 private:
  ma_engine* engine_ = nullptr;
};

}  // namespace karma::audio::backend
