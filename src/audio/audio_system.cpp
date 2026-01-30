#include "karma/audio/audio_system.h"

#include <exception>

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

namespace karma::audio {

AudioClip& AudioSystem::getClip(const std::string& key, int max_instances) {
  const std::string cache_key = key + "#" + std::to_string(max_instances);
  auto it = clip_cache_.find(cache_key);
  if (it != clip_cache_.end()) {
    return it->second;
  }
  auto clip = audio_.loadClip(key, max_instances);
  auto [inserted, inserted_ok] = clip_cache_.emplace(cache_key, std::move(clip));
  (void)inserted_ok;
  return inserted->second;
}

void AudioSystem::update(ecs::World& world, float /*dt*/) {
  ecs::Entity listener_entity{};
  bool has_listener = false;
  bool multiple_listeners = false;

  for (const ecs::Entity entity :
       world.view<components::AudioListenerComponent, components::TransformComponent>()) {
    if (!has_listener) {
      listener_entity = entity;
      has_listener = true;
    } else {
      multiple_listeners = true;
      break;
    }
  }

  if (multiple_listeners && !warned_multiple_listeners_) {
    spdlog::warn("Karma: Multiple AudioListenerComponents found; using the first.");
    warned_multiple_listeners_ = true;
  }
  if (!multiple_listeners) {
    warned_multiple_listeners_ = false;
  }

  if (has_listener) {
    const auto& transform = world.get<components::TransformComponent>(listener_entity);
    const math::Vec3 pos = transform.position();
    const math::Quat rot = transform.rotation();
    audio_.setListenerPosition({pos.x, pos.y, pos.z});
    audio_.setListenerRotation({rot.w, rot.x, rot.y, rot.z});
    warned_no_listener_ = false;
  }

  bool played_without_listener = false;
  for (const ecs::Entity entity :
       world.view<components::AudioSourceComponent, components::TransformComponent>()) {
    const uint64_t key = entityKey(entity);
    auto& source = world.get<components::AudioSourceComponent>(entity);
    const bool should_play_on_start = source.play_on_start && !played_on_start_[key];
    const bool should_play_requested = source.consumePlayRequest();
    if (!should_play_on_start && !should_play_requested) {
      continue;
    }

    const auto& transform = world.get<components::TransformComponent>(entity);
    try {
      const int max_instances = source.max_instances > 0 ? source.max_instances : 1;
      auto& clip = getClip(source.clip_key, max_instances);
      const math::Vec3 pos = transform.position();
      clip.setSpatialDefaults(source.spatialized, source.min_distance, source.max_distance);
      if (source.spatialized) {
        clip.playSpatial({pos.x, pos.y, pos.z},
                         source.gain,
                         source.min_distance,
                         source.max_distance);
      } else {
        clip.play({pos.x, pos.y, pos.z},
                  source.gain);
      }
      if (should_play_on_start) {
        played_on_start_[key] = true;
      }
      if (!has_listener) {
        played_without_listener = true;
      }
    } catch (const std::exception& ex) {
      spdlog::error("Karma: Failed to play audio '{}': {}", source.clip_key, ex.what());
    }
  }

  if (played_without_listener && !warned_no_listener_) {
    spdlog::warn("Karma: Audio played without an AudioListenerComponent in the scene.");
    warned_no_listener_ = true;
  }

  if (!played_on_start_.empty()) {
    for (auto it = played_on_start_.begin(); it != played_on_start_.end();) {
      const ecs::Entity entity{static_cast<uint32_t>(it->first >> 32),
                               static_cast<uint32_t>(it->first & 0xFFFFFFFFu)};
      if (!world.isAlive(entity)) {
        it = played_on_start_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

}  // namespace karma::audio
