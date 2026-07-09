#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "karma/audio.h"
#include "private/audio/spatial.hpp"

namespace {

bool near(float actual, float expected, float epsilon = 1.0e-5f) {
  return std::abs(actual - expected) <= epsilon;
}

void testPlaybackOptionValidation() {
  karma::audio::AudioPlaybackOptions options{};
  assert(options.valid());

  options.position.x = std::numeric_limits<float>::quiet_NaN();
  assert(!options.valid());
  options = {};
  options.gain = -0.01f;
  assert(!options.valid());
  options.gain = karma::audio::kMaxAudioGain + 0.01f;
  assert(!options.valid());
  options = {};
  options.pitch = karma::audio::kMinAudioPitch * 0.5f;
  assert(!options.valid());
  options = {};
  options.pitch = karma::audio::kMaxAudioPitch * 2.0f;
  assert(!options.valid());
  options = {};
  options.min_distance = -1.0f;
  assert(!options.valid());
  options = {};
  options.max_distance = options.min_distance - 0.1f;
  assert(!options.valid());
  options = {};
  options.gain = std::numeric_limits<float>::infinity();
  assert(!options.valid());
}

void testStereoSpatialGains() {
  karma::audio::AudioPlaybackOptions options{
      .position = {5.0f, 0.0f, 0.0f},
      .gain = 0.8f,
      .min_distance = 0.0f,
      .max_distance = 10.0f,
  };
  karma::audio::backend::ListenerState listener{};

  auto gains = karma::audio::backend::stereoGains(options, listener);
  assert(near(gains.left, 0.0f));
  assert(near(gains.right, 0.4f));

  options.position.x = -5.0f;
  gains = karma::audio::backend::stereoGains(options, listener);
  assert(near(gains.left, 0.4f));
  assert(near(gains.right, 0.0f));

  options.position = {0.0f, 0.0f, -2.5f};
  gains = karma::audio::backend::stereoGains(options, listener);
  assert(near(gains.left, 0.6f));
  assert(near(gains.right, 0.6f));

  options.position = {0.0f, 0.0f, -10.0f};
  gains = karma::audio::backend::stereoGains(options, listener);
  assert(near(gains.left, 0.0f));
  assert(near(gains.right, 0.0f));

  options.spatialized = false;
  gains = karma::audio::backend::stereoGains(options, listener);
  assert(near(gains.left, 0.8f));
  assert(near(gains.right, 0.8f));
}

void testAudioSourceRequestQueue() {
  karma::components::AudioSourceComponent source{};
  source.play(3u);
  source.play(5u);
  assert(source.consumePlayRequests() == 8u);
  assert(source.consumePlayRequests() == 0u);

  source.play(std::numeric_limits<uint32_t>::max());
  assert(source.consumePlayRequests() ==
         karma::components::AudioSourceComponent::kMaxPendingPlayRequests);

  source.stop();
  assert(source.consumeStopRequest());
  assert(!source.consumeStopRequest());
}

void testHeadlessFacadeValidation() {
  karma::audio::Audio audio;
  assert(!audio.isAvailable());
  assert(!audio.setListenerPosition({0.0f, 0.0f, 0.0f}));
  assert(!audio.setListenerRotation({1.0f, 0.0f, 0.0f, 0.0f}));

  bool threw = false;
  try {
    (void)audio.loadClip("", 1);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    (void)audio.loadClip("missing.wav", 0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);

  karma::audio::Audio moved = std::move(audio);
  assert(!audio.isAvailable());
  assert(!moved.isAvailable());
}

}  // namespace

int main() {
  testPlaybackOptionValidation();
  testStereoSpatialGains();
  testAudioSourceRequestQueue();
  testHeadlessFacadeValidation();
  return 0;
}
