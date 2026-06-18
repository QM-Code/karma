#pragma once

#include <string_view>
#include <unordered_map>

#include "karma/media/audio/audio.h"
#include "karma/world/components/audio_source.h"
#include "karma/world/components/audio_listener.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "karma/world/systems/system.h"

namespace karma::content {
class AssetRegistry;
}  // namespace karma::content

namespace karma::audio {

/// \ingroup karma_media
/// ECS audio source/listener system.
///
/// Consumes `AudioSourceComponent`, `AudioListenerComponent`, and transforms to
/// drive backend clip playback.
class AudioSystem final : public systems::ISystem {
 public:
  explicit AudioSystem(Audio& audio, const content::AssetRegistry* assets = nullptr)
      : audio_(audio), assets_(assets) {}

  std::string_view name() const override { return "AudioSystem"; }
  void update(ecs::World& world, float dt) override;

 private:
  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  AudioClip& getClip(const std::string& key, int max_instances);

  Audio& audio_;
  const content::AssetRegistry* assets_ = nullptr;
  std::unordered_map<std::string, AudioClip> clip_cache_;
  std::unordered_map<uint64_t, bool> played_on_start_;
  bool warned_multiple_listeners_ = false;
  bool warned_no_listener_ = false;
};

}  // namespace karma::audio
