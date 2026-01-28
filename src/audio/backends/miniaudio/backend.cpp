#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "karma/audio/backends/miniaudio/backend.hpp"
#include "karma/audio/backends/miniaudio/clip.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::audio_backend {

MiniaudioBackend::MiniaudioBackend() {
  engine_ = new ma_engine();
  const ma_result result = ma_engine_init(nullptr, engine_);
  if (result != MA_SUCCESS) {
    delete engine_;
    engine_ = nullptr;
    throw std::runtime_error("Audio: Failed to initialize miniaudio engine");
  }

  ma_engine_set_volume(engine_, 1.0f);
  spdlog::trace("Audio: Initialized miniaudio engine");
}

MiniaudioBackend::~MiniaudioBackend() {
  if (engine_ != nullptr) {
    ma_engine_uninit(engine_);
    delete engine_;
    engine_ = nullptr;
  }
}

std::shared_ptr<Clip> MiniaudioBackend::loadClip(const std::string& filepath,
                                                 const ClipOptions& options) {
  if (engine_ == nullptr) {
    throw std::runtime_error("Audio: Engine not initialized");
  }

  auto stem = std::make_unique<ma_sound>();
  if (ma_sound_init_from_file(engine_, filepath.c_str(), 0, nullptr, nullptr,
                              stem.get()) != MA_SUCCESS) {
    spdlog::error("Audio: Failed to load audio file '{}'", filepath);
    throw std::runtime_error("Audio: Failed to load audio file");
  }

  std::vector<ma_sound*> instances;
  const int instance_count = std::max(1, options.max_instances);
  instances.reserve(static_cast<size_t>(instance_count));

  for (int i = 0; i < instance_count; ++i) {
    auto pooled_sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file(engine_, filepath.c_str(), 0, nullptr, nullptr,
                                pooled_sound.get()) != MA_SUCCESS) {
      spdlog::error("Audio: Failed to create pooled instance {} for '{}'", i,
                    filepath);
      continue;
    }

    ma_sound_set_looping(pooled_sound.get(), MA_FALSE);
    ma_sound_stop(pooled_sound.get());
    ma_sound_seek_to_pcm_frame(pooled_sound.get(), 0);
    instances.push_back(pooled_sound.release());
  }

  if (instances.empty()) {
    ma_sound_uninit(stem.get());
    spdlog::error("Audio: Unable to create playable instances for '{}'", filepath);
    throw std::runtime_error("Audio: Clip has no playable instances");
  }

  return std::make_shared<MiniaudioClip>(stem.release(), std::move(instances));
}

void MiniaudioBackend::setListenerPosition(const glm::vec3& position) {
  ma_engine_listener_set_position(engine_, 0, position.x, position.y, position.z);
}

void MiniaudioBackend::setListenerRotation(const glm::quat& rotation) {
  const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
  ma_engine_listener_set_direction(engine_, 0, forward.x, forward.y, forward.z);
}

}  // namespace karma::audio_backend
