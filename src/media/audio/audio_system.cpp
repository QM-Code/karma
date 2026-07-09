#include "karma/audio.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "karma/assets.h"

namespace {

float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

}  // namespace

namespace karma::audio {

AudioSystem::~AudioSystem() {
  for (const auto& [entity, voices] : active_voices_) {
    (void)entity;
    for (const ActiveVoice& active : voices) {
      active.voice.stop();
    }
  }
}

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

void AudioSystem::update(world::World& world, float /*dt*/) {
  if (!audio_.isAvailable()) {
    return;
  }

  world::Entity listener_entity{};
  bool has_listener = false;
  bool multiple_listeners = false;

  world.forEach<components::AudioListenerComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    if (!has_listener) {
      listener_entity = entity;
      has_listener = true;
    } else {
      multiple_listeners = true;
      return false;
    }
    return true;
  });

  if (multiple_listeners && !warned_multiple_listeners_) {
    spdlog::warn("Karma: Multiple AudioListenerComponents found; using the first.");
    warned_multiple_listeners_ = true;
  }
  if (!multiple_listeners) {
    warned_multiple_listeners_ = false;
  }

  if (has_listener) {
    const auto& transform = world.get<components::TransformComponent>(listener_entity);
    const math::Vec3 pos = transform.getPosition();
    const math::Quat rot = transform.getRotation();
    const bool position_set = audio_.setListenerPosition({pos.x, pos.y, pos.z});
    const bool rotation_set = audio_.setListenerRotation({rot.w, rot.x, rot.y, rot.z});
    has_listener = position_set && rotation_set;
    if (has_listener) {
      warned_no_listener_ = false;
    }
  }

  for (auto it = active_voices_.begin(); it != active_voices_.end();) {
    const world::Entity entity{static_cast<uint32_t>(it->first >> 32),
                               static_cast<uint32_t>(it->first & 0xFFFFFFFFu)};
    if (!world.isAlive(entity) ||
        !world.has<components::AudioSourceComponent>(entity) ||
        !world.has<components::TransformComponent>(entity)) {
      for (const ActiveVoice& active : it->second) {
        if (active.looping) {
          active.voice.stop();
        }
      }
      it = active_voices_.erase(it);
      continue;
    }

    const math::Vec3 position =
        world.get<components::TransformComponent>(entity).getPosition();
    auto& voices = it->second;
    for (auto voice = voices.begin(); voice != voices.end();) {
      if (!voice->voice.isPlaying()) {
        voice = voices.erase(voice);
      } else {
        voice->voice.setPosition({position.x, position.y, position.z});
        ++voice;
      }
    }
    if (voices.empty()) {
      it = active_voices_.erase(it);
    } else {
      ++it;
    }
  }

  bool played_without_listener = false;
  world.forEach<components::AudioSourceComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    const uint64_t key = entityKey(entity);
    auto& source = world.get<components::AudioSourceComponent>(entity);

    if (source.consumeStopRequest()) {
      if (auto voices = active_voices_.find(key); voices != active_voices_.end()) {
        for (const ActiveVoice& active : voices->second) {
          active.voice.stop();
        }
        active_voices_.erase(voices);
      }
    }

    const bool should_play_on_start =
        source.play_on_start && !played_on_start_.contains(key);
    uint32_t playback_count = source.consumePlayRequests();
    if (should_play_on_start) {
      ++playback_count;
    }
    if (playback_count == 0u) {
      return;
    }

    const assets::AudioClipAsset* clip_asset =
        assets_ != nullptr ? assets_->findAudioClip(source.clip_key) : nullptr;
    if (clip_asset == nullptr) {
      spdlog::error("Karma: audio clip asset key '{}' was not registered",
                    source.clip_key);
      return;
    }

    const int configured_instances =
        source.max_instances > 0 ? source.max_instances : clip_asset->max_instances;
    const int max_instances =
        std::clamp(configured_instances, 1, kMaxAudioClipInstances);
    const auto& transform = world.get<components::TransformComponent>(entity);
    const math::Vec3 position = transform.getPosition();

    AudioPlaybackOptions options{
        .position = {position.x, position.y, position.z},
        .gain = std::clamp(finiteOr(source.gain, 1.0f), 0.0f, kMaxAudioGain),
        .pitch = std::clamp(finiteOr(source.pitch, 1.0f),
                            kMinAudioPitch,
                            kMaxAudioPitch),
        .min_distance = std::max(0.0f, finiteOr(source.min_distance, 1.0f)),
        .max_distance = finiteOr(source.max_distance, 20.0f),
        .looping = source.looping,
        .spatialized = source.spatialized,
    };
    options.max_distance = std::max(options.min_distance, options.max_distance);

    try {
      auto& clip = getClip(clip_asset->path.string(), max_instances);
      for (uint32_t request = 0; request < playback_count; ++request) {
        AudioVoice voice = clip.play(options);
        if (!voice) {
          break;
        }
        active_voices_[key].push_back(ActiveVoice{
            .voice = std::move(voice),
            .looping = options.looping,
        });
        if (!has_listener) {
          played_without_listener = true;
        }
      }
      if (should_play_on_start) {
        played_on_start_[key] = true;
      }
    } catch (const std::exception& ex) {
      spdlog::error("Karma: Failed to play audio '{}': {}",
                    source.clip_key,
                    ex.what());
    }
  });

  if (played_without_listener && !warned_no_listener_) {
    spdlog::warn("Karma: Audio played without an AudioListenerComponent in the scene.");
    warned_no_listener_ = true;
  }

  for (auto it = played_on_start_.begin(); it != played_on_start_.end();) {
    const world::Entity entity{static_cast<uint32_t>(it->first >> 32),
                               static_cast<uint32_t>(it->first & 0xFFFFFFFFu)};
    const auto* source = world.tryGet<components::AudioSourceComponent>(entity);
    if (source == nullptr || !source->play_on_start) {
      it = played_on_start_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace karma::audio
