#pragma once

#include <string_view>
#include <unordered_map>

#include "karma/audio/audio.h"
#include "karma/components/audio_source.h"
#include "karma/components/audio_listener.h"
#include "karma/components/transform.h"
#include "karma/ecs/world.h"
#include "karma/systems/system.h"

namespace karma::audio {

class AudioSystem final : public systems::ISystem {
 public:
  explicit AudioSystem(Audio& audio) : audio_(audio) {}

  std::string_view name() const override { return "AudioSystem"; }
  void update(ecs::World& world, float dt) override;

 private:
  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  AudioClip& getClip(const std::string& key, int max_instances);

  Audio& audio_;
  std::unordered_map<std::string, AudioClip> clip_cache_;
  std::unordered_map<uint64_t, bool> played_on_start_;
  bool warned_multiple_listeners_ = false;
  bool warned_no_listener_ = false;
};

}  // namespace karma::audio
